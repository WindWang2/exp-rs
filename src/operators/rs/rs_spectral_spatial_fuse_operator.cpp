/***************************************************************************
 * rs_spectral_spatial_fuse_operator.cpp — see the header for the contract.
 *
 * Streams the single-band score plane in halo'd tiles: every tile is read
 * with an r-pixel halo (clamped to the raster), fused by the kernel, and only
 * the interior is written back, so interior results are identical to a
 * whole-plane pass (pure window function, no cross-tile state). Per-pixel
 * validity comes from the declared NoData value and finiteness.
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

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

SpectralSpatialFusion::Method parseMethod( const std::string &text )
{
    if ( text == "mean" )
        return SpectralSpatialFusion::Method::Mean;
    if ( text == "bilateral" )
        return SpectralSpatialFusion::Method::Bilateral;
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "'method' must be 'mean' or 'bilateral', got '" + text + "'" );
}

} // namespace

Json::Value RsSpectralSpatialFuseOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Single-band spectral score raster" );
    props["output"] = makeOutputParam( "output", "Single-band fused score raster", "tif" );

    Json::Value radius( Json::objectValue );
    radius["type"] = "integer";
    radius["minimum"] = 0;
    radius["maximum"] = 128;
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

    props["method"] = makeEnumParam(
        "method",
        "Neighborhood aggregate: 'mean' averages the valid window members; 'bilateral' "
        "range-weights them so score edges are preserved.",
        { "mean", "bilateral" }, "mean" );

    Json::Value sigmaRange( Json::objectValue );
    sigmaRange["type"] = "number";
    sigmaRange["exclusiveMinimum"] = 0.0;
    sigmaRange["default"] = 1.0;
    sigmaRange["description"] =
        "Range Gaussian sigma (score units) for method 'bilateral': neighbors whose score "
        "differs from the center by many sigma are effectively excluded from the aggregate.";
    props["sigmaRange"] = sigmaRange;

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
                    "a neighbor's aggregate (renormalized, no zero-fill bias). Bit-exact grade. "
                    "method 'bilateral' range-weights the neighborhood so score edges are "
                    "preserved instead of smeared.";
    meta["gpu"] = false;
    meta["purpose"] = "Suppress isolated single-pixel false alarms after spectral target "
                      "detection while preserving NoData semantics; the bilateral method "
                      "additionally preserves score edges.";
    meta["prerequisites"].append( "Input must be a single-band score raster." );
    meta["workflowHints"].append( "Typical chain: rs:matched_filter|rs:ace|rs:cem_detection -> "
                                  "rs:spectral_spatial_fuse -> rs:threshold_raster." );
    meta["limitations"].append( "The bilateral method is O(pixels * (2r+1)^2) with no interior "
                                "cancellation point, like the mean; radius is bounded to "
                                "[0, 128]." );
    return meta;
}

Json::Value RsSpectralSpatialFuseOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    // Streaming with an r-pixel halo: the footprint scales with (256+2r)² —
    // 13 bytes/px at the nominal 256×256 tile with r = 1, rounded up. The
    // worst case (r = 128) is ~3.5x this nominal figure.
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 16ULL * 256ULL * 256ULL );
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
    if ( !std::isfinite( beta ) || beta < 0.0 || beta > 1.0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "'beta' must be within [0, 1], got " + std::to_string( beta ) );
    const SpectralSpatialFusion::Method method = parseMethod( getString( params, "method", "mean" ) );
    double sigmaRange = 1.0;
    if ( params.isMember( "sigmaRange" ) )
    {
        sigmaRange = getDouble( params, "sigmaRange", 1.0 );
        if ( !std::isfinite( sigmaRange ) || sigmaRange <= 0.0 )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "'sigmaRange' must be finite and > 0, got " +
                                       std::to_string( sigmaRange ) );
    }
    // The kernel is O(pixels * (2r+1)^2) with no interior cancellation point,
    // so an absurd radius is refused up front instead of hanging (schema
    // documents the same bound).
    if ( radius < 0 || radius > 128 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "'radius' must be within [0, 128], got " +
                                   std::to_string( radius ) );

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

    // Declared NoData for the score band (when finite); combined with
    // finiteness it defines per-pixel validity.
    bool hasNoData = false;
    float noData = 0.0f;
    {
        const double nd = ds.bandNoDataValue( 1, &hasNoData );
        if ( hasNoData && std::isfinite( nd ) )
            noData = static_cast<float>( nd );
        else
            hasNoData = false;
    }

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1, GDT_Float32,
                             ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    out.setNoDataValue( std::numeric_limits<float>::quiet_NaN() );

    SpectralSpatialFusion::Config config;
    config.radius = radius;
    config.beta = beta;
    config.method = method;
    config.sigmaRange = sigmaRange;

    // Halo'd tile loop: read [x0−r, x0+bw+r] × [y0−r, y0+bh+r] (clamped to the
    // raster), fuse the padded plane, write back only the interior. Out-of-
    // raster cells never enter the window because the read is clamped.
    constexpr int kTile = 256;
    const int tilesX = ( width + kTile - 1 ) / kTile;
    const int tilesY = ( height + kTile - 1 ) / kTile;
    const int totalTiles = tilesX * tilesY;
    const double perTile = totalTiles > 0 ? 1.0 / totalTiles : 0.0;

    std::vector<float> padded;
    std::vector<uint8_t> valid;
    std::vector<float> interior;
    SpectralSpatialFusion::Result result; // reused across tiles (no per-tile growth)
    size_t fusedCount = 0;
    int tilesSeen = 0;
    for ( int ty = 0; ty < tilesY; ++ty )
    {
        for ( int tx = 0; tx < tilesX; ++tx )
        {
            context.throwIfCancelled();
            const int x0 = tx * kTile;
            const int y0 = ty * kTile;
            const int bw = std::min( kTile, width - x0 );
            const int bh = std::min( kTile, height - y0 );
            const int px0 = std::max( 0, x0 - radius );
            const int py0 = std::max( 0, y0 - radius );
            const int pw = std::min( width, x0 + bw + radius ) - px0;
            const int ph = std::min( height, y0 + bh + radius ) - py0;
            const size_t paddedPixels = static_cast<size_t>( pw ) * ph;

            padded.resize( paddedPixels );
            if ( !ds.readBandWindow( 1, px0, py0, pw, ph, padded.data() ) )
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read score window at (" +
                                           std::to_string( px0 ) + "," +
                                           std::to_string( py0 ) + ")" );

            valid.assign( paddedPixels, 1 );
            for ( size_t p = 0; p < paddedPixels; ++p )
            {
                const float v = padded[p];
                if ( !std::isfinite( v ) || ( hasNoData && v == noData ) )
                    valid[p] = 0;
            }

            QString kernelError;
            if ( !SpectralSpatialFusion::fuseScores( padded.data(), valid.data(), pw, ph,
                                                     config, &result, &kernelError ) )
                throw RSOperatorError( ErrorCode::InvalidParameter, kernelError.toStdString() );

            interior.resize( static_cast<size_t>( bw ) * bh );
            for ( int y = 0; y < bh; ++y )
                for ( int x = 0; x < bw; ++x )
                {
                    const size_t src =
                        static_cast<size_t>( y0 - py0 + y ) * pw + ( x0 - px0 + x );
                    interior[static_cast<size_t>( y ) * bw + x] = result.fused[src];
                    if ( result.covered[src] )
                        ++fusedCount;
                }

            GdalBlockStream::Tile tile;
            tile.xOffset = x0;
            tile.yOffset = y0;
            tile.width = bw;
            tile.height = bh;
            tile.halo = 0;
            tile.bufferWidth = bw;
            tile.bufferHeight = bh;
            if ( !out.writeTile( 1, tile, interior.data() ) )
            {
                out.abandon();
                throw RSOperatorError( ErrorCode::GdalError, "Failed to write fused tile at (" +
                                                                 std::to_string( x0 ) + "," +
                                                                 std::to_string( y0 ) + ")" );
            }
            context.reportProgress( ( ++tilesSeen ) * perTile, "Fusing scores" );
        }
    }

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    const size_t plane = static_cast<size_t>( width ) * height;
    Json::Value json( Json::objectValue );
    json["output"] = outputPath;
    json["width"] = width;
    json["height"] = height;
    json["radius"] = radius;
    json["beta"] = beta;
    json["method"] = method == SpectralSpatialFusion::Method::Bilateral ? "bilateral" : "mean";
    if ( method == SpectralSpatialFusion::Method::Bilateral )
        json["sigmaRange"] = sigmaRange;
    json["fusedPixels"] = static_cast<Json::UInt64>( fusedCount );
    json["invalidPixels"] = static_cast<Json::UInt64>( plane - fusedCount );
    context.reportProgress( 1.0, "Spectral-spatial fusion complete" );
    return json;
}

} // namespace sicnu::operators::rs
