// src/operators/runtime/model_ensemble.cpp — see model_ensemble.h.
#include "operators/runtime/model_ensemble.h"

#include "operators/framework/rs_operator_error.h"
#include "runtime/observability/fault_point.h"

#include "operators/runtime/detection_fusion.h"
#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/model_publish.h"
#include "operators/runtime/model_runtime.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <QDir>
#include <QFile>
#include <QtGlobal>
#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <numeric>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sicnu::operators::runtime {

namespace {

/// Probability-stack NoData (the tile engine writes NaN for skipped tiles in
/// probability mode — the same sentinel the combine pass consumes).
constexpr float kStackNoData = std::numeric_limits<float>::quiet_NaN();

/// Combine-pass pixel budget: the pass reads one row block per member at a
/// time; the block is sized so members × channels × blockPixels stays at or
/// under this many float32 values (~64 MiB worst case at 16 members). A
/// deterministic formula, not a heuristic guess. SICNU_ENSEMBLE_BLOCK_VALUES
/// overrides the budget (tests force multi-block/tail-block combine paths;
/// deployments may trade block count for memory) — <=0 keeps the default.
std::int64_t combineBlockValues()
{
  const std::int64_t overridden = qEnvironmentVariableIntValue( "SICNU_ENSEMBLE_BLOCK_VALUES" );
  return overridden > 0 ? overridden : 16LL * 1024 * 1024;
}

/// Member-execution admission: the manifest budget is bounded by the catalog
/// ceiling; the semaphore type needs a compile-time bound.
constexpr int kMaxMemberConcurrency = 16;

/// Removes every staged path on scope exit unless disarmed (the final
/// product was published). Guarantees "no partial output" even when the
/// combine pass throws or the run is cancelled. Paths may be added as the
/// run stages them; removal happens on destruction, after every writer is
/// closed. Member sidecars (`<stack>.prov.json`) are tracked too — a member
/// engine publishes one next to its staged stack (Platform 13.0 residue fix:
/// 12.0 tracked only the stack itself).
class StagedFileGuard
{
  public:
    void addPath( const QString &path ) { m_paths.push_back( path ); }
    void disarm() { m_disarmed = true; }
    ~StagedFileGuard()
    {
      if ( m_disarmed )
        return;
      for ( const QString &path : m_paths )
        QFile::remove( path );
    }
    StagedFileGuard() = default;
    StagedFileGuard( const StagedFileGuard & ) = delete;
    StagedFileGuard &operator=( const StagedFileGuard & ) = delete;

  private:
    std::vector<QString> m_paths;
    bool m_disarmed = false;
};

/// Publishes the provenance sidecar next to a published product — moved to
/// model_publish.{h,cpp} (completion 13/15) so the single-model lanes share
/// the ONE staged-write + rename contract with the ensemble lanes.
/// DetectionPublishGuard and its shapefile-companion helpers moved with it
/// (the guard takes its backup suffix per lane: ".ensemble-prev~" here).

/// Per-member execution record for the payload, the provenance sidecar and
/// the parallel worker bookkeeping. Worker-owned fields (failure/aborted) are
/// written by exactly one worker thread each; the main thread reads them only
/// AFTER joining every worker.
struct MemberRun
{
  ModelInfo model;
  double weight = 1.0;   ///< ensemble weight from the ensemble contract
  ModelRuntimePtr session;
  TileInferenceStats stats;          ///< raster path
  DetectionTileStats detectionStats; ///< detection path
  SceneClassificationResult scene;   ///< scene path
  std::vector<DetectionBox> boxes;   ///< detection path (raster pixels)
  QString stagedPath;                ///< raster path stack (empty otherwise)
  ProviderSelectionReport selection; ///< provider chain trace for this member
  std::exception_ptr failure;        ///< worker failure (lowest index wins)
  bool failureAfterStop = false;     ///< failure raised after a sibling already failed
  bool aborted = false;              ///< worker stopped by fail-fast
};

/// Builds the shared provenance document for an ensemble product (schema
/// exp-rs-prov/1; the `ensemble` member block is additive — historical /1
/// consumers ignore unknown blocks, and verifyProductProvenance validates
/// the shared model/execution/inputs surface unchanged).
Json::Value buildEnsembleProvenance( const ModelInfo &ensembleModel,
                                     const std::vector<MemberRun> &runs,
                                     const TileInferenceStats &combined,
                                     const std::string &combination,
                                     const std::string &uncertaintyNote,
                                     int memberConcurrency,
                                     bool stagingCompressed,
                                     bool detectionPath )
{
  Json::Value prov( Json::objectValue );
  prov["schema"] = "exp-rs-prov/1";

  Json::Value modelJson( Json::objectValue );
  modelJson["name"] = ensembleModel.name;
  if ( !ensembleModel.id.empty() )
    modelJson["id"] = ensembleModel.id;
  if ( !ensembleModel.modelVersion.empty() )
    modelJson["version"] = ensembleModel.modelVersion;
  modelJson["identity_tag"] = ensembleModel.identityTag();
  modelJson["framework"] = "ensemble";
  // Provenance parity with the single-model lanes (buildProvenanceDocument /
  // buildDetectionProvenance): task intent and whole-package identity travel
  // with the ensemble model too — truthful absence when undeclared.
  if ( !ensembleModel.task.empty() )
    modelJson["task"] = ensembleModel.task;
  if ( !ensembleModel.packageDigest.empty() )
    modelJson["package_digest"] = ensembleModel.packageDigest;
  if ( !ensembleModel.sourceManifest.empty() )
    modelJson["source_manifest"] = ensembleModel.sourceManifest;
  if ( !ensembleModel.license.empty() )
    modelJson["license"] = ensembleModel.license;
  prov["model"] = modelJson;

  // The ensemble block: what the product actually IS — every member's full
  // execution identity plus the combination semantics. An ensemble product
  // that cannot name its members (and their weight digests) is not
  // reproducible.
  Json::Value ensembleJson( Json::objectValue );
  ensembleJson["combination"] = combination;
  if ( !uncertaintyNote.empty() )
    ensembleJson["uncertainty"] = uncertaintyNote;
  ensembleJson["member_concurrency"] = memberConcurrency;
  ensembleJson["staging_compression"] = stagingCompressed ? "deflate" : "none";
  Json::Value membersJson( Json::arrayValue );
  for ( const MemberRun &run : runs )
  {
    Json::Value member( Json::objectValue );
    member["identity_tag"] = run.model.identityTag();
    if ( !run.model.contentDigest.empty() )
      member["content_digest"] = run.model.contentDigest;
    // Member identity parity with the single-model sidecars: the task the
    // member declares and the whole-package digest it shipped with are part
    // of what makes the ensemble product reproducible (truthful absence).
    if ( !run.model.task.empty() )
      member["task"] = run.model.task;
    if ( !run.model.packageDigest.empty() )
      member["package_digest"] = run.model.packageDigest;
    member["framework"] = run.selection.resolvedFramework.empty() ? run.model.framework
                                                                  : run.selection.resolvedFramework;
    member["weight"] = run.weight;
    if ( run.session )
    {
      member["backend"] = run.session->backendName();
      member["device"] = run.session->deviceName();
      const ProviderRuntimeDetails details = run.session->providerDetails();
      if ( !details.executionProvider.empty() )
        member["execution_provider"] = details.executionProvider;
      if ( !details.runtimeVersion.empty() )
        member["runtime_version"] = details.runtimeVersion;
    }
    // Provider strategy: a fallback that fired is never silent — the sidecar
    // records the full attempt trail when the chain walked past the primary.
    if ( run.selection.attempts.size() > 1 )
      member["provider_selection"] = run.selection.toJson();
    Json::Value execution( Json::objectValue );
    // The stats struct that THIS path filled (a detection member never runs
    // the raster engine — reporting raster zeros would make the sidecar lie).
    if ( detectionPath )
    {
      execution["tiles_processed"] = run.detectionStats.tilesProcessed;
      execution["tiles_planned"] = run.detectionStats.tilesPlanned;
      execution["detections_kept"] = run.detectionStats.detectionsKept;
      if ( run.detectionStats.batchReductions > 0 )
        execution["batch_reductions"] = run.detectionStats.batchReductions;
    }
    else
    {
      execution["tiles_processed"] = run.stats.tilesProcessed;
      execution["tiles_skipped_nodata"] = run.stats.tilesSkippedNoData;
      if ( run.stats.batchReductions > 0 )
        execution["batch_reductions"] = run.stats.batchReductions;
    }
    member["execution"] = execution;
    membersJson.append( member );
  }
  ensembleJson["members"] = membersJson;
  prov["ensemble"] = ensembleJson;

  Json::Value executionJson( Json::objectValue );
  executionJson["tile_size"] = combined.tileSize;
  executionJson["halo"] = combined.halo;
  executionJson["batch_size"] = combined.batchSize;
  executionJson["tiles_planned"] = combined.tilesPlanned;
  executionJson["tiles_processed"] = combined.tilesProcessed;
  executionJson["tiles_skipped_nodata"] = combined.tilesSkippedNoData;
  if ( combined.batchReductions > 0 )
    executionJson["batch_reductions"] = combined.batchReductions;
  prov["execution"] = executionJson;

  // Grid provenance mirrors the member engines' verified feeds (all members
  // ran the same input; the primary member's record is the grid authority).
  if ( !combined.inputGrids.empty() )
  {
    Json::Value inputs( Json::arrayValue );
    for ( const GridProvenance &grid : combined.inputGrids )
    {
      Json::Value input( Json::objectValue );
      input["name"] = grid.name;
      input["path"] = grid.path;
      // Lineage parity with the single-model raster sidecar: when the feed
      // was prepared from origins, the reproduction record names them.
      if ( !grid.preparedFrom.empty() )
      {
        Json::Value prepared( Json::arrayValue );
        for ( const std::string &origin : grid.preparedFrom )
          prepared.append( origin );
        input["prepared_from"] = prepared;
      }
      if ( !grid.crs.empty() )
        input["crs"] = grid.crs;
      input["crs_verified"] = grid.crsVerified;
      input["width"] = grid.width;
      input["height"] = grid.height;
      if ( grid.frames > 1 )
        input["frames"] = grid.frames;
      if ( !grid.preprocessNote.empty() )
        input["preprocess"] = grid.preprocessNote;
      if ( grid.fingerprint.isObject() )
        input["fingerprint"] = grid.fingerprint;
      inputs.append( input );
    }
    prov["inputs"] = inputs;
  }

  // The output grid the product carries — the consumer-side verifier checks
  // this against the real raster (GridMismatch otherwise).
  Json::Value outputJson( Json::objectValue );
  outputJson["bands"] = combined.outBands;
  outputJson["width"] = combined.outWidth;
  outputJson["height"] = combined.outHeight;
  prov["output"] = outputJson;
  return prov;
}

struct MemberGrid
{
  int width = 0;
  int height = 0;
  int bands = 0;
  double geotransform[6] = {};
  std::string projection;
};

MemberGrid readGrid( const QString &path )
{
  GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    throw RSOperatorError( ErrorCode::GdalError,
                           "ensemble combine pass could not open member output: "
                             + path.toStdString() );
  MemberGrid grid;
  grid.width = GDALGetRasterXSize( ds );
  grid.height = GDALGetRasterYSize( ds );
  grid.bands = GDALGetRasterCount( ds );
  const char *projection = GDALGetProjectionRef( ds );
  if ( projection )
    grid.projection = projection;
  if ( GDALGetGeoTransform( ds, grid.geotransform ) != CE_None )
  {
    // No geotransform: identity, like the engine's writer fallback.
    grid.geotransform[0] = 0.0;
    grid.geotransform[1] = 1.0;
    grid.geotransform[2] = 0.0;
    grid.geotransform[3] = 0.0;
    grid.geotransform[4] = 0.0;
    grid.geotransform[5] = -1.0;
  }
  GDALClose( ds );
  return grid;
}

/// Reads one float32 row block from one band. Returns false when GDAL fails
/// (the caller turns it into a typed error naming the member).
bool readBlock( GDALDatasetH ds, int band, int y, int rows, float *buffer )
{
  const int width = GDALGetRasterXSize( ds );
  return GDALRasterIO( GDALGetRasterBand( ds, band ), GF_Read, 0, y, width, rows, buffer,
                       width, rows, GDT_Float32, 0, 0 ) == CE_None;
}

/// Resolves and gates every ensemble member BEFORE anything is acquired: a
/// broken member is a manifest-level failure, not a mid-run one. Applies the
/// SAME contract gates the single-model service applies (Platform 12.0
/// review P1-2) — a member is never silently under-fed.
std::vector<ModelInfo> resolveEnsembleMembers( const ModelInfo &ensembleModel,
                                               const ModelExecutionRequest &request )
{
  std::vector<ModelInfo> members;
  members.reserve( ensembleModel.ensemble.members.size() );
  for ( const ModelEnsembleMemberContract &entry : ensembleModel.ensemble.members )
  {
    std::string errorDetail;
    const ModelInfo member = resolveModelReference( entry.model, &errorDetail );
    if ( member.readiness != ModelReadiness::Ready )
      throw RSOperatorError( member.readiness == ModelReadiness::MissingArtifact
                               ? ErrorCode::FileNotFound
                               : ErrorCode::InvalidInputData,
                             "ensemble member '" + entry.model + "' is not ready: "
                               + ( errorDetail.empty() ? std::string( "unavailable" ) : errorDetail ) );
    if ( member.ensemble.declared )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "ensemble member '" + entry.model
                               + "' is itself an ensemble - nesting is not supported" );
    // Members run through the SINGLE-INPUT engine: a member whose manifest
    // demands temporal frames or several named inputs would be silently
    // under-fed (the #646 failure class) — the same loud refusal the service
    // applies to single-model runs.
    rejectUnwiredContracts( member, {} );
    preflightFeatureCube( member, request.inputPath );
    members.push_back( member );
  }
  return members;
}

