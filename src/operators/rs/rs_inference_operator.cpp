/***************************************************************************
 * rs_inference_operator.cpp  —  On-device ONNX inference RSOperator (tracer bullet)
 ***************************************************************************/
#include "rs_inference_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "opencv/opencv_utils.h"

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using opencv::readRasterBandsToMats;
using opencv::writeMatsToRaster;

Json::Value RsInferenceOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Input raster" );
    props["model"] = makeStringParam( "model", "Path to an ONNX model readable by cv::dnn" );
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
    outputs["outBands"] = makeIntegerParam( "outBands", "Number of bands written", 0 );
    outputs["width"] = makeIntegerParam( "width", "Output raster width", 0 );
    outputs["height"] = makeIntegerParam( "height", "Output raster height", 0 );

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
    meta["purpose"] = "Run a pretrained ONNX model on a raster with pure C++ (cv::dnn).";
    meta["prerequisites"].append( "Model must be loadable by cv::dnn::readNetFromONNX." );
    meta["workflowHints"].append( "No preprocessing/postprocessing — feed bands, write output." );
    return meta;
}

Json::Value RsInferenceOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string modelPath = requireString( params, "model" );
    const std::string outputPath = requireString( params, "output" );

    if ( !fileExists( inputPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound,
                               "Input raster not found: " + inputPath );
    if ( !fileExists( modelPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound,
                               "Model file not found: " + modelPath );

    std::string errorMessage;
    context.reportProgress( 0.1, "Reading input raster bands" );

    // Read the requested bands (default: all) as CV_32FC1 mats. Each band is one
    // channel of the NCHW blob the model consumes. parseBands validates 1-based
    // indices against the actual band count (same semantics as the other rs:
    // operators), throwing InvalidParameter on an out-of-range band.
    const int bandCount = opencv::rasterBandCount( inputPath );
    if ( bandCount <= 0 )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to read band count from input raster" );
    const std::vector<int> bands = parseBands( params, bandCount );

    std::vector<cv::Mat> bandMats;
    if ( bands.empty() )
    {
        bandMats = readRasterBandsToMats( inputPath, &errorMessage );
    }
    else
    {
        for ( int b : bands )
        {
            cv::Mat m = opencv::readRasterBandToMat( inputPath, b, &errorMessage );
            if ( m.empty() )
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read band " + std::to_string( b ) +
                                           ": " + errorMessage );
            bandMats.push_back( std::move( m ) );
        }
    }
    if ( bandMats.empty() )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to read input raster: " + errorMessage );

    const int height = bandMats[0].rows;
    const int width = bandMats[0].cols;

    context.throwIfCancelled();
    context.reportProgress( 0.3, "Loading ONNX model" );

    // Load the model. readNetFromONNX throws cv::Exception on a bad path/format;
    // translate to an operator error so a bad model surfaces, not a crash.
    cv::dnn::Net net;
    try
    {
        net = cv::dnn::readNetFromONNX( modelPath );
    }
    catch ( const cv::Exception &e )
    {
        throw RSOperatorError( ErrorCode::ComputationError,
                               "Failed to load ONNX model: " + std::string( e.what() ) );
    }
    if ( net.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Loaded model is empty: " + modelPath );

    context.throwIfCancelled();
    context.reportProgress( 0.5, "Running inference" );

    // Stack the bands into a 4-D NCHW float blob (1, bandCount, H, W): merge the
    // per-band mats into one (H, W, bandCount) image, then blobFromImage transposes
    // to NCHW and converts to float32 — the layout most image ONNX models expect.
    cv::Mat stacked;
    cv::merge( bandMats, stacked ); // (H, W, bandCount)
    cv::Mat nchw = cv::dnn::blobFromImage( stacked ); // (1, bandCount, H, W)
    net.setInput( nchw );

    cv::Mat output;
    try
    {
        output = net.forward();
    }
    catch ( const cv::Exception &e )
    {
        throw RSOperatorError( ErrorCode::ComputationError,
                               "Inference forward pass failed: " + std::string( e.what() ) );
    }
    if ( output.empty() )
        throw RSOperatorError( ErrorCode::ComputationError,
                               "Inference produced an empty output" );

    context.throwIfCancelled();
    context.reportProgress( 0.8, "Writing output raster" );

    // The output blob is expected to be NCHW (1, C_out, H, W) over the input
    // grid. Validate the shape before reshaping — a model whose output isn't
    // 4-D NCHW or whose spatial dims don't match the input is unsupported by
    // this tracer-bullet slice and must surface as an operator error, not an
    // uncaught cv::Exception from reshape(). Each output channel becomes a band.
    if ( output.dims != 4 || output.size[0] != 1 ||
         output.size[2] != height || output.size[3] != width )
    {
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "Model output is not a 4-D NCHW blob matching the input grid; this "
            "operator supports per-pixel models whose output keeps the input "
            "(height, width). Got a " +
                std::to_string( output.dims ) + "-D output." );
    }
    const int outChannels = output.size[1];
    std::vector<cv::Mat> outMats;
    outMats.reserve( outChannels );
    cv::Mat flat = output.reshape( 1, std::vector<int>{ outChannels, height * width } );
    for ( int c = 0; c < outChannels; ++c )
    {
        cv::Mat row = flat.row( c ).reshape( 1, height ).clone(); // (H, W) CV_32F
        outMats.push_back( std::move( row ) );
    }

    if ( !writeMatsToRaster( outputPath, outMats, inputPath, &errorMessage ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to write output raster: " + errorMessage );

    context.reportProgress( 1.0, "Inference complete" );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["backend"] = "opencv_dnn";
    result["outBands"] = outChannels;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::rs
