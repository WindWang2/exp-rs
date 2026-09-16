// tests/test_sar_platform11.cpp — Advanced InSAR 11.0 operator end-to-end
// known answers (package H).
//
// The centerpiece oracle is EXACT: an equatorial circular master orbit
// whose sub-satellite longitude is ωt + φinit, and a slave rigidly
// translated by a tangential Δ. On the equator every point is nadir for
// the master (LOS radial), so the zero-Doppler ranges have closed forms
// (test_sar_baseline.cpp / test_sar_topographic_phase.cpp): the
// interferogram is CONSTRUCTED from those analytic ranges, the operator
// removes the topographic phase it computes from a DEM + the orbit pair,
// and the residual must vanish to the numerical precision of the
// zero-Doppler solves. A 5×5-pixel DEM at 10 m keeps every pixel within
// ~3e-6 rad of longitude of the exact meridian, bounding the
// approximation error far below the assertion tolerance.
//
// The provider/network/inversion E2Es drive the real operators through
// the registry with GDAL fixtures, mirroring test_sar_platform10.cpp.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogr_srs_api.h>

#include <cmath>
#include <iostream>
#include <complex>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_baseline.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_topographic_phase.h"
#include "processing/algorithms/sar/sar_orbit.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;
using namespace sicnu::operators;
using namespace sicnu::sar;

#ifndef FAKE_PROVIDER_PATH
#define FAKE_PROVIDER_PATH "missing-fake-provider"
#endif

namespace
{
int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_platform11";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

constexpr double kOrbitRadius = 7000000.0;
constexpr double kOmega = 2.0 * M_PI / 5880.0;
constexpr double kSemiMajor = Wgs84::kSemiMajor;
constexpr double kWavelengthUm = 55500.0;
constexpr double kWavelengthM = kWavelengthUm * 1e-6;
// Master crosses the DEM meridian (105°E, UTM 48N central meridian) at
// t = 30 s inside the [0, 60] s segment.
constexpr double kLonCenterDeg = 105.0;
constexpr double kPhaseInit = kLonCenterDeg * M_PI / 180.0 - kOmega * 30.0;
constexpr double kDelta = 1000.0; // tangential slave offset (m)
constexpr double kDemHeight = 50.0;

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

sicnu::sar::OrbitSegment makeSlaveOrbit()
{
    const double phase0 = kLonCenterDeg * M_PI / 180.0;
    sicnu::sar::OrbitSegment orbit = makeMasterOrbit();
    for ( sicnu::sar::OrbitStateVector &s : orbit.states )
    {
        s.x += -kDelta * std::sin( phase0 );
        s.y += kDelta * std::cos( phase0 );
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

double wrapDiff( double a, double b )
{
    double d = std::fmod( a - b + M_PI, 2.0 * M_PI );
    if ( d < 0.0 )
        d += 2.0 * M_PI;
    return d - M_PI;
}

/// 5×5 DEM, 10 m pixels, centered on (lat 0, lon 105°E) in EPSG:32648
// (origin 2 pixels north/west of the center so pixel centers straddle it).
constexpr int kDemSize = 5;
constexpr double kDemPixel = 10.0;
const double kDemGt[6] = { 500000.0 - 2.0 * kDemPixel, kDemPixel, 0.0,
                           2.0 * kDemPixel, 0.0, -kDemPixel };

bool writeFloatRaster( const QString &path, const std::vector<float> &values, int width,
                       int height, const double *geoTransform, const char *projectionWkt )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( geoTransform ) );
    GDALSetProjection( ds, projectionWkt );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                  const_cast<float *>( values.data() ), width, height,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

template <typename F>
bool writeComplexRaster( const QString &path, int width, int height, F fill,
                         const double *geoTransform, const char *projectionWkt,
                         const char *wavelengthUm = nullptr )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds =
        GDALCreate( driver, path.toUtf8().constData(), width, height, 1, GDT_CFloat32,
                    nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( geoTransform ) );
    GDALSetProjection( ds, projectionWkt );
    if ( wavelengthUm )
        GDALSetMetadataItem( ds, "SICNU_SAR_WAVELENGTH_UM", wavelengthUm, nullptr );
    bool ok = true;
    std::vector<std::complex<float>> row( static_cast<size_t>( width ) );
    for ( int y = 0; y < height && ok; ++y )
    {
        for ( int x = 0; x < width; ++x )
            row[static_cast<size_t>( x )] = fill( x, y );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        ok = GDALRasterIO( band, GF_Write, 0, y, width, 1, row.data(), width, 1,
                           GDT_CFloat32, 0, 0 ) == CE_None;
    }
    GDALClose( ds );
    return ok;
}

char *utm48nWkt()
{
    static char *wkt = nullptr;
    if ( !wkt )
    {
        OGRSpatialReference srs;
        if ( srs.importFromEPSG( 32648 ) == OGRERR_NONE )
            srs.exportToWkt( &wkt );
    }
    return wkt;
}

/// Per-pixel geodetic positions of the DEM grid via the OSR authority.
bool demPixelGeodetic( int x, int y, double *latDeg, double *lonDeg )
{
    OGRSpatialReference utm;
    OGRSpatialReference wgs84;
    utm.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER );
    wgs84.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER );
    if ( utm.importFromEPSG( 32648 ) != OGRERR_NONE )
        return false;
    wgs84.SetWellKnownGeogCS( "WGS84" );
    OGRCoordinateTransformation *ct =
        OGRCreateCoordinateTransformation( &utm, &wgs84 );
    if ( !ct )
        return false;
    double mapX = kDemGt[0] + ( x + 0.5 ) * kDemGt[1];
    double mapY = kDemGt[3] + ( y + 0.5 ) * kDemGt[5];
    const bool ok = ct->Transform( 1, &mapX, &mapY );
    OCTDestroyCoordinateTransformation(
        OGRCoordinateTransformation::ToHandle( ct ) );
    if ( !ok )
        return false;
    *lonDeg = mapX;
    *latDeg = mapY;
    return true;
}

