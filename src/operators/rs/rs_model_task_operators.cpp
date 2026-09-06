/***************************************************************************
 * rs_model_task_operators.cpp — task-shaped adapters over the model
 * execution service. Every run() is: parse params → fill ONE
 * ModelExecutionRequest → runModelInference → payload. No engine, session,
 * or preprocessing code lives here (that is the convergence contract).
 ***************************************************************************/
#include "rs_model_task_operators.h"

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"

#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using runtime::ModelExecutionRequest;
using runtime::resolveModelReference;

namespace {

/// Shared parameter block for every model task operator.
void addCommonProps( Json::Value &props )
{
    using namespace schema;
    props["input"] = makeRasterParam( "input", "Input raster" );
    props["model"] = makeStringParam( "model", "Model catalog stable id (spatial:list_models) or a weight file path" );
    props["output"] = makeOutputParam( "output", "Output path", "tif" );
    Json::Value bandsParam( Json::objectValue );
    bandsParam["name"] = "bands";
    bandsParam["type"] = "array";
    bandsParam["description"] = "1-based band numbers to feed (default: all bands)";
    Json::Value items( Json::objectValue );
    items["type"] = "integer";
    items["minimum"] = 1;
    bandsParam["items"] = items;
    props["bands"] = bandsParam;
    props["device"] = makeStringParam( "device", "Execution device: auto | cpu | cuda | cuda:N (default: manifest/auto)", "" );
    props["tta"] = makeEnumParam( "tta", "Test-time augmentation (flip averaging)",
                                  { "none", "hflip", "hvflip" }, "none" );
    props["batchCap"] = makeIntegerParam( "batchCap", "Hard cap on tiles per forward pass (0 = manifest/budget default)", 0 );
}

/// Shared result payload block.
void addCommonOutputs( Json::Value &outputs, const std::string &outputDesc )
{
    using namespace schema;
    outputs["output"] = makeRasterParam( "output", outputDesc );
    outputs["backend"] = makeStringParam( "backend", "Inference backend", "" );
    outputs["device"] = makeStringParam( "device", "Execution device (cpu/cuda:N)", "" );
    outputs["model"] = makeStringParam( "model", "Resolved model stable id or path", "" );
    outputs["tileSize"] = makeIntegerParam( "tileSize", "Core tile edge used (px)", 0 );
    outputs["tiles"] = makeIntegerParam( "tiles", "Tiles processed", 0 );
}

/// Fills the request from the common parameters. Returns the resolved
/// contract (for estimates) after verifying the input exists.
void fillCommonRequest( ModelExecutionRequest &request, const Json::Value &params,
                        RSOperatorContext &context, int *bandCountOut = nullptr )
{
    request.inputPath = requireString( params, "input" );
    request.modelReference = requireString( params, "model" );
    request.outputPath = requireString( params, "output" );
    if ( params.isObject() && params.isMember( "device" ) )
    {
      if ( !params["device"].isString() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "device must be a string" );
      request.deviceToken = params["device"].asString();
    }
    const std::string tta = getEnum( params, "tta", { "none", "hflip", "hvflip" }, "none" );
    if ( tta == "hflip" )
      request.tta = runtime::TtaMode::HFlip;
    else if ( tta == "hvflip" )
      request.tta = runtime::TtaMode::HVFlip;
    request.batchSizeOverride = std::max( 0, getInt( params, "batchCap", 0 ) );

    int bandCount = 0;
    {
      GdalDatasetWrapper ds;
      if ( !ds.open( QString::fromStdString( request.inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open input raster: " + request.inputPath );
      bandCount = ds.bandCount();
    }
    if ( bandCount <= 0 )
      throw RSOperatorError( ErrorCode::GdalError, "Failed to read band count from input raster" );
    if ( bandCountOut )
      *bandCountOut = bandCount;
    request.bands = parseBands( params, bandCount );
    ( void )context;
}

/// The shared streaming estimate: the tiled working set from the resolved
/// contract (same math as rs:infer, which the service executes identically).
Json::Value commonEstimate( const Json::Value &params )
{
    const std::string inputPath = params.isObject() && params.isMember( "input" )
                                    ? params["input"].asString()
                                    : std::string();
    const std::string modelReference = params.isObject() && params.isMember( "model" )
                                         ? params["model"].asString()
                                         : std::string();
    const ModelInfo model = resolveModelReference( modelReference, nullptr );
    const int tile = runtime::TileInferenceEngine::effectiveTileSize( model );
    const int halo = runtime::TileInferenceEngine::effectiveHalo( model );
    const std::uint64_t batch = static_cast<std::uint64_t>( std::max( 1, model.tiling.batchSize ) );
    std::uint64_t bands = 4;
    GdalDatasetWrapper ds;
    if ( !inputPath.empty() && ds.open( QString::fromStdString( inputPath ) ) )
      bands = static_cast<std::uint64_t>( ds.bandCount() );
    const std::uint64_t edge = static_cast<std::uint64_t>( tile + 2 * halo );
    std::uint64_t fedW = edge;
    std::uint64_t fedH = edge;
    if ( model.preprocess.resize == "to_input" && model.input.width > 0 && model.input.height > 0 )
    {
      fedW = std::max( edge, static_cast<std::uint64_t>( model.input.width ) );
      fedH = std::max( edge, static_cast<std::uint64_t>( model.input.height ) );
    }
    std::uint64_t modelRamBytes =
      static_cast<std::uint64_t>( std::max( 0, model.runtime.estimatedRamMb ) ) * 1024 * 1024;
    if ( modelRamBytes == 0 && !model.resolvedArtifactPath.empty() )
    {
      const std::uint64_t artifactBytes = static_cast<std::uint64_t>(
        QFileInfo( QString::fromStdString( model.resolvedArtifactPath ) ).size() );
      constexpr std::uint64_t kMiB = 1024 * 1024;
      if ( artifactBytes > 0 )
        modelRamBytes = ( ( artifactBytes + kMiB - 1 ) / kMiB ) * kMiB;
    }
    std::uint64_t declaredOutChannels = 0;
    if ( !model.output.classes.empty() )
      declaredOutChannels +=
        model.output.classes.size() * std::max<std::size_t>( 1, model.output.tensorNames.size() );
    else if ( !model.output.tensorNames.empty() )
      declaredOutChannels = model.output.tensorNames.size();
    if ( model.output.uncertainty == "entropy" || model.output.uncertainty == "margin" )
      declaredOutChannels += 1;
    const std::uint64_t perTileSets = batch * ( 4 + declaredOutChannels );
    Json::Value est = sicnu::processing::makeStreamingEstimate( fedW, fedH, bands, 4, perTileSets,
                                                                /*matrixBytes*/ 0,
                                                                /*fixedOverhead*/ modelRamBytes
                                                                  + 32 * 1024 * 1024 );
    if ( model.runtime.gpu )
      est["estimatedVramMb"] = model.runtime.estimatedVramMb;
    return est;
}

} // namespace

// --- rs:segment ---------------------------------------------------------------

Json::Value RsSegmentOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    addCommonProps( props );
    props["format"] = makeEnumParam( "format", "Output product",
                                     { "probability", "labels", "mask", "confidence" },
                                     "probability" );
    Json::Value outputs( Json::objectValue );
    addCommonOutputs( outputs, "Output raster path" );
    outputs["outBands"] = makeIntegerParam( "outBands", "Number of bands written", 0 );
    outputs["width"] = makeIntegerParam( "width", "Output raster width", 0 );
    outputs["height"] = makeIntegerParam( "height", "Output raster height", 0 );
    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "model", "output" } );
    return root;
}

