// tests/test_r33_t01_sentinels.cpp — finite NoData sentinel propagation
// through the InSAR chain operators (R33-T01, issue #2102). A declared
// finite sentinel (-9999 and friends) is a MISSING sample, never signal:
//
//  * rs:sar_network_inversion — per-pair sentinels drop the pair row at
//    that pixel (intersect drops the pixel, perpixel solves from the rest)
//    instead of regressing a -9999 m "displacement" through every output.
//  * rs:sar_displacement — sentinel phase stays NaN through the
//    phase→displacement conversion and out of the Itoh jump statistics.
//  * rs:sar_remove_topographic_phase — DEM sentinels are NaN-ed BEFORE the
//    unit scaling (a -9999 cm sentinel must not become -99.99 m of fake
//    relief); the fail-closed bilinear sampler then leaves the topo phase
//    NaN at the hole while valid neighbours compute normally.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <complex>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_orbit.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r33_t01_sentinels";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

constexpr float kSentinel = -9999.0f;

bool writeFloatRaster( const QString &path, const std::vector<float> &values, int width,
                       int height, const double *geoTransform = nullptr,
                       const char *projectionWkt = nullptr,
                       bool declareSentinel = false, float sentinel = kSentinel )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    if ( geoTransform )
        GDALSetGeoTransform( ds, const_cast<double *>( geoTransform ) );
    if ( projectionWkt )
        GDALSetProjection( ds, projectionWkt );
    if ( declareSentinel )
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ),
                                  static_cast<double>( sentinel ) );
    const bool ok = GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width,
                                  height, const_cast<float *>( values.data() ), width,
                                  height, GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> values( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, values.data(), ds.width(), ds.height() ) )
        return {};
    return values;
}

