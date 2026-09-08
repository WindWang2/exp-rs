/***************************************************************************
 * rs_raster_spatial_operators.cpp — Milestone G implementation.
 *
 * Mask ops run full-frame over the Milestone A primitives (their documented
 * contract); window ops stream halo tiles. Every op shares one mask loader
 * with sentinel/NaN → NaN normalization and strict 0/1 validity.
 ***************************************************************************/
#include "rs_raster_spatial_operators.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/primitives/connected_components.h"
#include "processing/algorithms/primitives/distance_transform.h"
#include "processing/algorithms/primitives/morphology.h"
#include "processing/algorithms/primitives/window.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr float kNaNF = std::numeric_limits<float>::quiet_NaN();
constexpr int kTileDim = 256;

const std::vector<std::string> s_morphOps = { "erode", "dilate", "open", "close" };
const std::vector<std::string> s_conn = { "4", "8" };
const std::vector<std::string> s_extrema = { "max", "min" };
const std::vector<std::string> s_focalStats = { "mean", "sum", "min", "max", "stddev", "range" };

sicnu::rs::primitives::Connectivity parseConnectivity( const std::string &token )
{
    return token == "4" ? sicnu::rs::primitives::Connectivity::Four
                        : sicnu::rs::primitives::Connectivity::Eight;
}

struct MaskInput
{
    GdalDatasetWrapper ds;
    std::vector<float> values; // normalized: NaN = NoData/invalid
    int width = 0;
    int height = 0;
};

/// Loads band @a band of @a path fully into memory with sentinel/non-finite →
/// NaN normalization; validates the 0/1 mask contract (every finite value is
/// exactly 0 or 1) for ops that require it.
MaskInput loadMask( const std::string &path, int band, bool requireBinary,
                    const std::string &opName )
{
    MaskInput in;
    if ( !in.ds.open( QString::fromStdString( path ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + path );
    in.width = in.ds.width();
    in.height = in.ds.height();
    if ( in.width <= 0 || in.height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + path );
    const size_t n = static_cast<size_t>( in.width ) * in.height;
    in.values.resize( n );
    if ( !in.ds.readBandWindow( band, 0, 0, in.width, in.height, in.values.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to read band " + std::to_string( band ) );

    bool hasSentinel = false;
    float sentinel = 0.0f;
    {
        bool flag = false;
        const double nd = in.ds.bandNoDataValue( band, &flag );
        if ( flag && std::isfinite( nd ) )
        {
            hasSentinel = true;
            sentinel = static_cast<float>( nd );
        }
    }
    for ( size_t i = 0; i < n; ++i )
    {
        float v = in.values[i];
        if ( !std::isfinite( v ) || ( hasSentinel && v == sentinel ) )
        {
            in.values[i] = kNaNF;
            continue;
        }
        if ( requireBinary && v != 0.0f && v != 1.0f )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                opName + " requires a 0/1 mask; found value " + std::to_string( v ) +
                    " — normalize first (rs:threshold_raster / rs:recode)" );
    }
    return in;
}

/// Converts a float frame into the primitives' 0/1/255 byte encoding
/// (NaN → 255 NoData; 1 → foreground; 0 → background).
std::vector<uint8_t> toByteMask( const std::vector<float> &values )
{
    std::vector<uint8_t> mask( values.size(), 255 );
    for ( size_t i = 0; i < values.size(); ++i )
    {
        if ( !std::isfinite( values[i] ) )
            continue; // 255
        mask[i] = values[i] == 1.0f ? 1 : 0;
    }
    return mask;
}

/// Writes a float frame as one tile of a single-band NaN-NoData output and
/// commits it (abandon + throw on failure).
void writeFrame( GdalStreamingOutput &out, const std::vector<float> &frame, int width,
                 int height, const GdalDatasetWrapper &source )
{
    if ( !out.writeTile( 1, GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1 },
                         frame.data() ) )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to write output raster" );
    }
    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );
    (void)source;
}

Json::Value finishResult( const std::string &opName, const std::string &outputPath, int width,
                          int height, Json::Value extra = {} )
{
    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["op"] = opName;
    result["width"] = width;
    result["height"] = height;
    for ( const auto &member : extra.getMemberNames() )
        result[member] = extra[member];
    return result;
}