Json::Value RsSegmentOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "inference" );
    meta["tags"].append( "segmentation" );
    meta["tags"].append( "deep-learning" );
    meta["task"] = "segmentation";
    meta["gpu"] = true;
    meta["notes"] = "Thin adapter over the model execution service (Platform 4.0). Defaults come from the model manifest; labels/mask/confidence are derived from the model's own class planes — no second inference path exists.";
    return meta;
}

Json::Value RsSegmentOperator::executionEstimate() const
{
    return sicnu::processing::makeStreamingEstimate( 512, 512, 4, 4, 3, 0, 64 * 1024 * 1024 );
}

Json::Value RsSegmentOperator::estimateExecution( const Json::Value &params ) const
{
    return commonEstimate( params );
}

Json::Value RsSegmentOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );
    ModelExecutionRequest request;
    fillCommonRequest( request, params, context );
    const std::string format = getEnum( params, "format",
                                        { "probability", "labels", "mask", "confidence" },
                                        "probability" );
    if ( format == "labels" )
      request.outputMode = runtime::RasterOutputMode::Labels;
    else if ( format == "mask" )
      request.outputMode = runtime::RasterOutputMode::Mask;
    else if ( format == "confidence" )
      request.outputMode = runtime::RasterOutputMode::Confidence;
    const runtime::ModelExecutionResult result =
      runtime::runModelInference( request, context );
    return result.payload;
}

