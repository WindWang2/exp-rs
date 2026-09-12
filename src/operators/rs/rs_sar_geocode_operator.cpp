/***************************************************************************
 * rs_sar_geocode_operator.cpp — forward Range-Doppler geocoding onto the
 * DEM map grid (Scientific Processing 8.0, capability package A).
 *
 * Streams the DEM tile-by-tile (Horn gradients from the shared kernels,
 * pixel centers geolocated through the declared orbit contract), samples the
 * SAR radiometry through bounded source windows, and writes every requested
 * product band tile-by-tile — O(tile + bounded window) memory throughout.
 ***************************************************************************/
#include "rs_sar_geocode_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_geocoding.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_cell_geometry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput
#include "processing/algorithms/topographic_correction.h" // hornGradient (shared Horn kernels)

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "geospatial/crs/crs_policy.h"

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_products = { "backscatter", "gamma0", "incidence",
                                              "local_incidence", "layover_shadow" };
const std::vector<std::string> s_resampling = { "bilinear", "nearest" };

constexpr int kTileDim = 256;
constexpr int kHalo = 1;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
// Fixed product band order (also declared as SICNU_SAR_GEOCODE_BANDS).
constexpr int kProductCount = 5;
// Source-window budget: a tile radiometry read larger than this is NOT
// materialized; those tiles fall through to per-pixel 2x2 reads so a
// diagonal pass can never allocate a whole-SAR-image window.
constexpr size_t kMaxWindowFloats = 4u * 1024u * 1024u;

// Same WGS84 degrees->metres conversion the terrain/topographic operators
// apply (#612) — single owner: processing/gdal/gdal_cell_geometry.
using sicnu::processing::gdal_util::cellSizesMetres;

/// Sentinel -> NaN normalization over the stream's borrowed halo buffer.
void normalizeHalo( const float *haloBuf, size_t bufN, bool hasSentinel, float sentinel,
                    std::vector<float> *scratch )
{
    scratch->resize( bufN );
    std::copy( haloBuf, haloBuf + bufN, scratch->begin() );
    if ( hasSentinel )
    {
        for ( size_t i = 0; i < bufN; ++i )
            if ( ( *scratch )[i] == sentinel )
                ( *scratch )[i] = kNaN;
    }
}

} // anonymous namespace

Json::Value RsSarGeocodeOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Calibrated SAR raster (sigma0 power); its grid is azimuth/range, defined by the declared timing contract" );
    props["dem"] = makeRasterParam( "dem", "DEM raster — geodetic or projected, north-up; defines the output map grid" );
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );
    props["band"] = makeNumberParam( "band", "1-based SAR band to geocode", 1.0 );
    props["resampling"] = makeEnumParam( "resampling", "Radiometry resampling at the floating-point source position", s_resampling, "bilinear" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "dem", "output" } );
    return root;
}

Json::Value RsSarGeocodeOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "geocoding" );
    meta["tags"].append( "orthorectification" );
    meta["tags"].append( "rtc" );
    meta["task"] = "sar-geometry";
    meta["gpu"] = false;
    meta["purpose"] = "Geocode a calibrated SAR scene onto a DEM map grid with "
                      "real range-Doppler geometry, per-pixel incidence, "
                      "layover/shadow masks and gamma0 radiometric terrain "
                      "correction.";
    meta["prerequisites"].append( "The SAR scene must declare the orbit contract "
                                  "(SICNU_SAR_ORBIT_STATES, SICNU_SAR_AZIMUTH_START_UTC, "
                                  "SICNU_SAR_PRF, SICNU_SAR_RANGE_WINDOW, "
                                  "SICNU_SAR_RANGE_RATE) - missing declarations are typed "
                                  "refusals, never approximations." );
    meta["prerequisites"].append( "Calibrate first: rs:sar_calibrate -> rs:sar_geocode." );
    meta["prerequisites"].append( "DEM carries a CRS and a north-up geotransform; the DEM defines the output grid." );
    meta["limitations"].append( "gamma0 applies the per-pixel radiometric-terrain factor "
                                "sin(thetaL)/sin(theta0) (Ulander 1996, Small 2011 eq. 5) from "
                                "REAL geometry - distinct from the constant-geometry plane-fit "
                                "model of rs:sar_terrain_flatten." );
    meta["limitations"].append( "Rotated DEM grids are refused (terrain-family north-up contract)." );
    meta["limitations"].append( "No antenna pattern or fading-noise correction is applied." );
    return meta;
}

