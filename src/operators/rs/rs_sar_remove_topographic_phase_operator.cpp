/***************************************************************************
 * rs_sar_remove_topographic_phase_operator.cpp — see the header
 * (Advanced InSAR 11.0, package B; DECISIONS D-002)
 ***************************************************************************/
#include "rs_sar_remove_topographic_phase_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_baseline.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_orbit.h"
#include "processing/algorithms/sar/sar_topographic_phase.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QCoreApplication>
#include <QString>

#include <gdal.h>
#include <ogr_spatialref.h>
#include <ogr_srs_api.h>

#include <cmath>
#include <memory>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kTile = 256;

/// North-up axis-aligned geotransform contract (rotated/sheared rasters are
/// refused — DEM window math assumes axis alignment).
bool isNorthUpGt( const std::array<double, 6> &gt )
{
    return gt[2] == 0.0 && gt[4] == 0.0 && gt[1] > 0.0 && gt[5] < 0.0;
}

/// NaN-aware bilinear sample of a row-major float plane at fractional
/// coordinates relative to the buffer origin. Any non-finite corner
/// propagates NaN (invalid heights are never interpolated into plausible
/// ones).
double sampleBilinearNaN( const float *buf, int bufW, int bufH, double fx, double fy )
{
    // Only actually-tapped corners are read (the shiftComplexBilinear edge
    // convention): a zero fractional weight never touches its corner, so
    // pixel centers aligned with the last DEM row/column stay valid.
    const int i0 = static_cast<int>( std::floor( fx ) );
    const int j0 = static_cast<int>( std::floor( fy ) );
    if ( i0 < 0 || j0 < 0 )
        return std::numeric_limits<double>::quiet_NaN();
    const double ax = fx - i0;
    const double ay = fy - j0;
    bool ok = true;
    double acc = 0.0;
    const auto tap = [&]( int xx, int yy, double weight ) {
        if ( !ok || weight == 0.0 )
            return;
        if ( xx < 0 || yy < 0 || xx >= bufW || yy >= bufH )
        {
            ok = false;
            return;
        }
        const double h = buf[static_cast<size_t>( yy ) * bufW + xx];
        if ( !std::isfinite( h ) )
        {
            ok = false;
            return;
        }
        acc += weight * h;
    };
    tap( i0, j0, ( 1.0 - ax ) * ( 1.0 - ay ) );
    tap( i0 + 1, j0, ax * ( 1.0 - ay ) );
    tap( i0, j0 + 1, ( 1.0 - ax ) * ay );
    tap( i0 + 1, j0 + 1, ax * ay );
    return ok ? acc : std::numeric_limits<double>::quiet_NaN();
}

} // anonymous namespace

Json::Value RsSarRemoveTopographicPhaseOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["interferogram"] = makeRasterParam(
        "interferogram", "Complex (CFloat32) interferogram raster (same-grid pair product)" );
    props["band"] = makeNumberParam( "band", "1-based complex interferogram band", 1.0 );
    props["dem"] = makeRasterParam(
        "dem", "DEM raster (Float32, metres ABOVE the WGS84 ellipsoid; must share the "
               "interferogram CRS and fully cover its extent)" );
    props["demBand"] = makeNumberParam( "demBand", "1-based DEM height band", 1.0 );
    props["masterOrbitStates"] = makeStringParam(
        "masterOrbitStates", "Master scene orbit states (SICNU_SAR_ORBIT_STATES grammar: "
                             "\"t;x;y;z;vx;vy;vz|...\", WGS84 ECEF metres and m/s)" );
    props["slaveOrbitStates"] = makeStringParam(
        "slaveOrbitStates", "Slave scene orbit states (same grammar)" );
    props["wavelengthUm"] = makeNumberParam(
        "wavelengthUm", "Radar wavelength in micrometres; defaults to the interferogram's "
                        "SICNU_SAR_WAVELENGTH_UM metadata (refused when neither exists)" );
    props["output"] = makeOutputParam( "output",
                                       "Output residual complex interferogram path", "tif" );
    props["topoPhaseOutput"] = makeOutputParam(
        "topoPhaseOutput", "Optional Float32 topographic-phase diagnostics raster", "tif" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Residual complex interferogram" );
    outputs["topoPhaseOutput"] =
        makeRasterParam( "topoPhaseOutput", "Topographic phase in (−π, π] radians" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] =
        makeRequired( { "interferogram", "dem", "masterOrbitStates", "slaveOrbitStates",
                        "output" } );
    return root;
}

