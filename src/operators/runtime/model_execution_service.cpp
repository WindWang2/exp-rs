// src/operators/runtime/model_execution_service.cpp
#include "operators/runtime/model_execution_service.h"

#include "runtime/observability/fault_point.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/tile_inference_engine.h"

#include "processing/features/feature_cube.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <string>

namespace sicnu::operators::runtime {

ModelInfo resolveModelReference( const std::string &modelReference, std::string *errorDetail )
{
  const QFileInfo direct( QString::fromStdString( modelReference ) );
  if ( direct.exists() && direct.isFile() )
  {
    ModelInfo info;
    info.name = modelReference;
    info.task = "inference";
    info.framework = "onnx";
    info.readiness = ModelReadiness::Ready;
    info.resolvedArtifactPath = direct.absoluteFilePath().toStdString();
    info.path = modelReference;
    return info;
  }

  // Catalog lookup: lazy-loads on first use so run_workflow / direct operator
  // calls resolve names without a prior spatial:list_models call. A miss
  // triggers ONE refresh so newly installed models are found without paying
  // a directory rescan on every run.
  auto model = ModelCatalog::instance().find( modelReference );
  if ( !model )
  {
    ModelCatalog::instance().reload();
    model = ModelCatalog::instance().find( modelReference );
  }
  if ( !model )
  {
    if ( errorDetail )
      *errorDetail = "Model file not found and not a catalog name: " + modelReference
                     + " (catalog directory: " + ModelCatalog::instance().directory() + ")";
    ModelInfo missing;
    missing.readiness = ModelReadiness::MissingArtifact;
    return missing;
  }
  if ( model->readiness != ModelReadiness::Ready )
  {
    if ( errorDetail )
      *errorDetail = "Model '" + model->name + "' is not ready ("
                     + modelReadinessName( model->readiness ) + "): "
                     + ( model->readinessReason.empty() ? std::string( "unavailable" )
                                                        : model->readinessReason );
    return *model; // readiness != Ready signals the failure
  }
  return *model;
}

namespace {

/// Contract gates shared by every surface (inherited from rs:infer 3.0).
/// Platform 7.0: temporal / multi-input models execute through the
/// runMultiInput path — the loud refusal now applies only when the caller
/// did NOT provide the named feeds the contract demands (a silent single
/// frame or single branch run would be the #646 failure class).
void rejectUnwiredContracts( const ModelInfo &model,
                             const std::vector<NamedRasterFeed> &namedInputs )
{
  const bool multiFeeds = !namedInputs.empty();
  for ( const auto &input : model.inputs )
  {
    const bool temporal =
      input.temporalLength > 0
      || ( input.temporalCollapse == "sequence" && input.temporalDynamic );
    if ( temporal && !multiFeeds )
      throw RSOperatorError(
        ErrorCode::InvalidInputData,
        "Model '" + model.name + "' declares a temporal input (temporal_length=" +
          std::to_string( input.temporalLength ) +
          ( input.temporalDynamic ? ", dynamic T" : "" ) +
          "); provide temporal feed frames through the multi-input request "
          "(named inputs) — the single-input path would silently run one frame" );
  }
  if ( model.inputs.size() > 1 && !multiFeeds )
    throw RSOperatorError(
      ErrorCode::InvalidInputData,
      "Model '" + model.name + "' declares " + std::to_string( model.inputs.size() )
        + " named inputs; provide one feed per input through the multi-input "
          "request (named inputs) — the single-input path would silently run "
          "one branch" );
  if ( model.inputs.size() <= 1 && multiFeeds )
    throw RSOperatorError(
      ErrorCode::InvalidInputData,
      "Model '" + model.name + "' declares " + std::to_string( model.inputs.size() )
        + " input(s); named multi-input feeds apply to multi-input models — "
          "use the single-input path (input + bands)" );
}

/// Feature-cube preflight (goal §8 train/inference consistency): when the
/// input carries a feature cube contract, the model's declared band roles
/// must be covered. Plain rasters skip this check.
void preflightFeatureCube( const ModelInfo &model, const std::string &inputPath )
{
  sicnu::features::FeatureCubeContract cube;
  if ( sicnu::features::readFeatureCubeMetadata( QString::fromStdString( inputPath ), &cube ) )
  {
    const QStringList roles = [ & ] {
      QStringList out;
      for ( const auto &role : model.input.bandRoles )
        out << QString::fromStdString( role );
      return out;
    }();
    const sicnu::features::ModelInputMatch match = sicnu::features::matchesModelInput(
      cube, roles, 0 /* band count validated by the engine */, QString() );
    if ( !match.ok )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "feature cube does not match the model input contract: "
                               + match.problems.join( QLatin1String( "; " ) ).toStdString() );
  }
}

} // namespace

