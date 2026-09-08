/***************************************************************************
 * rs_sar_terrain_masks_operator.cpp — Milestone D.3 (terrain geometry masks)
 *
 * Streams the DEM with a 1-pixel halo (Horn gradients from the shared
 * kernels), computes per-pixel local incidence / layover-shadow class, and
 * writes the product tile-by-tile (Byte for the mask, Float32 for angles).
 ***************************************************************************/
#include "rs_sar_terrain_masks_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_terrain_geometry.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/topographic_correction.h" // hornGradient (shared Horn kernels)
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput

#include <QString>

#include <gdal.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_products = { "local_incidence", "layover_shadow_mask" };

constexpr int kTileDim = 256;
constexpr int kHalo = 1;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr uint8_t kMaskNoData = 255;

/// Same WGS84 degrees->metres conversion the terrain/topographic operators
/// apply (#612); consolidation tracked for Milestone F.1.
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

/// Sentinel -> NaN normalization over the stream's borrowed halo buffer
/// (copied into @a scratch, which the compute loop then indexes).
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

Json::Value RsSarTerrainMasksOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["dem"] = makeRasterParam( "dem", "DEM raster" );
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );
    props["product"] = makeEnumParam( "product", "Terrain geometry product", s_products, "layover_shadow_mask" );
    props["incidence"] = makeNumberParam( "incidence", "Incidence angle in degrees from vertical (0, 90); required — no scene-metadata fallback is consulted", 35.0 );
    props["heading"] = makeNumberParam( "heading", "Legacy name kept for compatibility: this is the ANTENNA LOOK AZIMUTH (boresight ground azimuth, degrees clockwise from north [0, 360)), not the flight heading — a right-looking antenna look azimuth is flight heading + 90 (#785). No scene-metadata fallback is consulted", 0.0 );
    props["lookAzimuthDeg"] = makeNumberParam( "lookAzimuthDeg", "Explicit antenna look azimuth in degrees clockwise from north; when present overrides the legacy 'heading' parameter", 0.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["product"] = makeStringParam( "product", "Computed product", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "dem", "output", "product" } );
    return root;
}

Json::Value RsSarTerrainMasksOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "terrain" );
    meta["tags"].append( "layover" );
    meta["tags"].append( "shadow" );
    meta["task"] = "sar-geometry";
    meta["notes"] = "Rigorous surface-normal geometry under the declared "
                    "constant-incidence/heading contract. Full range-Doppler "
                    "correction requires orbit state vectors + sensor timing, "
                    "which the product metadata contract does not carry — that "
                    "extension (SICNU_SAR_ORBIT_STATES et al.) is documented in "
                    "sar_terrain_geometry.h and deliberately NOT approximated.";
    meta["gpu"] = false;
    meta["purpose"] = "Flag layover/shadow distortions and provide local incidence "
                      "angles for terrain-aware SAR interpretation.";
    meta["prerequisites"].append( "DEM on any grid; the DEM defines the geometry, no co-registered SAR input is required." );
    meta["limitations"].append( "Constant-geometry approximation: per-scene incidence/heading, not per-pixel orbit geometry." );
    meta["limitations"].append( "The radiometric terrain factor cos(theta_i)/cos(theta_l) is exposed via the kernel for providers, not written by this operator (use rs:sar_terrain_flatten for normalized backscatter)." );
    return meta;
}

Json::Value RsSarTerrainMasksOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * kTileDim * kTileDim * sizeof( float ) );
    return est;
}

Json::Value RsSarTerrainMasksOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "dem" ) && params["dem"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["dem"].asString() ) ) && probe.bandCount() > 0 )
        {
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { 256ULL, 256ULL, 3ULL, static_cast<std::uint64_t>( sizeof( float ) ) } );
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