// --- Tier-2 independent oracle: test-local circular-orbit zero Doppler
// --- (true analytic circle; the implementation interpolates Hermite
// --- states, so no code or numbers are shared).

struct EcefPoint
{
    double x, y, z;
};

EcefPoint oraclePos( double t )
{
    const double phase = kOmega * t + kPhaseInit;
    return { kOrbitRadius * std::cos( phase ), kOrbitRadius * std::sin( phase ), 0.0 };
}
EcefPoint oracleVel( double t )
{
    const double phase = kOmega * t + kPhaseInit;
    return { -kOrbitRadius * kOmega * std::sin( phase ),
             kOrbitRadius * kOmega * std::cos( phase ), 0.0 };
}
double oracleDoppler( const EcefPoint &p, const EcefPoint &delta, double t )
{
    const EcefPoint s = oraclePos( t );
    const EcefPoint v = oracleVel( t );
    return ( s.x + delta.x - p.x ) * v.x + ( s.y + delta.y - p.y ) * v.y
           + ( s.z + delta.z - p.z ) * v.z;
}
bool oracleRange( const EcefPoint &p, const EcefPoint &delta, double tHint,
                  double *rangeM )
{
    const double halfPeriod = M_PI / kOmega;
    const double lo = tHint - 0.25 * halfPeriod;
    const double hi = tHint + 0.25 * halfPeriod;
    double fPrev = oracleDoppler( p, delta, lo );
    double a = lo, b = hi;
    bool bracketed = false;
    const int scanSteps = 64;
    for ( int i = 1; i <= scanSteps; ++i )
    {
        const double t = lo + ( hi - lo ) * i / scanSteps;
        const double f = oracleDoppler( p, delta, t );
        if ( ( fPrev <= 0.0 && f > 0.0 ) || ( fPrev >= 0.0 && f < 0.0 ) )
        {
            a = lo + ( hi - lo ) * ( i - 1 ) / scanSteps;
            b = t;
            bracketed = true;
            break;
        }
        fPrev = f;
    }
    if ( !bracketed )
        return false;
    for ( int i = 0; i < 80; ++i )
    {
        const double m = 0.5 * ( a + b );
        if ( ( oracleDoppler( p, delta, a ) <= 0.0 )
             == ( oracleDoppler( p, delta, m ) <= 0.0 ) )
            a = m;
        else
            b = m;
    }
    const double t = 0.5 * ( a + b );
    EcefPoint s = oraclePos( t );
    s.x += delta.x;
    s.y += delta.y;
    *rangeM = std::sqrt( ( s.x - p.x ) * ( s.x - p.x ) + ( s.y - p.y ) * ( s.y - p.y )
                         + ( s.z - p.z ) * ( s.z - p.z ) );
    return true;
}
double wrapPhase( double phase )
{
    if ( !std::isfinite( phase ) )
        return phase;
    double wrapped = std::fmod( phase + M_PI, 2.0 * M_PI );
    if ( wrapped < 0.0 )
        wrapped += 2.0 * M_PI;
    return wrapped - M_PI;
}