Json::Value RsSarRemoveTopographicPhaseOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["task"] = "sar-insar";
    meta["gpu"] = false;
    meta["purpose"] = "Isolate displacement/atmosphere phase by removing the exact "
                      "geometric topographic phase a DEM and the two orbits imprint "
                      "on an interferogram.";
    meta["prerequisites"].append( "Complex interferogram; DEM above the WGS84 ellipsoid "
                                  "in the interferogram CRS; both scene orbit state "
                                  "vectors; radar wavelength." );
    meta["limitations"].append( "North-up axis-aligned grids only; DEM must share the "
                                "interferogram CRS and cover it (warp/clip otherwise)." );
    meta["limitations"].append( "Height sensitivity degenerates near zero B⊥ — the "
                                "removal is exact for the given DEM, but pairs without "
                                "perpendicular baseline carry no height signal to "
                                "remove." );
    return meta;
}

Json::Value RsSarRemoveTopographicPhaseOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTile;
    est["tileHeight"] = kTile;
    est["estimatedRamBytes"] = Json::Value::UInt64( 8ULL * kTile * kTile * 8 );
    return est;
}

Json::Value RsSarRemoveTopographicPhaseOperator::estimateExecution(
    const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarRemoveTopographicPhaseOperator::run( const Json::Value &params,
                                                      RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string ifgPath = requireString( params, "interferogram" );
    const std::string demPath = requireString( params, "dem" );
    const std::string masterOrbitStr = requireString( params, "masterOrbitStates" );
    const std::string slaveOrbitStr = requireString( params, "slaveOrbitStates" );
    const std::string outputPath = requireString( params, "output" );
    const std::string topoPath = getString( params, "topoPhaseOutput", std::string() );
    const int band = getInt( params, "band", 1 );
    const int demBand = getInt( params, "demBand", 1 );
    const double wavelengthUmParam = getDouble( params, "wavelengthUm", 0.0 );

    // --- Orbit truth (typed refusals, never guessed) -------------------
    ensureGdalInit();
    sicnu::sar::OrbitSegment masterOrbit, slaveOrbit;
    QString orbitError;
    if ( !sicnu::sar::parseOrbitStates( QString::fromStdString( masterOrbitStr ),
                                        &masterOrbit, &orbitError ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "TOPO_PHASE_METADATA_MISSING: masterOrbitStates — "
                                   + orbitError.toStdString() );
    if ( !sicnu::sar::parseOrbitStates( QString::fromStdString( slaveOrbitStr ),
                                        &slaveOrbit, &orbitError ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "TOPO_PHASE_METADATA_MISSING: slaveOrbitStates — "
                                   + orbitError.toStdString() );

    // --- Interferogram grid --------------------------------------------
    GdalDatasetWrapper ifg;
    if ( !ifg.open( QString::fromStdString( ifgPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open interferogram raster: " + ifgPath );
    if ( ifg.width() <= 0 || ifg.height() <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Interferogram raster is empty: " + ifgPath );
    QString sarError;
    if ( !sicnu::sar::validateComplexBands( ifg, { band }, &sarError ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: " + sarError.toStdString() );
    if ( !ifg.hasGeoTransform() || !isNorthUpGt( ifg.geoTransform() ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM_GRID_UNSUPPORTED: the interferogram geotransform is "
                               "missing or not north-up axis-aligned (rotated/sheared "
                               "rasters are refused, not approximated)" );
    const std::string ifgWkt = ifg.projection().toStdString();
    if ( ifgWkt.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "GRID_CRS_MISSING: the interferogram carries no CRS — "
                               "topographic phase is undefined without georeferencing" );

    // --- Wavelength truth: param wins, metadata next, refusal otherwise -
    double wavelengthUm = wavelengthUmParam;
    bool wavelengthFromMetadata = false;
    if ( wavelengthUm <= 0.0 )
    {
        const QString metaWavelength = sicnu::sar::datasetMeta( ifg, "SICNU_SAR_WAVELENGTH_UM" );
        bool ok = false;
        wavelengthUm = metaWavelength.toDouble( &ok );
        if ( !ok || wavelengthUm <= 0.0 )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "TOPO_PHASE_METADATA_MISSING: no radar wavelength — pass wavelengthUm "
                "(µm) or declare SICNU_SAR_WAVELENGTH_UM on the interferogram" );
        wavelengthFromMetadata = true;
    }
    const double wavelengthM = sicnu::sar::wavelengthUmToM( wavelengthUm );

    // --- DEM grid: same CRS, north-up, full coverage --------------------
    GdalDatasetWrapper dem;
    if ( !dem.open( QString::fromStdString( demPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open DEM raster: " + demPath );
    if ( dem.width() <= 0 || dem.height() <= 0 || demBand < 1 || demBand > dem.bandCount() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM raster is empty or demBand out of range: " + demPath );
    if ( !dem.hasGeoTransform() || !isNorthUpGt( dem.geoTransform() ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM_GRID_UNSUPPORTED: the DEM geotransform is missing or "
                               "not north-up axis-aligned" );
    const std::string demWkt = dem.projection().toStdString();
    if ( demWkt.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "GRID_CRS_MISSING: the DEM carries no CRS" );

    // C-API CRS handles (the house pattern, rs_roi_labeler.cpp): the SR
    // handles are released as soon as the transform exists — the transform
    // keeps its own references.
    OGRSpatialReferenceH hIfg = OSRNewSpatialReference( ifgWkt.c_str() );
    OGRSpatialReferenceH hDem = OSRNewSpatialReference( demWkt.c_str() );
    OGRSpatialReferenceH hWgs84 = OSRNewSpatialReference( nullptr );
    if ( !hIfg || !hDem || !hWgs84 )
    {
        if ( hIfg )
            OSRDestroySpatialReference( hIfg );
        if ( hDem )
            OSRDestroySpatialReference( hDem );
        if ( hWgs84 )
            OSRDestroySpatialReference( hWgs84 );
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to parse the raster CRS WKT" );
    }
    OSRSetAxisMappingStrategy( hIfg, OAMS_TRADITIONAL_GIS_ORDER );
    OSRSetAxisMappingStrategy( hDem, OAMS_TRADITIONAL_GIS_ORDER );
    OSRSetAxisMappingStrategy( hWgs84, OAMS_TRADITIONAL_GIS_ORDER );
    if ( !OSRIsSame( hDem, hIfg ) )
    {
        OSRDestroySpatialReference( hIfg );
        OSRDestroySpatialReference( hDem );
        OSRDestroySpatialReference( hWgs84 );
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM_CRS_MISMATCH: the DEM CRS differs from the "
                               "interferogram CRS — warp the DEM onto the interferogram "
                               "grid first (no hidden reprojection is performed)" );
    }
    OSRSetWellKnownGeogCS( hWgs84, "WGS84" );
    OGRCoordinateTransformationH toGeodetic =
        OCTNewCoordinateTransformation( hIfg, hWgs84 );
    OSRDestroySpatialReference( hIfg );
    OSRDestroySpatialReference( hDem );
    OSRDestroySpatialReference( hWgs84 );
    if ( toGeodetic == nullptr )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to create the map→WGS84 coordinate transform" );

    const auto &gt = ifg.geoTransform();
    const auto &dgt = dem.geoTransform();
    const int width = ifg.width();
    const int height = ifg.height();
    // DEM must fully cover every interferogram pixel CENTER (the sampled
    // locations); edge centers sit at ±0.5 pixel from the corner origins.
    const double ifgMinX = gt[0] + 0.5 * gt[1];
    const double ifgMaxX = gt[0] + ( width - 0.5 ) * gt[1];
    const double ifgMinY = gt[3] + ( height - 0.5 ) * gt[5];
    const double ifgMaxY = gt[3] + 0.5 * gt[5];
    const double demMinX = dgt[0];
    const double demMaxX = dgt[0] + dem.width() * dgt[1];
    const double demMinY = dgt[3] + dem.height() * dgt[5];
    const double demMaxY = dgt[3];
    if ( ifgMinX < demMinX || ifgMaxX > demMaxX || ifgMinY < demMinY || ifgMaxY > demMaxY )
    {
        OCTDestroyCoordinateTransformation( toGeodetic );
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DEM_EXTENT_INSUFFICIENT: the DEM does not fully cover "
                               "the interferogram extent (interferogram x[" +
                                   std::to_string( ifgMinX ) + ", "
                                   + std::to_string( ifgMaxX ) + "] y[" +
                                   std::to_string( ifgMinY ) + ", "
                                   + std::to_string( ifgMaxY ) + "] vs DEM x[" +
                                   std::to_string( demMinX ) + ", "
                                   + std::to_string( demMaxX ) + "] y[" +
                                   std::to_string( demMinY ) + ", "
                                   + std::to_string( demMaxY )
                                   + "]) — clip the AOI or extend the DEM" );
    }

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1,
                             GDT_CFloat32, gt, ifg.projection() );
    if ( !out.isOpen() )
    {
        OCTDestroyCoordinateTransformation( toGeodetic );
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    }
    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "interferogram_topo_removed" );
    out.setMetadataItem( "SICNU_SAR_WAVELENGTH_UM", std::to_string( wavelengthUm ).c_str() );
    out.setMetadataItem( "SICNU_SAR_INSAR_TRUTH_VERSION",
                         std::to_string( sicnu::sar::kInSarTruthVersion ).c_str() );
    out.setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );

    std::unique_ptr<GdalStreamingOutput> topoOut;
    if ( !topoPath.empty() )
    {
        topoOut = std::make_unique<GdalStreamingOutput>(
            QString::fromStdString( topoPath ), width, height, 1, GDT_Float32, gt,
            ifg.projection() );
        if ( !topoOut->isOpen() )
        {
            out.abandon();
            OCTDestroyCoordinateTransformation( toGeodetic );
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create topographic-phase raster: "
                                       + topoPath );
        }
        topoOut->setMetadataItem( sicnu::sar::kModalityKey, "sar" );
        topoOut->setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "topographic_phase" );
        topoOut->setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );
    }

    // --- Streaming tiles ------------------------------------------------
    std::vector<double> mapX( static_cast<size_t>( kTile ) * kTile );
    std::vector<double> mapY( static_cast<size_t>( kTile ) * kTile );
    std::vector<double> topo( static_cast<size_t>( kTile ) * kTile );
    std::vector<std::complex<float>> slc( static_cast<size_t>( kTile ) * kTile );
    std::vector<std::complex<float>> residual( static_cast<size_t>( kTile ) * kTile );
    std::vector<float> topoFloat( static_cast<size_t>( kTile ) * kTile );
    std::vector<float> demBuf;

    long long topoValid = 0;
    long long topoNaN = 0;
    long long coverageFailures = 0;

    const int tilesX = ( width + kTile - 1 ) / kTile;
    const int tilesY = ( height + kTile - 1 ) / kTile;
    const int totalTiles = tilesX * tilesY;
    int tileIndex = 0;

    for ( int ty = 0; ty < tilesY; ++ty )
    {
        for ( int tx = 0; tx < tilesX; ++tx, ++tileIndex )
        {
            context.throwIfCancelled();
            const int x0 = tx * kTile;
            const int y0 = ty * kTile;
            const int tw = std::min( kTile, width - x0 );
            const int th = std::min( kTile, height - y0 );
            const size_t n = static_cast<size_t>( tw ) * th;

            // Pixel-center map coordinates (GDAL corner-origin convention).
            for ( int y = 0; y < th; ++y )
                for ( int x = 0; x < tw; ++x )
                {
                    mapX[static_cast<size_t>( y ) * tw + x] =
                        gt[0] + ( x0 + x + 0.5 ) * gt[1];
                    mapY[static_cast<size_t>( y ) * tw + x] =
                        gt[3] + ( y0 + y + 0.5 ) * gt[5];
                }

            // Batch transform to WGS84 geodetic (lon/lat degrees). Points
            // the transform cannot map come back as HUGE_VAL — counted as
            // NaN outputs, never interpolated across.
            // Points the transform cannot map come back as HUGE_VAL (the
            // per-pixel finiteness checks below are authoritative).
            // Points the transform cannot map come back as HUGE_VAL (the
            // per-pixel finiteness checks below are authoritative).
            OCTTransform( toGeodetic, static_cast<int>( n ), mapX.data(),
                          mapY.data(), nullptr );

            // DEM window covering this tile's pixel centers (+1 px halo for
            // the bilinear stencil). The bbox comes from the PRE-transform
            // map coordinates — mapX/mapY already hold lon/lat degrees at
            // this point and would corrupt the pixel-space bbox.
            const auto demFrac = [&]( double mx, double my, double *cf, double *rf ) {
                *cf = ( mx - dgt[0] ) / dgt[1] - 0.5;
                *rf = ( my - dgt[3] ) / dgt[5] - 0.5;
            };
            const auto mapXat = [&]( int x ) {
                return gt[0] + ( x0 + x + 0.5 ) * gt[1];
            };
            const auto mapYat = [&]( int y ) {
                return gt[3] + ( y0 + y + 0.5 ) * gt[5];
            };
            double cMin = 1e308, cMax = -1e308, rMin = 1e308, rMax = -1e308;
            for ( const auto &corner : { std::pair<int, int>{ 0, 0 },
                                         std::pair<int, int>{ tw - 1, 0 },
                                         std::pair<int, int>{ 0, th - 1 },
                                         std::pair<int, int>{ tw - 1, th - 1 } } )
            {
                double cf = 0.0, rf = 0.0;
                demFrac( mapXat( corner.first ), mapYat( corner.second ), &cf, &rf );
                cMin = std::min( cMin, cf );
                cMax = std::max( cMax, cf );
                rMin = std::min( rMin, rf );
                rMax = std::max( rMax, rf );
            }
            const int c0 = std::max( 0, static_cast<int>( std::floor( cMin ) - 1 ) );
            const int r0 = std::max( 0, static_cast<int>( std::floor( rMin ) - 1 ) );
            const int c1 =
                std::min( dem.width() - 1, static_cast<int>( std::ceil( cMax ) ) + 1 );
            const int r1 =
                std::min( dem.height() - 1, static_cast<int>( std::ceil( rMax ) ) + 1 );
            const int wc = c1 - c0 + 1;
            const int hc = r1 - r0 + 1;
            demBuf.resize( static_cast<size_t>( wc ) * hc );
            if ( !dem.readBandWindow( demBand, c0, r0, wc, hc, demBuf.data() ) )
            {
                out.abandon();
                if ( !topoPath.empty() )
                    topoOut->abandon();
                OCTDestroyCoordinateTransformation( toGeodetic );
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read the DEM window at tile (" +
                                           std::to_string( tx ) + ", "
                                           + std::to_string( ty ) + ")" );
            }

            // Topographic phase per pixel (kernel authority; per-pixel
            // failures stay NaN and are counted).
            for ( int y = 0; y < th; ++y )
            {
                for ( int x = 0; x < tw; ++x )
                {
                    const size_t idx = static_cast<size_t>( y ) * tw + x;
                    const double lon = mapX[idx];
                    const double lat = mapY[idx];
                    if ( !std::isfinite( lon ) || !std::isfinite( lat ) )
                    {
                        topo[idx] = std::numeric_limits<double>::quiet_NaN();
                        ++topoNaN;
                        continue;
                    }
                    double cf = 0.0, rf = 0.0;
                    demFrac( gt[0] + ( x0 + x + 0.5 ) * gt[1],
                             gt[3] + ( y0 + y + 0.5 ) * gt[5], &cf, &rf );
                    const double heightM =
                        sampleBilinearNaN( demBuf.data(), wc, hc, cf - c0, rf - r0 );
                    sicnu::sar::GeodeticPoint ground;
                    ground.latDeg = lat;
                    ground.lonDeg = lon;
                    ground.heightM = heightM;
                    QString kernelError;
                    if ( sicnu::sar::topographicPhaseAtGround(
                             masterOrbit, slaveOrbit, wavelengthM, ground,
                             &topo[idx], &kernelError ) )
                        ++topoValid;
                    else
                    {
                        topo[idx] = std::numeric_limits<double>::quiet_NaN();
                        ++topoNaN;
                        if ( kernelError.contains( "NO_ZERO_DOPPLER" ) )
                            ++coverageFailures;
                    }
                }
            }

            // Rotate the complex samples: φ_residual = φ_ifg − φ_topo.
            if ( !ifg.readBandWindowNative( band, x0, y0, tw, th,
                                            static_cast<void *>( slc.data() ) ) )
            {
                out.abandon();
                if ( !topoPath.empty() )
                    topoOut->abandon();
                OCTDestroyCoordinateTransformation( toGeodetic );
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read the interferogram tile at (" +
                                           std::to_string( tx ) + ", "
                                           + std::to_string( ty ) + ")" );
            }
            for ( size_t i = 0; i < n; ++i )
            {
                const double phase = topo[i];
                if ( !std::isfinite( phase ) )
                {
                    residual[i] = { std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::quiet_NaN() };
                    continue;
                }
                residual[i] = slc[i] * std::polar( 1.0f, static_cast<float>( -phase ) );
                topoFloat[i] = static_cast<float>( phase );
            }

            if ( !sicnu::sar::writeComplexTile( out, 1, x0, y0, tw, th,
                                                residual.data() ) )
            {
                out.abandon();
                if ( !topoPath.empty() )
                    topoOut->abandon();
                OCTDestroyCoordinateTransformation( toGeodetic );
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to write the residual tile at (" +
                                           std::to_string( tx ) + ", "
                                           + std::to_string( ty ) + ")" );
            }
            if ( !topoPath.empty() && !topoOut->writeTile(
                     1, GdalBlockStream::Tile{ x0, y0, tw, th, 0, tw, th, 0, 1, tw, th },
                     topoFloat.data() ) )
            {
                out.abandon();
                topoOut->abandon();
                OCTDestroyCoordinateTransformation( toGeodetic );
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to write the topographic-phase tile at (" +
                                           std::to_string( tx ) + ", "
                                           + std::to_string( ty ) + ")" );
            }

            context.reportProgress( 0.9 * static_cast<double>( tileIndex + 1 )
                                        / totalTiles,
                                    "Removed topographic phase "
                                        + std::to_string( tileIndex + 1 ) + "/"
                                        + std::to_string( totalTiles ) + " tiles" );
        }
    }

    OCTDestroyCoordinateTransformation( toGeodetic );

    if ( topoValid == 0 )
    {
        out.abandon();
        if ( !topoPath.empty() )
            topoOut->abandon();
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "TOPO_PHASE_ORBIT_COVERAGE: not a single pixel got a computable "
            "topographic phase ("
                + std::to_string( coverageFailures )
                + " zero-Doppler coverage failures of " + std::to_string( topoNaN )
                + " invalid samples) — the DEM area lies outside both orbit "
                  "windows or the CRS transform failed" );
    }

    if ( !out.closeWithError() || ( !topoPath.empty() && !topoOut->closeWithError() ) )
    {
        out.abandon();
        if ( !topoPath.empty() )
            topoOut->abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize the topographic-phase outputs" );
    }

    Json::Value json;
    json["output"] = outputPath;
    if ( !topoPath.empty() )
        json["topoPhaseOutput"] = topoPath;
    json["wavelengthUm"] = wavelengthUm;
    json["wavelengthFromMetadata"] = wavelengthFromMetadata;
    json["evaluatedPixels"] = Json::Value::Int64(
        static_cast<long long>( width ) * height );
    json["topoValidPixels"] = Json::Value::Int64( topoValid );
    json["topoNaNPixels"] = Json::Value::Int64( topoNaN );
    context.reportProgress( 1.0, "Topographic phase removal complete" );
    return json;
}

} // namespace sicnu::operators::rs