Json::Value runOperator( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

// --- remove_topographic_phase fixture (equatorial circular orbit) -------
constexpr double kOrbitRadius = 7000000.0;
constexpr double kOmega = 2.0 * M_PI / 5880.0;
constexpr double kLonCenterDeg = 105.0;
constexpr double kPhaseInit = kLonCenterDeg * M_PI / 180.0 - kOmega * 30.0;
constexpr double kDelta = 1000.0; // tangential slave offset (m)
constexpr int kDemSize = 5;
constexpr double kDemPixel = 10.0;
const double kDemGt[6] = { 500000.0 - 2.0 * kDemPixel, kDemPixel, 0.0,
                           2.0 * kDemPixel, 0.0, -kDemPixel };

sicnu::sar::OrbitSegment makeMasterOrbit()
{
    sicnu::sar::OrbitSegment orbit;
    for ( int i = 0; i <= 6; ++i )
    {
        const double t = 10.0 * i;
        const double phase = kOmega * t + kPhaseInit;
        sicnu::sar::OrbitStateVector s;
        s.t = t;
        s.x = kOrbitRadius * std::cos( phase );
        s.y = kOrbitRadius * std::sin( phase );
        s.z = 0.0;
        s.vx = -kOrbitRadius * kOmega * std::sin( phase );
        s.vy = kOrbitRadius * kOmega * std::cos( phase );
        s.vz = 0.0;
        orbit.states.push_back( s );
    }
    return orbit;
}

QString encodeOrbit( const sicnu::sar::OrbitSegment &orbit )
{
    QStringList records;
    for ( const sicnu::sar::OrbitStateVector &s : orbit.states )
        records << QString::number( s.t, 'g', 17 ) + ";" + QString::number( s.x, 'g', 17 )
                       + ";" + QString::number( s.y, 'g', 17 ) + ";"
                       + QString::number( s.z, 'g', 17 ) + ";"
                       + QString::number( s.vx, 'g', 17 ) + ";"
                       + QString::number( s.vy, 'g', 17 ) + ";"
                       + QString::number( s.vz, 'g', 17 );
    return records.join( QLatin1Char( '|' ) );
}

bool writeTopoInputs( QTemporaryDir &tmp, const std::vector<float> &demValues,
                      QString *ifgPath, QString *demPath )
{
    *ifgPath = tmp.filePath( "ifg.tif" );
    *demPath = tmp.filePath( "dem.tif" );
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;

    // North-up UTM geotransform + EPSG:32648 (the operator refuses rasters
    // without an axis-aligned geotransform).
    char *utmWkt = nullptr;
    {
        OGRSpatialReference srs;
        if ( srs.importFromEPSG( 32648 ) == OGRERR_NONE )
            srs.exportToWkt( &utmWkt );
    }
    if ( !utmWkt )
        return false;

    // Unit-amplitude complex interferogram, phase 0 everywhere.
    GDALDatasetH ifg = GDALCreate( driver, ifgPath->toUtf8().constData(), kDemSize,
                                   kDemSize, 1, GDT_CFloat32, nullptr );
    if ( !ifg )
        return false;
    GDALSetGeoTransform( ifg, const_cast<double *>( kDemGt ) );
    GDALSetProjection( ifg, utmWkt );
    GDALSetMetadataItem( ifg, "SICNU_SAR_WAVELENGTH_UM", "55500", nullptr );
    std::vector<std::complex<float>> samples( static_cast<size_t>( kDemSize ) * kDemSize,
                                              { 1.0f, 0.0f } );
    const bool ifgOk = GDALRasterIO( GDALGetRasterBand( ifg, 1 ), GF_Write, 0, 0,
                                     kDemSize, kDemSize, samples.data(), kDemSize,
                                     kDemSize, GDT_CFloat32, 0, 0 ) == CE_None;
    GDALClose( ifg );

    // DEM in CENTIMETRES with a declared sentinel hole.
    GDALDatasetH dem = GDALCreate( driver, demPath->toUtf8().constData(), kDemSize,
                                   kDemSize, 1, GDT_Float32, nullptr );
    bool demOk = false;
    if ( dem )
    {
        GDALSetGeoTransform( dem, const_cast<double *>( kDemGt ) );
        GDALSetProjection( dem, utmWkt );
        GDALSetMetadataItem( dem, "SICNU_DEM_UNIT", "cm", nullptr );
        GDALSetRasterNoDataValue( GDALGetRasterBand( dem, 1 ),
                                  static_cast<double>( kSentinel ) );
        demOk = GDALRasterIO( GDALGetRasterBand( dem, 1 ), GF_Write, 0, 0,
                              kDemSize, kDemSize, const_cast<float *>( demValues.data() ),
                              kDemSize, kDemSize, GDT_Float32, 0, 0 ) == CE_None;
        GDALClose( dem );
    }
    CPLFree( utmWkt );
    return ifgOk && demOk;
}

} // anonymous namespace

