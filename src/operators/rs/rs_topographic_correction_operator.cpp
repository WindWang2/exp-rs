/***************************************************************************
 * rs_topographic_correction_operator.cpp — Milestone B.1
 *
 * Two-pass streaming topographic correction:
 *   pass 1 — per tile: Horn gradients from the DEM halo window, illumination
 *            cosine per pixel, per-band OLS / Minnaert accumulation;
 *   pass 2 — per tile: recompute the illumination, apply the fitted
 *            correction, write band tiles to the streaming output.
 * The DEM is read twice (once per pass); no full-raster buffer is resident.
 * Single-threaded fixed-order accumulation (ADR 0124 bit-exact grade).
 ***************************************************************************/
#include "rs_topographic_correction_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/algorithms/topographic_correction.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput
#include "data/raster_grid_compat.h"

#include <QString>

#include <gdal.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using TopographicCorrection::Method;

namespace {

const std::vector<std::string> s_methods = { "cosine", "c_correction", "minnaert" };

constexpr int kTileDim = 256;
constexpr int kDemHalo = 1; // Horn 3×3 kernels need a one-pixel halo
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

/// Reads one band window with edge replication out to a @a halo margin (the
/// platform's streaming edge policy) and sentinel/non-finite → NaN
/// normalization. @a buf must hold (@a width + 2·@a halo) × (@a height +
/// 2·@a halo) floats, core at (halo, halo). Windows reaching past the raster
/// edge are read clamped and the missing ring is replicate-filled.
void readHaloWindow( GdalDatasetWrapper &ds, int band, int xOff, int yOff,
                     int width, int height, int halo, float *buf )
{
    const int bw = width + 2 * halo;
    const int bh = height + 2 * halo;
    const size_t bufN = static_cast<size_t>( bw ) * bh;

    // 1) Read the clamped core into the buffer's core rect (row-temp because
    // readBandWindow writes contiguously while the core rows sit at stride bw).
    const int cx0 = std::max( 0, xOff );
    const int cy0 = std::max( 0, yOff );
    const int cx1 = std::min( ds.width(), xOff + width );   // exclusive
    const int cy1 = std::min( ds.height(), yOff + height ); // exclusive
    const int coreW = cx1 - cx0;
    const int coreH = cy1 - cy0;
    float *core = buf + static_cast<size_t>( halo ) * bw + halo;
    if ( coreW > 0 && coreH > 0 )
    {
        std::vector<float> tmp( static_cast<size_t>( coreW ) * coreH );
        if ( !ds.readBandWindow( band, cx0, cy0, coreW, coreH, tmp.data() ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to read band " + std::to_string( band ) +
                                       " window at (" + std::to_string( cx0 ) + ", " +
                                       std::to_string( cy0 ) + ")" );
        for ( int y = 0; y < coreH; ++y )
            std::copy_n( tmp.data() + static_cast<size_t>( y ) * coreW, coreW,
                         core + static_cast<size_t>( y ) * bw );
    }

    // 2) Vertical replication: rings above/below the core copy the nearest
    // core row (coreH >= 1 for any in-raster tile).
    const int firstCoreRow = halo;
    const int lastCoreRow = halo + coreH - 1;
    for ( int y = 0; y < halo; ++y )
        std::copy_n( buf + static_cast<size_t>( firstCoreRow ) * bw, bw,
                     buf + static_cast<size_t>( y ) * bw );
    for ( int y = lastCoreRow + 1; y < bh; ++y )
        std::copy_n( buf + static_cast<size_t>( lastCoreRow ) * bw, bw,
                     buf + static_cast<size_t>( y ) * bw );

    // 3) Horizontal replication per row: left ring copies the first core
    // column, right ring the last one.
    for ( int y = 0; y < bh; ++y )
    {
        float *row = buf + static_cast<size_t>( y ) * bw;
        const float left = row[halo];
        const float right = row[halo + width - 1];
        for ( int x = 0; x < halo; ++x )
            row[x] = left;
        for ( int x = halo + width; x < bw; ++x )
            row[x] = right;
    }

    // 4) Sentinel / non-finite → NaN over the whole window.
    bool hasNodataFlag = false;
    const double nodata = ds.bandNoDataValue( band, &hasNodataFlag );
    if ( hasNodataFlag && std::isfinite( nodata ) )
    {
        const float sentinel = static_cast<float>( nodata );
        for ( size_t i = 0; i < bufN; ++i )
            if ( buf[i] == sentinel )
                buf[i] = kNaN;
    }
}

/// Per-axis pixel size in METRES for the DEM gradients — the same WGS84
/// arc-length conversion for geographic DEMs the terrain operator applies
/// (#612). Consolidation candidate tracked for Milestone F.1.
void cellSizesMetres( const GdalDatasetWrapper &ds, double *csx, double *csy )
{
    const std::array<double, 6> gt = ds.geoTransform();
    double x = std::abs( gt[1] );
    double y = std::abs( gt[5] );
    if ( x <= 1e-7 )
        x = 30.0;
    if ( y <= 1e-7 )
        y = x;

    const QString wkt = ds.projection();
    if ( !wkt.isEmpty() )
    {
        OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
        if ( srs )
        {
            const QByteArray wktBytes = wkt.toUtf8();
            char *wktPtr = const_cast<char *>( wktBytes.constData() );
            if ( OSRImportFromWkt( srs, &wktPtr ) == OGRERR_NONE && OSRIsGeographic( srs ) )
            {
                const double phiDeg = gt[3] + ( ds.height() / 2.0 ) * gt[5];
                const double phiRad = phiDeg * M_PI / 180.0;
                const double cosPhi = std::cos( phiRad );
                const double mPerDegLat =
                    111132.92 - 559.82 * std::cos( 2 * phiRad ) + 1.175 * std::cos( 4 * phiRad );
                const double mPerDegLon = 111412.84 * cosPhi - 93.5 * std::cos( 3 * phiRad );
                x = std::abs( gt[1] ) * mPerDegLon;
                y = ( std::abs( gt[5] ) > 1e-7 ? std::abs( gt[5] ) : std::abs( gt[1] ) ) * mPerDegLat;
            }
            OSRDestroySpatialReference( srs );
        }
    }
    *csx = x;
    *csy = y;
}

} // anonymous namespace

Json::Value RsTopographicCorrectionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Reflectance raster (multi-band) to correct");
    props["dem"] = makeRasterParam("dem", "DEM raster on the same grid as the input (blocking grid-compatibility check)");
    props["output"] = makeOutputParam("output", "Output corrected reflectance raster", "tif");
    props["method"] = makeEnumParam("method", "Illumination correction model", s_methods, "c_correction");
    props["solar_zenith"] = makeNumberParam("solar_zenith", "Solar zenith angle in degrees from vertical [0, 90)", 30.0);
    props["solar_azimuth"] = makeNumberParam("solar_azimuth", "Solar azimuth in degrees clockwise from north [0, 360)", 150.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied correction method", "");
    outputs["bandCount"] = makeIntegerParam("bandCount", "Number of corrected bands", 0);
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "dem", "output", "method", "solar_zenith", "solar_azimuth"});
    return root;
}