// --- mask/label family ----------------------------------------------------

Json::Value runMaskOp( const std::string &opName, const Json::Value &params,
                       RSOperatorContext &context )
{
    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const int band = std::max( 1, getInt( params, "band", 1 ) );
    const std::string connToken = getEnum( params, "connectivity", s_conn, "8" );
    const auto conn = parseConnectivity( connToken );

    MaskInput in = loadMask( inputPath, band, true, opName );
    std::vector<uint8_t> mask = toByteMask( in.values );
    std::vector<uint8_t> scratch( mask.size() );

    context.reportProgress( 0.2, "Computing " + opName );

    if ( opName == "rs:morphology" )
    {
        const std::string op = getEnum( params, "op", s_morphOps, "dilate" );
        const int iterations = std::clamp( getInt( params, "iterations", 1 ), 1, 100 );
        if ( op == "erode" )
            sicnu::rs::primitives::erodeN( mask.data(), scratch.data(), in.width, in.height, iterations, conn );
        else if ( op == "dilate" )
            sicnu::rs::primitives::dilateN( mask.data(), scratch.data(), in.width, in.height, iterations, conn );
        else if ( op == "open" )
        {
            for ( int it = 0; it < iterations; ++it )
                sicnu::rs::primitives::open( mask.data(), scratch.data(), in.width, in.height, conn );
        }
        else
        {
            for ( int it = 0; it < iterations; ++it )
                sicnu::rs::primitives::close( mask.data(), scratch.data(), in.width, in.height, conn );
        }
        GdalStreamingOutput out( QString::fromStdString( outputPath ), in.width, in.height, 1,
                                 GDT_Float32, in.ds.geoTransform(), in.ds.projection() );
        if ( !out.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
        out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
        std::vector<float> frame( mask.size() );
        for ( size_t i = 0; i < mask.size(); ++i )
            frame[i] = mask[i] == 255 ? kNaNF : static_cast<float>( mask[i] );
        writeFrame( out, frame, in.width, in.height, in.ds );
        context.reportProgress( 1.0, "Morphology complete" );
        return finishResult( opName, outputPath, in.width, in.height );
    }
    else if ( opName == "rs:sieve" )
    {
        const int minArea = std::max( 1, getInt( params, "min_area_pixels", 10 ) );
        const size_t removed = sicnu::rs::primitives::removeSmallObjects(
            mask.data(), in.width, in.height, minArea, conn );
        Json::Value extra( Json::objectValue );
        extra["removedPixels"] = static_cast<Json::UInt64>( removed );
        GdalStreamingOutput out( QString::fromStdString( outputPath ), in.width, in.height, 1,
                                 GDT_Float32, in.ds.geoTransform(), in.ds.projection() );
        if ( !out.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
        out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
        std::vector<float> frame( mask.size() );
        for ( size_t i = 0; i < mask.size(); ++i )
            frame[i] = mask[i] == 255 ? kNaNF : static_cast<float>( mask[i] );
        writeFrame( out, frame, in.width, in.height, in.ds );
        context.reportProgress( 1.0, "Sieve complete" );
        return finishResult( opName, outputPath, in.width, in.height, extra );
    }
    else if ( opName == "rs:fill_holes" )
    {
        // Holes = background components with no border contact. Label the
        // inverted mask and clear every border-reaching component's "hole"
        // claim; remaining background cells become foreground.
        std::vector<uint8_t> inverted( mask.size() );
        for ( size_t i = 0; i < mask.size(); ++i )
            inverted[i] = mask[i] == 0 ? 1 : 0;
        const auto labeling = sicnu::rs::primitives::labelComponents(
            inverted.data(), in.width, in.height, conn );
        const auto areas = sicnu::rs::primitives::componentAreas( labeling );
        std::vector<uint8_t> touchesBorder( static_cast<size_t>( std::max( 0, labeling.componentCount ) ) + 1, 0 );
        for ( int x = 0; x < in.width; ++x )
        {
            const size_t top = static_cast<size_t>( x );
            const size_t bottom = static_cast<size_t>( in.height - 1 ) * in.width + x;
            if ( labeling.labels[top] > 0 )
                touchesBorder[static_cast<size_t>( labeling.labels[top] )] = 1;
            if ( labeling.labels[bottom] > 0 )
                touchesBorder[static_cast<size_t>( labeling.labels[bottom] )] = 1;
        }
        for ( int y = 0; y < in.height; ++y )
        {
            const size_t left = static_cast<size_t>( y ) * in.width;
            const size_t right = left + in.width - 1;
            if ( labeling.labels[left] > 0 )
                touchesBorder[static_cast<size_t>( labeling.labels[left] )] = 1;
            if ( labeling.labels[right] > 0 )
                touchesBorder[static_cast<size_t>( labeling.labels[right] )] = 1;
        }
        Json::Value extra( Json::objectValue );
        std::uint64_t filled = 0;
        for ( size_t i = 0; i < mask.size(); ++i )
        {
            const int32_t label = labeling.labels[i];
            if ( label > 0 && !touchesBorder[static_cast<size_t>( label )] && mask[i] == 0 )
            {
                mask[i] = 1;
                ++filled;
            }
        }
        extra["filledPixels"] = static_cast<Json::UInt64>( filled );
        GdalStreamingOutput out( QString::fromStdString( outputPath ), in.width, in.height, 1,
                                 GDT_Float32, in.ds.geoTransform(), in.ds.projection() );
        if ( !out.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
        out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
        std::vector<float> frame( mask.size() );
        for ( size_t i = 0; i < mask.size(); ++i )
            frame[i] = mask[i] == 255 ? kNaNF : static_cast<float>( mask[i] );
        writeFrame( out, frame, in.width, in.height, in.ds );
        context.reportProgress( 1.0, "Fill holes complete" );
        return finishResult( opName, outputPath, in.width, in.height, extra );
    }
    else if ( opName == "rs:connected_components" )
    {
        const auto labeling =
            sicnu::rs::primitives::labelComponents( mask.data(), in.width, in.height, conn );
        Json::Value extra( Json::objectValue );
        extra["componentCount"] = labeling.componentCount;
        GdalStreamingOutput out( QString::fromStdString( outputPath ), in.width, in.height, 1,
                                 GDT_Float32, in.ds.geoTransform(), in.ds.projection() );
        if ( !out.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
        out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
        std::vector<float> frame( mask.size(), kNaNF );
        for ( size_t i = 0; i < mask.size(); ++i )
            if ( labeling.labels[i] > 0 )
                frame[i] = static_cast<float>( labeling.labels[i] );
        writeFrame( out, frame, in.width, in.height, in.ds );
        context.reportProgress( 1.0, "Connected components complete" );
        return finishResult( opName, outputPath, in.width, in.height, extra );
    }
    else if ( opName == "rs:proximity" )
    {
        std::vector<float> distances( mask.size() );
        if ( !sicnu::rs::primitives::distanceToForeground( mask.data(), in.width, in.height,
                                                           distances.data() ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Distance transform failed" );
        std::uint64_t unreached = 0;
        for ( const float d : distances )
            if ( std::isinf( d ) )
                ++unreached;
        if ( unreached == distances.size() )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "mask contains no foreground: every distance is infinite" );
        Json::Value extra( Json::objectValue );
        extra["unreachablePixels"] = static_cast<Json::UInt64>( unreached );
        GdalStreamingOutput out( QString::fromStdString( outputPath ), in.width, in.height, 1,
                                 GDT_Float32, in.ds.geoTransform(), in.ds.projection() );
        if ( !out.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
        out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
        for ( float &d : distances )
            if ( std::isinf( d ) )
                d = kNaNF; // unreachable → NoData, never an infinite value
        writeFrame( out, distances, in.width, in.height, in.ds );
        context.reportProgress( 1.0, "Proximity complete" );
        return finishResult( opName, outputPath, in.width, in.height, extra );
    }

    throw RSOperatorError( ErrorCode::InvalidParameter, "unknown mask op " + opName );
}

// --- window family (streamed halo tiles) -----------------------------------

Json::Value runWindowOp( const std::string &opName, const Json::Value &params,
                         RSOperatorContext &context )
{
    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const int band = std::max( 1, getInt( params, "band", 1 ) );
    const int window = std::clamp( getInt( params, "window", 3 ), 3, 101 );
    if ( window % 2 == 0 )
        throw RSOperatorError( ErrorCode::InvalidParameter, "window must be odd" );
    const int radius = window / 2;

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    const int width = ds.width();
    const int height = ds.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );

    bool hasSentinel = false;
    float sentinel = 0.0f;
    {
        bool flag = false;
        const double nd = ds.bandNoDataValue( band, &flag );
        if ( flag && std::isfinite( nd ) )
        {
            hasSentinel = true;
            sentinel = static_cast<float>( nd );
        }
    }

    const int tileSize = std::clamp( getInt( params, "tile_size", kTileDim ), 16, 4096 );
    GdalBlockStream stream( ds, band, tileSize, tileSize, radius );
    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1, GDT_Float32,
                             ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );

    const std::string stat = opName == "rs:focal_stats"
        ? getEnum( params, "stat", s_focalStats, "mean" )
        : getEnum( params, "extremum", s_extrema, "max" );

    const int totalTiles = stream.tileCount();
    int tileIndex = 0;
    std::vector<float> scratch;
    std::vector<float> outTile;

    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *haloBuf ) {
        context.throwIfCancelled();
        const int bw = tile.bufferWidth;
        const size_t bufN = static_cast<size_t>( bw ) * tile.bufferHeight;
        scratch.resize( bufN );
        std::copy( haloBuf, haloBuf + bufN, scratch.begin() );
        if ( hasSentinel )
            for ( size_t i = 0; i < bufN; ++i )
                if ( scratch[i] == sentinel )
                    scratch[i] = kNaNF;

        outTile.resize( static_cast<size_t>( tile.width ) * tile.height );
        for ( int y = 0; y < tile.height; ++y )
            for ( int x = 0; x < tile.width; ++x )
            {
                const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                const float centre = scratch[static_cast<size_t>( y + radius ) * bw + ( x + radius )];
                if ( !std::isfinite( centre ) )
                {
                    outTile[idx] = kNaNF;
                    continue;
                }
                float lo = centre;
                float hi = centre;
                double sum = 0.0;
                double sumSq = 0.0;
                size_t used = 0;
                for ( int dy = -radius; dy <= radius; ++dy )
                    for ( int dx = -radius; dx <= radius; ++dx )
                    {
                        const float v =
                            scratch[static_cast<size_t>( y + radius + dy ) * bw + ( x + radius + dx )];
                        if ( !std::isfinite( v ) )
                            continue; // NoData neighbours are excluded, not zeroed
                        lo = std::min( lo, v );
                        hi = std::max( hi, v );
                        sum += v;
                        sumSq += static_cast<double>( v ) * v;
                        ++used;
                    }
                if ( opName == "rs:local_extrema" )
                {
                    const bool isExtreme =
                        stat == "max" ? centre >= hi : centre <= lo;
                    outTile[idx] = isExtreme ? 1.0f : 0.0f;
                }
                else
                {
                    if ( used == 0 )
                    {
                        outTile[idx] = kNaNF;
                        continue;
                    }
                    if ( stat == "mean" )
                        outTile[idx] = static_cast<float>( sum / used );
                    else if ( stat == "sum" )
                        outTile[idx] = static_cast<float>( sum );
                    else if ( stat == "min" )
                        outTile[idx] = lo;
                    else if ( stat == "max" )
                        outTile[idx] = hi;
                    else if ( stat == "range" )
                        outTile[idx] = hi - lo;
                    else // stddev (population, window descriptive)
                    {
                        const double m = sum / used;
                        outTile[idx] = static_cast<float>( std::sqrt( std::max( 0.0, sumSq / used - m * m ) ) );
                    }
                }
            }
        if ( !out.writeTile( 1, tile, outTile.data() ) )
            return false;
        context.reportProgress( 0.9 * ( ++tileIndex ) / totalTiles, "Window pass" );
        return true;
    } );
    if ( !ok )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream window tiles" );
    }
    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    return finishResult( opName, outputPath, width, height );
}

} // anonymous namespace