Json::Value runOperator( const std::string &id, const Json::Value &params,
                         RSOperatorContext &ctx )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    return op->run( params, ctx );
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, out.data(), ds.width(), ds.height() ) )
        return {};
    return out;
}
} // namespace

TEST_CASE( "rs:sar_remove_topographic_phase leaves a vanishing residual on "
           "an analytic interferogram", "[sar][insar11][operator][topo]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Analytic forward model on the DEM grid, PER PIXEL: the independent
    // Tier-2 oracle (true-circle scan+bisect) provides each pixel's exact
    // topographic phase for the DEM height; the interferogram is
    // CONSTRUCTED from that same oracle, so the removal residual must
    // vanish to the zero-Doppler solve precision.
    const sicnu::sar::OrbitSegment master = makeMasterOrbit();
    const sicnu::sar::OrbitSegment slave = makeSlaveOrbit();
    const EcefPoint slaveDelta{ -kDelta * std::sin( kLonCenterDeg * M_PI / 180.0 ),
                                kDelta * std::cos( kLonCenterDeg * M_PI / 180.0 ), 0.0 };
    const EcefPoint noDelta{ 0.0, 0.0, 0.0 };

    std::vector<double> expectedPhase( static_cast<size_t>( kDemSize ) * kDemSize );
    for ( int y = 0; y < kDemSize; ++y )
        for ( int x = 0; x < kDemSize; ++x )
        {
            double lat = 0.0, lon = 0.0;
            REQUIRE( demPixelGeodetic( x, y, &lat, &lon ) );
            double px = 0.0, py = 0.0, pz = 0.0;
            Wgs84::geodeticToEcef( lat, lon, kDemHeight, &px, &py, &pz );
            const EcefPoint p{ px, py, pz };
            double rMaster = 0.0, rSlave = 0.0;
            REQUIRE( oracleRange( p, noDelta, 30.0, &rMaster ) );
            REQUIRE( oracleRange( p, slaveDelta, 30.0, &rSlave ) );
            expectedPhase[static_cast<size_t>( y ) * kDemSize + x] =
                wrapPhase( -4.0 * M_PI * ( rMaster - rSlave ) / kWavelengthM );
        }

    // Complex interferogram carrying EXACTLY the per-pixel analytic
    // topographic phase (unit amplitude); after removal the residual must
    // vanish.
    const QString ifgPath = tmp.filePath( "ifg.tif" );
    const QString demPath = tmp.filePath( "dem.tif" );
    const QString outPath = tmp.filePath( "residual.tif" );
    REQUIRE( writeComplexRaster(
        ifgPath, kDemSize, kDemSize,
        [ & ]( int x, int y ) {
            const double phase =
                expectedPhase[static_cast<size_t>( y ) * kDemSize + x];
            return std::complex<float>( static_cast<float>( std::cos( phase ) ),
                                        static_cast<float>( std::sin( phase ) ) );
        },
        kDemGt, utm48nWkt(), "55500" ) );
    REQUIRE( writeFloatRaster( demPath, std::vector<float>( kDemSize * kDemSize,
                                                           static_cast<float>( kDemHeight ) ),
                               kDemSize, kDemSize, kDemGt, utm48nWkt() ) );

    Json::Value params( Json::objectValue );
    params["interferogram"] = ifgPath.toStdString();
    params["dem"] = demPath.toStdString();
    params["masterOrbitStates"] = encodeOrbit( master ).toStdString();
    params["slaveOrbitStates"] = encodeOrbit( slave ).toStdString();
    params["output"] = outPath.toStdString();
    params["topoPhaseOutput"] = tmp.filePath( "topo.tif" ).toStdString();

    RSOperatorContext ctx;
    Json::Value result = runOperator( "rs:sar_remove_topographic_phase", params, ctx );
    REQUIRE( result["topoValidPixels"].asInt64() == kDemSize * kDemSize );

    std::vector<std::complex<float>> residualComplex(
        static_cast<size_t>( kDemSize ) * kDemSize );
    {
        GdalDatasetWrapper outDs;
        REQUIRE( outDs.open( outPath ) );
        REQUIRE( outDs.readBandWindowNative( 1, 0, 0, kDemSize, kDemSize,
                                             static_cast<void *>(
                                                 residualComplex.data() ) ) );
    }
    // Per-pixel INDEPENDENT oracle: test-local circular-orbit state
    // functions + scan/bisect zero-Doppler solve (no code shared with the
    // implementation). The interferogram was CONSTRUCTED from the same
    // oracle, so the residual must vanish to the zero-Doppler solve
    // precision (~2e-4 rad) plus the small DEM-spread terms (< 1e-3 rad).
    for ( int y = 0; y < kDemSize; ++y )
    {
        for ( int x = 0; x < kDemSize; ++x )
        {
            const std::complex<float> z =
                residualComplex[static_cast<size_t>( y ) * kDemSize + x];
            REQUIRE( std::isfinite( z.real() ) );
            double lat = 0.0, lon = 0.0;
            REQUIRE( demPixelGeodetic( x, y, &lat, &lon ) );
            double px = 0.0, py = 0.0, pz = 0.0;
            Wgs84::geodeticToEcef( lat, lon, kDemHeight, &px, &py, &pz );
            const EcefPoint p{ px, py, pz };
            const double residualPhase = std::atan2( static_cast<double>( z.imag() ),
                                                     static_cast<double>( z.real() ) );
            // Vanishing residual: the kernel removes ITS OWN per-pixel
            // phase for the same geometry; the difference to the oracle
            // construction is the zero-Doppler solve + interpolation error
            // (well below the bound).
            REQUIRE( std::abs( wrapDiff( residualPhase, 0.0 ) ) < 0.02 );
        }
    }

    // No-wavelength refusal: strip the metadata and drop the parameter.
    const QString ifgNoLambda = tmp.filePath( "ifg_nolambda.tif" );
    REQUIRE( writeComplexRaster(
        ifgNoLambda, kDemSize, kDemSize,
        [] ( int, int ) { return std::complex<float>( 1.0f, 0.0f ); }, kDemGt,
        utm48nWkt() ) );
    Json::Value noLambda = params;
    noLambda["interferogram"] = ifgNoLambda.toStdString();
    RSOperatorContext ctx2;
    REQUIRE_THROWS_AS( runOperator( "rs:sar_remove_topographic_phase", noLambda, ctx2 ),
                       RSOperatorError );

    // CRS mismatch refusal: a DEM in plain geographic WGS84.
    const char *wgs84Wkt = "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\","
                           "6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
                           "UNIT[\"degree\",0.0174532925199433]]";
    const QString demWgs84 = tmp.filePath( "dem_wgs84.tif" );
    REQUIRE( writeFloatRaster( demWgs84,
                               std::vector<float>( kDemSize * kDemSize,
                                                   static_cast<float>( kDemHeight ) ),
                               kDemSize, kDemSize, kDemGt, wgs84Wkt ) );
    Json::Value crsMismatch = params;
    crsMismatch["dem"] = demWgs84.toStdString();
    RSOperatorContext ctx3;
    try
    {
        runOperator( "rs:sar_remove_topographic_phase", crsMismatch, ctx3 );
        FAIL( "DEM CRS mismatch must be refused" );
    }
    catch ( const RSOperatorError &error )
    {
        REQUIRE( std::string( error.message() ).find( "DEM_CRS_MISMATCH" )
                 != std::string::npos );
    }
}