// --- rs:detect ----------------------------------------------------------------

Json::Value RsDetectOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    addCommonProps( props );
    props["conf"] = makeStringParam( "conf", "Confidence threshold override [0,1] (default: manifest)", "" );
    props["nms_iou"] = makeStringParam( "nms_iou", "NMS IoU threshold override (0,1] (default: manifest)", "" );
    Json::Value outputs( Json::objectValue );
    addCommonOutputs( outputs, "Output vector path (.gpkg | .geojson | .shp)" );
    outputs["detections"] = makeIntegerParam( "detections", "Detections kept after NMS/dedup", 0 );
    outputs["rawDetections"] = makeIntegerParam( "rawDetections", "Detections decoded before NMS/dedup", 0 );
    outputs["classes"] = makeStringParam( "classes", "Class names from the manifest", "" );
    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "model", "output" } );
    return root;
}

Json::Value RsDetectOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "inference" );
    meta["tags"].append( "detection" );
    meta["tags"].append( "deep-learning" );
    meta["task"] = "detection";
    meta["gpu"] = true;
    meta["notes"] = "Requires a manifest with an output.detection contract. Tiles are decoded per window; overlap duplicates are removed by whole-raster NMS; boxes are published in geographic coordinates via an atomic vector write.";
    return meta;
}

Json::Value RsDetectOperator::executionEstimate() const
{
    return sicnu::processing::makeStreamingEstimate( 512, 512, 4, 4, 3, 0, 64 * 1024 * 1024 );
}

Json::Value RsDetectOperator::estimateExecution( const Json::Value &params ) const
{
    return commonEstimate( params );
}

Json::Value RsDetectOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );
    // Fail loudly when the model cannot run as detection (before opening the
    // input): the service re-checks, but the operator names the knob.
    const std::string modelRef = requireString( params, "model" );
    ModelExecutionRequest request;
    fillCommonRequest( request, params, context );
    request.asDetection = true;
    if ( params.isObject() && params.isMember( "conf" ) )
    {
      const Json::Value &conf = params["conf"];
      if ( !conf.isNumeric() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "conf must be a number in [0,1]" );
      request.confOverride = conf.asDouble();
    }
    if ( params.isObject() && params.isMember( "nms_iou" ) )
    {
      const Json::Value &iou = params["nms_iou"];
      if ( !iou.isNumeric() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "nms_iou must be a number in (0,1]" );
      request.nmsIouOverride = iou.asDouble();
    }
    const runtime::ModelExecutionResult result =
      runtime::runModelInference( request, context );
    return result.payload;
}

// --- rs:embedding -------------------------------------------------------------