Json::Value runRasterSpatialOp( const std::string &opName, const Json::Value &params,
                                RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );
    if ( opName == "rs:local_extrema" || opName == "rs:focal_stats" )
        return runWindowOp( opName, params, context );
    return runMaskOp( opName, params, context );
}

// --- schemas / metadata / estimates ----------------------------------------

namespace {

Json::Value spatialSchema( const std::string &displayName, const std::string &description,
                           bool windowFamily, std::initializer_list<std::string> extraParams = {} )
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", windowFamily
        ? "Input raster (NoData pixels excluded from window statistics)"
        : "Input 0/1 mask raster (normalize with rs:threshold_raster first)" );
    props["output"] = makeOutputParam( "output", "Output raster", "tif" );
    props["band"] = makeIntegerParam( "band", "1-based input band", 1 );
    if ( windowFamily )
    {
        props["window"] = makeIntegerParam( "window", "Odd window side length (3..101)", 3 );
    }
    else
    {
        props["connectivity"] = makeEnumParam( "connectivity", "Neighbourhood connectivity", s_conn, "8" );
        for ( const auto &p : extraParams )
        {
            if ( p == "op" )
                props["op"] = makeEnumParam( "op", "Morphological operation", s_morphOps, "dilate" );
            else if ( p == "iterations" )
                props["iterations"] = makeIntegerParam( "iterations", "Operation iterations (1..100)", 1 );
            else if ( p == "min_area_pixels" )
                props["min_area_pixels"] = makeIntegerParam( "min_area_pixels", "Minimum component area in pixels", 10 );
        }
    }

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );

    Json::Value root = makeRootSchema( displayName, description, props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value spatialMetadata( const std::string &displayName, const std::string &notes,
                             const char *task, bool largeSafe )
{
    Json::Value meta( Json::objectValue );
    meta["group"] = "raster_spatial";
    meta["displayName"] = displayName;
    meta["tags"].append( "raster_spatial" );
    meta["task"] = task;
    meta["notes"] = notes;
    meta["gpu"] = false;
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    meta["largeRasterSafe"] = largeSafe;
    return meta;
}

} // namespace