Json::Value RsTopographicCorrectionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("optical");
    meta["tags"].append("topographic");
    meta["tags"].append("c-correction");
    meta["tags"].append("minnaert");
    meta["task"] = "radiometric-normalization";
    meta["notes"] = "Two-pass streaming correction (regression + apply); fits are "
                    "full-scene per band, accumulated in double; bit-exact grade "
                    "(single-threaded, fixed order).";
    meta["gpu"] = false;
    meta["purpose"] = "Remove slope/aspect-driven illumination variation from optical reflectance after atmospheric correction.";
    meta["prerequisites"].append("DEM must be on the same grid as the input (CRS, geotransform, size) — mismatched grids are refused, never resampled.");
    meta["prerequisites"].append("Solar zenith/azimuth of the scene must be supplied explicitly (import metadata does not carry them yet).");
    meta["workflowHints"].append("Apply after atmospheric correction, before spectral indices on rugged terrain.");
    meta["limitations"].append("Self-shadowed pixels (cos_i <= 0, or cos_i + c <= 0 for the C model) and Minnaert-domain violations become NaN NoData.");
    meta["limitations"].append("The empirical C factor requires a usable radiance~illumination regression (|b| >= 1e-6, >= 2 valid pairs); otherwise the operator refuses with a typed error instead of passing data through.");
    meta["limitations"].append("c_correction is also the SCS+C form (Soenen et al. 2005) when c is estimated from the same radiance~illumination regression, as done here.");
    return meta;
}

Json::Value RsTopographicCorrectionOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    // Refl BIP tile (1 band) + DEM halo tile + cosi core + out tile, floats.
    est["estimatedRamBytes"] = Json::Value::UInt64(
        5ULL * kTileDim * kTileDim * sizeof( float ) );
    return est;
}

Json::Value RsTopographicCorrectionOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) )
             && probe.bandCount() > 0 )
        {
            const std::uint64_t bands = static_cast<std::uint64_t>( probe.bandCount() );
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { 256ULL, 256ULL, bands + 4ULL, static_cast<std::uint64_t>( sizeof( float ) ) } );
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