TEST_CASE( "rs:sar_unwrap external provider runs end-to-end and refuses "
           "missing binaries", "[sar][insar11][operator][provider]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Simple wrapped phase ramp; the fake provider echoes it (identity
    // "unwrap" — the provider contract, not unwrapping quality).
    const QString ifgPath = tmp.filePath( "ifg.tif" );
    const QString outPath = tmp.filePath( "unwrapped.tif" );
    constexpr int kN = 8;
    REQUIRE( writeComplexRaster(
        ifgPath, kN, kN,
        []( int x, int y ) {
            const double phase = 0.1 * ( x + y );
            return std::complex<float>( static_cast<float>( std::cos( phase ) ),
                                        static_cast<float>( std::sin( phase ) ) );
        },
        kDemGt, utm48nWkt() ) );

    Json::Value params( Json::objectValue );
    params["input"] = ifgPath.toStdString();
    params["output"] = outPath.toStdString();
    params["provider"] = "fakeunwrap";
    params["providerBin"] = FAKE_PROVIDER_PATH;
    Json::Value args( Json::arrayValue );
    args.append( "{input}" );
    args.append( "{output}" );
    args.append( "{width}" );
    params["providerArgs"] = args;
    params["providerTimeoutSec"] = 60.0;

    RSOperatorContext ctx;
    Json::Value result = runOperator( "rs:sar_unwrap", params, ctx );
    REQUIRE( result["provider"].asString() == "fakeunwrap" );
    REQUIRE( result["unwrappedPixels"].asInt64() == kN * kN );

    // The echo must equal the input phase sample by sample.
    GdalDatasetWrapper inDs;
    REQUIRE( inDs.open( ifgPath ) );
    std::vector<std::complex<float>> inPlane( static_cast<size_t>( kN ) * kN );
    REQUIRE( inDs.readBandWindowNative( 1, 0, 0, kN, kN,
                                        static_cast<void *>( inPlane.data() ) ) );
    const std::vector<float> unwrapped = readBand( outPath );
    for ( size_t i = 0; i < unwrapped.size(); ++i )
        REQUIRE( unwrapped[i]
                 == Approx( std::atan2( inPlane[i].imag(), inPlane[i].real() ) )
                        .margin( 1e-5 ) );

    // Missing binary: typed refusal, no builtin fallback.
    Json::Value missing = params;
    missing["providerBin"] = "Z:/definitely/not/here/nowhere.exe";
    RSOperatorContext ctx2;
    try
    {
        runOperator( "rs:sar_unwrap", missing, ctx2 );
        FAIL( "missing provider binary must be refused" );
    }
    catch ( const RSOperatorError &error )
    {
        REQUIRE( std::string( error.message() ).find( "UNWRAP_PROVIDER_UNAVAILABLE" )
                 != std::string::npos );
    }
}