Json::Value RsMorphologyOperator::schema() const
{
    return spatialSchema( displayName(), description(), false, { "op", "iterations" } );
}
Json::Value RsMorphologyOperator::metadata() const
{
    return spatialMetadata( displayName(),
        "Shared primitives/morphology kernel (0/1/255 masks, NoData protected, "
        "documented replicate borders). FullRaster contract.", "raster_cleanup", false );
}
Json::Value RsMorphologyOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 4096ULL * 4096ULL );
    return est;
}
Json::Value RsMorphologyOperator::estimateExecution( const Json::Value &params ) const
{
    Json::Value est = executionEstimate();
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) )
        {
            if ( auto ram = sicnu::processing::checkedMulN(
                     { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                       static_cast<std::uint64_t>( std::max( 1, probe.height() ) ), 3ULL } ) )
            {
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
            }
        }
    }
    return est;
}

Json::Value RsConnectedComponentsOperator::schema() const
{
    return spatialSchema( displayName(), description(), false );
}
Json::Value RsConnectedComponentsOperator::metadata() const
{
    return spatialMetadata( displayName(),
        "Two-pass union-find labeling with raster-order compact labels; output "
        "0 = background/NoData. FullRaster contract.", "raster_cleanup", false );
}
Json::Value RsConnectedComponentsOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 9ULL * 4096ULL * 4096ULL );
    return est;
}
Json::Value RsConnectedComponentsOperator::estimateExecution( const Json::Value &params ) const
{
    Json::Value est = executionEstimate();
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) )
        {
            if ( auto ram = sicnu::processing::checkedMulN(
                     { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                       static_cast<std::uint64_t>( std::max( 1, probe.height() ) ), 9ULL } ) )
            {
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
            }
        }
    }
    return est;
}

