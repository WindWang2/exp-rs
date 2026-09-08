/***************************************************************************
 * rs_spectral_derivative_operator.cpp — Milestone B3
 *
 * Tile-streamed derivative: read all B bands of a tile (BIP), compute the
 * per-pixel derivative vector, write the B−1 (or B−2) output bands.
 ***************************************************************************/
#include "rs_spectral_derivative_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_derivative.h"
#include "processing/framework/resource_estimation.h"
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
constexpr int kTileDim = 256;

/// Resolves the wavelength axis: explicit param (nm, strictly ascending)
/// wins, else the per-band WAVELENGTH metadata. Returns false (with a
/// message) when the axis is missing or not strictly ascending.
bool resolveWavelengths( const Json::Value &params, GdalDatasetWrapper &ds, int bandCount,
                         std::vector<double> &wavelengths, std::string *errorMessage )
{
    wavelengths.clear();
    if ( params.isMember( "wavelengths" ) && params["wavelengths"].isArray() )
    {
        const Json::Value &arr = params["wavelengths"];
        if ( static_cast<int>( arr.size() ) != bandCount )
        {
            if ( errorMessage )
                *errorMessage = "wavelengths array must have one entry per band (" +
                                std::to_string( bandCount ) + "), got " +
                                std::to_string( arr.size() );
            return false;
        }
        for ( const auto &v : arr )
            wavelengths.push_back( v.asDouble() );
    }
    else
    {
        for ( int b = 1; b <= bandCount; ++b )
        {
            const QString raw = ds.bandMetadataItem( b, "WAVELENGTH" );
            if ( raw.isEmpty() )
            {
                if ( errorMessage )
                    *errorMessage = "band " + std::to_string( b ) +
                                    " carries no WAVELENGTH metadata and no explicit 'wavelengths' "
                                    "parameter was given; a derivative against the band index is not "
                                    "a spectral derivative — stamp WAVELENGTH or pass the axis";
                return false;
            }
            bool ok = false;
            const double wl = raw.toDouble( &ok );
            if ( !ok || !std::isfinite( wl ) )
            {
                if ( errorMessage )
                    *errorMessage = "band " + std::to_string( b ) + " WAVELENGTH metadata is not a number: " +
                                    raw.toStdString();
                return false;
            }
            wavelengths.push_back( wl );
        }
    }
    for ( int b = 1; b < bandCount; ++b )
    {
        if ( !( wavelengths[b] > wavelengths[b - 1] ) )
        {
            if ( errorMessage )
                *errorMessage = "wavelength axis must be strictly ascending in band order "
                                "(band " + std::to_string( b + 1 ) + " <= band " + std::to_string( b ) + ")";
            return false;
        }
    }
    return true;
}
} // anonymous namespace

Json::Value RsSpectralDerivativeOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band reflectance raster (bands in wavelength order)");
    props["output"] = makeOutputParam("output", "Output derivative raster", "tif");
    props["order"] = makeIntegerParam("order", "Derivative order (1 or 2)", 1);
    Json::Value wlParam( Json::objectValue );
    wlParam["type"] = "array";
    wlParam["description"] = "Explicit wavelength axis in nm (strictly ascending, one per band); default reads per-band WAVELENGTH metadata";
    wlParam["items"]["type"] = "number";
    props["wavelengths"] = wlParam;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["order"] = makeIntegerParam("order", "Derivative order", 1);
    outputs["bandsOut"] = makeIntegerParam("bandsOut", "Output band count", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "order"});
    return root;
}

Json::Value RsSpectralDerivativeOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("derivative");
    meta["tags"].append("feature-engineering");
    meta["task"] = "feature-engineering";
    meta["notes"] = "Finite differences along the wavelength axis; second order is "
                    "the first derivative applied twice along the successive midpoint "
                    "axes (honest irregular-spacing form). Bit-exact grade.";
    meta["gpu"] = false;
    meta["purpose"] = "Derive band-ratio-free spectral shape features (red-edge slope, absorption asymmetry) per pixel.";
    meta["prerequisites"].append("Bands must carry WAVELENGTH metadata (nm) or an explicit 'wavelengths' parameter; the axis must be strictly ascending in band order.");
    meta["workflowHints"].append("Apply after atmospheric correction; derivatives amplify noise — consider rs:temporal_smooth-style smoothing upstream.");
    meta["limitations"].append("Output band count shrinks by the derivative order (B−1 / B−2); output band b carries the midpoint wavelength of its input pair.");
    meta["limitations"].append("NaN pixels propagate to every output derivative that touches them.");
    return meta;
}

Json::Value RsSpectralDerivativeOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        8ULL * kTileDim * kTileDim * sizeof( float ) );
    return est;
}