ModelExecutionResult runModelInference( const ModelExecutionRequest &request,
                                        RSOperatorContext &context )
{
  const bool multiInput = !request.namedInputs.empty();
  if ( !multiInput && !sicnu::operators::params::fileExists( request.inputPath ) )
    throw RSOperatorError( ErrorCode::FileNotFound,
                           "Input raster not found: " + request.inputPath );
  for ( const NamedRasterFeed &feed : request.namedInputs )
  {
    if ( feed.paths.empty() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "named input '" + feed.name + "' provides no raster paths" );
    for ( const std::string &path : feed.paths )
    {
      if ( !sicnu::operators::params::fileExists( path ) )
        throw RSOperatorError( ErrorCode::FileNotFound,
                               "Input raster not found (feed '" + feed.name + "'): " + path );
    }
  }

  // Resolve catalog name or direct path to a ready model contract.
  std::string errorDetail;
  const ModelInfo model = resolveModelReference( request.modelReference, &errorDetail );
  if ( model.readiness != ModelReadiness::Ready )
  {
    const ErrorCode code = model.readiness == ModelReadiness::MissingArtifact
                             ? ErrorCode::FileNotFound
                             : ErrorCode::InvalidInputData;
    throw RSOperatorError( code, errorDetail.empty() ? "model is not ready" : errorDetail );
  }

  // Runtime-layer verdict: provider availability + device/VRAM feasibility.
  auto &registry = ModelRuntimeRegistry::instance();
  const ModelHardwareCapabilities hw = registry.hardware();
  std::string runtimeReason;
  const ModelReadiness runtimeReadiness =
    evaluateRuntimeReadiness( model, hw, &runtimeReason );
  if ( runtimeReadiness != ModelReadiness::Ready )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "Model '" + model.name + "' cannot execute: " + runtimeReason );

  if ( multiInput && request.asDetection )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "detection decode runs on the single-input path — multi-input "
                             "detection heads are not wired yet" );
  rejectUnwiredContracts( model, request.namedInputs );
  if ( !multiInput )
    preflightFeatureCube( model, request.inputPath );

  // Detection contracts run through the detection engine; everything else is
  // raster-stack output. The engines share the tile skeleton and the session.
  RequestedDevice device;
  if ( !request.deviceToken.empty() )
  {
    if ( !RequestedDevice::parse( request.deviceToken, &device ) )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "device '" + request.deviceToken
                               + "' is not parsable (supported: cpu, cuda, cuda:N, auto)" );
  }

  context.reportProgress( 0.05, "Acquiring model runtime session" );
  if ( SICNU_FAULT_POINT( "model_provider.acquire" ) )
  {
    // Injected provider failure (Verification Platform 8.0 fault matrix,
    // test-only arming): exactly the real session-load failure path — typed
    // error, no session fabricated, pool state untouched.
    throw RSOperatorError( ErrorCode::ComputationError,
                           "Failed to load model session: fault-injected acquire failure" );
  }
  std::string loadError;
  const auto session = request.deviceToken.empty()
                         ? registry.acquire( model, &loadError )
                         : registry.acquire( model, device, &loadError );
  if ( !session )
    throw RSOperatorError( ErrorCode::ComputationError,
                           "Failed to load model session: " + loadError );

  int bandCount = 0;
  if ( !multiInput )
  {
    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( request.inputPath ) ) )
      throw RSOperatorError( ErrorCode::GdalError,
                             "Failed to open input raster: " + request.inputPath );
    bandCount = ds.bandCount();
    if ( bandCount <= 0 )
      throw RSOperatorError( ErrorCode::GdalError,
                             "Failed to read band count from input raster" );
    if ( !request.bands.empty() )
    {
      for ( int b : request.bands )
      {
        if ( b < 1 || b > bandCount )
          throw RSOperatorError( ErrorCode::InvalidParameter,
                                 "band " + std::to_string( b ) + " out of range (1.."
                                   + std::to_string( bandCount ) + ")" );
      }
    }
  }
  const std::vector<int> bands = request.bands;

  context.throwIfCancelled();
  context.reportProgressForced( 0.1, request.asDetection ? "Running tiled detection"
                                                         : "Running tiled inference" );

  // Detection knob overrides apply to a COPY of the contract (the catalog
  // entry itself is never mutated by a run).
  ModelInfo effectiveModel = model;
  if ( request.asDetection
       && ( request.confOverride >= 0.0 || request.nmsIouOverride >= 0.0 ) )
  {
    if ( request.confOverride >= 0.0 )
      effectiveModel.output.detection.confThreshold = request.confOverride;
    if ( request.nmsIouOverride >= 0.0 )
      effectiveModel.output.detection.nmsIou = request.nmsIouOverride;
  }

  TileInferenceRunOptions options;
  options.tta = request.tta;
  options.batchSizeOverride = std::max( 0, request.batchSizeOverride );
  options.outputMode = request.outputMode;
  options.blend = request.blend; // Platform 9.0 (M5)

  ModelExecutionResult result;
  result.identityTag = model.identityTag();
  result.contentDigest = model.contentDigest;
  result.backend = session->backendName();
  result.device = session->deviceName();

  if ( request.asDetection )
  {
    DetectionTileEngine engine( effectiveModel, session );
    result.detectionStats = engine.run( request.inputPath, bands, request.outputPath, context, options );

    Json::Value payload( Json::objectValue );
    payload["output"] = request.outputPath;
    payload["backend"] = result.backend;
    payload["device"] = result.device;
    payload["model"] = model.stableId();
    payload["width"] = result.detectionStats.rasterWidth;
    payload["height"] = result.detectionStats.rasterHeight;
    payload["tileSize"] = result.detectionStats.tileSize;
    payload["tiles"] = result.detectionStats.tilesProcessed;
    payload["detections"] = result.detectionStats.detectionsKept;
    payload["rawDetections"] = result.detectionStats.rawDetections;
    Json::Value classes( Json::arrayValue );
    for ( const auto &cls : effectiveModel.output.detection.classes )
      classes.append( cls );
    payload["classes"] = classes;
    result.payload = payload;
    return result;
  }

  TileInferenceEngine engine( effectiveModel, session );
  if ( multiInput )
    result.rasterStats = engine.runMultiInput( request.namedInputs, request.outputPath, context, options );
  else
    result.rasterStats = engine.run( request.inputPath, bands, request.outputPath, context, options );

  Json::Value payload( Json::objectValue );
  payload["output"] = request.outputPath;
  payload["backend"] = result.backend;
  payload["device"] = result.device;
  payload["model"] = model.stableId();
  payload["outBands"] = result.rasterStats.outBands;
  payload["width"] = result.rasterStats.outWidth;
  payload["height"] = result.rasterStats.outHeight;
  payload["tileSize"] = result.rasterStats.tileSize;
  payload["tiles"] = result.rasterStats.tilesProcessed;
  payload["tilesSkippedNoData"] = result.rasterStats.tilesSkippedNoData;
  // Platform 9.0 (M8) execution identity in the payload: EP + backend
  // version, honest-and-possibly-absent, mirroring the provenance sidecar.
  {
    const ProviderRuntimeDetails details = session->providerDetails();
    Json::Value provider( Json::objectValue );
    if ( !details.executionProvider.empty() )
      provider["execution_provider"] = details.executionProvider;
    if ( !details.runtimeVersion.empty() )
      provider["runtime_version"] = details.runtimeVersion;
    if ( !provider.empty() )
      payload["provider"] = provider;
  }
  if ( result.rasterStats.batchReductions > 0 )
    payload["batchReductions"] = result.rasterStats.batchReductions;
  // Platform 9.0 (M6): per-product-class metadata for Labels/Mask products.
  if ( !result.rasterStats.classPixelCounts.empty() )
  {
    Json::Value counts( Json::arrayValue );
    for ( long long pixels : result.rasterStats.classPixelCounts )
      counts.append( static_cast<Json::Int64>( pixels ) );
    payload["classPixelCounts"] = counts;
    // Names index the counts only when no remap reorders the product domain.
    if ( request.outputMode == RasterOutputMode::Labels && !model.output.classes.empty()
         && model.postprocess.classMapping.empty() )
    {
      Json::Value names( Json::arrayValue );
      for ( const std::string &cls : model.output.classes )
        names.append( cls );
      payload["classes"] = names;
    }
  }
  // Platform 8.0 grid provenance: what was verified about each fed input
  // (co-registration verdicts, CRS, pre-alignment origins). Consumers and
  // the .prov.json sidecar tell the same story.
  if ( !result.rasterStats.inputGrids.empty() )
  {
    Json::Value inputs( Json::arrayValue );
    for ( const GridProvenance &grid : result.rasterStats.inputGrids )
    {
      Json::Value input( Json::objectValue );
      input["name"] = grid.name;
      input["path"] = grid.path;
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
      // Platform 9.0 (M3): effective preprocess + fingerprint mirror the sidecar.
      if ( !grid.preprocessNote.empty() )
        input["preprocess"] = grid.preprocessNote;
      if ( grid.fingerprint.isObject() )
        input["fingerprint"] = grid.fingerprint;
      inputs.append( input );
    }
    payload["inputs"] = inputs;
  }
  result.payload = std::move( payload );
  return result;
}

} // namespace sicnu::operators::runtime