Json::Value RsFillHolesOperator::schema() const
{
    return spatialSchema( displayName(), description(), false );
}
Json::Value RsFillHolesOperator::metadata() const
{
    return spatialMetadata( displayName(),
        "Holes are background components without border contact; reports "
        "filledPixels. FullRaster contract.", "raster_cleanup", false );
}
Json::Value RsFillHolesOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 9ULL * 4096ULL * 4096ULL );
    return est;
}
Json::Value RsFillHolesOperator::estimateExecution( const Json::Value &params ) const
{
    Json::Value est = executionEstimate();
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) )
        {
            if ( auto ram = sicnu::processing::checkedMulN(
                     { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                       static_cast<std::uint64_t>( std::max( 1, probe.height() ) ), 9ULL } ) )
            {
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
            }
        }
    }
    return est;
}

Json::Value RsSieveOperator::schema() const
{
    return spatialSchema( displayName(), description(), false, { "min_area_pixels" } );
}
Json::Value RsSieveOperator::metadata() const
{
    return spatialMetadata( displayName(),
        "Removes foreground components smaller than min_area_pixels; reports "
        "removedPixels. FullRaster contract.", "raster_cleanup", false );
}
Json::Value RsSieveOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 9ULL * 4096ULL * 4096ULL );
    return est;
}
Json::Value RsSieveOperator::estimateExecution( const Json::Value &params ) const
{
    Json::Value est = executionEstimate();
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) )
        {
            if ( auto ram = sicnu::processing::checkedMulN(
                     { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                       static_cast<std::uint64_t>( std::max( 1, probe.height() ) ), 9ULL } ) )
            {
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
            }
        }
    }
    return est;
}