/// Acquires one session per member through its own provider fallback chain.
/// Serial by construction: the registry serializes admission itself, and the
/// acquisition order (member order) is what the ledger and the provenance
/// record. The request's explicit device token overrides every member.
std::vector<MemberRun> acquireEnsembleSessions( const ModelInfo &ensembleModel,
                                                const std::vector<ModelInfo> &members,
                                                const bool hasDeviceOverride,
                                                const RequestedDevice &deviceRequest,
                                                RSOperatorContext &context )
{
  auto &registry = ModelRuntimeRegistry::instance();
  std::vector<MemberRun> runs;
  runs.reserve( members.size() );
  for ( std::size_t i = 0; i < members.size(); ++i )
  {
    MemberRun run;
    run.model = members[i];
    run.weight = ensembleModel.ensemble.members[i].weight;

    context.reportProgress( 0.05, "Acquiring ensemble member " + std::to_string( i + 1 ) + "/"
                                     + std::to_string( members.size() ) + " ("
                                     + members[i].stableId() + ")" );
    std::string loadError;
    run.session =
      hasDeviceOverride
        ? registry.acquireWithFallback( run.model, &deviceRequest, &loadError, &run.selection )
        : registry.acquireWithFallback( run.model, nullptr, &loadError, &run.selection );
    if ( !run.session )
      throw RSOperatorError( ErrorCode::ComputationError,
                             "Failed to load ensemble member '" + run.model.stableId()
                               + "': " + loadError );
    runs.push_back( std::move( run ) );
  }
  return runs;
}

/// Runs every member body under the bounded admission budget, one worker
/// thread per member. THE concurrency contract (Platform 13.0):
///   - admission: at most @p budget members execute at any instant (a
///     counting semaphore acquired INSIDE the worker — "parallel" never
///     means "start everything").
///   - fail-fast: the first worker failure (or a parent cancellation) raises
///     a shared stop flag; every other worker observes it at its next check
///     point and exits WITHOUT publishing anything.
///   - isolation: exceptions never escape a worker (no terminate); each
///     worker captures its own exception_ptr. The main thread joins all
///     workers and rethrows the LOWEST-INDEX real failure, so the surfaced
///     error is a deterministic function of the failure set, never of
///     scheduling.
///   - determinism: the caller assembles results strictly in member order
///     afterwards; completion order never reaches the product.
void runMembersBounded( std::vector<MemberRun> &runs, int budget,
                        const RSOperatorContext &parent,
                        const std::function<void( std::size_t, MemberRun &, RSOperatorContext & )>
                          &body )
{
  budget = std::clamp( budget, 1, kMaxMemberConcurrency );
  std::counting_semaphore<kMaxMemberConcurrency> admission(
    static_cast<std::ptrdiff_t>( budget ) );
  std::atomic<bool> stopWorkers{ false };

  auto worker = [ & ]( std::size_t index ) {
    // Bounded admission: the slot is taken FIRST, then guarded — a throwing
    // acquire must not release a slot it never held. Every exit path (abort,
    // failure, success) returns the slot through the guard.
    admission.acquire();
    struct AdmissionRelease
    {
      std::counting_semaphore<kMaxMemberConcurrency> &semaphore;
      ~AdmissionRelease() { semaphore.release(); }
    } release{ admission };

    MemberRun &run = runs[index];
    if ( stopWorkers.load( std::memory_order_acquire ) || parent.isCancelled() )
    {
      run.aborted = true;
      return;
    }
    try
    {
      // Child context: the worker polls the parent's cancellation AND the
      // fail-fast flag, so a sibling failure stops this member at its next
      // tile boundary instead of running the whole raster.
      RSOperatorContext child( parent.workDir() );
      child.setCancelCallback( [ &parent, &stopWorkers ]() {
        return parent.isCancelled() || stopWorkers.load( std::memory_order_acquire );
      } );
      body( index, run, child );
      if ( stopWorkers.load( std::memory_order_acquire ) )
        run.aborted = true; // finished alongside a sibling failure: discard
    }
    catch ( const RSOperatorError &error )
    {
      // A cancellation raised by the FAIL-FAST flag (a sibling failed) is an
      // abort of THIS worker, not this member's failure — recording it as a
      // failure would let the abort mask the real error. A cancellation the
      // PARENT asked for is a real outcome and stays a failure.
      if ( error.code() == ErrorCode::Cancelled && !parent.isCancelled()
           && stopWorkers.load( std::memory_order_acquire ) )
      {
        run.aborted = true;
        return;
      }
      // Any other error is this member's failure. When the stop flag was
      // ALREADY set, the error is (or may be) a consequence of a sibling's
      // failure unwinding through this worker — recorded, but flagged so the
      // surfaced error stays the deterministic lowest-index REAL failure
      // (a failure recorded before any sibling failed).
      run.failure = std::current_exception();
      run.failureAfterStop = stopWorkers.load( std::memory_order_acquire );
      stopWorkers.store( true, std::memory_order_release );
    }
    catch ( ... )
    {
      run.failure = std::current_exception();
      run.failureAfterStop = stopWorkers.load( std::memory_order_acquire );
      stopWorkers.store( true, std::memory_order_release );
    }
  };

  std::vector<std::thread> workers;
  workers.reserve( runs.size() );
  try
  {
    for ( std::size_t i = 0; i < runs.size(); ++i )
      workers.emplace_back( worker, i );
  }
  catch ( const std::system_error & )
  {
    // Thread creation failed (RLIMIT_NPROC / address space): stop the workers
    // already running and join them — a joinable std::thread destroyed by the
    // vector would terminate the process.
    stopWorkers.store( true, std::memory_order_release );
    for ( std::thread &thread : workers )
      thread.join();
    throw;
  }
  for ( std::thread &thread : workers )
    thread.join();

  // Deterministic surfaced error: the lowest-index REAL failure (recorded
  // before any sibling failed). When every failure is a consequence of an
  // abort, the lowest-index one is surfaced — still deterministic.
  const MemberRun *surfaced = nullptr;
  for ( const MemberRun &run : runs )
  {
    if ( !run.failure )
      continue;
    if ( !run.failureAfterStop )
    {
      surfaced = &run;
      break; // lowest-index real failure
    }
    if ( !surfaced )
      surfaced = &run; // first consequence-only failure (fallback)
  }
  if ( surfaced )
    std::rethrow_exception( surfaced->failure );
}

