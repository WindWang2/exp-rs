// src/operators/runtime/model_ensemble.cpp — see model_ensemble.h.
#include "operators/runtime/model_ensemble.h"

#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"

#include <gdal.h>

#include <QDir>
#include <QFile>
#include <QtGlobal>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <string>
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

/// Removes every staged path on scope exit unless disarmed (the final
/// product was published). Guarantees "no partial output" even when the
/// combine pass throws or the run is cancelled. Paths may be added as the
/// run stages them; removal happens on destruction, after every writer is
/// closed.
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

/// Publishes the provenance sidecar next to a published product — same
/// staged-write + rename contract as the engine's own writer (the caller
/// removes any previous sidecar BEFORE the product rename, so a crash can
/// only ever leave a DETECTABLE absence).
bool publishSidecar( const QString &finalPath, const Json::Value &provenance, std::string *error )
{
  const QString sidecarPath = finalPath + QStringLiteral( ".prov.json" );
  const QString stagePath = sidecarPath + QStringLiteral( ".stage~" );
  QFile stage( stagePath );
  if ( !stage.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
  {
    if ( error )
      *error = "failed to stage the provenance sidecar: " + stagePath.toStdString();
    return false;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string text = Json::writeString( builder, provenance );
  const qint64 written = stage.write( text.data(), static_cast<qint64>( text.size() ) );
  stage.close();
  if ( stage.error() != QFileDevice::NoError || written != static_cast<qint64>( text.size() ) )
  {
    stage.remove();
    if ( error )
      *error = "failed to write the provenance sidecar: " + stagePath.toStdString();
    return false;
  }
  QFile::remove( sidecarPath ); // Windows rename does not overwrite
  if ( !QFile::rename( stagePath, sidecarPath ) )
  {
    stage.remove();
    if ( error )
      *error = "failed to publish the provenance sidecar: " + sidecarPath.toStdString();
    return false;
  }
  return true;
}

/// Per-member execution record for the payload and the provenance sidecar.
struct MemberRun
{
  ModelInfo model;
  double weight = 1.0;   ///< ensemble weight from the ensemble contract
  ModelRuntimePtr session;
  TileInferenceStats stats;
  QString stagedPath;
  ProviderSelectionReport selection;  ///< provider chain trace for this member
};

/// Builds the shared provenance document for the ensemble product (schema
/// exp-rs-prov/1; the `ensemble` member block is additive — historical /1
/// consumers ignore unknown blocks, and verifyProductProvenance validates
/// the shared model/execution/inputs surface unchanged).
Json::Value buildEnsembleProvenance( const ModelInfo &ensembleModel,
                                     const std::vector<MemberRun> &runs,
                                     const TileInferenceStats &combined,
                                     const std::string &combination,
                                     const std::string &uncertaintyNote )
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
  Json::Value membersJson( Json::arrayValue );
  for ( const MemberRun &run : runs )
  {
    Json::Value member( Json::objectValue );
    member["identity_tag"] = run.model.identityTag();
    if ( !run.model.contentDigest.empty() )
      member["content_digest"] = run.model.contentDigest;
    member["framework"] = run.model.framework;
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
    Json::Value execution( Json::objectValue );
    execution["tiles_processed"] = run.stats.tilesProcessed;
    execution["tiles_skipped_nodata"] = run.stats.tilesSkippedNoData;
    if ( run.stats.batchReductions > 0 )
      execution["batch_reductions"] = run.stats.batchReductions;
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

} // namespace

ModelExecutionResult runEnsembleInference( const ModelInfo &ensembleModel,
                                           const ModelExecutionRequest &request,
                                           RSOperatorContext &context )
{
  // Surface exclusions: the ensemble combines RASTER probability products.
  // Detection decode (boxes, NMS) and single-forward scene classification
  // have no defined combination here — a typed refusal, never a silent
  // misread. Multi-feed temporal ensembles and request-level derived output
  // modes are refused for the same honesty.
  if ( request.asDetection )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: detection decode cannot be combined yet "
                               "(weighted_mean / weighted_vote raster products only)" );
  if ( request.asSceneClassification )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: scene classification (single-forward artifacts) "
                               "cannot be combined yet" );
  if ( !request.namedInputs.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: multi-feed requests are not wired for "
                               "ensembles yet — run the members' contracts through a "
                               "non-ensemble model" );
  if ( request.outputMode != RasterOutputMode::Probability )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "model '" + ensembleModel.name
                             + "' is an ensemble: derived output modes (labels/mask/confidence) "
                               "apply to single-model runs only; the ensemble publishes its "
                               "combined probability stack" );
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

  // Resolve + gate every member BEFORE acquiring anything: a broken member
  // is a manifest-level failure, not a mid-run one.
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

  // Acquire each member through its own provider fallback chain.
  auto &registry = ModelRuntimeRegistry::instance();
  const QFileInfo outputInfo( QString::fromStdString( request.outputPath ) );
  std::vector<MemberRun> runs;
  runs.reserve( members.size() );
  for ( std::size_t i = 0; i < members.size(); ++i )
  {
    MemberRun run;
    run.model = members[i];
    run.weight = ensembleModel.ensemble.members[i].weight;
    run.stagedPath = QDir( outputInfo.absolutePath() ).filePath(
      QString( ".%1.ensemble-member%2.tmp~" ).arg( outputInfo.fileName(), QString::number( i ) ) );

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

  // RAII: every staged path (members + final stage) is removed unless the
  // final product was published. A member failure, a combine failure, a
  // cancellation — none of them may leave an output behind.
  StagedFileGuard stagedGuard;
  for ( const MemberRun &run : runs )
    stagedGuard.addPath( run.stagedPath );

  TileInferenceStats combined;
  const std::vector<int> bands = request.bands;
  for ( std::size_t i = 0; i < runs.size(); ++i )
  {
    context.throwIfCancelled();
    MemberRun &run = runs[i];
    context.reportProgressForced(
      static_cast<double>( i ) / static_cast<double>( runs.size() + 1 ),
      "Running ensemble member " + std::to_string( i + 1 ) + "/" + std::to_string( runs.size() )
        + " (" + run.model.stableId() + ")" );
    // Members always produce probability stacks — the combination owns the
    // product semantics; a member applying its own derived collapse would
    // destroy the probabilities the vote/mean needs.
    TileInferenceRunOptions memberOptions;
    memberOptions.tta = TtaMode::None;
    memberOptions.batchSizeOverride = std::max( 0, request.batchSizeOverride );
    memberOptions.outputMode = RasterOutputMode::Probability;
    memberOptions.blend = request.blend;
    memberOptions.computeFeedFingerprints = i == 0; // grid provenance from the primary member
    TileInferenceEngine engine( run.model, run.session );
    run.stats = engine.run( request.inputPath, bands, run.stagedPath.toStdString(), context,
                            memberOptions );
    if ( i == 0 )
    {
      combined.tileSize = run.stats.tileSize;
      combined.halo = run.stats.halo;
      combined.batchSize = run.stats.batchSize;
      combined.inputGrids = run.stats.inputGrids;
      combined.eoPreflight = run.stats.eoPreflight;
    }
    combined.tilesPlanned += run.stats.tilesPlanned;
    combined.tilesProcessed += run.stats.tilesProcessed;
    combined.tilesSkippedNoData += run.stats.tilesSkippedNoData;
    combined.batchReductions += run.stats.batchReductions;
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
  const std::string uncertaintyToken =
    ensembleModel.ensemble.uncertainty.empty() ? "auto" : ensembleModel.ensemble.uncertainty;
  bool withUncertainty = true;
  std::string uncertaintyNote;
  if ( uncertaintyToken == "none" )
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

  // Weights, normalized once. An all-zero weight set makes both combinations
  // undefined (mean divides by zero; vote degenerates to the lowest class).
  double weightSum = 0.0;
  for ( const MemberRun &run : runs )
    weightSum += run.weight;
  if ( !( weightSum > 0.0 ) )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "ensemble weights sum to zero - the combination is undefined "
                             "(declare at least one positive weight)" );

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
  const int outBands = vote ? 1 : channels;
  const int totalBands = outBands + ( withUncertainty ? 1 : 0 );
  const GDALDataType writeType = vote ? ( channels <= 255 ? GDT_Byte : GDT_UInt16 )
                                      : GDT_Float32;
  dst =
    GDALCreate( driver, stagePath.toUtf8().constData(), width, height, totalBands, writeType, nullptr );
  if ( !dst )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "ensemble combine pass could not create the staged output: "
                             + stagePath.toStdString() );
  stagedGuard.addPath( stagePath );

  // Label/uncertainty encoding for vote products: GDAL bands share one
  // dtype, so the agreement band is quantized into the label raster's dtype
  // with the encoding recorded in the payload/sidecar. The quantized range
  // excludes the NoData sentinel (share 1.0 must not encode as NoData).
  const double writeNoData = vote ? ( channels <= 255 ? 255.0 : 65535.0 ) : static_cast<double>( kStackNoData );
  const double agreementQuant = vote ? writeNoData - 1.0 : 0.0;
  const std::string agreementEncoding =
    vote ? ( "round(share*" + std::to_string( static_cast<long long>( agreementQuant ) ) + ")"
             + "; sentinel " + std::to_string( static_cast<long long>( writeNoData ) ) )
         : std::string();

  GDALSetGeoTransform( dst, grids.front().geotransform );
  GDALSetProjection( dst, grids.front().projection.c_str() );
  for ( int b = 1; b <= totalBands; ++b )
    GDALSetRasterNoDataValue( GDALGetRasterBand( dst, b ), writeNoData );

  // Shared class schema metadata for vote products (all members must agree;
  // disagreement degrades to no metadata, never to wrong names).
  if ( vote )
  {
    const std::vector<std::string> &schema = runs.front().model.output.classes;
    const bool agree =
      std::all_of( runs.begin(), runs.end(),
                   [ & ]( const MemberRun &run ) { return run.model.output.classes == schema; } );
    const bool namesClean =
      std::all_of( schema.begin(), schema.end(), []( const std::string &cls ) {
        return cls.find( ';' ) == std::string::npos;
      } );
    if ( agree && namesClean && !schema.empty() && static_cast<int>( schema.size() ) == channels )
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
      // weighted_vote: per pixel, each member votes its argmax class with
      // its weight; winner = highest accumulated vote, ties → LOWEST class
      // index (deterministic). agreement = winning vote share, quantized to
      // the label dtype (encoding recorded in the payload/sidecar).
      std::vector<double> votes( static_cast<std::size_t>( channels ), 0.0 );
      std::vector<float> labels( blockPlane );
      std::vector<float> agreement( blockPlane );
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
        std::vector<float> scaled( blockPlane );
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

  // Atomic publish + provenance sidecar (the sidecar can only ever be
  // ABSENT after a crash, never stale).
  const bool hadExisting = QFile::exists( finalPath );
  const QString backupPath = finalPath + QStringLiteral( ".prev~" );
  if ( hadExisting )
  {
    QFile::remove( backupPath );
    if ( !QFile::rename( finalPath, backupPath ) )
    {
      QFile::remove( stagePath );
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "ensemble combine pass could not back up the previous product: "
                               + finalPath.toStdString() );
    }
  }
  if ( !QFile::rename( stagePath, finalPath ) )
  {
    if ( hadExisting )
      QFile::rename( backupPath, finalPath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "ensemble combine pass could not publish the product: "
                             + finalPath.toStdString() );
  }
  if ( hadExisting )
    QFile::remove( backupPath );
  QFile::remove( finalPath + QStringLiteral( ".prov.json" ) );
  const Json::Value provenance =
    buildEnsembleProvenance( ensembleModel, runs, combined, combination,
                             withUncertainty
                               ? ( uncertaintyNote
                                     + ( vote && !agreementEncoding.empty()
                                           ? " (" + agreementEncoding + ")" : std::string() ) )
                               : std::string() );
  std::string sidecarError;
  if ( !publishSidecar( finalPath, provenance, &sidecarError ) )
    throw RSOperatorError( ErrorCode::FileNotWritable, sidecarError );

  // Success: release the member stacks explicitly (the staged guard stays
  // as a safety net until the very end — its paths no longer exist).
  for ( const MemberRun &run : runs )
    QFile::remove( run.stagedPath );
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