TEST_CASE( "rs:sar_pair_network builds, filters, and refuses through the "
           "registry", "[sar][insar11][operator][network]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    auto sceneJson = []( double utcDays, double shift ) {
        const sicnu::sar::OrbitSegment orbit = [ shift ] {
            const double phase0 = kLonCenterDeg * M_PI / 180.0;
            sicnu::sar::OrbitSegment o;
            for ( int i = 0; i <= 6; ++i )
            {
                const double t = 10.0 * i;
                const double phase = kOmega * t + kPhaseInit;
                sicnu::sar::OrbitStateVector s;
                s.t = t;
                s.x = kOrbitRadius * std::cos( phase ) - shift * std::sin( phase0 );
                s.y = kOrbitRadius * std::sin( phase ) + shift * std::cos( phase0 );
                s.z = 0.0;
                s.vx = -kOrbitRadius * kOmega * std::sin( phase );
                s.vy = kOrbitRadius * kOmega * std::cos( phase );
                s.vz = 0.0;
                o.states.push_back( s );
            }
            return o;
        }();
        Json::Value scene( Json::objectValue );
        scene["acquisitionUtcSec"] = 1700000000.0 + utcDays * 86400.0;
        scene["wavelengthUm"] = kWavelengthUm;
        scene["orbitStates"] = encodeOrbit( orbit ).toStdString();
        return scene;
    };

    Json::Value scenes( Json::arrayValue );
    scenes.append( sceneJson( 0.0, 0.0 ) );
    scenes.append( sceneJson( 12.0, 500.0 ) );
    scenes.append( sceneJson( 24.0, 1000.0 ) );

    Json::Value params( Json::objectValue );
    params["scenes"] = scenes;
    Json::Value pairs( Json::arrayValue );
    pairs.append( 0 );
    pairs.append( 1 );
    (void)pairs;

    RSOperatorContext ctx;
    Json::Value result = runOperator( "rs:sar_pair_network", params, ctx );
    REQUIRE( result["connected"].asBool() );
    REQUIRE( result["pairs"].size() == 3 );

    // Wavelength mismatch refuses.
    Json::Value mixedScenes = scenes;
    mixedScenes[1]["wavelengthUm"] = 54000.0;
    Json::Value mixed = params;
    mixed["scenes"] = mixedScenes;
    RSOperatorContext ctx2;
    REQUIRE_THROWS_AS( runOperator( "rs:sar_pair_network", mixed, ctx2 ), RSOperatorError );

    // Disconnected refuses; explicit allowance returns the component map.
    Json::Value farScenes( Json::arrayValue );
    farScenes.append( sceneJson( 0.0, 0.0 ) );
    farScenes.append( sceneJson( 12.0, 500.0 ) );
    farScenes.append( sceneJson( 400.0, 0.0 ) );
    Json::Value disconnected = params;
    disconnected["scenes"] = farScenes;
    disconnected["maxTemporalDays"] = 30.0;
    RSOperatorContext ctx3;
    REQUIRE_THROWS_AS( runOperator( "rs:sar_pair_network", disconnected, ctx3 ),
                       RSOperatorError );
    disconnected["allowDisconnected"] = true;
    RSOperatorContext ctx4;
    Json::Value allowed = runOperator( "rs:sar_pair_network", disconnected, ctx4 );
    REQUIRE( !allowed["connected"].asBool() );
    REQUIRE( allowed["componentCount"].asInt() == 2 );
}