TEST_CASE( "rs:sar_network_inversion treats pair sentinels as missing (#2102)",
           "[r33t01][sar][sentinel]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    constexpr int kN = 5;

    // Linear truth u = [0, 1, 2, 3] mm on t = (0,1,2,3); pairs
    // (1,0) (2,0) (3,0) (2,1) (3,2).
    const double u[] = { 0.0, 1e-3, 2e-3, 3e-3 };
    const int pairMaster[] = { 1, 2, 3, 2, 3 };
    const int pairSlave[] = { 0, 0, 0, 1, 2 };

    Json::Value inputs( Json::arrayValue );
    for ( int p = 0; p < 5; ++p )
    {
        std::vector<float> plane( static_cast<size_t>( kN ) * kN,
                                  static_cast<float>( u[pairMaster[p]] - u[pairSlave[p]] ) );
        // Hole: sentinel at pixel 0 of the LAST pair (a different declared
        // sentinel per raster proves per-band reading).
        const float sentinel = kSentinel - p;
        if ( p == 4 )
            plane[0] = sentinel;
        const QString path = tmp.filePath( QString( "d_%1.tif" ).arg( p ) );
        REQUIRE( writeFloatRaster( path, plane, kN, kN, nullptr, nullptr, true, sentinel ) );
        inputs.append( path.toStdString() );
    }
    Json::Value pairsJson( Json::arrayValue );
    for ( int p = 0; p < 5; ++p )
    {
        Json::Value pair( Json::arrayValue );
        pair.append( pairMaster[p] );
        pair.append( pairSlave[p] );
        pairsJson.append( pair );
    }
    Json::Value temporal( Json::arrayValue );
    for ( const double t : { 0.0, 1.0, 2.0, 3.0 } )
        temporal.append( t );

    // perpixel: pixel 0 solves from the 4 remaining pairs — same truth.
    Json::Value params( Json::objectValue );
    params["displacementInputs"] = inputs;
    params["pairs"] = pairsJson;
    params["epochTemporalYears"] = temporal;
    params["maskStrategy"] = "perpixel";
    params["velocityOutput"] = tmp.filePath( "velocity_p.tif" ).toStdString();
    Json::Value result = runOperator( "rs:sar_network_inversion", params );
    REQUIRE( result["noDataSentinelSamples"].asInt64() == 1 );
    const std::vector<float> velocityP =
        readBand( QString::fromStdString( result["velocityOutput"].asString() ) );
    REQUIRE( velocityP.size() == static_cast<size_t>( kN ) * kN );
    for ( const float v : velocityP )
        REQUIRE( v == Approx( 1e-3f ).margin( 1e-9f ) ); // sentinel did NOT bias the fit

    // intersect: the sentinel pixel drops out entirely (NaN velocity).
    params["maskStrategy"] = "intersect";
    params["velocityOutput"] = tmp.filePath( "velocity_i.tif" ).toStdString();
    Json::Value resultI = runOperator( "rs:sar_network_inversion", params );
    REQUIRE( resultI["noDataSentinelSamples"].asInt64() == 1 );
    REQUIRE( resultI["intersectDroppedPixels"].asInt64() == 1 );
    const std::vector<float> velocityI =
        readBand( QString::fromStdString( resultI["velocityOutput"].asString() ) );
    REQUIRE( std::isnan( velocityI[0] ) );
    REQUIRE( velocityI[7] == Approx( 1e-3f ).margin( 1e-9f ) );
}

TEST_CASE( "rs:sar_displacement keeps sentinel phase NaN (#2102)",
           "[r33t01][sar][sentinel]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    constexpr int kN = 4;

    // Uniform phase π/2 with one sentinel hole at (1, 1).
    std::vector<float> phase( static_cast<size_t>( kN ) * kN,
                              static_cast<float>( M_PI / 2.0 ) );
    phase[static_cast<size_t>( kN ) + 1] = kSentinel;
    const QString input = tmp.filePath( "phase.tif" );
    REQUIRE( writeFloatRaster( input, phase, kN, kN, nullptr, nullptr, true, kSentinel ) );

    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = tmp.filePath( "disp.tif" ).toStdString();
    params["wavelengthUm"] = 55500.0;
    Json::Value result = runOperator( "rs:sar_displacement", params );

    // Sentinel pixel is NaN, valid pixels convert exactly:
    // d = -λ·φ/(4π) with λ = 0.0555 m, φ = π/2 → -0.0069375 m.
    const std::vector<float> disp =
        readBand( QString::fromStdString( result["output"].asString() ) );
    REQUIRE( disp.size() == static_cast<size_t>( kN ) * kN );
    const size_t hole = static_cast<size_t>( kN ) + 1;
    REQUIRE( std::isnan( disp[hole] ) );
    for ( size_t i = 0; i < disp.size(); ++i )
        if ( i != hole )
            REQUIRE( disp[i] == Approx( -0.0555 * ( M_PI / 2.0 ) / ( 4.0 * M_PI ) ).margin( 1e-9 ) );
    // Counts: 15 valid, 1 sentinel; the sentinel pair never entered the
    // Itoh statistics.
    REQUIRE( result["validPixels"].asInt64() == kN * kN - 1 );
    REQUIRE( result["noDataSentinelPixels"].asInt64() == 1 );
    REQUIRE( result["phaseDiscontinuityRatio"].asDouble() == Approx( 0.0 ).margin( 1e-12 ) );
}

