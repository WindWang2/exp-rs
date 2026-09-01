/***************************************************************************
 * rs_inference_operator.cpp  —  On-device ONNX inference RSOperator
 ***************************************************************************/
#include "rs_inference_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"

#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFileInfo>

#include <algorithm>
#include <string>

namespace sicnu::operators::rs {

using namespace params;
using runtime::ModelRuntimeRegistry;
using runtime::TileInferenceEngine;

namespace {

/**
 * Resolve the `model` parameter to a catalog ModelInfo ready for execution.
 * Direct file references build an ad-hoc contract (default preprocessing,
 * default tiling); catalog names go through the full readiness pipeline.
 * The returned info's readiness signals resolution success/failure.
 */
ModelInfo resolveModel( const std::string &modelReference, std::string *errorDetail )
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

} // namespace

Json::Value RsInferenceOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Input raster" );
    props["model"] = makeStringParam( "model", "Path to an ONNX model readable by cv::dnn, or a model catalog name (see spatial:list_models)" );
    props["output"] = makeOutputParam( "output", "Output inference raster", "tif" );
    // `bands` is an optional array of 1-based band indices (default: all bands).
    // Described as a raw JSON-schema array (no array helper exists yet) so an
    // agent reading the schema passes ["bands": [1,2,3]], not a single int.
    Json::Value bandsParam( Json::objectValue );
    bandsParam["name"] = "bands";
    bandsParam["type"] = "array";
    bandsParam["description"] = "1-based band numbers to feed (default: all bands)";
    Json::Value items( Json::objectValue );
    items["type"] = "integer";
    items["minimum"] = 1;
    bandsParam["items"] = items;
    props["bands"] = bandsParam;

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["backend"] = makeStringParam( "backend", "Inference backend", "" );
    outputs["device"] = makeStringParam( "device", "Execution device (cpu/cuda)", "" );
    outputs["model"] = makeStringParam( "model", "Resolved model name or path", "" );
    outputs["outBands"] = makeIntegerParam( "outBands", "Number of bands written", 0 );
    outputs["width"] = makeIntegerParam( "width", "Output raster width", 0 );
    outputs["height"] = makeIntegerParam( "height", "Output raster height", 0 );
    outputs["tileSize"] = makeIntegerParam( "tileSize", "Core tile edge used (px)", 0 );
    outputs["tiles"] = makeIntegerParam( "tiles", "Tiles processed", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "model", "output" } );
    return root;
}

Json::Value RsInferenceOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "inference" );
    meta["tags"].append( "onnx" );
    meta["tags"].append( "edge-ai" );
    meta["tags"].append( "deep-learning" );
    meta["purpose"] = "Run a pretrained ONNX model on a raster with pure C++ (cv::dnn), tiled with bounded memory.";
    meta["prerequisites"].append( "Model must be loadable by cv::dnn::readNetFromONNX." );
    meta["workflowHints"].append( "Preprocessing/postprocessing follow the model manifest (v2) contracts; default is bands-in/raster-out identity chaining." );
    meta["workflowHints"].append( "Catalog models must be ready (artifact present, checksum verified) — see spatial:list_models." );
    return meta;
}

Json::Value RsInferenceOperator::executionEstimate() const
{
    // Static fallback: the tiled engine's per-tile working set for a default
    // 512 px tile, 4 bands, batch 1 (~3 tile-sized buffers) plus a weights
    // overhead floor. The dynamic estimateExecution(params) refines this from
    // the actual raster header and model contracts.
    return sicnu::processing::makeStreamingEstimate( 512, 512, 4, 4, 3,
                                                     /*matrixBytes*/ 0,
                                                     /*fixedOverhead*/ 64 * 1024 * 1024 );
}

