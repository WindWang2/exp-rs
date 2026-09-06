/***************************************************************************
 * rs_inference_operator.cpp  —  On-device ONNX inference RSOperator
 ***************************************************************************/
#include "rs_inference_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"

#include "processing/features/feature_cube.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <string>

namespace sicnu::operators::rs {

using namespace params;
using runtime::ModelRuntimeRegistry;
using runtime::TileInferenceEngine;

namespace {

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
    // Platform 3.0 knobs (goal §10): flip test-time augmentation and a hard
    // batch cap for memory-pinned runs.
    props["tta"] = makeEnumParam( "tta", "Test-time augmentation (flip averaging)",
                                  { "none", "hflip", "hvflip" }, "none" );
    props["batchCap"] = makeIntegerParam( "batchCap", "Hard cap on tiles per forward pass (0 = manifest/budget default)", 0 );

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
    meta["task"] = "inference";
    meta["notes"] = "ONNX inference via the model runtime (cv::dnn provider, cached sessions). Accepts a model path or a ModelCatalog name (spatial:list_models); catalog models must be ready (artifact present, checksum verified). Bounded tiled execution with manifest v2 preprocessing/postprocessing contracts; memory policy Streaming.";
    meta["gpu"] = true; // CUDA-capable per model (opencv_dnn_runtime); CPU fallback per manifest
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
    const ModelInfo model = runtime::resolveModelReference( modelReference, nullptr );

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
    // #689: with resize "to_input" the engine feeds model.input.width x
    // .height tensors — the batched tiles, the blob and the per-tile output
    // planes are all sized by the fixed graph input, not the read window, so
    // a small tile_size with a large graph input under-estimated RAM by orders
    // of magnitude. Estimate on the LARGER of the two geometries so neither
    // the halo window nor the fed tensor is under-counted.
    std::uint64_t fedW = edge;
    std::uint64_t fedH = edge;
    if ( model.preprocess.resize == "to_input" && model.input.width > 0 && model.input.height > 0 )
    {
        fedW = std::max( edge, static_cast<std::uint64_t>( model.input.width ) );
        fedH = std::max( edge, static_cast<std::uint64_t>( model.input.height ) );
    }
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
    // Multi-head/uncertainty amplification (review 3): flushBatch retains
    // every declared head's output planes alongside the input blob. Count the
    // DECLARED channels (classes x tensor_names + optional uncertainty band);
    // undeclared heads keep the historical input-sized allowance.
    std::uint64_t declaredOutChannels = 0;
    if ( !model.output.classes.empty() )
        declaredOutChannels +=
            model.output.classes.size() *
            std::max<std::size_t>( 1, model.output.tensorNames.size() );
    else if ( !model.output.tensorNames.empty() )
        declaredOutChannels = model.output.tensorNames.size();
    if ( model.output.uncertainty == "entropy" || model.output.uncertainty == "margin" )
        declaredOutChannels += 1;
    const std::uint64_t perTileSets = batch * ( 4 + declaredOutChannels );

    Json::Value est = sicnu::processing::makeStreamingEstimate( fedW, fedH, bands, 4,
                                                                perTileSets, /*matrixBytes*/ 0,
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

    runtime::ModelExecutionRequest request;
    request.inputPath = requireString( params, "input" );
    request.modelReference = requireString( params, "model" );
    request.outputPath = requireString( params, "output" );
    // Free token, not an enum: explicit cuda:N references must pass through.
    if ( params.isObject() && params.isMember( "device" ) )
    {
      if ( !params["device"].isString() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "device must be a string" );
      request.deviceToken = params["device"].asString();
    }

    const int bandCount = [ & ] {
      GdalDatasetWrapper ds;
      if ( !ds.open( QString::fromStdString( request.inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open input raster: " + request.inputPath );
      return ds.bandCount();
    }();
    if ( bandCount <= 0 )
      throw RSOperatorError( ErrorCode::GdalError,
                             "Failed to read band count from input raster" );
    request.bands = parseBands( params, bandCount );

    const std::string tta = getEnum( params, "tta", { "none", "hflip", "hvflip" }, "none" );
    if ( tta == "hflip" )
      request.tta = runtime::TtaMode::HFlip;
    else if ( tta == "hvflip" )
      request.tta = runtime::TtaMode::HVFlip;
    request.batchSizeOverride = std::max( 0, getInt( params, "batchCap", 0 ) );

    const runtime::ModelExecutionResult result =
      runtime::runModelInference( request, context );
    return result.payload;
}

} // namespace sicnu::operators::rs
