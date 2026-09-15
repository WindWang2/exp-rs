/***************************************************************************
 * rs_brdf_normalization_operator.cpp — see the header for the contract.
 * Single-pass streaming: read reflectance tile → per-pixel kernel
 * normalization → write tile. Weights and angles are resolved once before
 * streaming; NaN pixels pass through; single-threaded fixed order
 * (ADR 0124 bit-exact grade).
 ***************************************************************************/
#include "rs_brdf_normalization_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/brdf_normalization.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/algorithms/solar_geometry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <gdal.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using BrdfNormalization::normalizeKernelDriven;

namespace {
constexpr int kTileDim = 256;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

/// Stamped/required angle metadata keys (rs:solar_geometry stamps the sun
/// keys; view keys come from the sensor provider metadata).
constexpr const char *kSunZenithKey = "SICNU_SUN_ZENITH";
constexpr const char *kSunAzimuthKey = "SICNU_SUN_AZIMUTH";
constexpr const char *kViewZenithKey = "SICNU_VIEW_ZENITH";
constexpr const char *kViewAzimuthKey = "SICNU_VIEW_AZIMUTH";

bool readAngleMetadata( GdalDatasetWrapper &ds, const char *key, double *outDeg )
{
    if ( void *handle = ds.dataset() )
        if ( const char *value = GDALGetMetadataItem( static_cast<GDALDatasetH>( handle ),
                                                      key, nullptr ) )
        {
            bool ok = false;
            const double v = QString::fromUtf8( value ).toDouble( &ok );
            if ( ok && std::isfinite( v ) )
            {
                *outDeg = v;
                return true;
            }
        }
    return false;
}

/// Per-band weight parameter: a number applies to every band; an array must
/// match the band count. Returns false with a typed message otherwise.
bool resolveWeights( const Json::Value &params, const std::string &key, int bandCount,
                     std::vector<double> *out, QString *errorMessage )
{
    const Json::Value &v = params[key];
    if ( v.isNumeric() )
    {
        out->assign( bandCount, v.asDouble() );
        return true;
    }
    if ( v.isArray() && static_cast<int>( v.size() ) == bandCount )
    {
        out->clear();
        for ( const Json::Value &entry : v )
        {
            if ( !entry.isNumeric() )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "%1: array entries must be numbers" )
                                        .arg( QString::fromStdString( key ) );
                return false;
            }
            out->push_back( entry.asDouble() );
        }
        return true;
    }
    if ( errorMessage )
        *errorMessage = QStringLiteral( "%1 must be a number or an array of %2 per-band weights" )
                            .arg( QString::fromStdString( key ) )
                            .arg( bandCount );
    return false;
}
} // namespace

Json::Value RsBrdfNormalizationOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Reflectance raster to normalize" );
    props["output"] = makeOutputParam( "output", "Normalized reflectance raster", "tif" );
    props["sun_zenith"] = makeNumberParam( "sun_zenith", "Sun zenith in degrees [0, 90); defaults to the SICNU_SUN_ZENITH raster metadata", -1.0 );
    props["sun_azimuth"] = makeNumberParam( "sun_azimuth", "Sun azimuth in degrees from north; defaults to the SICNU_SUN_AZIMUTH metadata", -1.0 );
    props["view_zenith"] = makeNumberParam( "view_zenith", "Sensor view zenith in degrees [0, 90); defaults to the SICNU_VIEW_ZENITH metadata", -1.0 );
    props["view_azimuth"] = makeNumberParam( "view_azimuth", "Sensor view azimuth in degrees from north; defaults to the SICNU_VIEW_AZIMUTH metadata", -1.0 );
    props["f_vol"] = makeNumberParam( "f_vol", "Volumetric kernel weight (scalar applied to all bands, or one per band)", 0.0 );
    props["f_geo"] = makeNumberParam( "f_geo", "Geometric kernel weight (scalar applied to all bands, or one per band)", 0.0 );
    props["ref_view_zenith"] = makeNumberParam( "ref_view_zenith", "Reference view zenith (nadir default)", 0.0 );
    props["ref_relative_azimuth"] = makeNumberParam( "ref_relative_azimuth", "Reference relative (sun−view) azimuth (0 = sun behind sensor)", 0.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path", false );
    outputs["bandCount"] = makeIntegerParam( "bandCount", "Number of normalized bands", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "f_vol", "f_geo" } );
    return root;
}