Json::Value RsTopographicCorrectionOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string demPath = requireString( params, "dem" );
    const std::string outputPath = requireString( params, "output" );
    const std::string methodToken = getEnum( params, "method", s_methods, "c_correction" );
    Method method = Method::CCorrection;
    if ( !TopographicCorrection::parseMethod( methodToken.c_str(), &method ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Unknown method: " + methodToken );

    const double solarZenith = getDouble( params, "solar_zenith", 30.0 );
    const double solarAzimuth = getDouble( params, "solar_azimuth", 150.0 );
    if ( !( solarZenith >= 0.0 && solarZenith < 90.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "solar_zenith must be in [0, 90) degrees, got " + std::to_string( solarZenith ) );
    if ( !( solarAzimuth >= 0.0 && solarAzimuth < 360.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "solar_azimuth must be in [0, 360) degrees, got " + std::to_string( solarAzimuth ) );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    GdalDatasetWrapper demDs;
    if ( !demDs.open( QString::fromStdString( demPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open DEM raster: " + demPath );

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if ( width <= 0 || height <= 0 || bandCount <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );
    if ( demDs.width() < 1 || demDs.height() < 1 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "DEM raster is empty: " + demPath );

    // Grid contract: DEM and image must share the grid — typed refusal, no
    // hidden warp (docs/processing/grid-and-radiometric-policy.md §1).
    const sicnu::data::GridCompatReport gridReport = sicnu::data::compareGrids(
        sicnu::processing::gridFromDataset( ds ), sicnu::processing::gridFromDataset( demDs ) );
    if ( !gridReport.compatible() )
    {
        std::string message = "DEMs and imagery must share a grid for topographic correction";
        if ( const std::optional<sicnu::data::GridCompatIssue> primary = gridReport.primaryBlocking() )
            message += ": " + primary->message.toStdString();
        throw RSOperatorError( ErrorCode::InvalidInputData, message );
    }

    double csx = 0.0;
    double csy = 0.0;
    cellSizesMetres( demDs, &csx, &csy );
    context.logInfo( "Topographic correction (" + methodToken + ") over " + demPath +
                     " cell " + std::to_string( csx ) + "x" + std::to_string( csy ) + " m" );

    // Per-band NoData sentinels resolved once.
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

    const size_t tilePixels = static_cast<size_t>( kTileDim ) * kTileDim;
    std::vector<float> demHalo( static_cast<size_t>( kTileDim + 2 * kDemHalo ) *
                                ( kTileDim + 2 * kDemHalo ) );
    std::vector<float> cosi( tilePixels );
    std::vector<TopographicCorrection::OlsRegression> ols( bandCount );
    std::vector<TopographicCorrection::MinnaertRegression> minnaert( bandCount );

    // Illumination cosine for every pixel of one tile; NaN where the DEM
    // neighbourhood is invalid.
    const auto computeIllumination = [&]( const GdalBlockStream::Tile &tile ) {
        const int bw = tile.width + 2 * kDemHalo;
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const int bx = x + kDemHalo;
                const int by = y + kDemHalo;
                float k9[9];
                bool anyNaN = false;
                for ( int dy = -1; dy <= 1 && !anyNaN; ++dy )
                    for ( int dx = -1; dx <= 1; ++dx )
                    {
                        const float v = demHalo[static_cast<size_t>( by + dy ) * bw + bx + dx];
                        if ( !std::isfinite( v ) )
                        {
                            anyNaN = true;
                            break;
                        }
                        k9[( dy + 1 ) * 3 + ( dx + 1 )] = v;
                    }
                const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                if ( anyNaN )
                {
                    cosi[idx] = kNaN;
                    continue;
                }
                double dzdx = 0.0;
                double dzdy = 0.0;
                TopographicCorrection::hornGradient( k9, csx, csy, &dzdx, &dzdy );
                double slopeDeg = 0.0;
                double aspectDeg = -1.0;
                TopographicCorrection::slopeAspectDeg( dzdx, dzdy, &slopeDeg, &aspectDeg );
                cosi[idx] = static_cast<float>( TopographicCorrection::illuminationCosine(
                    slopeDeg, aspectDeg, solarZenith, solarAzimuth ) );
            }
        }
    };

    const int totalTiles = reflStream.tileCount();

    // ---- Pass 1: illumination + per-band regression accumulation -----------
    context.reportProgress( 0.0, "Accumulating illumination regression" );
    {
        int tileIndex = 0;
        const bool ok = reflStream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *bip ) {
            context.throwIfCancelled();
            readHaloWindow( demDs, 1, tile.xOffset, tile.yOffset,
                            tile.width, tile.height, kDemHalo, demHalo.data() );
            computeIllumination( tile );
            for ( int b = 0; b < bandCount; ++b )
            {
                const float sentinel = sentinels[b];
                for ( int y = 0; y < tile.height; ++y )
                {
                    for ( int x = 0; x < tile.width; ++x )
                    {
                        const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                        float v = bip[idx * bandCount + b];
                        if ( hasSentinel[b] && v == sentinel )
                            v = kNaN;
                        const float ci = cosi[idx];
                        if ( std::isfinite( v ) && std::isfinite( ci ) )
                        {
                            ols[b].add( ci, v );
                            minnaert[b].add( ci, v );
                        }
                    }
                }
            }
            ++tileIndex;
            context.reportProgress( 0.45 * tileIndex / totalTiles,
                                    "Accumulating illumination regression" );
            return true;
        } );
        if ( !ok )
            throw RSOperatorError( ErrorCode::GdalError, "Failed to stream input tiles" );
    }

    // ---- Fit ----------------------------------------------------------------
    std::vector<TopographicCorrection::BandFit> fits( bandCount );
    for ( int b = 0; b < bandCount; ++b )
        fits[b] = TopographicCorrection::fitBand( method, solarZenith, ols[b], minnaert[b] );
    if ( method == Method::CCorrection || method == Method::Minnaert )
    {
        std::string unusable;
        for ( int b = 0; b < bandCount; ++b )
            if ( !fits[b].usable )
                unusable += ( unusable.empty() ? "" : ", " ) + std::to_string( b + 1 );
        if ( !unusable.empty() )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                method == Method::CCorrection
                    ? "C-correction regression degenerate (|b| < 1e-6 or < 2 valid illumination pairs) for band(s): "
                    : "Minnaert log-log fit degenerate (< 2 valid (cos_i > 0, L > 0) pairs or non-positive exponent) for band(s): "
                          + unusable + ". The scene carries no usable illumination signal for this model; "
                                       "choose 'cosine', or use a larger scene." );
    }

    // ---- Pass 2: apply + stream out ------------------------------------------
    GdalStreamingOutput output( QString::fromStdString( outputPath ), width, height, bandCount,
                                GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !output.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    output.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );

    std::vector<float> outTile( tilePixels );
    {
        int tileIndex = 0;
        const bool ok = reflStream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *bip ) {
            context.throwIfCancelled();
            readHaloWindow( demDs, 1, tile.xOffset, tile.yOffset,
                            tile.width, tile.height, kDemHalo, demHalo.data() );
            computeIllumination( tile );
            for ( int b = 0; b < bandCount; ++b )
            {
                const float sentinel = sentinels[b];
                for ( int y = 0; y < tile.height; ++y )
                {
                    for ( int x = 0; x < tile.width; ++x )
                    {
                        const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                        float v = bip[idx * bandCount + b];
                        if ( hasSentinel[b] && v == sentinel )
                            v = kNaN;
                        outTile[idx] = TopographicCorrection::correctPixel( method, v, cosi[idx], fits[b] );
                    }
                }
                if ( !output.writeTile( b + 1, tile, outTile.data() ) )
                    return false;
            }
            ++tileIndex;
            context.reportProgress( 0.45 + 0.5 * tileIndex / totalTiles, "Applying correction" );
            return true;
        } );
        if ( !ok )
        {
            output.abandon();
            throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/apply tiles" );
        }
    }

    // Provenance metadata: keep the radiometric domain truthful (the
    // correction multiplies reflectance; the domain stays reflectance).
    if ( void *handle = ds.dataset() )
    {
        if ( const char *state = GDALGetMetadataItem( static_cast<GDALDatasetH>( handle ),
                                                      SatelliteProducts::kRadiometricStateKey, nullptr ) )
            output.setMetadataItem( QLatin1String( SatelliteProducts::kRadiometricStateKey ),
                                    QString::fromUtf8( state ) );
    }
    output.setMetadataItem( QLatin1String( "SICNU_TOPO_CORRECTION_METHOD" ), QString::fromStdString( methodToken ) );
    output.setMetadataItem( QLatin1String( "SICNU_TOPO_SUN_ZENITH" ), QString::number( solarZenith ) );
    output.setMetadataItem( QLatin1String( "SICNU_TOPO_SUN_AZIMUTH" ), QString::number( solarAzimuth ) );

    QString closeError;
    if ( !output.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["method"] = methodToken;
    result["bandCount"] = bandCount;
    result["width"] = width;
    result["height"] = height;
    Json::Value fitsJson( Json::objectValue );
    for ( int b = 0; b < bandCount; ++b )
    {
        Json::Value fj( Json::objectValue );
        fj["usable"] = fits[b].usable;
        fj["a"] = fits[b].a;
        fj["b"] = fits[b].b;
        fj["c"] = fits[b].c;
        fj["k"] = fits[b].k;
        fj["olsCount"] = static_cast<Json::UInt64>( ols[b].count() );
        fj["minnaertCount"] = static_cast<Json::UInt64>( minnaert[b].count() );
        fitsJson["band_" + std::to_string( b + 1 )] = fj;
    }
    result["fit"] = fitsJson;
    context.reportProgress( 1.0, "Topographic correction complete" );
    return result;
}

} // namespace sicnu::operators::rs