Json::Value RsSarTerrainMasksOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string demPath = requireString( params, "dem" );
    const std::string outputPath = requireString( params, "output" );
    const std::string product = getEnum( params, "product", s_products, "layover_shadow_mask" );

    ensureGdalInit();

    GdalDatasetWrapper demDs;
    if ( !demDs.open( QString::fromStdString( demPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open DEM raster: " + demPath );
    const int width = demDs.width();
    const int height = demDs.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "DEM raster is empty: " + demPath );

    // Geometry: explicit parameters win; declared SAR scene metadata
    // (SICNU_SAR_INCIDENCE_DEG / SICNU_SAR_HEADING_DEG on the DEM-derived
    // product is uncommon, so callers normally pass the SAR scene's values).
    double incidence = getDouble( params, "incidence", 35.0 );
    double heading = getDouble( params, "heading", 0.0 );
    // #785 disambiguation: 'heading' always meant the look azimuth here; the
    // canonical 'lookAzimuthDeg' override makes that explicit and lets
    // callers deriving the boresight from flight heading + antenna side
    // (lookAzimuthFromHeading) pass it without the legacy name.
    if ( params.isMember( "lookAzimuthDeg" ) && params["lookAzimuthDeg"].isNumeric() )
    {
        const double lookAzimuth = params["lookAzimuthDeg"].asDouble();
        if ( !std::isfinite( lookAzimuth ) )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "lookAzimuthDeg must be a finite angle in degrees" );
        heading = lookAzimuth;
    }

    if ( !( incidence > 0.0 && incidence < 90.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "incidence must be in (0, 90) degrees, got " + std::to_string( incidence ) );
    if ( !( heading >= 0.0 && heading < 360.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "heading must be in [0, 360) degrees, got " + std::to_string( heading ) );

    double csx = 0.0;
    double csy = 0.0;
    cellSizesMetres( demDs, &csx, &csy );

    const bool byteMask = ( product == "layover_shadow_mask" );

    GdalBlockStream demStream( demDs, 1, kTileDim, kTileDim, kHalo );

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1,
                             byteMask ? GDT_Byte : GDT_Float32, demDs.geoTransform(),
                             demDs.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    if ( byteMask )
        out.setNoDataValue( kMaskNoData );
    else
        out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );

    std::vector<float> demScratch;
    std::vector<float> outF( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<uint8_t> outB( static_cast<size_t>( kTileDim ) * kTileDim );

    bool demHasSentinel = false;
    float demSentinel = 0.0f;
    {
        bool hasNodataFlag = false;
        const double nodata = demDs.bandNoDataValue( 1, &hasNodataFlag );
        if ( hasNodataFlag && std::isfinite( nodata ) )
        {
            demHasSentinel = true;
            demSentinel = static_cast<float>( nodata );
        }
    }

    const int totalTiles = demStream.tileCount();
    int tileIndex = 0;
    const bool ok = demStream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *haloBuf ) {
        context.throwIfCancelled();
        const int bw = tile.bufferWidth;
        const size_t bufN = static_cast<size_t>( bw ) * tile.bufferHeight;
        normalizeHalo( haloBuf, bufN, demHasSentinel, demSentinel, &demScratch );
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const size_t idx = static_cast<size_t>( y ) * tile.width + x;
                float k9[9];
                bool anyNaN = false;
                for ( int dy = -1; dy <= 1 && !anyNaN; ++dy )
                    for ( int dx = -1; dx <= 1; ++dx )
                    {
                        const float v =
                            demScratch[static_cast<size_t>( y + kHalo + dy ) * bw + ( x + kHalo + dx )];
                        if ( !std::isfinite( v ) )
                        {
                            anyNaN = true;
                            break;
                        }
                        k9[( dy + 1 ) * 3 + ( dx + 1 )] = v;
                    }
                if ( anyNaN )
                {
                    outF[idx] = kNaN;
                    outB[idx] = kMaskNoData;
                    continue;
                }
                double dzdx = 0.0;
                double dzdy = 0.0;
                TopographicCorrection::hornGradient( k9, csx, csy, &dzdx, &dzdy );
                // hornGradient's dzdy is dz/d(row+) = dz/dSouth on the
                // north-up rasters used here, while terrainGeometry
                // contracts dz/dNorth — negate it (#785 review; the
                // pre-6.0 masks were mirrored along every N-S look).
                const sicnu::sar::TerrainGeometryResult geo =
                    sicnu::sar::terrainGeometry( dzdx, -dzdy, incidence, heading );
                if ( byteMask )
                    outB[idx] = static_cast<uint8_t>( static_cast<int>( geo.maskClass ) );
                else
                    outF[idx] = static_cast<float>( geo.localIncidenceDeg );
            }
        }
        if ( byteMask )
        {
            if ( !out.writeTileRaw( 1, tile, outB.data(), GDT_Byte ) )
                return false;
        }
        else if ( !out.writeTile( 1, tile, outF.data() ) )
        {
            return false;
        }
        context.reportProgress( 0.9 * ( ++tileIndex ) / totalTiles, "Computing " + product );
        return true;
    } );
    if ( !ok )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/compute terrain masks" );
    }

    out.setMetadataItem( QLatin1String( sicnu::sar::kModalityKey ), QLatin1String( "sar" ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_GEOMETRY_PRODUCT" ), QString::fromStdString( product ) );
    out.setMetadataItem( QLatin1String( sicnu::sar::kIncidenceKey ), QString::number( incidence ) );
    out.setMetadataItem( QLatin1String( sicnu::sar::kHeadingKey ), QString::number( heading ) );

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["product"] = product;
    result["incidenceDeg"] = incidence;
    result["headingDeg"] = heading;
    result["lookAzimuthDeg"] = heading; // canonical name for the look azimuth (#785)
    result["width"] = width;
    result["height"] = height;
    context.reportProgress( 1.0, "SAR terrain masks complete" );
    return result;
}

} // namespace sicnu::operators::rs
