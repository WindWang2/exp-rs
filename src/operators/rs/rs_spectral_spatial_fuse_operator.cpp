/***************************************************************************
 * rs_spectral_spatial_fuse_operator.cpp — see the header for the contract.
 *
 * Reads the single-band score plane, derives per-pixel validity from the
 * declared NoData value and finiteness, runs the fusion kernel, and writes a
 * Float32 score raster with NaN NoData.
 ***************************************************************************/
#include "rs_spectral_spatial_fuse_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_spatial_fusion.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsSpectralSpatialFuseOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Single-band spectral score raster" );
    props["output"] = makeOutputParam( "output", "Single-band fused score raster", "tif" );

    Json::Value radius( Json::objectValue );
    radius["type"] = "integer";
    radius["minimum"] = 0;
    radius["default"] = 1;
    radius["description"] = "Spatial window half-side (window = (2r+1)^2, clamped to the raster).";
    props["radius"] = radius;

    Json::Value beta( Json::objectValue );
    beta["type"] = "number";
    beta["minimum"] = 0.0;
    beta["maximum"] = 1.0;
    beta["default"] = 0.5;
    beta["description"] = "Weight of the valid-window mean: fused = (1-beta)*score + beta*mean.";
    props["beta"] = beta;

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value RsSpectralSpatialFuseOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "spectral" );
    meta["tags"].append( "detection" );
    meta["tags"].append( "fusion" );
    meta["task"] = "target-detection";
    meta["notes"] = "Pure window function of the score plane: tile-agnostic, deterministic; "
                    "invalid (NoData / non-finite) pixels are never fused and never enter "
                    "a neighbor's mean (renormalized, no zero-fill bias). Bit-exact grade.";
    meta["gpu"] = false;
    meta["purpose"] = "Suppress isolated single-pixel false alarms after spectral target "
                      "detection while preserving NoData semantics.";
    meta["prerequisites"].append( "Input must be a single-band score raster." );
    meta["workflowHints"].append( "Typical chain: rs:matched_filter|rs:ace|rs:cem_detection -> "
                                  "rs:spectral_spatial_fuse -> rs:threshold_raster." );
    meta["limitations"].append( "Full-raster memory policy: the whole single-band score plane "
                                "is held in memory (float32 width*height)." );
    return meta;
}

Json::Value RsSpectralSpatialFuseOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    // FullRaster: the single-band score plane, the fused plane and the validity
    // mask are resident (input float32 + output float32 + 1 byte/px mask).
    // Nominal 4096×4096 scene, same convention as rs:morphology.
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 9ULL * 4096ULL * 4096ULL );
    return est;
}

Json::Value RsSpectralSpatialFuseOperator::run( const Json::Value &params,
                                                RSOperatorContext &context )
{
    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    if ( !fileExists( inputPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "Input raster not found: " + inputPath );

    const int radius = getInt( params, "radius", 1 );
    const double beta = getDouble( params, "beta", 0.5 );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    if ( ds.bandCount() != 1 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "rs:spectral_spatial_fuse requires a single-band score raster, got " +
                                   std::to_string( ds.bandCount() ) + " bands" );

    const int width = ds.width();
    const int height = ds.height();

    // Read the full score plane band by band (single band).
    std::vector<float> scores( static_cast<size_t>( width ) * height, 0.0f );
    if ( !ds.readBandData( 1, scores.data(), width, height ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to read score plane from: " + inputPath );

    // Validity: finite scores, excluding pixels equal to the band's declared
    // NoData value when one is declared.
    bool hasNoData = false;
    float noData = 0.0f;
    {
        const double nd = ds.bandNoDataValue( 1, &hasNoData );
        if ( hasNoData && std::isfinite( nd ) )
            noData = static_cast<float>( nd );
        else
            hasNoData = false;
    }
    std::vector<uint8_t> valid( static_cast<size_t>( width ) * height, 1 );
    for ( size_t p = 0; p < scores.size(); ++p )
    {
        const float v = scores[p];
        if ( !std::isfinite( v ) || ( hasNoData && v == noData ) )
            valid[p] = 0;
    }

    context.reportProgress( 0.1, "Fusing scores" );
    SpectralSpatialFusion::Config config;
    config.radius = radius;
    config.beta = beta;
    SpectralSpatialFusion::Result result;
    QString kernelError;
    if ( !SpectralSpatialFusion::fuseScores( scores.data(), valid.data(), width, height,
                                             config, &result, &kernelError ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, kernelError.toStdString() );

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1, GDT_Float32,
                             ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    out.setNoDataValue( std::numeric_limits<float>::quiet_NaN() );

    // Write the fused plane in strips through the streaming output.
    GdalBlockStream::Tile tile;
    constexpr int kStrip = 256;
    for ( int y = 0; y < height; y += kStrip )
    {
        context.throwIfCancelled();
        const int h = std::min( kStrip, height - y );
        tile.xOffset = 0;
        tile.yOffset = y;
        tile.width = width;
        tile.height = h;
        tile.halo = 0;
        tile.bufferWidth = width;
        tile.bufferHeight = h;
        if ( !out.writeTile( 1, tile, result.fused.data() + static_cast<size_t>( y ) * width ) )
        {
            out.abandon();
            throw RSOperatorError( ErrorCode::GdalError, "Failed to write fused strip at row " +
                                                             std::to_string( y ) );
        }
        context.reportProgress( 0.1 + 0.85 * static_cast<double>( y ) / std::max( 1, height ),
                                "Writing fused raster" );
    }

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    size_t fusedCount = 0;
    for ( const uint8_t c : result.covered )
        fusedCount += c;

    Json::Value json( Json::objectValue );
    json["output"] = outputPath;
    json["width"] = width;
    json["height"] = height;
    json["radius"] = radius;
    json["beta"] = beta;
    json["fusedPixels"] = static_cast<Json::UInt64>( fusedCount );
    json["invalidPixels"] = static_cast<Json::UInt64>( scores.size() - fusedCount );
    context.reportProgress( 1.0, "Spectral-spatial fusion complete" );
    return json;
}

} // namespace sicnu::operators::rs