Json::Value RsSarGeocodeOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    // Halo buffer + gradient/geometry planes + bounded source window (the
    // window budget dominates) — all O(tile), never full-frame.
    const uint64_t windowBytes = kMaxWindowFloats * sizeof( float );
    est["estimatedRamBytes"] = Json::Value::UInt64(
        windowBytes + 16ULL * kTileDim * kTileDim * sizeof( float ) );
    return est;
}

Json::Value RsSarGeocodeOperator::estimateExecution( const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarGeocodeOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string demPath = requireString( params, "dem" );
    const std::string outputPath = requireString( params, "output" );
    const int band = static_cast<int>( getDouble( params, "band", 1.0 ) );
    const std::string resampling = getEnum( params, "resampling", s_resampling, "bilinear" );

    ensureGdalInit();

    GdalDatasetWrapper sarDs;
    if ( !sarDs.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open SAR raster: " + inputPath );
    if ( band < 1 || band > sarDs.bandCount() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "band must be in [1, " + std::to_string( sarDs.bandCount() ) + "]" );
    const int sarW = sarDs.width();
    const int sarH = sarDs.height();
    if ( sarW <= 0 || sarH <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "SAR raster is empty: " + inputPath );

    GdalDatasetWrapper demDs;
    if ( !demDs.open( QString::fromStdString( demPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open DEM raster: " + demPath );
    const int width = demDs.width();
    const int height = demDs.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "DEM raster is empty: " + demPath );

    // The DEM defines the output grid: it must carry a CRS and a north-up
    // geotransform (rotated grids are outside the terrain family's
    // north-up contract and are refused, not silently sheared).
    const std::array<double, 6> gt = demDs.geoTransform();
    if ( !demDs.hasGeoTransform() || demDs.projection().isEmpty() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM must carry a geotransform and a CRS to define a geocoding "
                               "target grid: " + demPath );
    if ( gt[2] != 0.0 || gt[4] != 0.0 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM geotransform is rotated; the geocoding family requires "
                               "north-up grids (geotransform rotX = rotY = 0)" );

    // Pixel-center coordinates → geodetic. Geographic grids convert by the
    // geotransform alone; projected grids transform every center through the
    // foundation CRS policy (explicit, deterministic).
    sicnu::geo::CrsTransform demToGeographic;
    bool demProjected = false;
    try
    {
        const sicnu::geo::Crs demCrs = sicnu::geo::Crs::fromWkt( demDs.projection().toStdString() );
        demProjected = !demCrs.isGeographic();
        if ( demProjected )
            demToGeographic = sicnu::geo::CrsTransform::create(
                demCrs, sicnu::geo::Crs::fromAuthid( "EPSG:4326" ),
                sicnu::geo::AxisOrder::TraditionalGis );
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM CRS is unusable for geolocation: " + std::string( e.what() ) );
    }

    // Scene timing contract: declared on the SAR product (the radar scene's
    // own metadata) with the DEM (the co-registered scene carrier, as the
    // backward orbit product reads it) as fallback — one parser for both.
    auto metaItem = [&]( const char *key ) -> QString {
        if ( const char *v = GDALGetMetadataItem( static_cast<GDALDatasetH>( sarDs.dataset() ), key, nullptr ) )
            return QString::fromUtf8( v );
        if ( const char *v = GDALGetMetadataItem( static_cast<GDALDatasetH>( demDs.dataset() ), key, nullptr ) )
            return QString::fromUtf8( v );
        return QString();
    };
    sicnu::sar::SarSceneContract contract;
    {
        QString contractError;
        if ( !sicnu::sar::parseSarSceneContract( metaItem, sarW, sarH, &contract, &contractError ) )
            throw RSOperatorError( ErrorCode::InvalidInputData, contractError.toStdString() );
    }

    // DEM cell sizes in metres for the Horn gradients (degrees vs projected
    // units handled by the shared #612 seam).
    double csx = 0.0;
    double csy = 0.0;
    cellSizesMetres( demDs, &csx, &csy );

    bool demHasSentinel = false;
    float demSentinel = 0.0f;
    {
        bool hasNodataFlag = false;
        const double nodata = demDs.bandNoDataValue( 1, &hasNodataFlag );
        if ( hasNodataFlag && std::isfinite( nodata ) )
        {
            demSentinel = static_cast<float>( nodata );
            demHasSentinel = true;
        }
    }
    bool sarHasSentinel = false;
    float sarSentinel = 0.0f;
    {
        bool hasNodataFlag = false;
        const double nodata = sarDs.bandNoDataValue( band, &hasNodataFlag );
        if ( hasNodataFlag && std::isfinite( nodata ) )
        {
            sarSentinel = static_cast<float>( nodata );
            sarHasSentinel = true;
        }
    }

    GdalBlockStream demStream( demDs, 1, kTileDim, kTileDim, kHalo );
    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height,
                             static_cast<int>( kProductCount ), GDT_Float32,
                             demDs.geoTransform(), demDs.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );

    std::vector<float> demScratch;
    // Per-tile working planes — one float/double per tile pixel each,
    // O(tile) total.
    std::vector<float> dzdE( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<float> dzdN( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<float> backscatter( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<float> outPlane( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<sicnu::sar::GeocodeGeometry> geom( static_cast<size_t>( kTileDim ) * kTileDim );
    // Cell status: 0 = geocoded, 1 = DEM NoData, 2 = geometry unresolved.
    std::vector<uint8_t> cellState( static_cast<size_t>( kTileDim ) * kTileDim );

    std::vector<float> window; // radiometry source window (sentinel-normalized)

    std::uint64_t sampled = 0;
    std::uint64_t demNoData = 0;
    std::uint64_t unresolved = 0;
    std::uint64_t outsideImage = 0;
    std::uint64_t sourceNoData = 0;
    // In-image cells whose radiometry came from the bounded per-pixel 2x2
    // path (over-budget source windows that are never materialized).
    // Reported so callers can see when the slow path carried the run.
    std::uint64_t perPixelFallback = 0;

    const int totalTiles = demStream.tileCount();
    int tileIndex = 0;
    const bool ok = demStream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *haloBuf ) {
        context.throwIfCancelled();
        const int bw = tile.bufferWidth;
        const size_t bufN = static_cast<size_t>( bw ) * tile.bufferHeight;
        normalizeHalo( haloBuf, bufN, demHasSentinel, demSentinel, &demScratch );
        std::fill( cellState.begin(), cellState.end(), 0 );

        // Pass 1 — DEM gradients and forward geocoding per cell.
        for ( int y = 0; y < tile.height; ++y )
        {
            const double centerY = gt[3] + ( tile.yOffset + y + 0.5 ) * gt[5];
            for ( int x = 0; x < tile.width; ++x )
            {
                const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                const float demValue =
                    demScratch[static_cast<size_t>( y + kHalo ) * bw + ( x + kHalo )];
                if ( !std::isfinite( demValue ) )
                {
                    cellState[idx] = 1;
                    ++demNoData;
                    continue;
                }
                // Horn 3x3 gradients from the halo buffer (shared kernel;
                // dzdy is dz/d(row+) = south on north-up rasters — negate to
                // dz/dNorth, the #785 review convention).
                float k9[9];
                bool anyNaN = false;
                for ( int dy = -1; dy <= 1 && !anyNaN; ++dy )
                    for ( int dx = -1; dx <= 1; ++dx )
                    {
                        const float v = demScratch[static_cast<size_t>( y + kHalo + dy ) * bw
                                                   + ( x + kHalo + dx )];
                        if ( !std::isfinite( v ) )
                        {
                            anyNaN = true;
                            break;
                        }
                        k9[( dy + 1 ) * 3 + ( dx + 1 )] = v;
                    }
                double dzdEv = std::numeric_limits<double>::quiet_NaN();
                double dzdNv = std::numeric_limits<double>::quiet_NaN();
                if ( !anyNaN )
                {
                    double dzdx = 0.0;
                    double dzdy = 0.0;
                    TopographicCorrection::hornGradient( k9, csx, csy, &dzdx, &dzdy );
                    dzdEv = dzdx;
                    dzdNv = -dzdy;
                }
                dzdE[idx] = static_cast<float>( dzdEv );
                dzdN[idx] = static_cast<float>( dzdNv );

                // Pixel-center geodetic coordinates (projected grids go
                // through the foundation CRS transform).
                const double centerX = gt[0] + ( tile.xOffset + x + 0.5 ) * gt[1];
                double latDeg = centerY;
                double lonDeg = centerX;
                if ( demProjected )
                {
                    try
                    {
                        const sicnu::geo::CrsPoint p =
                            demToGeographic.forward( { centerX, centerY } );
                        lonDeg = p.x;
                        latDeg = p.y;
                    }
                    catch ( const sicnu::geo::GeoError & )
                    {
                        // Outside the projection's valid domain: a genuinely
                        // unresolvable cell, not a run failure.
                        cellState[idx] = 2;
                        ++unresolved;
                        continue;
                    }
                }

                sicnu::sar::GeocodeGeometry g;
                if ( !sicnu::sar::geocodeGroundCell( contract, latDeg, lonDeg, demValue,
                                                     dzdEv, dzdNv, &g ) || !g.resolved )
                {
                    cellState[idx] = 2;
                    ++unresolved;
                    continue;
                }
                geom[idx] = g;
            }
        }

        // Pass 2 — radiometry: bounded source window over the tile's
        // in-image cells. Geometry products stay valid for every resolved
        // cell regardless of where the source position falls.
        double minRow = 0, maxRow = -1, minCol = 0, maxCol = -1;
        bool anyInside = false;
        const double kMaxRow = sarH - 1.0;
        const double kMaxCol = sarW - 1.0;
        for ( int y = 0; y < tile.height; ++y )
            for ( int x = 0; x < tile.width; ++x )
            {
                const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                if ( cellState[idx] != 0 )
                    continue;
                const double r = geom[idx].rowF;
                const double c = geom[idx].colF;
                if ( !( r >= 0.0 && r <= kMaxRow && c >= 0.0 && c <= kMaxCol ) )
                {
                    outsideImage++;
                    continue;
                }
                if ( !anyInside )
                {
                    minRow = maxRow = r;
                    minCol = maxCol = c;
                    anyInside = true;
                }
                else
                {
                    minRow = std::min( minRow, r );
                    maxRow = std::max( maxRow, r );
                    minCol = std::min( minCol, c );
                    maxCol = std::max( maxCol, c );
                }
            }

        int winX = 0, winY = 0, winW = 0, winH = 0;
        bool windowReady = false;
        if ( anyInside )
        {
            winX = std::max( 0, static_cast<int>( std::floor( minCol ) ) - 1 );
            winY = std::max( 0, static_cast<int>( std::floor( minRow ) ) - 1 );
            winW = std::min( sarW - 1, static_cast<int>( std::ceil( maxCol ) ) + 1 ) - winX + 1;
            winH = std::min( sarH - 1, static_cast<int>( std::ceil( maxRow ) ) + 1 ) - winY + 1;
            if ( winW > 0 && winH > 0
                 && static_cast<size_t>( winW ) * static_cast<size_t>( winH ) <= kMaxWindowFloats )
            {
                window.resize( static_cast<size_t>( winW ) * winH );
                windowReady = sarDs.readBandWindow( band, winX, winY, winW, winH, window.data() );
                if ( !windowReady )
                {
                    out.abandon();
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to read the SAR source window" );
                }
                if ( sarHasSentinel )
                    for ( float &v : window )
                        if ( v == sarSentinel )
                            v = kNaN;
            }
            // Over-budget windows are never materialized: those tiles use
            // the bounded per-pixel 2x2 read path below.
        }

        auto sampleAt = [&]( double r, double c, float *dst ) -> bool {
            if ( windowReady )
            {
                const double wr = r - winY;
                const double wc = c - winX;
                if ( resampling == "nearest" )
                    return sicnu::sar::nearestSample( window.data(), winW, winH, wc, wr, dst );
                return sicnu::sar::bilinearSample( window.data(), winW, winH, wc, wr, dst );
            }
            // Per-pixel fallback: a 2x2 window read per cell (bounded,
            // slow, rare — only for over-budget windows).
            const int x0 = static_cast<int>( std::floor( c ) );
            const int y0 = static_cast<int>( std::floor( r ) );
            if ( x0 < 0 || y0 < 0 || x0 > sarW - 1 || y0 > sarH - 1 )
                return false;
            float tap[4];
            for ( int dy = 0; dy < 2; ++dy )
                for ( int dx = 0; dx < 2; ++dx )
                {
                    const int px = std::clamp( x0 + dx, 0, sarW - 1 );
                    const int py = std::clamp( y0 + dy, 0, sarH - 1 );
                    float v = 0.0f;
                    if ( !sarDs.readPixel( band, px, py, &v ) )
                        return false;
                    if ( sarHasSentinel && v == sarSentinel )
                        v = kNaN;
                    tap[dy * 2 + dx] = v;
                }
            if ( resampling == "nearest" )
            {
                const int nx = ( c - x0 ) < 0.5 ? 0 : 1;
                const int ny = ( r - y0 ) < 0.5 ? 0 : 1;
                *dst = tap[ny * 2 + nx];
                return std::isfinite( *dst );
            }
            const double fx = c - x0;
            const double fy = r - y0;
            if ( !std::isfinite( tap[0] ) || !std::isfinite( tap[1] )
                 || !std::isfinite( tap[2] ) || !std::isfinite( tap[3] ) )
                return false;
            const double top = tap[0] + ( tap[1] - tap[0] ) * fx;
            const double bottom = tap[2] + ( tap[3] - tap[2] ) * fx;
            *dst = static_cast<float>( top + ( bottom - top ) * fy );
            return std::isfinite( *dst );
        };

        // Pass 3 — sample the radiometry once per cell.
        for ( int y = 0; y < tile.height; ++y )
            for ( int x = 0; x < tile.width; ++x )
            {
                const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                backscatter[idx] = kNaN;
                if ( cellState[idx] != 0 )
                    continue;
                const double r = geom[idx].rowF;
                const double c = geom[idx].colF;
                if ( !( r >= 0.0 && r <= kMaxRow && c >= 0.0 && c <= kMaxCol ) )
                    continue;
                float v = kNaN;
                if ( !windowReady )
                    ++perPixelFallback;
                if ( sampleAt( r, c, &v ) )
                {
                    backscatter[idx] = v;
                    ++sampled;
                }
                else
                {
                    ++sourceNoData;
                }
            }

        // Pass 4 — write the fixed five product bands. Consumers read the
        // band order from SICNU_SAR_GEOCODE_BANDS.
        for ( int productIdx = 0; productIdx < kProductCount; ++productIdx )
        {
            for ( int y = 0; y < tile.height; ++y )
                for ( int x = 0; x < tile.width; ++x )
                {
                    const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                    const bool resolved = cellState[idx] == 0;
                    const bool facetValid = resolved
                                            && std::isfinite( dzdE[idx] )
                                            && std::isfinite( dzdN[idx] );
                    float value = kNaN;
                    if ( resolved )
                    {
                        switch ( productIdx )
                        {
                            case 0: // backscatter: resampled input radiometry
                                value = backscatter[idx];
                                break;
                            case 1: // gamma0 = sigma0 * sin(thetaL)/sin(theta0);
                                    // NaN factor (facet at/past grazing) stays NaN
                                if ( facetValid && std::isfinite( backscatter[idx] )
                                     && std::isfinite( geom[idx].rtcFactor ) )
                                    value = static_cast<float>( backscatter[idx] * geom[idx].rtcFactor );
                                break;
                            case 2: // incidence (ellipsoid reference, degrees)
                                value = static_cast<float>( geom[idx].incidenceDeg );
                                break;
                            case 3: // local incidence (terrain facet, degrees)
                                value = static_cast<float>( geom[idx].localIncidenceDeg );
                                break;
                            case 4: // layover/shadow class (0/1/2)
                                value = facetValid
                                            ? static_cast<float>( static_cast<int>( geom[idx].maskClass ) )
                                            : kNaN;
                                break;
                        }
                    }
                    outPlane[idx] = value;
                }
            if ( !out.writeTile( productIdx + 1, tile, outPlane.data() ) )
                return false;
        }

        context.reportProgress( 0.95 * ( ++tileIndex ) / totalTiles, "Geocoding" );
        return true;
    } );
    if ( !ok )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/compute the geocoding products" );
    }

    // Provenance: the geocoded products carry the SAR scene's radiometric
    // declarations plus the geocoding band order.
    out.setMetadataItem( QLatin1String( sicnu::sar::kModalityKey ), QLatin1String( "sar" ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_GEOMETRY_PRODUCT" ), QLatin1String( "geocoded_rd" ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_ORBIT_GEOMETRY" ), QLatin1String( "declared" ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_GEOCODE_BANDS" ),
                         QLatin1String( "backscatter,gamma0,incidence,local_incidence,layover_shadow" ) );
    if ( const char *cal = GDALGetMetadataItem( static_cast<GDALDatasetH>( sarDs.dataset() ),
                                                sicnu::sar::kCalibrationKey, nullptr ) )
        out.setMetadataItem( QLatin1String( sicnu::sar::kCalibrationKey ), QLatin1String( cal ) );
    if ( const char *domain = GDALGetMetadataItem( static_cast<GDALDatasetH>( sarDs.dataset() ),
                                                   sicnu::sar::kDomainKey, nullptr ) )
        out.setMetadataItem( QLatin1String( sicnu::sar::kDomainKey ), QLatin1String( domain ) );

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize output: " + closeError.toStdString() );

    const std::uint64_t total = static_cast<std::uint64_t>( width ) * height;
    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["width"] = width;
    result["height"] = height;
    result["bands"] = kProductCount;
    result["bandOrder"] = "backscatter,gamma0,incidence,local_incidence,layover_shadow";
    result["sampledPixels"] = Json::Value::UInt64( sampled );
    result["perPixelFallbackPixels"] = Json::Value::UInt64( perPixelFallback );
    result["demNoDataPixels"] = Json::Value::UInt64( demNoData );
    result["unresolvedGeometryPixels"] = Json::Value::UInt64( unresolved );
    result["outsideImagePixels"] = Json::Value::UInt64( outsideImage );
    result["sourceNoDataPixels"] = Json::Value::UInt64( sourceNoData );
    result["resampling"] = resampling;
    result["inputPixels"] = Json::Value::UInt64( total );
    context.reportProgress( 1.0, "SAR geocoding complete" );
    return result;
}

} // namespace sicnu::operators::rs