/// Member bodies ------------------------------------------------------------

/// Raster member: one probability stack into the member's staged path.
/// Members always produce probability stacks — the combination owns the
/// product semantics; a member applying its own derived collapse would
/// destroy the probabilities the vote/mean needs.
void runRasterMember( std::size_t index, MemberRun &run, RSOperatorContext &context,
                      const ModelExecutionRequest &request, const std::vector<int> &bands )
{
  TileInferenceRunOptions memberOptions;
  memberOptions.tta = TtaMode::None;
  memberOptions.batchSizeOverride = std::max( 0, request.batchSizeOverride );
  memberOptions.outputMode = RasterOutputMode::Probability;
  memberOptions.blend = request.blend;
  memberOptions.computeFeedFingerprints = index == 0; // grid provenance from the primary member
  TileInferenceEngine engine( run.model, run.session );
  run.stats = engine.run( request.inputPath, bands, run.stagedPath.toStdString(), context,
                          memberOptions );
}

/// Detection member: boxes collected in memory, nothing published. Request-
/// level knob overrides apply to EVERY member's decode gate (what a caller
/// forcing conf=0.5 means); the fusion thresholds stay the ensemble
/// manifest's (the combination owns the product semantics).
void runDetectionMember( MemberRun &run, RSOperatorContext &context,
                         const ModelExecutionRequest &request, const std::vector<int> &bands )
{
  ModelInfo effective = run.model;
  if ( request.confOverride >= 0.0 )
    effective.output.detection.confThreshold = request.confOverride;
  if ( request.nmsIouOverride >= 0.0 )
    effective.output.detection.nmsIou = request.nmsIouOverride;
  TileInferenceRunOptions memberOptions;
  memberOptions.batchSizeOverride = std::max( 0, request.batchSizeOverride );
  DetectionTileEngine engine( effective, run.session );
  run.detectionStats =
    engine.run( request.inputPath, bands, run.stagedPath.toStdString(), context, memberOptions,
                &run.boxes );
}

/// Scene-classification member: one probability vector, nothing published.
void runSceneMember( MemberRun &run, RSOperatorContext &context,
                     const ModelExecutionRequest &request, const std::vector<int> &bands )
{
  TileInferenceRunOptions memberOptions;
  memberOptions.batchSizeOverride = std::max( 0, request.batchSizeOverride );
  TileInferenceEngine engine( run.model, run.session );
  run.scene = engine.classifyScene( request.inputPath, bands, context, memberOptions );
}

/// Rethrows the lowest-index worker failure (the deterministic surfaced
/// error) and refuses to continue on a cancelled/aborted run.
void collectMemberOutcomes( const std::vector<MemberRun> &runs, RSOperatorContext &context )
{
  for ( const MemberRun &run : runs )
  {
    if ( run.failure )
      std::rethrow_exception( run.failure );
  }
  context.throwIfCancelled();
  for ( const MemberRun &run : runs )
  {
    if ( run.aborted )
      throw RSOperatorError( ErrorCode::Cancelled,
                             "ensemble member run stopped before completing" );
  }
}

} // namespace