Json::Value RsInferenceOperator::estimateExecution( const Json::Value &params ) const
{
    const std::string inputPath = params.isObject() && params.isMember( "input" )
                                    ? params["input"].asString()
                                    : std::string();
    const std::string modelReference = params.isObject() && params.isMember( "model" )
                                         ? params["model"].asString()
                                         : std::string();

    // Contract lookup for estimation must not require readiness (a missing
    // artifact still carries parseable tiling/runtime contracts).
    const ModelInfo model = resolveModel( modelReference, nullptr );

    const int tile = TileInferenceEngine::effectiveTileSize( model );
    const int halo = TileInferenceEngine::effectiveHalo( model );
    const std::uint64_t batch = static_cast<std::uint64_t>( std::max( 1, model.tiling.batchSize ) );

    std::uint64_t bands = 4; // conservative default when the raster is unknown
    GdalDatasetWrapper ds;
    if ( !inputPath.empty() && ds.open( QString::fromStdString( inputPath ) ) )
    {
        bands = static_cast<std::uint64_t>( ds.bandCount() );
        // A bands parameter narrows the fed channels.
        if ( params.isObject() && params.isMember( "bands" ) && params["bands"].isArray() )
            bands = std::max<std::uint64_t>( 1, static_cast<std::uint64_t>( params["bands"].size() ) );
    }

    const std::uint64_t edge = static_cast<std::uint64_t>( tile + 2 * halo );
    // Read window + detached tile + blob + output planes ≈ 4 tile-sized sets
    // per batched tile; model weights are the fixed overhead when declared.
    std::uint64_t modelRamBytes =
        static_cast<std::uint64_t>( std::max( 0, model.runtime.estimatedRamMb ) ) * 1024 * 1024;
    // #689: no shipped manifest declares estimated_ram_mb, which hid the
    // (dominant) weight bytes from the admission estimate. When undeclared,
    // floor the model term with the resolved artifact's size on disk (the
    // serialized weights, rounded up to whole MiB) and keep the read-window
    // math unchanged.
    if ( modelRamBytes == 0 && !model.resolvedArtifactPath.empty() )
    {
        const std::uint64_t artifactBytes = static_cast<std::uint64_t>(
            QFileInfo( QString::fromStdString( model.resolvedArtifactPath ) ).size() );
        constexpr std::uint64_t kMiB = 1024 * 1024;
        if ( artifactBytes > 0 )
            modelRamBytes = ( ( artifactBytes + kMiB - 1 ) / kMiB ) * kMiB;
    }
    Json::Value est = sicnu::processing::makeStreamingEstimate( edge, edge, bands, 4,
                                                                batch * 4, /*matrixBytes*/ 0,
                                                                /*fixedOverhead*/ modelRamBytes + 32 * 1024 * 1024 );
    // VRAM contract surfaces for admission tooling (TaskCenter admits on RAM
    // today; GPU-aware admission is a documented follow-up).
    if ( model.runtime.gpu )
        est["estimatedVramMb"] = model.runtime.estimatedVramMb;
    return est;
}

Json::Value RsInferenceOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string modelReference = requireString( params, "model" );
    const std::string outputPath = requireString( params, "output" );

    if ( !fileExists( inputPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound,
                               "Input raster not found: " + inputPath );

    // Resolve catalog name or direct path to a ready model contract.
    std::string errorDetail;
    const ModelInfo model = resolveModel( modelReference, &errorDetail );
    if ( model.readiness != ModelReadiness::Ready )
    {
        const ErrorCode code = model.readiness == ModelReadiness::MissingArtifact
                                   ? ErrorCode::FileNotFound
                                   : ErrorCode::InvalidInputData;
        throw RSOperatorError( code, errorDetail.empty() ? "model is not ready" : errorDetail );
    }

    // Runtime-layer verdict: provider availability + GPU/VRAM contract.
    auto &registry = ModelRuntimeRegistry::instance();
    const runtime::ModelHardwareCapabilities hw = registry.hardware();
    std::string runtimeReason;
    const ModelReadiness runtimeReadiness =
        runtime::evaluateRuntimeReadiness( model, hw, &runtimeReason );
    if ( runtimeReadiness != ModelReadiness::Ready )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Model '" + model.name + "' cannot execute: " + runtimeReason );

    context.reportProgress( 0.05, "Acquiring model runtime session" );
    std::string loadError;
    const auto session = registry.acquire( model, &loadError );
    if ( !session )
        throw RSOperatorError( ErrorCode::ComputationError,
                               "Failed to load model session: " + loadError );

    const int bandCount = [ & ] {
        GdalDatasetWrapper ds;
        if ( !ds.open( QString::fromStdString( inputPath ) ) )
            throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
        return ds.bandCount();
    }();
    if ( bandCount <= 0 )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to read band count from input raster" );
    const std::vector<int> bands = parseBands( params, bandCount );

    context.throwIfCancelled();
    context.reportProgressForced( 0.1, "Running tiled inference" );

    TileInferenceEngine engine( model, session );
    const runtime::TileInferenceStats stats = engine.run( inputPath, bands, outputPath, context );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["backend"] = session->backendName();
    result["device"] = session->deviceName();
    result["model"] = model.name;
    result["outBands"] = stats.outBands;
    result["width"] = stats.outWidth;
    result["height"] = stats.outHeight;
    result["tileSize"] = stats.tileSize;
    result["tiles"] = stats.tilesProcessed;
    result["tilesSkippedNoData"] = stats.tilesSkippedNoData;
    return result;
}

} // namespace sicnu::operators::rs