TEST_CASE( "rs:sar_remove_topographic_phase NaNs DEM sentinels before unit scale (#2102)",
           "[r33t01][sar][sentinel][topo]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // DEM in cm (5000 cm = 50 m), sentinel hole at the CENTER pixel.
    std::vector<float> dem( static_cast<size_t>( kDemSize ) * kDemSize, 5000.0f );
    dem[static_cast<size_t>( kDemSize ) * ( kDemSize / 2 ) + kDemSize / 2] = kSentinel;

    QString ifgPath, demPath;
    REQUIRE( writeTopoInputs( tmp, dem, &ifgPath, &demPath ) );

    sicnu::sar::OrbitSegment master = makeMasterOrbit();
    const sicnu::sar::OrbitSegment slave = [&master] {
        sicnu::sar::OrbitSegment orbit = master;
        const double phase0 = kLonCenterDeg * M_PI / 180.0;
        for ( sicnu::sar::OrbitStateVector &s : orbit.states )
        {
            s.x += -kDelta * std::sin( phase0 );
            s.y += kDelta * std::cos( phase0 );
        }
        return orbit;
    }();

    Json::Value params( Json::objectValue );
    params["interferogram"] = ifgPath.toStdString();
    params["dem"] = demPath.toStdString();
    params["masterOrbitStates"] = encodeOrbit( master ).toStdString();
    params["slaveOrbitStates"] = encodeOrbit( slave ).toStdString();
    params["output"] = tmp.filePath( "residual.tif" ).toStdString();
    params["topoPhaseOutput"] = tmp.filePath( "topo.tif" ).toStdString();
    Json::Value result = runOperator( "rs:sar_remove_topographic_phase", params );

    // Exactly the hole pixel is unresolvable (output grid == DEM grid, the
    // bilinear sampler fails closed on the sentinel tap); every neighbour
    // resolves against the honest 50 m shell. Pre-fix, the sentinel was
    // scaled to -99.99 m of fake relief and ALL 25 pixels "resolved".
    REQUIRE( result["noDataSentinelSamples"].asInt64() == 1 );
    REQUIRE( result["topoNaNPixels"].asInt64() == 1 );
    REQUIRE( result["topoValidPixels"].asInt64() == kDemSize * kDemSize - 1 );

    const std::vector<float> topo = readBand( tmp.filePath( "topo.tif" ) );
    const size_t hole =
        static_cast<size_t>( kDemSize ) * ( kDemSize / 2 ) + kDemSize / 2;
    REQUIRE( topo.size() == static_cast<size_t>( kDemSize ) * kDemSize );
    REQUIRE( std::isnan( topo[hole] ) );
    for ( size_t i = 0; i < topo.size(); ++i )
        if ( i != hole )
        {
            INFO( "pixel " << i );
            REQUIRE( std::isfinite( topo[i] ) );
        }

    // The residual inherits the NaN at the hole (rotation of a NaN phase
    // must not fabricate a sample) — read the complex band natively.
    std::vector<std::complex<float>> residualComplex(
        static_cast<size_t>( kDemSize ) * kDemSize );
    {
        GdalDatasetWrapper outDs;
        REQUIRE( outDs.open( tmp.filePath( "residual.tif" ) ) );
        REQUIRE( outDs.readBandWindowNative( 1, 0, 0, kDemSize, kDemSize,
                                             static_cast<void *>( residualComplex.data() ) ) );
    }
    REQUIRE( std::isnan( residualComplex[hole].real() ) );
    REQUIRE( std::isnan( residualComplex[hole].imag() ) );
}