Json::Value RsEmbeddingOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    addCommonProps( props );
    props["aggregate"] = makeEnumParam( "aggregate", "Aggregate the feature stack to a per-scene mean vector in the result",
                                        { "none", "mean" }, "none" );
    Json::Value outputs( Json::objectValue );
    addCommonOutputs( outputs, "Output feature-stack raster path" );
    outputs["outBands"] = makeIntegerParam( "outBands", "Feature channels written", 0 );
    outputs["embedding_dim"] = makeIntegerParam( "embedding_dim", "Feature dimensionality (== outBands)", 0 );
    outputs["mean_vector"] = makeStringParam( "mean_vector", "Per-scene mean feature vector (aggregate=mean)", "" );
    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "model", "output" } );
    return root;
}

Json::Value RsEmbeddingOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "inference" );
    meta["tags"].append( "embedding" );
    meta["tags"].append( "deep-learning" );
    meta["task"] = "embedding";
    meta["gpu"] = true;
    meta["notes"] = "Writes the model's feature stack as float32 bands (NaN = invalid pixels). aggregate=mean adds a bounded per-scene mean vector to the result payload; the stack itself is never materialized whole.";
    return meta;
}

Json::Value RsEmbeddingOperator::executionEstimate() const
{
    return sicnu::processing::makeStreamingEstimate( 512, 512, 4, 4, 3, 0, 64 * 1024 * 1024 );
}

Json::Value RsEmbeddingOperator::estimateExecution( const Json::Value &params ) const
{
    return commonEstimate( params );
}

Json::Value RsEmbeddingOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );
    ModelExecutionRequest request;
    fillCommonRequest( request, params, context );
    const runtime::ModelExecutionResult result =
      runtime::runModelInference( request, context );

    Json::Value payload = result.payload;
    payload["embedding_dim"] = result.rasterStats.outBands;
    const std::string aggregate = getEnum( params, "aggregate", { "none", "mean" }, "none" );
    if ( aggregate == "mean" )
    {
      // Bounded per-scene aggregation: one row at a time through GDAL, no
      // whole-raster materialization.
      GdalDatasetWrapper ds;
      if ( !ds.open( QString::fromStdString( request.outputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to reopen feature stack: " + request.outputPath );
      const int bands = result.rasterStats.outBands;
      std::vector<double> sum( static_cast<std::size_t>( bands ), 0.0 );
      std::uint64_t valid = 0;
      std::vector<float> row( static_cast<std::size_t>( ds.width() ) * bands );
      for ( int y = 0; y < ds.height(); ++y )
      {
        context.throwIfCancelled();
        std::vector<int> allBands( static_cast<std::size_t>( bands ) );
        for ( int c = 0; c < bands; ++c )
          allBands[static_cast<std::size_t>( c )] = c + 1;
        if ( !ds.readWindowBip( allBands, 0, y, ds.width(), 1, row.data() ) )
          throw RSOperatorError( ErrorCode::GdalError, "Failed to read feature row" );
        for ( int x = 0; x < ds.width(); ++x )
        {
          bool ok = true;
          for ( int c = 0; c < bands; ++c )
          {
            const float v = row[static_cast<std::size_t>( x ) * bands + c];
            if ( !std::isfinite( v ) )
            {
              ok = false;
              break;
            }
          }
          if ( !ok )
            continue;
          ++valid;
          for ( int c = 0; c < bands; ++c )
            sum[static_cast<std::size_t>( c )] += row[static_cast<std::size_t>( x ) * bands + c];
        }
      }
      Json::Value mean( Json::arrayValue );
      if ( valid > 0 )
      {
        for ( int c = 0; c < bands; ++c )
          mean.append( sum[static_cast<std::size_t>( c )] / static_cast<double>( valid ) );
      }
      payload["mean_vector"] = mean;
      payload["mean_vector_pixels"] = static_cast<Json::UInt64>( valid );
    }
    return payload;
}

} // namespace sicnu::operators::rs
