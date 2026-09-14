/***************************************************************************
 * rs_sar_phase_filter_operator.cpp — Goldstein-Werner phase filtering
 * (Advanced SAR / PolSAR / InSAR 10.0, package C).
 *
 * Streams the complex interferogram with a halo (the filter window),
 * evaluates the Goldstein phasor average per core pixel, and writes the
 * filtered phasor tile-by-tile as CFloat32.
 ***************************************************************************/
#include "rs_sar_phase_filter_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_insar.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kTileDim = 256;
constexpr int kMaxWindow = 33;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

} // anonymous namespace

Json::Value RsSarPhaseFilterOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Complex (CFloat32) interferogram raster" );
    props["output"] = makeOutputParam( "output", "Output filtered phasor raster path", "tif" );
    props["band"] = makeNumberParam( "band", "1-based complex interferogram band", 1.0 );
    props["window"] = makeNumberParam( "window", "Filter window size in pixels (odd)", 5.0 );
    props["alpha"] = makeNumberParam( "alpha",
                                      "Goldstein exponent: 0 = plain phasor average, "
                                      "1 = magnitude-weighted (default 0.5)",
                                      0.5 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output filtered phasor raster path" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value RsSarPhaseFilterOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["task"] = "sar-insar";
    meta["gpu"] = false;
    meta["purpose"] = "Reduce phase noise of an interferogram before unwrapping.";
    meta["prerequisites"].append( "Complex interferogram from rs:sar_interferogram." );
    meta["limitations"].append( "Spatial Goldstein-Werner form; the output is a UNIT phasor "
                                "(magnitude information is deliberately dropped — the filter "
                                "is defined on phase)." );
    return meta;
}

Json::Value RsSarPhaseFilterOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        8ULL * ( kTileDim + kMaxWindow ) * ( kTileDim + kMaxWindow ) );
    return est;
}

Json::Value RsSarPhaseFilterOperator::estimateExecution( const Json::Value &params ) const {
    int window = getInt( params, "window", 5 );
    if ( window < 1 )
        window = 5;
    const uint64_t bufDim = static_cast<uint64_t>( kTileDim + window + 1 );
    Json::Value est( Json::objectValue );
    est["tileWidth"] = Json::Value::UInt64( kTileDim );
    est["tileHeight"] = Json::Value::UInt64( kTileDim );
    est["estimatedRamBytes"] = Json::Value::UInt64( 8ULL * bufDim * bufDim );
    est["basis"] = "dynamic";
    return est;
}

Json::Value RsSarPhaseFilterOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const int band = getInt( params, "band", 1 );
    const int window = getInt( params, "window", 5 );
    if ( window < 1 || window > kMaxWindow || window % 2 == 0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "window must be an odd integer in [1, "
                                   + std::to_string( kMaxWindow ) + "]" );
    const double alpha = getDouble( params, "alpha", 0.5 );
    if ( !( alpha >= 0.0 && alpha <= 1.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, "alpha must be in [0, 1]" );
    const int radius = window / 2;

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    if ( ds.width() <= 0 || ds.height() <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );

    QString error;
    if ( !sicnu::sar::validateComplexBands( ds, { band }, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: " + error.toStdString() );

    sicnu::sar::ComplexBandTileStream stream( ds, { band }, kTileDim, kTileDim, radius );
    GdalStreamingOutput out( QString::fromStdString( outputPath ), ds.width(), ds.height(), 1,
                             GDT_CFloat32, ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "filtered_phasor" );
    out.setMetadataItem( "SICNU_SAR_INSAR_GOLDSTEIN_ALPHA",
                         std::to_string( alpha ).c_str() );
    out.setMetadataItem( "SICNU_SAR_INSAR_FILTER_WINDOW", std::to_string( window ).c_str() );

    std::vector<std::complex<float>> outTile( static_cast<size_t>( kTileDim ) * kTileDim );
    long filteredPixels = 0;
    long totalPixels = 0;
    long tileIndex = 0;
    const long totalTiles = stream.tileCount();

    const bool ok = stream.forEach( [&]( const sicnu::sar::ComplexTile &tile,
                                         const std::complex<float> *bip ) {
        context.throwIfCancelled();
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const double phase = sicnu::sar::goldsteinPhase(
                    bip, tile.bufferWidth, tile.bufferHeight, x + radius, y + radius, radius,
                    alpha );
                const float phasorRe =
                    std::isfinite( phase ) ? static_cast<float>( std::cos( phase ) ) : kNaN;
                const float phasorIm =
                    std::isfinite( phase ) ? static_cast<float>( std::sin( phase ) ) : kNaN;
                outTile[static_cast<size_t>( y ) * tile.width + x] = { phasorRe, phasorIm };
                if ( std::isfinite( phase ) )
                    ++filteredPixels;
                ++totalPixels;
            }
        }

        ++tileIndex;
        context.reportProgress( 0.95 * tileIndex / totalTiles, "Phase filtering" );
        return out.writeTileRaw( 1,
                                 GdalBlockStream::Tile{ tile.xOffset, tile.yOffset, tile.width,
                                                        tile.height, 0, tile.width,
                                                        tile.height, tile.index,
                                                        tile.totalTiles, 0, 0 },
                                 outTile.data(), GDT_CFloat32 );
    } );

    if ( !ok || !out.closeWithError() )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/filter the phase" );
    }

    Json::Value result;
    result["output"] = outputPath;
    result["window"] = window;
    result["alpha"] = alpha;
    result["filteredPixels"] = Json::Value::Int64( filteredPixels );
    result["totalPixels"] = Json::Value::Int64( totalPixels );
    context.reportProgress( 1.0, "Phase filtering complete" );
    return result;
}

} // namespace sicnu::operators::rs