TEST_CASE( "rs:sar_network_inversion recovers constructed epoch displacements "
           "end-to-end", "[sar][insar11][operator][inversion]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const char *wkt = utm48nWkt();
    constexpr int kN = 6;
    const double gt[6] = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };

    // Truth: u = [0, 1, 2, 3] mm per epoch (linear, 1 mm/year on a
    // [0,1,2,3] year base); pairs (1,0) (2,0) (3,1) (3,2).
    const double u[] = { 0.0, 1e-3, 2e-3, 3e-3 };
    const int pairMaster[] = { 1, 2, 3, 3 };
    const int pairSlave[] = { 0, 0, 1, 2 };
    Json::Value inputs( Json::arrayValue );
    std::vector<QString> paths;
    for ( int p = 0; p < 4; ++p )
    {
        std::vector<float> plane( static_cast<size_t>( kN ) * kN );
        for ( auto &v : plane )
            v = static_cast<float>( u[pairMaster[p]] - u[pairSlave[p]] );
        const QString path = tmp.filePath( QString( "d_%1.tif" ).arg( p ) );
        REQUIRE( writeFloatRaster( path, plane, kN, kN, gt, wkt ) );
        inputs.append( path.toStdString() );
        paths.push_back( path );
    }

    Json::Value pairsJson( Json::arrayValue );
    for ( int p = 0; p < 4; ++p )
    {
        Json::Value pair( Json::arrayValue );
        pair.append( pairMaster[p] );
        pair.append( pairSlave[p] );
        pairsJson.append( pair );
    }
    Json::Value temporal( Json::arrayValue );
    for ( const double t : { 0.0, 1.0, 2.0, 3.0 } )
        temporal.append( t );

    const QString velocityPath = tmp.filePath( "velocity.tif" );
    const QString stackPath = tmp.filePath( "stack.tif" );
    Json::Value params( Json::objectValue );
    params["displacementInputs"] = inputs;
    params["pairs"] = pairsJson;
    params["epochTemporalYears"] = temporal;
    params["velocityOutput"] = velocityPath.toStdString();
    params["displacementOutput"] = stackPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = runOperator( "rs:sar_network_inversion", params, ctx );
    REQUIRE( result["solvedPixels"].asInt64() == kN * kN );

    const std::vector<float> velocity = readBand( velocityPath );
    for ( const float v : velocity )
        REQUIRE( v == Approx( 1e-3 ).margin( 1e-9 ) );

    // Epoch 3 band of the stack carries u = 3 mm.
    const std::vector<float> epoch3 = readBand( stackPath, 4 );
    for ( const float v : epoch3 )
        REQUIRE( v == Approx( 3e-3 ).margin( 1e-9 ) );

    // Missing-data handling: punch a NaN hole into pair 3's raster.
    // intersect → those pixels drop out of the velocity product.
    {
        GdalDatasetWrapper ds3;
        REQUIRE( ds3.open( paths[3] ) );
        std::vector<float> plane( static_cast<size_t>( kN ) * kN );
        REQUIRE( ds3.readBandData( 1, plane.data(), kN, kN ) );
        plane[0] = std::numeric_limits<float>::quiet_NaN();
        REQUIRE( writeFloatRaster( paths[3], plane, kN, kN, gt, wkt ) );
    }
    Json::Value intersectParams = params;
    intersectParams["maskStrategy"] = "intersect";
    intersectParams["velocityOutput"] = tmp.filePath( "velocity_i.tif" ).toStdString();
    RSOperatorContext ctx2;
    Json::Value r2 = runOperator( "rs:sar_network_inversion", intersectParams, ctx2 );
    REQUIRE( r2["intersectDroppedPixels"].asInt64() == 1 );
    const std::vector<float> velocityI = readBand(
        QString::fromStdString( r2["velocityOutput"].asString() ) );
    REQUIRE( std::isnan( velocityI[0] ) );
    REQUIRE( velocityI[7] == Approx( 1e-3 ).margin( 1e-9 ) );

    // perpixel keeps the pixel solvable from the remaining pairs.
    Json::Value perPixelParams = params;
    perPixelParams["maskStrategy"] = "perpixel";
    perPixelParams["velocityOutput"] = tmp.filePath( "velocity_p.tif" ).toStdString();
    RSOperatorContext ctx3;
    Json::Value r3 = runOperator( "rs:sar_network_inversion", perPixelParams, ctx3 );
    REQUIRE( r3["intersectDroppedPixels"].asInt64() == 0 );
    const std::vector<float> velocityP = readBand(
        QString::fromStdString( r3["velocityOutput"].asString() ) );
    REQUIRE( velocityP[0] == Approx( 1e-3 ).margin( 1e-6 ) );
}