Json::Value RsBrdfNormalizationOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "optical" );
    meta["tags"].append( "brdf" );
    meta["tags"].append( "view-angle" );
    meta["task"] = "radiometric-normalization";
    meta["notes"] = "Ross-Thick + Li-Sparse-Reciprocal (Lucht et al. 2000); single-pass "
                    "streaming; bit-exact grade (single-threaded, fixed order). Kernel weights "
                    "are band-specific physical quantities — derive them from multi-angle "
                    "observations or published per-biome values; do not guess.";
    meta["gpu"] = false;
    meta["purpose"] = "Make acquisitions from different view geometries radiometrically "
                      "comparable before change detection / compositing.";
    meta["prerequisites"].append( "Sun and view angles via parameters or SICNU_SUN_* / "
                                  "SICNU_VIEW_* dataset metadata — a missing angle is a typed "
                                  "refusal (use rs:solar_geometry to stamp sun angles)." );
    meta["prerequisites"].append( "Per-band kernel weights (f_vol, f_geo); the normalization "
                                  "denominator must stay positive." );
    meta["workflowHints"].append( "Apply on surface reflectance before multi-date stacking; "
                                  "the output keeps the input radiometric state." );
    meta["limitations"].append( "Single-scene weights cannot be fitted from the scene itself; "
                                "for angle-less two-date leveling use the "
                                "BrdfNormalization::PairStatistics::fitCFactor API "
                                "(mean-preserving c = mean(ref)/mean(target))." );
    meta["limitations"].append( "Non-finite pixels pass through as NaN NoData." );
    return meta;
}

Json::Value RsBrdfNormalizationOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    // BIP tile (bandCount×) + out tile — conservative nominal 4-band stack.
    est["estimatedRamBytes"] = Json::Value::UInt64(
        5ULL * kTileDim * kTileDim * sizeof( float ) );
    return est;
}

Json::Value RsBrdfNormalizationOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) )
             && probe.bandCount() > 0 )
        {
            const std::uint64_t bands = static_cast<std::uint64_t>( probe.bandCount() );
            // BIP read tile scales with bandCount; the per-band out tile does not.
            const std::uint64_t ram = ( bands + 1ULL ) * kTileDim * kTileDim * sizeof( float );
            Json::Value est( Json::objectValue );
            est["tileWidth"] = Json::Value::UInt64( kTileDim );
            est["tileHeight"] = Json::Value::UInt64( kTileDim );
            est["estimatedRamBytes"] = Json::Value::UInt64( ram );
            return est;
        }
    }
    return executionEstimate();
}