Json::Value RsSpectralDerivativeOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) )
             && probe.bandCount() > 0 )
        {
            const std::uint64_t bands = static_cast<std::uint64_t>( probe.bandCount() );
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { 256ULL, 256ULL, bands + 2ULL, static_cast<std::uint64_t>( sizeof( float ) ) } );
            if ( ram )
            {
                Json::Value est( Json::objectValue );
                est["tileWidth"] = Json::Value::UInt64( kTileDim );
                est["tileHeight"] = Json::Value::UInt64( kTileDim );
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
                return est;
            }
        }
    }
    return executionEstimate();
}

Json::Value RsSpectralDerivativeOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const int order = getInt( params, "order", 1 );
    if ( order != 1 && order != 2 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "order must be 1 or 2, got " + std::to_string( order ) );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    const int minBands = order + 1;
    if ( width <= 0 || height <= 0 || bandCount < minBands )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Order-" + std::to_string( order ) +
                                   " derivative needs at least " + std::to_string( minBands ) + " bands" );

    std::vector<double> wavelengths;
    std::string axisError;
    if ( !resolveWavelengths( params, ds, bandCount, wavelengths, &axisError ) )
        throw RSOperatorError( ErrorCode::InvalidInputData, axisError );

    // Midpoint axes for the output metadata (order 1: B−1; order 2: B−2).
    std::vector<double> midAxis1( static_cast<size_t>( std::max( 0, bandCount - 1 ) ) );
    SpectralDerivative::midpointAxis( wavelengths.data(), bandCount, midAxis1.data() );
    std::vector<double> midAxis2( static_cast<size_t>( std::max( 0, bandCount - 2 ) ) );
    if ( order == 2 )
        SpectralDerivative::midpointAxis( midAxis1.data(), bandCount - 1, midAxis2.data() );
    const std::vector<double> &outAxis = ( order == 1 ) ? midAxis1 : midAxis2;
    const int bandsOut = bandCount - order;

    context.logInfo( "Spectral derivative order " + std::to_string( order ) + " over " +
                     std::to_string( bandCount ) + " bands -> " + std::to_string( bandsOut ) );

    GdalMultibandBlockStream stream( ds, bandCount, kTileDim, kTileDim );

    GdalStreamingOutput output( QString::fromStdString( outputPath ), width, height, bandsOut,
                                GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !output.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    output.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );

    const size_t tilePixels = static_cast<size_t>( kTileDim ) * kTileDim;
    std::vector<float> outTile( tilePixels );
    std::vector<float> d1( tilePixels * std::max( 1, bandCount - 1 ) );
    std::vector<float> d2( tilePixels * std::max( 1, bandCount - 2 ) );
    const int totalTiles = stream.tileCount();
    int tileIndex = 0;

    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *bip ) {
        context.throwIfCancelled();
        const size_t tp = static_cast<size_t>( tile.width ) * tile.height;
        // Per-pixel derivative over the band axis.
        for ( size_t p = 0; p < tp; ++p )
        {
            const float *pixel = bip + p * bandCount;
            if ( order == 1 )
            {
                SpectralDerivative::firstDerivative( pixel, wavelengths.data(), bandCount,
                                                     &d1[p * ( bandCount - 1 )] );
            }
            else
            {
                SpectralDerivative::secondDerivative( pixel, wavelengths.data(), bandCount,
                                                      &d2[p * ( bandCount - 2 )] );
            }
        }
        const float *src = ( order == 1 ) ? d1.data() : d2.data();
        const int srcBands = bandsOut;
        for ( int b = 0; b < srcBands; ++b )
        {
            for ( size_t p = 0; p < tp; ++p )
                outTile[p] = src[p * srcBands + b];
            if ( !output.writeTile( b + 1, tile, outTile.data() ) )
                return false;
        }
        ++tileIndex;
        context.reportProgress( 0.9 * tileIndex / totalTiles, "Computing derivative" );
        return true;
    } );
    if ( !ok )
    {
        output.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/derive tiles" );
    }

    QString closeError;
    if ( !output.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["order"] = order;
    result["bandsOut"] = bandsOut;
    result["width"] = width;
    result["height"] = height;
    // Output band b corresponds to the midpoint wavelength of its input pair
    // (the streaming writer cannot stamp per-band metadata; the axis travels
    // in the result JSON for downstream feature steps).
    Json::Value axis( Json::arrayValue );
    for ( const double wl : outAxis )
        axis.append( wl );
    result["outputWavelengthsNm"] = axis;
    context.reportProgress( 1.0, "Spectral derivative complete" );
    return result;
}

} // namespace sicnu::operators::rs