TEST_CASE( "rs:sar_coregister_local aligns a shifted slave through the "
           "registry", "[sar][insar11][operator][coreg]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    constexpr int kN = 128;
    const double gt[6] = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };

    // Deterministic LCG texture master; slave = master sampled at y − 2
    // (content displacement dy = +2; the warp applies the negated field).
    auto sample = []( unsigned &state ) {
        state = state * 1103515245u + 12345u;
        const bool bright = ( ( state >> 8 ) % 100 ) < 12; // sparse scatterers
        state = state * 1103515245u + 12345u;
        const double amp = bright ? 1.0 + ( ( state >> 8 ) % 1000 ) / 500.0 : 0.03;
        state = state * 1103515245u + 12345u;
        const double phase = -M_PI + 2.0 * M_PI * ( ( state >> 8 ) % 1000 ) / 1000.0;
        return std::complex<float>( static_cast<float>( amp * std::cos( phase ) ),
                                    static_cast<float>( amp * std::sin( phase ) ) );
    };
    std::vector<std::complex<float>> masterPlane( static_cast<size_t>( kN ) * kN );
    {
        unsigned lcg = 24681357u;
        for ( auto &z : masterPlane )
            z = sample( lcg );
    }
    std::vector<std::complex<float>> slavePlane( static_cast<size_t>( kN ) * kN );
    const std::complex<float> nanSample{ std::numeric_limits<float>::quiet_NaN(),
                                         std::numeric_limits<float>::quiet_NaN() };
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
            slavePlane[static_cast<size_t>( y ) * kN + x] =
                ( y - 2 >= 0 ) ? masterPlane[static_cast<size_t>( y - 2 ) * kN + x]
                               : nanSample;

    const QString masterPath = tmp.filePath( "master.tif" );
    const QString slavePath = tmp.filePath( "slave.tif" );
    const QString alignedPath = tmp.filePath( "aligned.tif" );
    auto writeC = [ & ]( const QString &path,
                         const std::vector<std::complex<float>> &plane ) {
        ensureGdalInit();
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), kN, kN, 1,
                                      GDT_CFloat32, nullptr );
        REQUIRE( ds != nullptr );
        GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
        GDALSetProjection( ds, utm48nWkt() );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, kN, kN,
                               const_cast<std::complex<float> *>( plane.data() ), kN, kN,
                               GDT_CFloat32, 0, 0 ) == CE_None );
        GDALClose( ds );
    };
    writeC( masterPath, masterPlane );
    writeC( slavePath, slavePlane );

    Json::Value params( Json::objectValue );
    params["master"] = masterPath.toStdString();
    params["slave"] = slavePath.toStdString();
    params["output"] = alignedPath.toStdString();
    params["patchSize"] = 32;
    params["patchStride"] = 32;
    params["searchRadius"] = 4;
    params["minPeakRatio"] = 1.2;
    params["medianRadius"] = 1;

    RSOperatorContext ctx;
    Json::Value result = runOperator( "rs:sar_coregister_local", params, ctx );
    REQUIRE( result["confidentPatches"].asInt64() >= 9 );

    // Interior of the aligned slave reconstructs the master (bilinear-tap
    // error is bounded by the sub-pixel residual; NaN border stays NaN).
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( alignedPath ) );
    std::vector<std::complex<float>> aligned( static_cast<size_t>( kN ) * kN );
    REQUIRE( outDs.readBandWindowNative( 1, 0, 0, kN, kN,
                                         static_cast<void *>( aligned.data() ) ) );
    // Robust account: the interior reconstructs the master with
    // median-scale error far below one amplitude unit; individual samples
    // may carry interpolation error bounded by the sub-pixel node residual
    // (~0.25 px) times the texture amplitude range (~3) — a rare sample can
    // legitimately reach ~1. The tight per-sample bound is the kernel
    // test's job (test_sar_coregistration.cpp); this asserts plumbing at
    // honest tolerances.
    long checked = 0;
    long tight = 0;
    double sumAbsDiff = 0.0;
    double maxAbsDiff = 0.0;
    for ( int y = 20; y < kN - 20; ++y )
        for ( int x = 20; x < kN - 20; ++x )
        {
            const std::complex<float> z = aligned[static_cast<size_t>( y ) * kN + x];
            const std::complex<float> m = masterPlane[static_cast<size_t>( y ) * kN + x];
            const double diff = std::abs( z - m );
            sumAbsDiff += diff;
            maxAbsDiff = std::max( maxAbsDiff, diff );
            if ( diff < 0.3 )
                ++tight;
            ++checked;
        }
    REQUIRE( checked > 4000 );
    REQUIRE( sumAbsDiff / checked < 0.05 );     // median-scale error tiny
    REQUIRE( tight >= checked - checked / 100 ); // >= 99% within 0.3
    REQUIRE( maxAbsDiff < 1.5 );                 // bounded interpolation tail
}
