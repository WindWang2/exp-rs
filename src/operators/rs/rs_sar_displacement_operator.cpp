/***************************************************************************
 * rs_sar_displacement_operator.cpp — phase-to-displacement conversion
 * (Advanced SAR / PolSAR / InSAR 10.0, package C; DECISIONS D-009).
 *
 * Streams the unwrapped phase with a 1-pixel halo: displacement per core
 * pixel, plus the Itoh discontinuity ratio accumulated from pairs whose
 * left/top member is in the core (no double counting, no cross-tile miss).
 ***************************************************************************/
#include "rs_sar_displacement_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_insar.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_block_stream.h"
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
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

} // anonymous namespace

Json::Value RsSarDisplacementOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Unwrapped phase raster (radians)" );
    props["output"] = makeOutputParam( "output", "Output displacement raster path", "tif" );
    props["band"] = makeNumberParam( "band", "1-based phase band", 1.0 );
    props["wavelengthUm"] = makeNumberParam( "wavelengthUm",
                                             "Radar wavelength in micrometres (e.g. 56235 for "
                                             "Sentinel-1 C-band); falls back to the scene "
                                             "metadata SICNU_SAR_WAVELENGTH_UM",
                                             0.0 );
    props["warnThreshold"] = makeNumberParam( "warnThreshold",
                                              "Itoh discontinuity ratio above which the "
                                              "result warns (0–1)",
                                              0.02 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output displacement raster path" );
    outputs["discontinuityRatio"] = makeStringParam( "discontinuityRatio",
                                                     "Phase wrap diagnostic", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value RsSarDisplacementOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["tags"].append( "deformation" );
    meta["task"] = "sar-insar";
    meta["gpu"] = false;
    meta["purpose"] = "Radar-interferometric surface displacement in line-of-sight "
                      "metres for deformation mapping.";
    meta["prerequisites"].append( "Unwrapped phase (rs:sar_unwrap or an external provider); "
                                  "declared or explicit radar wavelength." );
    meta["limitations"].append( "Sign convention: positive d_los = motion TOWARD the sensor." );
    meta["limitations"].append( "Cannot reliably distinguish wrapped from unwrapped input; "
                                "the Itoh discontinuity ratio is reported for the caller "
                                "to judge." );
    return meta;
}

Json::Value RsSarDisplacementOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        2ULL * 4ULL * ( kTileDim + 2 ) * ( kTileDim + 2 ) );
    return est;
}

Json::Value RsSarDisplacementOperator::estimateExecution( const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarDisplacementOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const int band = getInt( params, "band", 1 );
    double wavelengthUm = getDouble( params, "wavelengthUm", 0.0 );
    const double warnThreshold = getDouble( params, "warnThreshold", 0.02 );
    if ( !( warnThreshold >= 0.0 && warnThreshold <= 1.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, "warnThreshold must be in [0, 1]" );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    if ( ds.width() <= 0 || ds.height() <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );
    if ( band < 1 || band > ds.bandCount() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "band out of range" );

    if ( wavelengthUm <= 0.0 )
    {
        if ( const char *declared = GDALGetMetadataItem(
                 static_cast<GDALDatasetH>( ds.dataset() ), "SICNU_SAR_WAVELENGTH_UM", nullptr ) )
            wavelengthUm = QString::fromUtf8( declared ).toDouble();
    }
    if ( !( wavelengthUm > 0.0 ) )
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "wavelength missing: pass wavelengthUm (micrometres, e.g. 56235 for "
            "Sentinel-1) or declare SICNU_SAR_WAVELENGTH_UM on the input" );
    const double wavelengthM = wavelengthUm * 1e-6;

    GdalBlockStream stream( ds, band, kTileDim, kTileDim, /*halo=*/1 );
    GdalStreamingOutput out( QString::fromStdString( outputPath ), ds.width(), ds.height(), 1,
                             GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "los_displacement_m" );
    out.setMetadataItem( "SICNU_SAR_WAVELENGTH_UM", std::to_string( wavelengthUm ).c_str() );
    out.setBandNoDataValue( 1, kNaN );

    std::vector<float> outTile( static_cast<size_t>( kTileDim ) * kTileDim );
    long validPixels = 0;
    long jumpPairs = 0;
    long totalPairs = 0;
    long tileIndex = 0;
    const long totalTiles = stream.tileCount();

    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile,
                                         const float *pixels ) {
        context.throwIfCancelled();
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const float phi =
                    pixels[static_cast<size_t>( y + tile.halo ) * tile.bufferWidth + x + tile.halo];
                const double phiRad = static_cast<double>( phi );
                const double d = sicnu::sar::losDisplacementM( phiRad, wavelengthM );
                outTile[static_cast<size_t>( y ) * tile.width + x] =
                    static_cast<float>( d );
                if ( std::isfinite( d ) )
                    ++validPixels;

                // Itoh pairs: right and down neighbors. Both members must
                // live in the CORE tile (the halo's edge-replicated fringe
                // would pair a pixel with itself at raster borders).
                const size_t cIdx =
                    static_cast<size_t>( y + tile.halo ) * tile.bufferWidth + x + tile.halo;
                if ( std::isfinite( pixels[cIdx] ) )
                {
                    if ( x + 1 < tile.width )
                    {
                        const float right = pixels[cIdx + 1];
                        if ( std::isfinite( right ) )
                        {
                            ++totalPairs;
                            if ( std::abs( pixels[cIdx] - right ) > static_cast<float>( M_PI ) )
                                ++jumpPairs;
                        }
                    }
                    if ( y + 1 < tile.height )
                    {
                        const float down = pixels[cIdx + tile.bufferWidth];
                        if ( std::isfinite( down ) )
                        {
                            ++totalPairs;
                            if ( std::abs( pixels[cIdx] - down ) > static_cast<float>( M_PI ) )
                                ++jumpPairs;
                        }
                    }
                }
            }
        }

        ++tileIndex;
        context.reportProgress( 0.95 * tileIndex / totalTiles, "Displacement" );
        return out.writeTile( 1,
                              GdalBlockStream::Tile{ tile.xOffset, tile.yOffset, tile.width,
                                                     tile.height, 0, tile.width, tile.height,
                                                     tile.index, tile.totalTiles, 0, 0 },
                              outTile.data() );
    } );

    if ( !ok || !out.closeWithError() )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to stream/compute the displacement" );
    }

    const double discontinuityRatio =
        totalPairs > 0 ? static_cast<double>( jumpPairs ) / static_cast<double>( totalPairs )
                       : kNaN;

    Json::Value result;
    result["output"] = outputPath;
    result["wavelengthUm"] = wavelengthUm;
    result["signConvention"] = "positive d_los = motion toward the sensor";
    result["validPixels"] = Json::Value::Int64( validPixels );
    result["phaseDiscontinuityRatio"] = discontinuityRatio;
    result["wrappedSuspicionWarning"] = discontinuityRatio > warnThreshold;
    context.reportProgress( 1.0, "Displacement complete" );
    return result;
}

} // namespace sicnu::operators::rs
