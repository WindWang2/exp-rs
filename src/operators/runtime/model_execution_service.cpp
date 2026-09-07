// src/operators/runtime/model_execution_service.cpp
#include "operators/runtime/model_execution_service.h"

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

/// Contract gates shared by every surface (inherited from rs:infer 3.0):
/// temporal / multi-input execution is not wired into the tile engine and
/// must fail loudly instead of silently running one frame of a T-frame model.
void rejectUnwiredContracts( const ModelInfo &model )
{
  for ( const auto &input : model.inputs )
  {
    if ( input.temporalLength > 0 )
      throw RSOperatorError(
        ErrorCode::InvalidInputData,
        "Model '" + model.name + "' declares temporal_length=" +
          std::to_string( input.temporalLength ) +
          "; temporal (T-frame) inference is not wired into the tile "
          "engine yet — the graph would silently run on a single frame" );
  }
  if ( model.inputs.size() > 1 )
    throw RSOperatorError(
      ErrorCode::InvalidInputData,
      "Model '" + model.name + "' declares " + std::to_string( model.inputs.size() )
        + " named inputs; multi-input execution is not wired into the tile "
          "engine yet — pick a single-input model or run each branch "
          "separately" );
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
  if ( !sicnu::operators::params::fileExists( request.inputPath ) )
    throw RSOperatorError( ErrorCode::FileNotFound,
                           "Input raster not found: " + request.inputPath );

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

  rejectUnwiredContracts( model );
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
  std::string loadError;
  const auto session = request.deviceToken.empty()
                         ? registry.acquire( model, &loadError )
                         : registry.acquire( model, device, &loadError );
  if ( !session )
    throw RSOperatorError( ErrorCode::ComputationError,
                           "Failed to load model session: " + loadError );

  int bandCount = 0;
  {
    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( request.inputPath ) ) )
      throw RSOperatorError( ErrorCode::GdalError,
                             "Failed to open input raster: " + request.inputPath );
    bandCount = ds.bandCount();
  }
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
  if ( result.rasterStats.batchReductions > 0 )
    payload["batchReductions"] = result.rasterStats.batchReductions;
  result.payload = std::move( payload );
  return result;
}

} // namespace sicnu::operators::runtime