Json::Value RsProximityOperator::schema() const
{
    return spatialSchema( displayName(), description(), false );
}
Json::Value RsProximityOperator::metadata() const
{
    return spatialMetadata( displayName(),
        "Exact Euclidean distance transform (Felzenszwalb lower envelope), "
        "distance in pixels; unreachable pixels are NaN NoData; an all-background "
        "mask is a typed refusal. FullRaster contract.", "raster_cleanup", false );
}
Json::Value RsProximityOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 24ULL * 4096ULL * 4096ULL );
    return est;
}
Json::Value RsProximityOperator::estimateExecution( const Json::Value &params ) const
{
    Json::Value est = executionEstimate();
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) )
        {
            if ( auto ram = sicnu::processing::checkedMulN(
                     { static_cast<std::uint64_t>( std::max( 1, probe.width() ) ),
                       static_cast<std::uint64_t>( std::max( 1, probe.height() ) ), 24ULL } ) )
            {
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
            }
        }
    }
    return est;
}

Json::Value RsLocalExtremaOperator::schema() const
{
    using namespace schema;
    Json::Value root = spatialSchema( displayName(), description(), true );
    return root;
}
Json::Value RsLocalExtremaOperator::metadata() const
{
    return spatialMetadata( displayName(),
        "Window max/min test over streamed halo tiles (replicate edges); "
        "NoData neighbours are excluded from the comparison, NoData centres "
        "are NaN. Streaming.", "raster_cleanup", true );
}
Json::Value RsLocalExtremaOperator::executionEstimate() const
{
    return sicnu::processing::makeStreamingEstimate( 256, 256, 1, 2, 2, 0, 1024 * 1024 );
}
Json::Value RsLocalExtremaOperator::estimateExecution( const Json::Value &params ) const
{
    const int tileSize = std::clamp( getInt( params, "tile_size", 256 ), 16, 4096 );
    return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 2, 2, 0, 1024 * 1024 );
}

Json::Value RsFocalStatsOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Input raster" );
    props["output"] = makeOutputParam( "output", "Output raster", "tif" );
    props["band"] = makeIntegerParam( "band", "1-based input band", 1 );
    props["window"] = makeIntegerParam( "window", "Odd window side length (3..101)", 3 );
    props["stat"] = makeEnumParam( "stat", "Window statistic", s_focalStats, "mean" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}
Json::Value RsFocalStatsOperator::metadata() const
{
    return spatialMetadata( displayName(),
        "Window statistics with the replicate edge policy (primitives/window.h); "
        "NoData neighbours are excluded; stddev is the population convention. "
        "Streaming.", "raster_cleanup", true );
}
Json::Value RsFocalStatsOperator::executionEstimate() const
{
    return sicnu::processing::makeStreamingEstimate( 256, 256, 1, 2, 2, 0, 1024 * 1024 );
}
Json::Value RsFocalStatsOperator::estimateExecution( const Json::Value &params ) const
{
    const int tileSize = std::clamp( getInt( params, "tile_size", 256 ), 16, 4096 );
    return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 2, 2, 0, 1024 * 1024 );
}

} // namespace sicnu::operators::rs