Json::Value RsBrdfNormalizationOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if ( width <= 0 || height <= 0 || bandCount <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );

    // ---- Angle resolution: parameters first, SICNU_* metadata fallback ----
    // A missing angle is a typed refusal (GOAL Oracle 2), never a default.
    const auto resolveAngle = [&]( const std::string &key, const char *metaKey ) -> double {
        if ( params.isMember( key ) && params[key].isNumeric() )
            return params[key].asDouble(); // provided: validators range-check it
        double v = -1.0;
        if ( readAngleMetadata( ds, metaKey, &v ) )
            return v;
        QString message = QStringLiteral( "%1: missing sun/view angle '%2' (pass it as a "
                                          "parameter or stamp '%3' metadata, e.g. via "
                                          "rs:solar_geometry)" )
                              .arg( QString::fromStdString( name() ),
                                    QString::fromStdString( key ), QString::fromUtf8( metaKey ) );
        throw RSOperatorError( ErrorCode::MissingRequiredParameter, message.toStdString() );
    };

    const double sunZenith = resolveAngle( "sun_zenith", kSunZenithKey );
    const double sunAzimuth = resolveAngle( "sun_azimuth", kSunAzimuthKey );
    const double viewZenith = resolveAngle( "view_zenith", kViewZenithKey );
    const double viewAzimuth = resolveAngle( "view_azimuth", kViewAzimuthKey );

    QString error;
    if ( !SolarGeometry::validateSunGeometryDeg( 90.0 - sunZenith, sunAzimuth, &error ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, error.toStdString() );
    if ( !SolarGeometry::validateViewGeometryDeg( viewZenith, viewAzimuth, &error ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, error.toStdString() );

    const double refViewZenith = getDouble( params, "ref_view_zenith", 0.0 );
    const double refRelativeAzimuth = getDouble( params, "ref_relative_azimuth", 0.0 );

    // ---- Per-band weights ------------------------------------------------
    std::vector<double> fVol, fGeo;
    if ( !resolveWeights( params, "f_vol", bandCount, &fVol, &error ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, error.toStdString() );
    if ( !resolveWeights( params, "f_geo", bandCount, &fGeo, &error ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, error.toStdString() );

    // Validate every band's geometry/weights up-front so a bad band refuses
    // before any output is written (the factors are evaluated once more below,
    // hoisted out of the pixel loop).
    const double relativeAzimuth = sunAzimuth - viewAzimuth;
    for ( int b = 0; b < bandCount; ++b )
    {
        double f = 0.0;
        if ( !BrdfNormalization::anisotropyFactor( sunZenith, viewZenith, relativeAzimuth,
                                                   fVol[b], fGeo[b], &f, &error ) )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   ( QStringLiteral( "band %1: " ).arg( b + 1 )
                                     + error ).toStdString() );
        if ( !BrdfNormalization::anisotropyFactor( sunZenith, refViewZenith,
                                                   refRelativeAzimuth, fVol[b], fGeo[b], &f,
                                                   &error ) )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   ( QStringLiteral( "band %1 reference geometry: " ).arg( b + 1 )
                                     + error ).toStdString() );
    }

    context.logInfo( "BRDF normalization: sun (" + std::to_string( sunZenith ) + "°, "
                     + std::to_string( sunAzimuth ) + "°) view (" + std::to_string( viewZenith )
                     + "°, " + std::to_string( viewAzimuth ) + "°) → ref view "
                     + std::to_string( refViewZenith ) + "°" );

    // ---- Single-pass streaming normalize ----------------------------------
    // Per-band NoData sentinels resolved once and mapped to NaN before the
    // kernel (house streaming policy): multiplying a finite sentinel would
    // destroy the exact value downstream nodata detection relies on.
    std::vector<float> sentinels( bandCount, 0.0f );
    std::vector<char> hasSentinel( bandCount, 0 );
    for ( int b = 0; b < bandCount; ++b )
    {
        bool hasSentinelFlag = false;
        const double nodata = ds.bandNoDataValue( b + 1, &hasSentinelFlag );
        if ( hasSentinelFlag && std::isfinite( nodata ) )
        {
            sentinels[b] = static_cast<float>( nodata );
            hasSentinel[b] = 1;
        }
    }

    GdalMultibandBlockStream reflStream( ds, bandCount, kTileDim, kTileDim );
    GdalStreamingOutput output( QString::fromStdString( outputPath ), width, height, bandCount,
                                GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !output.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    output.setNoDataValue( static_cast<double>( kNaN ) );

    std::vector<float> outTile( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<Json::UInt64> corrected( bandCount, 0 ), nonFinite( bandCount, 0 );
    const size_t tilePixels = static_cast<size_t>( kTileDim ) * kTileDim;
    const int totalTiles = reflStream.tileCount();
    int tileIndex = 0;
    // Scene-constant geometry: the anisotropy factors are identical for every
    // pixel of a band, so the normalization reduces to one multiply by
    // fRef/fObs per pixel (hoisted out of the loop; bit-identical output).
    std::vector<double> ratio( bandCount, 0.0 );
    for ( int b = 0; b < bandCount; ++b )
    {
        double fObs = 0.0, fRef = 0.0;
        if ( !BrdfNormalization::anisotropyFactor( sunZenith, viewZenith, relativeAzimuth,
                                                   fVol[b], fGeo[b], &fObs )
             || !BrdfNormalization::anisotropyFactor( sunZenith, refViewZenith,
                                                      refRelativeAzimuth, fVol[b], fGeo[b],
                                                      &fRef ) )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "anisotropy factor evaluation failed for band "
                                       + std::to_string( b + 1 ) );
        ratio[b] = fRef / fObs;
    }
    const bool ok = reflStream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *bip ) {
        context.throwIfCancelled();
        for ( int b = 0; b < bandCount; ++b )
        {
            const float sentinel = sentinels[b];
            const double gain = ratio[b];
            for ( int y = 0; y < tile.height; ++y )
                for ( int x = 0; x < tile.width; ++x )
                {
                    const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                    float v = bip[idx * bandCount + b];
                    if ( hasSentinel[b] && v == sentinel )
                        v = kNaN;
                    // rho_ref = rho_obs · f(G_ref)/f(G_obs); NaN in → NaN out.
                    outTile[idx] = std::isfinite( v )
                                       ? static_cast<float>( v * gain )
                                       : kNaN;
                    if ( std::isfinite( v ) )
                        ++corrected[b];
                    else
                        ++nonFinite[b];
                }
            if ( !output.writeTile( b + 1, tile, outTile.data() ) )
                return false;
        }
        ++tileIndex;
        context.reportProgress( static_cast<double>( tileIndex ) / totalTiles,
                                "Normalizing view geometry" );
        return true;
    } );
    if ( !ok )
    {
        output.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/normalize tiles" );
    }

    // ---- State-preserving provenance metadata ------------------------------
    if ( void *handle = ds.dataset() )
    {
        if ( const char *state = GDALGetMetadataItem( static_cast<GDALDatasetH>( handle ),
                                                      SatelliteProducts::kRadiometricStateKey,
                                                      nullptr ) )
            output.setMetadataItem( QLatin1String( SatelliteProducts::kRadiometricStateKey ),
                                    QString::fromUtf8( state ) );
    }
    output.setMetadataItem( QLatin1String( "SICNU_BRDF_SUN_ZENITH" ), QString::number( sunZenith, 'g', 10 ) );
    output.setMetadataItem( QLatin1String( "SICNU_BRDF_SUN_AZIMUTH" ), QString::number( sunAzimuth, 'g', 10 ) );
    output.setMetadataItem( QLatin1String( "SICNU_BRDF_VIEW_ZENITH" ), QString::number( viewZenith, 'g', 10 ) );
    output.setMetadataItem( QLatin1String( "SICNU_BRDF_VIEW_AZIMUTH" ), QString::number( viewAzimuth, 'g', 10 ) );
    output.setMetadataItem( QLatin1String( "SICNU_BRDF_REF_VIEW_ZENITH" ), QString::number( refViewZenith, 'g', 10 ) );

    QString closeError;
    if ( !output.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["bandCount"] = bandCount;
    result["width"] = width;
    result["height"] = height;
    result["sun_zenith"] = sunZenith;
    result["sun_azimuth"] = sunAzimuth;
    result["view_zenith"] = viewZenith;
    result["view_azimuth"] = viewAzimuth;
    result["relative_azimuth"] = relativeAzimuth;
    result["ref_view_zenith"] = refViewZenith;
    Json::Value bands( Json::objectValue );
    for ( int b = 0; b < bandCount; ++b )
    {
        Json::Value bj( Json::objectValue );
        bj["f_vol"] = fVol[b];
        bj["f_geo"] = fGeo[b];
        bj["corrected_pixels"] = corrected[b];
        bj["nonfinite_pixels"] = nonFinite[b];
        bands["band_" + std::to_string( b + 1 )] = bj;
    }
    result["bands"] = bands;
    context.reportProgress( 1.0, "BRDF normalization complete" );
    return result;
}

} // namespace sicnu::operators::rs