ModelExecutionResult runEnsembleInference( const ModelInfo &ensembleModel,
                                           const ModelExecutionRequest &request,
                                           RSOperatorContext &context )
{
  // Surface routing. The ensemble combines member products: raster
  // probability stacks (weighted_mean / weighted_vote), detection boxes
  // (wbf — Weighted Boxes Fusion, ADR 0171), or scene-classification
  // probability vectors (weighted_mean / weighted_vote). Multi-feed temporal
  // ensembles and request-level TTA stay refusals for the same honesty.
  if ( !request.namedInputs.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: multi-feed requests are not wired for "
                               "ensembles yet — run the members' contracts through a "
                               "non-ensemble model" );
  if ( request.tta != TtaMode::None )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: TTA is a single-model knob (members may "
                               "declare their own preprocessing contracts)" );

  // Defense in depth for programmatically built ModelInfo (the manifest
  // parser already validates this): the combination/uncertainty pairing must
  // hold at run time too, or an unserved band could reach the product.
  if ( const std::string contractIssue = ensembleModel.ensemble.validate(); !contractIssue.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "ensemble contract invalid for model '" + ensembleModel.name
                             + "': " + contractIssue );

  const std::string combination = ensembleModel.ensemble.effectiveCombination();

  // Detection routing: the manifest must declare the box-fusion combination,
  // and a wbf manifest never serves a raster/scene request.
  if ( request.asDetection && combination != "wbf" )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: detection decode requires combination 'wbf' "
                               "(Weighted Boxes Fusion); this manifest declares '"
                             + combination + "'" );
  if ( combination == "wbf" && !request.asDetection )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' declares combination 'wbf' (detection box fusion) but the "
                               "request is not a detection run — use the detection operator or "
                               "a raster/scene combination" );
  // Resolve + gate every member BEFORE acquiring anything.
  const std::vector<ModelInfo> members = resolveEnsembleMembers( ensembleModel, request );

  // Hardening 15/20: the all-zero-weight refusal is a STATIC manifest
  // property — evaluate it before any member session is acquired or run.
  // It used to fire only in each lane's combine pass, after every member
  // had been acquired (VRAM reserved) and fully executed: a statically
  // undefined ensemble paid full inference for a guaranteed typed refusal.
  // The per-lane copies were removed; this is the single authority now.
  {
    double manifestWeightSum = 0.0;
    for ( const ModelEnsembleMemberContract &entry : ensembleModel.ensemble.members )
      manifestWeightSum += entry.weight;
    if ( !( manifestWeightSum > 0.0 ) )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "ensemble weights sum to zero - the combination is undefined "
                               "(declare at least one positive weight)" );
  }

  // Device override: the request's token applies to EVERY member (what a
  // caller forcing `cpu` means); without it each member resolves its own
  // manifest contract.
  RequestedDevice deviceRequest;
  const bool hasDeviceOverride =
    !request.deviceToken.empty() && RequestedDevice::parse( request.deviceToken, &deviceRequest );
  if ( !request.deviceToken.empty() && !hasDeviceOverride )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "device '" + request.deviceToken
                             + "' is not parsable (supported: cpu, cuda, cuda:N, auto)" );

  std::vector<MemberRun> runs =
    acquireEnsembleSessions( ensembleModel, members, hasDeviceOverride, deviceRequest, context );

  const int budget = ensembleModel.ensemble.effectiveMaxConcurrentMembers();

  if ( request.asDetection )
  {
    // ===================== Detection ensemble (WBF) =====================
    // Every member must be detection-executable and share ONE class
    // vocabulary (names AND order): classId indexes the vocabulary, so a
    // mismatch would silently relabel the product — typed refusal instead.
    for ( const MemberRun &run : runs )
    {
      if ( const std::string contractError = DetectionTileEngine::checkContract( run.model );
           !contractError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "ensemble member '" + run.model.stableId()
                                 + "' cannot run as a detection member: " + contractError );
    }
    const std::vector<std::string> &vocabulary = runs.front().model.output.detection.classes;
    for ( std::size_t i = 1; i < runs.size(); ++i )
    {
      if ( runs[i].model.output.detection.classes != vocabulary )
        throw RSOperatorError(
          ErrorCode::InvalidInputData,
          "ensemble member '" + runs[i].model.stableId()
            + "' declares a different detection class vocabulary than the primary member '"
            + runs.front().model.stableId()
            + "' — box fusion requires one shared class map (names and order); "
              "a silent remap would relabel the product" );
    }

    // Detection members publish nothing: the boxes are fused in memory and
    // the ensemble writes ONE product, so there is no member residue to
    // clean by construction.
    runMembersBounded(
      runs, budget, context,
      [ & ]( std::size_t index, MemberRun &run, RSOperatorContext &child ) {
        context.reportProgressForced(
          static_cast<double>( index ) / static_cast<double>( runs.size() + 1 ),
          "Running detection ensemble member " + std::to_string( index + 1 ) + "/"
            + std::to_string( runs.size() ) + " (" + run.model.stableId() + ")" );
        runDetectionMember( run, child, request, request.bands );
      } );
    collectMemberOutcomes( runs, context );

    // Fuse in the ONE frame every member already shares: raster pixels (each
    // member engine mapped its own letterbox/tiling reverse transform back
    // to raster coordinates; the ensemble never re-derives coordinates).
    std::vector<DetectionMemberBoxes> contributions;
    contributions.reserve( runs.size() );
    for ( MemberRun &run : runs )
      contributions.push_back( DetectionMemberBoxes{ run.weight, std::move( run.boxes ) } );
    // (The all-zero-weight refusal moved above session acquisition — single
    // authority in runEnsembleInference.)
    DetectionFusionContract fusion;
    fusion.iouThreshold = ensembleModel.ensemble.detection.iouThreshold;
    fusion.skipBoxThreshold = ensembleModel.ensemble.detection.skipBoxThreshold;
    const DetectionFusionResult fused = fuseDetectionsWbf(
      contributions, fusion, CancelProbe( [ &context ]() { context.throwIfCancelled(); } ) );

    // The final writer applies the INPUT raster's geotransform once.
    GdalDatasetWrapper input;
    if ( !input.open( QString::fromStdString( request.inputPath ) ) )
      throw RSOperatorError( ErrorCode::GdalError,
                             "failed to open input raster: " + request.inputPath );
    // The publish guard owns the previous product across the vector publish
    // AND the sidecar write: any failure (including a throw from the writer)
    // restores it exactly — main file, shapefile sidecars and provenance.
    const QString detectionFinal = QString::fromStdString( request.outputPath );
    DetectionPublishGuard publishGuard( detectionFinal, QStringLiteral( ".ensemble-prev~" ) );
    writeDetectionVector( fused.boxes, vocabulary, input.geoTransform(), input.projection(),
                          request.outputPath );

    // Provenance sidecar: the fusion block records WHY the product looks the
    // way it does (algorithm, thresholds, pooled/fused counts) next to the
    // member identities — the product must be explainable after the fact.
    TileInferenceStats combined;
    for ( const MemberRun &run : runs )
    {
      combined.tilesPlanned += run.detectionStats.tilesPlanned;
      combined.tilesProcessed += run.detectionStats.tilesProcessed;
    }
    // Grid provenance mirrors the member engines' verified feeds: all members
    // ran the SAME input, the primary member's record is the grid authority
    // (completion 13/15 — the detection lane used to publish no inputs block).
    if ( !runs.empty() )
      combined.inputGrids = runs.front().detectionStats.inputGrids;
    Json::Value provenance =
      buildEnsembleProvenance( ensembleModel, runs, combined, combination, std::string(), budget,
                               ensembleModel.ensemble.stagingCompressed(), true );
    Json::Value fusionJson( Json::objectValue );
    fusionJson["algorithm"] = "wbf";
    fusionJson["iou_threshold"] = fusion.iouThreshold;
    fusionJson["skip_box_threshold"] = fusion.skipBoxThreshold;
    fusionJson["boxes_pooled"] = static_cast<Json::UInt64>( fused.boxesPooled );
    fusionJson["boxes_gated"] = static_cast<Json::UInt64>( fused.boxesGated );
    fusionJson["boxes_clustered"] = static_cast<Json::UInt64>( fused.boxesClustered );
    fusionJson["boxes_merged"] = static_cast<Json::UInt64>( fused.boxesMerged );
    fusionJson["detections"] = static_cast<Json::UInt64>( fused.clusters );
    // ADR 0171: per-member surviving/merged counts (#1186).
    Json::Value surviving( Json::arrayValue );
    Json::Value merged( Json::arrayValue );
    for ( std::size_t i = 0; i < fused.memberSurviving.size(); ++i )
    {
      surviving.append( static_cast<Json::UInt64>( fused.memberSurviving[i] ) );
      const std::size_t m = i < fused.memberMerged.size() ? fused.memberMerged[i] : 0;
      merged.append( static_cast<Json::UInt64>( m ) );
    }
    fusionJson["member_surviving"] = surviving;
    fusionJson["member_merged"] = merged;
    provenance["fusion"] = fusionJson;
    // A vector product: the output block describes features, not a grid.
    provenance["output"]["format"] = "vector";
    provenance["output"]["features"] = static_cast<Json::UInt64>( fused.clusters );
    std::string sidecarError;
    if ( !publishProvenanceSidecar( detectionFinal, provenance, "ensemble.publish_sidecar",
                                    &sidecarError ) )
      throw RSOperatorError( ErrorCode::FileNotWritable, sidecarError );
    publishGuard.disarm();

    ModelExecutionResult result;
    result.identityTag = ensembleModel.identityTag();
    result.backend = "ensemble(wbf)";
    QString devices;
    for ( const MemberRun &run : runs )
    {
      if ( !devices.isEmpty() )
        devices += QLatin1Char( ',' );
      devices += QString::fromStdString( run.session->deviceName() );
    }
    result.device = devices.toStdString();

    Json::Value payload( Json::objectValue );
    payload["output"] = request.outputPath;
    payload["backend"] = result.backend;
    payload["device"] = result.device;
    payload["model"] = ensembleModel.stableId();
    payload["combination"] = combination;
    payload["detections"] = static_cast<Json::UInt64>( fused.clusters );
    payload["rawDetections"] = static_cast<Json::UInt64>( fused.boxesPooled );
    Json::Value classes( Json::arrayValue );
    for ( const std::string &cls : vocabulary )
      classes.append( cls );
    payload["classes"] = classes;
    Json::Value fusionPayload( Json::objectValue );
    fusionPayload["algorithm"] = "wbf";
    fusionPayload["iou_threshold"] = fusion.iouThreshold;
    fusionPayload["skip_box_threshold"] = fusion.skipBoxThreshold;
    fusionPayload["detections"] = static_cast<Json::UInt64>( fused.clusters );
    payload["fusion"] = fusionPayload;
    Json::Value membersPayload( Json::arrayValue );
    for ( const MemberRun &run : runs )
    {
      Json::Value member( Json::objectValue );
      member["model"] = run.model.stableId();
      member["identity_tag"] = run.model.identityTag();
      member["weight"] = run.weight;
      member["backend"] = run.session->backendName();
      member["device"] = run.session->deviceName();
      member["framework"] = run.selection.resolvedFramework.empty() ? run.model.framework
                                                                   : run.selection.resolvedFramework;
      if ( !run.selection.resolvedFramework.empty() && run.selection.attempts.size() > 1 )
        member["provider_selection"] = run.selection.toJson();
      member["tiles"] = run.detectionStats.tilesProcessed;
      member["detections"] = run.detectionStats.detectionsKept;
      membersPayload.append( member );
    }
    payload["ensemble_members"] = membersPayload;
    result.payload = std::move( payload );
    return result;
  }

  if ( request.asSceneClassification )
  {
    // ================= Scene-classification ensemble ====================
    // Every member must declare the SAME class vocabulary (names and order):
    // the combination is defined over aligned per-class probability vectors,
    // and an implicit reorder would silently relabel the product.
    for ( const MemberRun &run : runs )
    {
      if ( run.model.output.classes.empty() )
        throw RSOperatorError(
          ErrorCode::InvalidInputData,
          "ensemble member '" + run.model.stableId()
            + "' declares no output.classes — scene classification combines per-class "
              "probability vectors over a shared vocabulary" );
    }
    const std::vector<std::string> &vocabulary = runs.front().model.output.classes;
    for ( std::size_t i = 1; i < runs.size(); ++i )
    {
      if ( runs[i].model.output.classes != vocabulary )
        throw RSOperatorError(
          ErrorCode::InvalidInputData,
          "ensemble member '" + runs[i].model.stableId()
            + "' declares a different class vocabulary than the primary member '"
            + runs.front().model.stableId()
            + "' — scene-classification fusion requires one shared vocabulary "
              "(names and order); a silent remap would relabel the product" );
    }
    const int classCount = static_cast<int>( vocabulary.size() );

    runMembersBounded(
      runs, budget, context,
      [ & ]( std::size_t index, MemberRun &run, RSOperatorContext &child ) {
        context.reportProgressForced(
          static_cast<double>( index ) / static_cast<double>( runs.size() + 1 ),
          "Running scene-classification ensemble member " + std::to_string( index + 1 ) + "/"
            + std::to_string( runs.size() ) + " (" + run.model.stableId() + ")" );
        runSceneMember( run, child, request, request.bands );
      } );
    collectMemberOutcomes( runs, context );

    // Combine. weighted_mean: combined_c = Σ w·p_c / Σ w (a proper
    // distribution when every member is logit-semantics; otherwise the
    // weighted mean of the members' clamped scores — never renormalized
    // silently). weighted_vote: each member votes its argmax (ties lowest
    // index) with its weight; the winner is the highest accumulated vote,
    // ties to the lowest class index; agreement = winner vote share.
    // (All-zero weights are refused before session acquisition — single
    // authority in runEnsembleInference; the sum feeds the mean/vote.)
    double weightSum = 0.0;
    for ( const MemberRun &run : runs )
      weightSum += run.weight;

    std::vector<double> combined( static_cast<std::size_t>( classCount ), 0.0 );
    std::vector<double> votes( static_cast<std::size_t>( classCount ), 0.0 );
    for ( const MemberRun &run : runs )
    {
      if ( static_cast<int>( run.scene.probabilities.size() ) != classCount )
        throw RSOperatorError( ErrorCode::ComputationError,
                               "ensemble member '" + run.model.stableId()
                                 + "' produced " + std::to_string( run.scene.probabilities.size() )
                                 + " class scores but the shared vocabulary declares "
                                 + std::to_string( classCount ) );
      for ( int c = 0; c < classCount; ++c )
        combined[static_cast<std::size_t>( c )] +=
          run.weight * run.scene.probabilities[static_cast<std::size_t>( c )];
      int best = 0;
      for ( int c = 1; c < classCount; ++c )
        if ( run.scene.probabilities[static_cast<std::size_t>( c )]
             > run.scene.probabilities[static_cast<std::size_t>( best )] )
          best = c;
      votes[static_cast<std::size_t>( best )] += run.weight;
    }
    for ( double &value : combined )
      value /= weightSum;

    int predicted = 0;
    double agreement = 0.0;
    if ( combination == "weighted_vote" )
    {
      for ( int c = 1; c < classCount; ++c )
        if ( votes[static_cast<std::size_t>( c )] > votes[static_cast<std::size_t>( predicted )] )
          predicted = c; // strict > keeps the LOWEST index on ties
      agreement = votes[static_cast<std::size_t>( predicted )] / weightSum;
    }
    else
    {
      for ( int c = 1; c < classCount; ++c )
        if ( combined[static_cast<std::size_t>( c )] > combined[static_cast<std::size_t>( predicted )] )
          predicted = c; // ties keep the lowest index (deterministic)
    }

    // Typed classification artifact (exp-rs-classification/1 + the additive
    // `ensemble` block). `probabilities` carries the weighted mean of the
    // member vectors under BOTH combinations; `predicted_index` follows the
    // combination (argmax of the mean / vote winner).
    Json::Value doc( Json::objectValue );
    doc["schema"] = "exp-rs-classification/1";
    Json::Value artifact( Json::objectValue );
    artifact["kind"] = "classification";
    artifact["path"] = request.outputPath;
    artifact["schema_version"] = 1;
    doc["artifact"] = artifact;
    doc["predicted_index"] = predicted;
    doc["predicted_class"] = vocabulary[static_cast<std::size_t>( predicted )];
    Json::Value probabilitiesJson( Json::objectValue );
    for ( int c = 0; c < classCount; ++c )
      probabilitiesJson[vocabulary[static_cast<std::size_t>( c )].c_str()] =
        combined[static_cast<std::size_t>( c )];
    doc["probabilities"] = probabilitiesJson;
    doc["score_semantics"] = combination == "weighted_vote" ? "ensemble_weighted_vote"
                                                            : "ensemble_weighted_mean";
    if ( combination == "weighted_vote" )
      doc["agreement"] = agreement;
    Json::Value scene( Json::objectValue );
    scene["width"] = runs.front().scene.width;
    scene["height"] = runs.front().scene.height;
    scene["bands"] = runs.front().scene.bands;
    scene["valid_fraction"] = runs.front().scene.totalSamples > 0
                                ? static_cast<double>( runs.front().scene.validSamples )
                                    / static_cast<double>( runs.front().scene.totalSamples )
                                : 0.0;
    doc["scene"] = scene;
    Json::Value modelJson( Json::objectValue );
    modelJson["name"] = ensembleModel.name;
    modelJson["identity_tag"] = ensembleModel.identityTag();
    modelJson["task"] = ensembleModel.task;
    doc["model"] = modelJson;
    // The ensemble block: members, weights, execution identity and the
    // per-member score semantics (the combined vector's meaning depends on
    // them — recorded, never inferred).
    Json::Value ensembleJson( Json::objectValue );
    ensembleJson["combination"] = combination;
    ensembleJson["member_concurrency"] = budget;
    Json::Value membersJson( Json::arrayValue );
    for ( const MemberRun &run : runs )
    {
      Json::Value member( Json::objectValue );
      member["identity_tag"] = run.model.identityTag();
      if ( !run.model.contentDigest.empty() )
        member["content_digest"] = run.model.contentDigest;
      // Member identity parity with the other ensemble lanes (truthful
      // absence when the member declares neither).
      if ( !run.model.task.empty() )
        member["task"] = run.model.task;
      if ( !run.model.packageDigest.empty() )
        member["package_digest"] = run.model.packageDigest;
      member["framework"] = run.selection.resolvedFramework.empty() ? run.model.framework
                                                                    : run.selection.resolvedFramework;
      member["weight"] = run.weight;
      member["score_semantics"] = run.scene.scoreSemantics;
      if ( run.session )
      {
        member["backend"] = run.session->backendName();
        member["device"] = run.session->deviceName();
        const ProviderRuntimeDetails details = run.session->providerDetails();
        if ( !details.executionProvider.empty() )
          member["execution_provider"] = details.executionProvider;
        if ( !details.runtimeVersion.empty() )
          member["runtime_version"] = details.runtimeVersion;
      }
      if ( run.selection.attempts.size() > 1 )
        member["provider_selection"] = run.selection.toJson();
      membersJson.append( member );
    }
    ensembleJson["members"] = membersJson;
    doc["ensemble"] = ensembleJson;
    Json::Value inputJson( Json::objectValue );
    inputJson["path"] = request.inputPath;
    inputJson["fingerprint"] = runs.front().scene.inputFingerprint;
    doc["input"] = inputJson;

    ModelExecutionResult result;
    result.identityTag = ensembleModel.identityTag();
    result.backend = "ensemble(" + combination + ")";
    QString devices;
    for ( const MemberRun &run : runs )
    {
      if ( !devices.isEmpty() )
        devices += QLatin1Char( ',' );
      devices += QString::fromStdString( run.session->deviceName() );
    }
    result.device = devices.toStdString();
    // Hardening 15/20: the durable artifact carries the SAME top-level
    // identity fields as the returned payload. They used to be added only
    // AFTER the publish, so the on-disk document silently lacked
    // backend/device/model_ref/ensemble_members.
    doc["backend"] = result.backend;
    doc["device"] = result.device;
    doc["model_ref"] = ensembleModel.stableId();
    Json::Value membersPayload( Json::arrayValue );
    for ( const MemberRun &run : runs )
    {
      Json::Value member( Json::objectValue );
      member["model"] = run.model.stableId();
      member["identity_tag"] = run.model.identityTag();
      member["weight"] = run.weight;
      member["backend"] = run.session->backendName();
      member["device"] = run.session->deviceName();
      member["framework"] = run.selection.resolvedFramework.empty() ? run.model.framework
                                                                   : run.selection.resolvedFramework;
      if ( !run.selection.resolvedFramework.empty() && run.selection.attempts.size() > 1 )
        member["provider_selection"] = run.selection.toJson();
      membersPayload.append( member );
    }
    doc["ensemble_members"] = membersPayload;
    TileInferenceEngine::publishClassificationArtifact( doc, request.outputPath );
    result.payload = std::move( doc );
    return result;
  }

  // ========================= Raster ensemble ==============================
  // Derived output modes (Platform 13.0): the request-level collapse is
  // defined ONLY over the weighted mean of member probabilities. weighted_vote
  // publishes its own label+agreement product (requesting Labels on it is the
  // identity; mask/confidence are undefined over hard votes).
  const RasterOutputMode mode = request.outputMode;
  if ( mode != RasterOutputMode::Probability && combination == "weighted_vote"
       && mode != RasterOutputMode::Labels )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: derived output mode '"
                             + ( mode == RasterOutputMode::Mask ? std::string( "mask" )
                                                                : std::string( "confidence" ) )
                             + "' is undefined over weighted votes — the vote product IS the "
                               "label product (with its agreement band)" );
  const bool derived = mode != RasterOutputMode::Probability && combination != "weighted_vote";
  const std::string uncertaintyToken =
    ensembleModel.ensemble.uncertainty.empty() ? "auto" : ensembleModel.ensemble.uncertainty;
  if ( derived )
  {
    // Under a derived request the natural uncertainty band IS the derived
    // collapse (auto resolves to none); an explicit variance band cannot
    // share one GDAL dataset dtype with a Byte product — the same refusal
    // the single-model engine makes.
    if ( uncertaintyToken == "variance" )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "model '" + ensembleModel.name
                               + "' is an ensemble: the variance uncertainty band requires the "
                                 "combined probability stack; a derived output mode publishes "
                                 "its own single-band product" );
    if ( mode == RasterOutputMode::Labels && ensembleModel.postprocess.maskThreshold >= 0.0 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "postprocess.mask_threshold is meaningless with labels output "
                               "(argmax never thresholds) — remove one of the two" );
    if ( !ensembleModel.postprocess.classMapping.empty()
         || !ensembleModel.postprocess.morphology.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "model '" + ensembleModel.name
                               + "' is an ensemble: class remapping and morphology are "
                                 "single-model product knobs — the ensemble publishes the "
                                 "combination's product" );
  }

  const QFileInfo outputInfo( QString::fromStdString( request.outputPath ) );
  for ( std::size_t i = 0; i < runs.size(); ++i )
  {
    runs[i].stagedPath = QDir( outputInfo.absolutePath() ).filePath(
      QString( ".%1.ensemble-member%2.tmp~" ).arg( outputInfo.fileName(), QString::number( i ) ) );
  }

  // RAII: every staged path (members + sidecars + final stage) is removed
  // unless the final product was published. A member failure, a combine
  // failure, a cancellation — none of them may leave an output behind.
  StagedFileGuard stagedGuard;
  for ( const MemberRun &run : runs )
  {
    stagedGuard.addPath( run.stagedPath );
    stagedGuard.addPath( run.stagedPath + QStringLiteral( ".prov.json" ) );
    // The member engine's own streaming stage (its writer closes and renames
    // it, but a cancelled member can leave it behind).
    stagedGuard.addPath( run.stagedPath + QStringLiteral( ".tmp~" ) );
  }

  const std::vector<int> bands = request.bands;
  runMembersBounded( runs, budget, context,
                     [ & ]( std::size_t index, MemberRun &run, RSOperatorContext &child ) {
                       context.reportProgressForced(
                         static_cast<double>( index ) / static_cast<double>( runs.size() + 1 ),
                         "Running ensemble member " + std::to_string( index + 1 ) + "/"
                           + std::to_string( runs.size() ) + " (" + run.model.stableId() + ")" );
                       runRasterMember( index, run, child, request, bands );
                     } );
  collectMemberOutcomes( runs, context );

  // Aggregate stats in member order (completion order never mattered).
  TileInferenceStats combined;
  for ( std::size_t i = 0; i < runs.size(); ++i )
  {
    const TileInferenceStats &stats = runs[i].stats;
    if ( i == 0 )
    {
      combined.tileSize = stats.tileSize;
      combined.halo = stats.halo;
      combined.batchSize = stats.batchSize;
      combined.inputGrids = stats.inputGrids;
      combined.eoPreflight = stats.eoPreflight;
    }
    combined.tilesPlanned += stats.tilesPlanned;
    combined.tilesProcessed += stats.tilesProcessed;
    combined.tilesSkippedNoData += stats.tilesSkippedNoData;
    combined.batchReductions += stats.batchReductions;
  }

  // Grid + channel agreement. Members ran the SAME windowed input on the
  // same grid authority, so a divergence here is an engine contract bug or
  // a head-count mismatch — refuse instead of combining misaligned planes.
  std::vector<MemberGrid> grids;
  grids.reserve( runs.size() );
  for ( std::size_t i = 0; i < runs.size(); ++i )
  {
    MemberGrid grid = readGrid( runs[i].stagedPath );
    if ( i > 0 )
    {
      const MemberGrid &first = grids.front();
      bool shapeMismatch = grid.width != first.width || grid.height != first.height
                           || grid.bands != first.bands;
      bool geoMismatch = false;
      if ( !shapeMismatch )
      {
        // Spatial reference must agree too: identical grid NUMBERS under a
        // different CRS/geotransform describe different places, and the
        // product would silently carry the primary member's reference.
        geoMismatch = grid.projection != first.projection;
        for ( int g = 0; g < 6 && !geoMismatch; ++g )
          geoMismatch = std::fabs( grid.geotransform[g] - first.geotransform[g] ) > 1e-6;
      }
      if ( shapeMismatch || geoMismatch )
        throw RSOperatorError(
          ErrorCode::InvalidInputData,
          "ensemble member '" + runs[i].model.stableId() + "' produced a " + std::to_string( grid.width )
            + "x" + std::to_string( grid.height ) + "x" + std::to_string( grid.bands )
            + ( geoMismatch ? " stack with a different spatial reference"
                            : " stack" )
            + ", but the primary member produced " + std::to_string( first.width ) + "x"
            + std::to_string( first.height ) + "x" + std::to_string( first.bands )
            + " - members must agree on the output grid, spatial reference and channel count"
              " (combination '" + combination + "')" );
    }
    grids.push_back( grid );
  }

  context.throwIfCancelled();
  const int width = grids.front().width;
  const int height = grids.front().height;
  const int channels = grids.front().bands;
  const bool vote = combination == "weighted_vote";
  if ( width <= 0 || height <= 0 || channels <= 0 )
    throw RSOperatorError( ErrorCode::GdalError,
                           "ensemble member output has an empty grid ("
                             + std::to_string( width ) + "x" + std::to_string( height ) + "x"
                             + std::to_string( channels ) + ")" );
  if ( vote && channels > 65535 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "ensemble product needs " + std::to_string( channels )
                             + " label classes; the maximum encodable vote domain is 65535 "
                               "classes (combination 'weighted_vote')" );

  // Uncertainty plan (the vocabulary and the combination/uncertainty
  // agreement are validated at manifest parse; "auto" resolves by mode).
  bool withUncertainty = true;
  std::string uncertaintyNote;
  if ( uncertaintyToken == "none" || ( derived && uncertaintyToken == "auto" ) )
  {
    withUncertainty = false;
  }
  else if ( uncertaintyToken == "auto" )
  {
    uncertaintyNote = vote ? "agreement" : "variance";
  }
  else
  {
    uncertaintyNote = uncertaintyToken;
  }

  // Weights, normalized once. (All-zero weight sets are refused before
  // session acquisition — single authority in runEnsembleInference.)
  double weightSum = 0.0;
  for ( const MemberRun &run : runs )
    weightSum += run.weight;

  // RAII for the combine pass: every escape (typed failure, cancellation,
  // normal end) closes the member handles and the staged writer exactly once
  // — GTiff must be closed before the stage file is removable on Windows.
  std::vector<GDALDatasetH> memberDs( runs.size(), nullptr );
  auto closeMembers = [ & ]() {
    for ( GDALDatasetH &ds : memberDs )
    {
      if ( ds )
      {
        GDALClose( ds );
        ds = nullptr;
      }
    }
  };
  struct HandleGuard
  {
    std::function<void()> close;
    ~HandleGuard() { close(); }
  } membersGuard{ closeMembers };

  GDALDatasetH dst = nullptr;
  auto closeWriter = [ & ]() {
    if ( dst )
    {
      GDALClose( dst );
      dst = nullptr;
    }
  };
  struct WriterGuard
  {
    std::function<void()> close;
    ~WriterGuard() { close(); }
  } writerGuard{ closeWriter };

  for ( std::size_t i = 0; i < runs.size(); ++i )
  {
    memberDs[i] = GDALOpen( runs[i].stagedPath.toUtf8().constData(), GA_ReadOnly );
    if ( !memberDs[i] )
      throw RSOperatorError( ErrorCode::GdalError,
                             "ensemble combine pass could not open member output: "
                               + runs[i].stagedPath.toStdString() );
  }

  // Final stage + writer. The stage is renamed onto the caller's path only
  // after the combine pass fully succeeded; the staged guard removes it on
  // every failure path (after the writer is closed).
  const QString finalPath = QString::fromStdString( request.outputPath );
  const QString stagePath = finalPath + QStringLiteral( ".tmp~" );
  QFile::remove( stagePath );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    throw RSOperatorError( ErrorCode::GdalError, "GDAL GTiff driver is unavailable" );
  const int outBands = vote || derived ? 1 : channels;
  const int totalBands = outBands + ( withUncertainty ? 1 : 0 );
  // Derived collapses mirror the single-model engine's product domains:
  // labels Byte/UInt16 with the sentinel-excluded domain, mask Byte,
  // confidence float32. The probability stack stays float32.
  int labelDomain = 0;
  if ( mode == RasterOutputMode::Labels && !vote )
  {
    // The writable domain is the combined class (channel) count; a declared
    // vocabulary is used for the NAMES metadata only when it matches that
    // count (checked below) — never to renumber the domain.
    labelDomain = channels;
    if ( labelDomain > 65535 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "ensemble product declares a label domain of "
                               + std::to_string( labelDomain )
                               + " classes; the maximum encodable domain is 65535 "
                                 "(NoData sentinel excluded)" );
  }
  const GDALDataType writeType =
    vote ? ( channels <= 255 ? GDT_Byte : GDT_UInt16 )
          : ( mode == RasterOutputMode::Labels
                ? ( labelDomain <= 255 ? GDT_Byte : GDT_UInt16 )
                : ( mode == RasterOutputMode::Mask ? GDT_Byte : GDT_Float32 ) );
  // Platform 13.0 staging: the combine stage is compressed+tiled by default
  // (the published product is what the user keeps; the 12.0 uncompressed
  // stage was a known disk-cost limitation). The knob is the manifest's
  // `ensemble.staging_compression`; "none" reproduces the historical bytes.
  const bool stagingCompressed = ensembleModel.ensemble.stagingCompressed();
  const char *createOptions[] = { "TILED=YES", "BLOCKXSIZE=256", "BLOCKYSIZE=256",
                                  "COMPRESS=DEFLATE", nullptr };
  dst = GDALCreate( driver, stagePath.toUtf8().constData(), width, height, totalBands, writeType,
                    stagingCompressed ? const_cast<char **>( createOptions ) : nullptr );
  if ( !dst )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "ensemble combine pass could not create the staged output: "
                             + stagePath.toStdString() );
  stagedGuard.addPath( stagePath );

  // Label/uncertainty encoding for vote products: GDAL bands share one
  // dtype, so the agreement band is quantized into the label raster's dtype
  // with the encoding recorded in the payload/sidecar. The quantized range
  // excludes the NoData sentinel (share 1.0 must not encode as NoData).
  const int labelDomainForNoData = vote ? channels : labelDomain;
  const double writeNoData = ( vote || mode == RasterOutputMode::Labels )
                               ? ( labelDomainForNoData <= 255 ? 255.0 : 65535.0 )
                               : ( mode == RasterOutputMode::Mask
                                     ? 255.0
                                     : static_cast<double>( kStackNoData ) );
  const double agreementQuant = vote ? writeNoData - 1.0 : 0.0;
  const std::string agreementEncoding =
    vote ? ( "round(share*" + std::to_string( static_cast<long long>( agreementQuant ) ) + ")"
             + "; sentinel " + std::to_string( static_cast<long long>( writeNoData ) ) )
         : std::string();

  GDALSetGeoTransform( dst, grids.front().geotransform );
  GDALSetProjection( dst, grids.front().projection.c_str() );
  for ( int b = 1; b <= totalBands; ++b )
    GDALSetRasterNoDataValue( GDALGetRasterBand( dst, b ), writeNoData );

  // Shared class schema metadata for label products (all members must agree;
  // disagreement degrades to no metadata, never to wrong names).
  if ( vote || mode == RasterOutputMode::Labels )
  {
    const std::vector<std::string> &schema = runs.front().model.output.classes;
    const bool agree =
      std::all_of( runs.begin(), runs.end(),
                   [ & ]( const MemberRun &run ) { return run.model.output.classes == schema; } );
    const bool namesClean =
      std::all_of( schema.begin(), schema.end(), []( const std::string &cls ) {
        return cls.find( ';' ) == std::string::npos;
      } );
    if ( agree && namesClean && !schema.empty()
         && static_cast<int>( schema.size() ) == ( vote ? channels : labelDomain ) )
    {
      QString names;
      for ( const std::string &cls : schema )
      {
        if ( !names.isEmpty() )
          names += QLatin1Char( ';' );
        names += QString::fromStdString( cls );
      }
      GDALSetMetadataItem( dst, "SICNU_CLASS_NAMES", names.toUtf8().constData(), "" );
    }
  }

  // Row-block loop. Block height derives from the pixel budget: members ×
  // channels × width × rows ≤ kCombineBlockValues float32 values.
  const std::int64_t valuesPerRow =
    static_cast<std::int64_t>( runs.size() ) * channels * width;
  int rowsPerBlock = valuesPerRow > 0
                       ? static_cast<int>( std::max<std::int64_t>(
                           1, combineBlockValues() / valuesPerRow ) )
                       : height;
  rowsPerBlock = std::clamp( rowsPerBlock, 1, height );

  const std::size_t planeSize = static_cast<std::size_t>( rowsPerBlock ) * width;
  std::vector<std::vector<float>> memberBuffers( runs.size() );
  for ( std::size_t i = 0; i < runs.size(); ++i )
    memberBuffers[i].assign( planeSize * static_cast<std::size_t>( channels ), 0.0f );
  std::vector<float> productPlane( planeSize * static_cast<std::size_t>( channels ), 0.0f );
  std::vector<float> uncertaintyPlane( planeSize, 0.0f );
  // Row-loop scratch, allocated once (not per block).
  std::vector<float> collapsedScratch( planeSize, 0.0f );
  std::vector<float> scaledScratch( planeSize, 0.0f );
  std::vector<double> voteScratch( static_cast<std::size_t>( channels ), 0.0 );
  std::vector<float> labelScratch( planeSize, 0.0f );
  std::vector<float> agreementScratch( planeSize, 0.0f );
  // Derived-mode product tally (parity with the single-model payload).
  std::vector<long long> classPixelCounts;
  if ( mode == RasterOutputMode::Labels )
    classPixelCounts.assign( static_cast<std::size_t>( std::max( 1, labelDomain ) ), 0 );
  if ( mode == RasterOutputMode::Mask )
    classPixelCounts.assign( 2, 0 );

  // Typed loop failure: cleanup is delegated to the RAII guards below (the
  // members guard and the writer guard), which cover BOTH fail() throws and
  // cancellation escapes — no path leaks an open handle or a stage file.
  auto fail = [ & ]( const std::string &why ) {
    throw RSOperatorError( ErrorCode::GdalError, why );
  };

  bool varianceMode = withUncertainty && !vote && uncertaintyNote == "variance";
  for ( int y = 0; y < height; y += rowsPerBlock )
  {
    if ( y > 0 )
      context.throwIfCancelled();
    const int rows = std::min( rowsPerBlock, height - y );
    const std::size_t blockPlane = static_cast<std::size_t>( rows ) * width;
    for ( std::size_t m = 0; m < runs.size(); ++m )
    {
      for ( int c = 1; c <= channels; ++c )
      {
        float *dstBuffer = memberBuffers[m].data()
                             + static_cast<std::size_t>( c - 1 ) * blockPlane;
        if ( !readBlock( memberDs[m], c, y, rows, dstBuffer ) )
          fail( "ensemble combine pass failed to read band " + std::to_string( c )
                  + " of member '" + runs[m].model.stableId() + "'" );
      }
    }

    if ( !vote )
    {
      // weighted_mean (+ weighted variance). NoData honesty: a NaN in ANY
      // member poisons that (pixel, channel) — one member's skipped tile is
      // the ensemble's skipped tile.
      for ( int c = 1; c <= channels; ++c )
      {
        float *meanPlane = productPlane.data() + static_cast<std::size_t>( c - 1 ) * blockPlane;
        std::fill( meanPlane, meanPlane + blockPlane, 0.0f );
        for ( std::size_t p = 0; p < blockPlane; ++p )
        {
          bool valid = true;
          double acc = 0.0;
          for ( std::size_t m = 0; m < runs.size(); ++m )
          {
            const float v = memberBuffers[m][static_cast<std::size_t>( c - 1 ) * blockPlane + p];
            if ( std::isnan( v ) )
            {
              valid = false;
              break;
            }
            acc += v * runs[m].weight;
          }
          meanPlane[p] = valid ? static_cast<float>( acc / weightSum ) : kStackNoData;
        }
      }
      if ( varianceMode )
      {
        std::fill( uncertaintyPlane.begin(), uncertaintyPlane.begin() + blockPlane, 0.0f );
        for ( int c = 1; c <= channels; ++c )
        {
          const float *meanPlane = productPlane.data() + static_cast<std::size_t>( c - 1 ) * blockPlane;
          for ( std::size_t p = 0; p < blockPlane; ++p )
          {
            const float mean = meanPlane[p];
            if ( std::isnan( mean ) )
            {
              uncertaintyPlane[p] = kStackNoData;
              continue;
            }
            double acc = 0.0;
            for ( std::size_t m = 0; m < runs.size(); ++m )
            {
              const float v = memberBuffers[m][static_cast<std::size_t>( c - 1 ) * blockPlane + p];
              if ( std::isnan( v ) )
                continue; // poisoned pixel: the mean band already carries NoData
              acc += runs[m].weight * ( static_cast<double>( v ) - mean )
                       * ( static_cast<double>( v ) - mean );
            }
            uncertaintyPlane[p] += static_cast<float>( acc / weightSum );
          }
        }
        // Mean per-class variance (scale-free w.r.t. the class count).
        for ( std::size_t p = 0; p < blockPlane; ++p )
          if ( !std::isnan( uncertaintyPlane[p] ) )
            uncertaintyPlane[p] = static_cast<float>( uncertaintyPlane[p] / channels );
      }
      if ( !derived )
      {
        for ( int c = 1; c <= channels; ++c )
        {
          const float *meanPlane = productPlane.data() + static_cast<std::size_t>( c - 1 ) * blockPlane;
          if ( GDALRasterIO( GDALGetRasterBand( dst, c ), GF_Write, 0, y, width, rows,
                             const_cast<float *>( meanPlane ), width, rows, GDT_Float32, 0, 0 )
               != CE_None )
            fail( "ensemble combine pass failed to write band " + std::to_string( c ) );
        }
        if ( varianceMode )
        {
          if ( GDALRasterIO( GDALGetRasterBand( dst, totalBands ), GF_Write, 0, y, width, rows,
                             uncertaintyPlane.data(), width, rows, GDT_Float32, 0, 0 )
               != CE_None )
            fail( "ensemble combine pass failed to write the variance band" );
        }
      }
      else
      {
        // Request-level derived collapse of the COMBINED mean (Platform 13.0):
        // the member preprocessing/task contracts were already enforced per
        // member; this collapse owns the product semantics only.
        //   labels     — argmax over the combined classes, ties → lowest index
        //   confidence — top-1 combined probability
        //   mask       — 1-class: mean >= threshold; else argmax != 0
        const float maskThreshold = ensembleModel.postprocess.maskThreshold >= 0.0
                                      ? static_cast<float>( ensembleModel.postprocess.maskThreshold )
                                      : 0.5f;
        std::vector<float> &collapsed = collapsedScratch;
        std::fill( collapsed.begin(), collapsed.begin() + static_cast<std::ptrdiff_t>( blockPlane ),
                   static_cast<float>( writeNoData ) );
        for ( std::size_t p = 0; p < blockPlane; ++p )
        {
          bool valid = true;
          int best = 0;
          float bestValue = 0.0f;
          for ( int c = 1; c <= channels; ++c )
          {
            const float v = productPlane[static_cast<std::size_t>( c - 1 ) * blockPlane + p];
            if ( std::isnan( v ) )
            {
              valid = false;
              break;
            }
            if ( c == 1 || v > bestValue )
            {
              bestValue = v;
              best = c - 1;
            }
          }
          if ( !valid )
            continue; // NoData sentinel: one member's skipped tile poisons
          switch ( mode )
          {
            case RasterOutputMode::Labels:
              collapsed[p] = static_cast<float>( best );
              if ( best >= 0 && static_cast<std::size_t>( best ) < classPixelCounts.size() )
                classPixelCounts[static_cast<std::size_t>( best )]++;
              break;
            case RasterOutputMode::Confidence:
              collapsed[p] = bestValue;
              break;
            case RasterOutputMode::Mask:
            {
              const float maskValue =
                ( channels == 1 ) ? ( bestValue >= maskThreshold ? 1.0f : 0.0f )
                                  : ( best != 0 ? 1.0f : 0.0f );
              collapsed[p] = maskValue;
              if ( static_cast<std::size_t>( maskValue ) < classPixelCounts.size() )
                classPixelCounts[static_cast<std::size_t>( maskValue )]++;
              break;
            }
            default:
              break;
          }
        }
        if ( GDALRasterIO( GDALGetRasterBand( dst, 1 ), GF_Write, 0, y, width, rows,
                           collapsed.data(), width, rows, GDT_Float32, 0, 0 )
             != CE_None )
          fail( "ensemble combine pass failed to write the derived product band" );
      }
    }
    else
    {
      // weighted_vote: per pixel, each member votes its argmax class with
      // its weight; winner = highest accumulated vote, ties → LOWEST class
      // index (deterministic). agreement = winning vote share, quantized to
      // the label dtype (encoding recorded in the payload/sidecar).
      std::vector<double> &votes = voteScratch;
      std::vector<float> &labels = labelScratch;
      std::vector<float> &agreement = agreementScratch;
      for ( std::size_t p = 0; p < blockPlane; ++p )
      {
        labels[p] = static_cast<float>( writeNoData );
        agreement[p] = static_cast<float>( writeNoData );
        bool anyNoData = false;
        std::fill( votes.begin(), votes.end(), 0.0 );
        for ( std::size_t m = 0; m < runs.size(); ++m )
        {
          const float *base = memberBuffers[m].data() + p;
          float best = base[0];
          bool memberNoData = std::isnan( best );
          int argmax = 0;
          for ( int c = 1; c < channels && !memberNoData; ++c )
          {
            const float v = base[static_cast<std::size_t>( c ) * blockPlane];
            if ( std::isnan( v ) )
              memberNoData = true;
            else if ( v > best )
            {
              best = v;
              argmax = c;
            }
          }
          if ( memberNoData )
          {
            anyNoData = true;
            break;
          }
          votes[static_cast<std::size_t>( argmax )] += runs[m].weight;
        }
        if ( anyNoData )
          continue;
        int winner = 0;
        for ( int c = 1; c < channels; ++c )
          if ( votes[static_cast<std::size_t>( c )] > votes[static_cast<std::size_t>( winner )] )
            winner = c; // strict > keeps the LOWEST index on ties
        labels[p] = static_cast<float>( winner );
        agreement[p] = static_cast<float>( votes[static_cast<std::size_t>( winner )] / weightSum );
      }
      if ( GDALRasterIO( GDALGetRasterBand( dst, 1 ), GF_Write, 0, y, width, rows, labels.data(),
                         width, rows, GDT_Float32, 0, 0 )
           != CE_None )
        fail( "ensemble combine pass failed to write the label band" );
      if ( withUncertainty )
      {
        std::vector<float> &scaled = scaledScratch;
        for ( std::size_t p = 0; p < blockPlane; ++p )
          scaled[p] = std::isnan( agreement[p] )
                        ? static_cast<float>( writeNoData )
                        : static_cast<float>( std::floor( agreement[p] * agreementQuant + 0.5 ) );
        if ( GDALRasterIO( GDALGetRasterBand( dst, totalBands ), GF_Write, 0, y, width, rows,
                           scaled.data(), width, rows, GDT_Float32, 0, 0 )
             != CE_None )
          fail( "ensemble combine pass failed to write the agreement band" );
      }
    }
  }

  closeMembers();
  // GDALClose flushes and closes; GTiff reports write failures only through
  // the file itself, so the publish gate is the closed stage's existence.
  closeWriter();
  if ( !QFile::exists( stagePath ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "ensemble combine pass could not write the staged output: "
                             + stagePath.toStdString() );

  // Output identity for the payload AND the sidecar's grid evidence.
  combined.outBands = totalBands;
  combined.outWidth = width;
  combined.outHeight = height;
  combined.headChannels = { totalBands };

  // Atomic publish + provenance sidecar — the same ProductPublishGuard
  // contract as the single-model raster lanes: the previous pair is parked
  // (sidecar first, main last) and restored on ANY failure; the crash-orphan
  // states a killed run leaves are adopted back on the next publish instead
  // of being destroyed.
  ProductPublishGuard publishGuard( finalPath, QStringLiteral( ".prev~" ),
                                    "ensemble.publish_park", stagePath );
  publishGuard.publishStaged( stagePath, "ensemble.publish_swap",
                              "ensemble combine pass could not publish the product: "
                                + finalPath.toStdString() );
  const Json::Value provenance =
    buildEnsembleProvenance( ensembleModel, runs, combined, combination,
                             withUncertainty
                               ? ( uncertaintyNote
                                     + ( vote && !agreementEncoding.empty()
                                           ? " (" + agreementEncoding + ")" : std::string() ) )
                               : std::string(),
                             budget, stagingCompressed, false );
  std::string sidecarError;
  if ( !publishProvenanceSidecar( finalPath, provenance, "ensemble.publish_sidecar",
                                  &sidecarError ) )
  {
    // The guard's destructor restores the previous product — WITH its
    // sidecar — instead of destroying or downgrading it (same invariant as
    // the single-model engines' writers).
    throw RSOperatorError( ErrorCode::FileNotWritable, sidecarError );
  }
  publishGuard.disarm();

  // Success: release the member stacks and their sidecars explicitly (the
  // staged guard stays as a safety net until the very end — its paths no
  // longer exist).
  for ( const MemberRun &run : runs )
  {
    QFile::remove( run.stagedPath );
    QFile::remove( run.stagedPath + QStringLiteral( ".prov.json" ) );
  }
  stagedGuard.disarm();

  ModelExecutionResult result;
  result.rasterStats = std::move( combined );
  result.identityTag = ensembleModel.identityTag();
  result.backend = "ensemble(" + combination + ")";
  QString devices;
  for ( const MemberRun &run : runs )
  {
    if ( !devices.isEmpty() )
      devices += QLatin1Char( ',' );
    devices += QString::fromStdString( run.session->deviceName() );
  }
  result.device = devices.toStdString();

  Json::Value payload( Json::objectValue );
  payload["output"] = request.outputPath;
  payload["backend"] = result.backend;
  payload["device"] = result.device;
  payload["model"] = ensembleModel.stableId();
  payload["combination"] = combination;
  payload["outBands"] = totalBands;
  payload["width"] = width;
  payload["height"] = height;
  payload["tileSize"] = result.rasterStats.tileSize;
  payload["tiles"] = result.rasterStats.tilesProcessed;
  payload["tilesSkippedNoData"] = result.rasterStats.tilesSkippedNoData;
  if ( result.rasterStats.batchReductions > 0 )
    payload["batchReductions"] = result.rasterStats.batchReductions;
  if ( withUncertainty )
    payload["uncertainty_band"] =
      vote ? "agreement (" + agreementEncoding + ")" : uncertaintyNote;
  if ( !classPixelCounts.empty() )
  {
    Json::Value counts( Json::arrayValue );
    for ( long long pixels : classPixelCounts )
      counts.append( static_cast<Json::Int64>( pixels ) );
    payload["classPixelCounts"] = counts;
    // Names index the counts only when the vocabulary matches the domain.
    if ( mode == RasterOutputMode::Labels && !runs.front().model.output.classes.empty()
         && static_cast<int>( runs.front().model.output.classes.size() ) == labelDomain )
    {
      Json::Value names( Json::arrayValue );
      for ( const std::string &cls : runs.front().model.output.classes )
        names.append( cls );
      payload["classes"] = names;
    }
  }
  Json::Value membersPayload( Json::arrayValue );
  for ( const MemberRun &run : runs )
  {
    Json::Value member( Json::objectValue );
    member["model"] = run.model.stableId();
    member["identity_tag"] = run.model.identityTag();
    member["weight"] = run.weight;
    member["backend"] = run.session->backendName();
    member["device"] = run.session->deviceName();
    member["framework"] = run.selection.resolvedFramework.empty() ? run.model.framework
                                                                  : run.selection.resolvedFramework;
    if ( !run.selection.resolvedFramework.empty() && run.selection.attempts.size() > 1 )
      member["provider_selection"] = run.selection.toJson();
    member["tiles"] = run.stats.tilesProcessed;
    if ( run.stats.batchReductions > 0 )
      member["batchReductions"] = run.stats.batchReductions;
    membersPayload.append( member );
  }
  payload["ensemble_members"] = membersPayload;
  result.payload = std::move( payload );
  return result;
}

} // namespace sicnu::operators::runtime
