// tests/test_sar_geocoding.cpp — forward Range-Doppler geocoding
// (Scientific Processing 8.0, capability package A).
//
// Every expectation is analytic. The anchor orbit is the synthetic circular
// equatorial orbit of test_sar_orbit.cpp: P(t) = R·(cos ωt, sin ωt, 0).
// For that orbit the zero-Doppler plane at time t is the sub-satellite
// MERIDIAN plane, so a ground cell at longitude λ has azimuth time t = λ/ω
// exactly, and the reference incidence / slant range follow from the
// geodetic normal and the triangle law computed here from the public Wgs84
// primitives (independent of the code under test's solver path).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_geocoding.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_orbit.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::sar;
using namespace sicnu::operators;
using Catch::Approx;

namespace
{

constexpr double kR = 7000000.0;               // orbit radius (m)
constexpr double kOmega = 2.0 * M_PI / 5880.0; // rad/s
constexpr double kA = 6378137.0;               // WGS84 semi-major (m)
constexpr double kNadirRange = kR - kA;

constexpr int kSarW = 8;
constexpr int kSarH = 8;
constexpr double kPrf = 100.0;                             // rows/s
constexpr double kAzStart = 30.0;                          // s
constexpr double kRangeRate = 299792458.0 / ( 2.0 * 30.0 ); // samples/s (30 m)
constexpr double kSampleSpacing = 30.0;                    // m per column
constexpr double kRangeStart = kNadirRange - 90.0;         // m (col 0)

// Scene longitude span: azimuth time t ↔ sub-satellite longitude ωt.
constexpr double kSceneCenterLonDeg = kOmega * kAzStart * 180.0 / M_PI;

OrbitSegment makeCircularOrbit()
{
    OrbitSegment orbit;
    for ( int i = 0; i <= 6; ++i )
    {
        const double t = 10.0 * i;
        const double phase = kOmega * t;
        OrbitStateVector s;
        s.t = t;
        s.x = kR * std::cos( phase );
        s.y = kR * std::sin( phase );
        s.z = 0.0;
        s.vx = -kR * kOmega * std::sin( phase );
        s.vy = kR * kOmega * std::cos( phase );
        s.vz = 0.0;
        orbit.states.push_back( s );
    }
    return orbit;
}

QString encodeOrbit( const OrbitSegment &orbit )
{
    QStringList records;
    for ( const OrbitStateVector &s : orbit.states )
        records << QString::number( s.t, 'g', 17 ) + ";" + QString::number( s.x, 'g', 17 )
                       + ";" + QString::number( s.y, 'g', 17 ) + ";"
                       + QString::number( s.z, 'g', 17 ) + ";"
                       + QString::number( s.vx, 'g', 17 ) + ";"
                       + QString::number( s.vy, 'g', 17 ) + ";"
                       + QString::number( s.vz, 'g', 17 );
    return records.join( QLatin1Char( '|' ) );
}

/// Analytic forward mapping for the circular orbit (test-side ground truth).
struct AnalyticFix
{
    double rowF = 0.0;
    double colF = 0.0;
    double incidenceDeg = 0.0;
    bool inImage = false;
};

AnalyticFix analyticFix( double latDeg, double lonDeg )
{
    AnalyticFix fix;
    const double lonRad = lonDeg * M_PI / 180.0;
    const double t = lonRad / kOmega; // zero-Doppler plane = meridian of lon
    fix.rowF = ( t - kAzStart ) * kPrf;

    double px, py, pz;
    Wgs84::geodeticToEcef( latDeg, lonDeg, 0.0, &px, &py, &pz );
    const double phase = kOmega * t;
    const double sx = kR * std::cos( phase );
    const double sy = kR * std::sin( phase );
    const double rho = std::sqrt( ( sx - px ) * ( sx - px ) + ( sy - py ) * ( sy - py ) + pz * pz );
    fix.colF = ( rho - kRangeStart ) / kSampleSpacing;

    // Reference incidence: geodetic normal · LOS unit vector (exact).
    const double latRad = latDeg * M_PI / 180.0;
    const double nx = std::cos( latRad ) * std::cos( lonRad );
    const double ny = std::cos( latRad ) * std::sin( lonRad );
    const double nz = std::sin( latRad );
    const double cosTheta = ( nx * ( sx - px ) + ny * ( sy - py ) + nz * ( 0.0 - pz ) ) / rho;
    fix.incidenceDeg = std::acos( std::clamp( cosTheta, -1.0, 1.0 ) ) * 180.0 / M_PI;

    fix.inImage = fix.rowF >= 0.0 && fix.rowF <= kSarH - 1
                  && fix.colF >= 0.0 && fix.colF <= kSarW - 1;
    return fix;
}

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_geocoding";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

/// Writes a Float32 GeoTIFF with geotransform/CRS/metadata under test control.
bool writeDem( const QString &path, const std::vector<float> &values, int width, int height,
               const double gt[6], const char *wkt, bool withCrs,
               float nodata = 0.0f, bool withNodata = false )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    if ( withCrs && wkt )
        GDALSetProjection( ds, wkt );
    if ( withNodata )
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), nodata );
    if ( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width, height,
                       const_cast<float *>( values.data() ), width, height, GDT_Float32,
                       0, 0 ) != CE_None )
    {
        GDALClose( ds );
        return false;
    }
    GDALClose( ds );
    return true;
}

bool writeSarScene( const QString &path, const std::vector<float> &values, int width, int height,
                    int bands, bool withContract, const OrbitSegment &orbit,
                    bool corruptOrbit = false, bool wrongWindow = false )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, bands,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    for ( int b = 1; b <= bands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b );
        if ( GDALRasterIO( band, GF_Write, 0, 0, width, height,
                           const_cast<float *>( values.data() ), width, height, GDT_Float32,
                           0, 0 ) != CE_None )
        {
            GDALClose( ds );
            return false;
        }
    }
    auto setState = [&]( const char *key, const QString &value ) {
        GDALSetMetadataItem( ds, key, value.toUtf8().constData(), nullptr );
    };
    if ( withContract )
    {
        setState( "SICNU_SAR_ORBIT_STATES",
                  corruptOrbit ? QStringLiteral( "0;1;2;3;0;0;0|1;2;3;4;0;0" )
                               : encodeOrbit( orbit ) );
        setState( "SICNU_SAR_AZIMUTH_START_UTC", QString::number( kAzStart, 'g', 17 ) );
        setState( "SICNU_SAR_PRF", QString::number( kPrf, 'g', 17 ) );
        setState( "SICNU_SAR_RANGE_RATE", QString::number( kRangeRate, 'g', 17 ) );
        // The window must cover width-1 samples within tolerance 2.
        const double startT = kRangeStart / ( 299792458.0 / 2.0 );
        const double stopT = wrongWindow
                                 ? startT + ( width + 50.0 ) / kRangeRate
                                 : startT + ( width - 1.0 ) / kRangeRate;
        setState( "SICNU_SAR_RANGE_WINDOW", QString::number( startT, 'g', 17 ) + ";"
                                                + QString::number( stopT, 'g', 17 ) );
    }
    GDALClose( ds );
    return true;
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

// The geocoding E2E DEM: 6x6 geographic grid straddling the swath's leading
// edge (column 0 falls before the scene → typed out-of-swath NoData).
constexpr int kDemW = 6;
constexpr int kDemH = 6;
constexpr double kDlonDeg = 0.001;
constexpr double kDlatDeg = 0.004;
// Column spacing maps to 1.634 SAR rows per 0.001 deg; the -0.0015 deg
// offset places cols 0-1 solidly before the scene leading edge (rowF < -0.8)
// and cols 2-5 solidly inside (rowF > +0.8) — no cell near the boundary.
constexpr double kLonStartDeg = kSceneCenterLonDeg - 0.0015;
constexpr double kLatStartDeg = 0.002;                      // all cells north of the equator

void demGeoTransform( double *gt )
{
    gt[0] = kLonStartDeg - 0.5 * kDlonDeg;
    gt[1] = kDlonDeg;
    gt[2] = 0.0;
    gt[3] = kLatStartDeg + 0.5 * kDlatDeg;
    gt[4] = 0.0;
    gt[5] = -kDlatDeg;
}

const char *epsg4326Wkt()
{
    static std::string wkt;
    if ( wkt.empty() )
    {
        OGRSpatialReference srs;
        srs.importFromEPSG( 4326 );
        char *out = nullptr;
        srs.exportToWkt( &out );
        wkt = out;
        CPLFree( out );
    }
    return wkt.c_str();
}

double demCenterLat( int y ) { return kLatStartDeg + y * kDlatDeg; }
double demCenterLon( int x ) { return kLonStartDeg + x * kDlonDeg; }

} // namespace

// ---------------------------------------------------------------------------
// Kernel: scene contract parser
// ---------------------------------------------------------------------------

TEST_CASE( "parseSarSceneContract: valid contract parses, every class of garbage is refused",
           "[sar][geocoding]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    const QString encoded = encodeOrbit( orbit );
    const double startT = kRangeStart / ( 299792458.0 / 2.0 );
    auto meta = []( const QString &orbitStates, const QString &win ) {
        return [orbitStates, win]( const char *key ) -> QString {
            const QString k = QString::fromLatin1( key );
            if ( k == "SICNU_SAR_ORBIT_STATES" )
                return orbitStates;
            if ( k == "SICNU_SAR_AZIMUTH_START_UTC" )
                return QString::number( kAzStart, 'g', 17 );
            if ( k == "SICNU_SAR_PRF" )
                return QString::number( kPrf, 'g', 17 );
            if ( k == "SICNU_SAR_RANGE_RATE" )
                return QString::number( kRangeRate, 'g', 17 );
            if ( k == "SICNU_SAR_RANGE_WINDOW" )
                return win;
            return QString();
        };
    };
    const QString goodWindow = QString::number( startT, 'g', 17 ) + ";"
                               + QString::number( startT + ( kSarW - 1.0 ) / kRangeRate, 'g', 17 );

    SarSceneContract c;
    REQUIRE( parseSarSceneContract( meta( encoded, goodWindow ), kSarW, kSarH, &c ) );
    REQUIRE( c.orbit.states.size() == orbit.states.size() );
    REQUIRE( c.azimuthStartSeconds == Approx( kAzStart ).margin( 1e-12 ) );
    REQUIRE( c.prfHz == Approx( kPrf ).margin( 1e-12 ) );
    REQUIRE( c.rangeStartM == Approx( kRangeStart ).margin( 1e-6 ) );
    REQUIRE( c.rangeSampleSpacingM == Approx( kSampleSpacing ).margin( 1e-9 ) );
    // Row/range ↔ time/range identities.
    REQUIRE( c.azimuthTimeOfRow( 2.0 ) == Approx( kAzStart + 2.0 / kPrf ).margin( 1e-12 ) );
    REQUIRE( c.rowOfAzimuthTime( c.azimuthTimeOfRow( 2.0 ) ) == Approx( 2.0 ).margin( 1e-12 ) );
    REQUIRE( c.colOfSlantRange( c.slantRangeOfCol( 3.0 ) ) == Approx( 3.0 ).margin( 1e-12 ) );

    QString error;
    // Missing keys.
    REQUIRE_FALSE( parseSarSceneContract( meta( QString(), goodWindow ), kSarW, kSarH, &c, &error ) );
    REQUIRE( error.contains( "incomplete" ) );
    // Malformed orbit.
    REQUIRE_FALSE( parseSarSceneContract( meta( QStringLiteral( "0;1;2;3;0;0;0" ), goodWindow ),
                                          kSarW, kSarH, &c, &error ) );
    // Non-numeric azimuth start.
    REQUIRE_FALSE( parseSarSceneContract(
                       [&]( const char *key ) {
                           return QString::fromLatin1( key ) == QString( "SICNU_SAR_AZIMUTH_START_UTC" )
                                      ? QStringLiteral( "x" )
                                      : QString();
                       },
                       kSarW, kSarH, &c, &error ) );
    // Non-positive PRF.
    REQUIRE_FALSE( parseSarSceneContract(
                       [&]( const char *key ) {
                           const QString k = QString::fromLatin1( key );
                           if ( k == "SICNU_SAR_ORBIT_STATES" )
                               return encoded;
                           if ( k == "SICNU_SAR_AZIMUTH_START_UTC" )
                               return QString::number( kAzStart, 'g', 17 );
                           if ( k == "SICNU_SAR_PRF" )
                               return QStringLiteral( "0" );
                           if ( k == "SICNU_SAR_RANGE_RATE" )
                               return QString::number( kRangeRate, 'g', 17 );
                           if ( k == "SICNU_SAR_RANGE_WINDOW" )
                               return goodWindow;
                           return QString();
                       },
                       kSarW, kSarH, &c, &error ) );
    // Window not covering the columns.
    const QString shortWindow = QString::number( startT, 'g', 17 ) + ";"
                                + QString::number( startT + 1.0 / kRangeRate, 'g', 17 );
    REQUIRE_FALSE( parseSarSceneContract( meta( encoded, shortWindow ), kSarW, kSarH, &c, &error ) );
    // Reversed window.
    const QString reversedWindow = goodWindow.split( QLatin1Char( ';' ) )[1] + QLatin1Char( ';' )
                                   + goodWindow.split( QLatin1Char( ';' ) )[0];
    REQUIRE_FALSE( parseSarSceneContract( meta( encoded, reversedWindow ), kSarW, kSarH, &c, &error ) );
}

// ---------------------------------------------------------------------------
// Kernel: per-cell geometry
// ---------------------------------------------------------------------------

TEST_CASE( "geocodeGroundCell: analytic forward mapping on the circular orbit",
           "[sar][geocoding]" )
{
    SarSceneContract contract;
    REQUIRE( parseSarSceneContract(
                 [&]( const char * ) { return QString(); }, kSarW, kSarH, &contract ) == false );
    contract = SarSceneContract{};
    contract.orbit = makeCircularOrbit();
    contract.azimuthStartSeconds = kAzStart;
    contract.prfHz = kPrf;
    contract.rangeStartM = kRangeStart;
    contract.rangeSampleSpacingM = kSampleSpacing;
    contract.imageWidth = kSarW;
    contract.imageHeight = kSarH;

    // Cells across the DEM grid (and a few beyond it).
    for ( int y = 0; y < kDemH; ++y )
        for ( int x = 0; x < kDemW; ++x )
        {
            const double lat = demCenterLat( y );
            const double lon = demCenterLon( x );
            const AnalyticFix fix = analyticFix( lat, lon );
            GeocodeGeometry g;
            REQUIRE( geocodeGroundCell( contract, lat, lon, 0.0, 0.0, 0.0, &g ) );
            REQUIRE( g.resolved == true );
            // Hermite interpolation of the exact circular arc deviates by
            // < 1e-2 m → sub-1e-6 rows / sub-1e-3 columns.
            REQUIRE( g.rowF == Approx( fix.rowF ).margin( 1e-5 ) );
            REQUIRE( g.colF == Approx( fix.colF ).margin( 1e-3 ) );
            REQUIRE( g.incidenceDeg == Approx( fix.incidenceDeg ).margin( 1e-6 ) );
            // Flat ground: facet normal == geodetic normal.
            REQUIRE( g.localIncidenceDeg == Approx( fix.incidenceDeg ).margin( 1e-9 ) );
            REQUIRE( g.rtcFactor == Approx( 1.0 ).margin( 1e-12 ) );
            REQUIRE( g.maskClass == TerrainMaskClass::Normal );
            // Look elevation complements the incidence (flat ground).
            REQUIRE( g.lookElevationDeg == Approx( 90.0 - fix.incidenceDeg ).margin( 1e-9 ) );
        }

    // Backward∘forward round trip: geolocating the cell's (azimuth time,
    // slant range, height) must recover the input geodetic position.
    std::mt19937 rng( 42 );
    std::uniform_real_distribution<double> lonDist( demCenterLon( 0 ), demCenterLon( kDemW - 1 ) );
    std::uniform_real_distribution<double> latDist( demCenterLat( 0 ), demCenterLat( kDemH - 1 ) );
    std::uniform_real_distribution<double> hDist( 0.0, 300.0 );
    for ( int i = 0; i < 64; ++i )
    {
        const double lat = latDist( rng );
        const double lon = lonDist( rng );
        const double h = hDist( rng );
        GeocodeGeometry g;
        REQUIRE( geocodeGroundCell( contract, lat, lon, h, 0.0, 0.0, &g ) );
        const double t = contract.azimuthTimeOfRow( g.rowF );
        const double rho = contract.slantRangeOfCol( g.colF );
        GeodeticPoint back;
        REQUIRE( geolocateZeroDoppler( contract.orbit, t, rho, h, &back ) );
        // Sub-millimetre closure on the shell.
        const double backRad = back.latDeg * M_PI / 180.0;
        const double latRad = lat * M_PI / 180.0;
        const double arcMeters = 6378137.0 * std::sqrt(
                                     std::pow( ( back.lonDeg - lon ) * M_PI / 180.0
                                                   * std::cos( latRad ),
                                               2 )
                                         + std::pow( backRad - latRad, 2 ) );
        REQUIRE( arcMeters < 1e-3 );
        REQUIRE( back.heightM == Approx( h ).margin( 1e-6 ) );
    }
}

TEST_CASE( "geocodeGroundCell: tilted facet matches independent vector math (real)",
           "[sar][geocoding][tilt]" )
{
    SarSceneContract contract;
    contract.orbit = makeCircularOrbit();
    contract.azimuthStartSeconds = kAzStart;
    contract.prfHz = kPrf;
    contract.rangeStartM = kRangeStart;
    contract.rangeSampleSpacingM = kSampleSpacing;
    contract.imageWidth = kSarW;
    contract.imageHeight = kSarH;

    const double lat = demCenterLat( 2 );
    const double lon = demCenterLon( 3 );
    const double dzdN = -0.06; // facet rising toward the sensor (satellite at lat 0)
    const double dzdE = 0.0;

    GeocodeGeometry g;
    REQUIRE( geocodeGroundCell( contract, lat, lon, 0.0, dzdE, dzdN, &g ) );
    REQUIRE( g.resolved );

    // Independent vector math (public primitives only).
    const double lonRad = lon * M_PI / 180.0;
    const double latRad = lat * M_PI / 180.0;
    const double t = lonRad / kOmega;
    double px, py, pz;
    Wgs84::geodeticToEcef( lat, lon, 0.0, &px, &py, &pz );
    const double phase = kOmega * t;
    const double losx = kR * std::cos( phase ) - px;
    const double losy = kR * std::sin( phase ) - py;
    const double losz = -pz;
    const double rho = std::sqrt( losx * losx + losy * losy + losz * losz );
    const double shx = losx / rho, shy = losy / rho, shz = losz / rho;
    const double ex = -std::sin( lonRad ), ey = std::cos( lonRad ), ez = 0.0;
    const double nx = -std::sin( latRad ) * std::cos( lonRad ),
                 ny = -std::sin( latRad ) * std::sin( lonRad ), nz = std::cos( latRad );
    const double ux = std::cos( latRad ) * std::cos( lonRad ),
                 uy = std::cos( latRad ) * std::sin( lonRad ), uz = std::sin( latRad );
    const double inv = 1.0 / std::sqrt( dzdE * dzdE + dzdN * dzdN + 1.0 );
    const double fnx = ( -dzdE * ex - dzdN * nx + ux ) * inv;
    const double fny = ( -dzdE * ey - dzdN * ny + uy ) * inv;
    const double fnz = ( -dzdE * ez - dzdN * nz + uz ) * inv;
    const double cosThetaL = fnx * shx + fny * shy + fnz * shz;
    const double thetaLDeg = std::acos( std::clamp( cosThetaL, -1.0, 1.0 ) ) * 180.0 / M_PI;
    const double theta0Deg = std::acos( std::clamp(
        ( std::cos( latRad ) * std::cos( lonRad ) * shx
          + std::cos( latRad ) * std::sin( lonRad ) * shy + std::sin( latRad ) * shz ),
        -1.0, 1.0 ) ) * 180.0 / M_PI;

    REQUIRE( g.localIncidenceDeg == Approx( thetaLDeg ).margin( 1e-9 ) );
    REQUIRE( g.incidenceDeg == Approx( theta0Deg ).margin( 1e-9 ) );
    REQUIRE( g.rtcFactor == Approx( std::sin( theta0Deg * M_PI / 180.0 )
                                        / std::sin( thetaLDeg * M_PI / 180.0 ) )
                                .margin( 1e-12 ) );
    // The slope toward the sensor (atan(0.06) ≈ 3.43°) exceeds the
    // near-nadir incidence (θ0 < 2°): classic layover.
    REQUIRE( g.maskClass == TerrainMaskClass::Layover );

    // Mirror the slope (falling toward the sensor): local incidence opens,
    // the factor drops below 1, no layover.
    GeocodeGeometry g2;
    REQUIRE( geocodeGroundCell( contract, lat, lon, 0.0, 0.0, -dzdN, &g2 ) );
    REQUIRE( g2.maskClass == TerrainMaskClass::Normal );
    REQUIRE( g2.rtcFactor < 1.0 );
    REQUIRE( g2.localIncidenceDeg > g2.incidenceDeg );

    // Degenerate gradients: NaN facet geometry, class Normal, resolved.
    GeocodeGeometry g3;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE( geocodeGroundCell( contract, lat, lon, 0.0, nan, 0.0, &g3 ) );
    REQUIRE( g3.resolved );
    REQUIRE( std::isnan( g3.localIncidenceDeg ) );
    REQUIRE( std::isnan( g3.rtcFactor ) );
    REQUIRE( g3.maskClass == TerrainMaskClass::Normal );
    REQUIRE( std::isfinite( g3.incidenceDeg ) );
}

// ---------------------------------------------------------------------------
// Kernel: samplers
// ---------------------------------------------------------------------------

TEST_CASE( "bilinear/nearest samplers: exact on linear fields, NaN-safe, bounds-safe",
           "[sar][geocoding]" )
{
    std::vector<float> buf( 12 ); // 4x3: f(r,c) = 10r + c
    for ( int r = 0; r < 3; ++r )
        for ( int c = 0; c < 4; ++c )
            buf[static_cast<size_t>( r ) * 4 + c] = static_cast<float>( 10 * r + c );

    float out = 0.0f;
    // Integer positions are exact for both samplers.
    for ( int r = 0; r < 3; ++r )
        for ( int c = 0; c < 4; ++c )
        {
            REQUIRE( bilinearSample( buf.data(), 4, 3, c, r, &out ) );
            REQUIRE( out == Approx( 10 * r + c ).margin( 1e-12 ) );
            REQUIRE( nearestSample( buf.data(), 4, 3, c, r, &out ) );
            REQUIRE( out == Approx( 10 * r + c ).margin( 1e-12 ) );
        }
    // A linear field is reproduced exactly at fractional positions.
    REQUIRE( bilinearSample( buf.data(), 4, 3, 1.5, 0.25, &out ) );
    REQUIRE( out == Approx( 10 * 0.25 + 1.5 ).margin( 1e-9 ) );
    REQUIRE( nearestSample( buf.data(), 4, 3, 1.5, 0.25, &out ) );
    REQUIRE( out == Approx( 2.0 ).margin( 1e-12 ) ); // round-half-up to col 2
    // Edges of the domain are inclusive.
    REQUIRE( bilinearSample( buf.data(), 4, 3, 3.0, 2.0, &out ) );
    REQUIRE_FALSE( bilinearSample( buf.data(), 4, 3, 3.01, 2.0, &out ) );
    REQUIRE_FALSE( bilinearSample( buf.data(), 4, 3, -0.01, 0.0, &out ) );
    // Any non-finite tap poisons the sample.
    std::vector<float> poisoned = buf;
    poisoned[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE( bilinearSample( poisoned.data(), 4, 3, 0.0, 0.0, &out ) );
    REQUIRE_FALSE( nearestSample( poisoned.data(), 4, 3, 0.0, 0.0, &out ) );
    poisoned[0] = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE( bilinearSample( poisoned.data(), 4, 3, 0.0, 0.0, &out ) );
}

// ---------------------------------------------------------------------------
// Operator E2E: registry → rs:sar_geocode → GeoTIFF products
// ---------------------------------------------------------------------------

TEST_CASE( "rs:sar_geocode round-trips a linear radiometry field onto the DEM grid",
           "[sar][geocoding][operator][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const OrbitSegment orbit = makeCircularOrbit();

    // Linear SAR field: bilinear resampling of a linear field is exact, so
    // every geocoded pixel has an analytic expected value.
    std::vector<float> sar( static_cast<size_t>( kSarW ) * kSarH );
    for ( int r = 0; r < kSarH; ++r )
        for ( int c = 0; c < kSarW; ++c )
            sar[static_cast<size_t>( r ) * kSarW + c] = static_cast<float>( 100 + 3 * r + 2 * c );
    const QString sarPath = tmp.filePath( "sar.tif" );
    REQUIRE( writeSarScene( sarPath, sar, kSarW, kSarH, 1, true, orbit ) );

    std::vector<float> dem( static_cast<size_t>( kDemW ) * kDemH, 0.0f );
    double gt[6];
    demGeoTransform( gt );
    const QString demPath = tmp.filePath( "dem.tif" );
    REQUIRE( writeDem( demPath, dem, kDemW, kDemH, gt, epsg4326Wkt(), true ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_geocode" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = sarPath.toStdString();
    params["dem"] = demPath.toStdString();
    params["output"] = tmp.filePath( "geocoded.tif" ).toStdString();
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );

    REQUIRE( result["bands"].asInt() == 5 );
    REQUIRE( result["bandOrder"].asString()
             == "backscatter,gamma0,incidence,local_incidence,layover_shadow" );

    const auto backscatter = readBand( tmp.filePath( "geocoded.tif" ), 1 );
    const auto gamma0 = readBand( tmp.filePath( "geocoded.tif" ), 2 );
    const auto incidence = readBand( tmp.filePath( "geocoded.tif" ), 3 );
    const auto localIncidence = readBand( tmp.filePath( "geocoded.tif" ), 4 );
    const auto mask = readBand( tmp.filePath( "geocoded.tif" ), 5 );
    REQUIRE( backscatter.size() == static_cast<size_t>( kDemW ) * kDemH );

    std::uint64_t expectedOutside = 0;
    std::uint64_t expectedSampled = 0;
    for ( int y = 0; y < kDemH; ++y )
        for ( int x = 0; x < kDemW; ++x )
        {
            const size_t idx = static_cast<size_t>( y ) * kDemW + x;
            const AnalyticFix fix = analyticFix( demCenterLat( y ), demCenterLon( x ) );
            if ( !fix.inImage )
            {
                ++expectedOutside;
                // Geometry is still valid; radiometry is honestly absent.
                REQUIRE( std::isnan( backscatter[idx] ) );
                REQUIRE( std::isnan( gamma0[idx] ) );
                REQUIRE( ( std::isnan( mask[idx] ) || mask[idx] == 0.0f ) );
                REQUIRE( std::isfinite( incidence[idx] ) );
                continue;
            }
            const float expected = static_cast<float>( 100 + 3 * fix.rowF + 2 * fix.colF );
            INFO( "cell (" << x << "," << y << ") rowF=" << fix.rowF << " colF=" << fix.colF );
            REQUIRE( backscatter[idx] == Approx( expected ).epsilon( 1e-4 ).margin( 1e-2 ) );
            // Flat ground keeps the RTC factor at exactly 1.
            REQUIRE( gamma0[idx] == Approx( backscatter[idx] ).margin( 1e-3 ) );
            REQUIRE( incidence[idx] == Approx( fix.incidenceDeg ).margin( 1e-3 ) );
            REQUIRE( localIncidence[idx] == Approx( fix.incidenceDeg ).margin( 1e-3 ) );
            REQUIRE( mask[idx] == 0.0f );
            ++expectedSampled;
        }
    REQUIRE( result["sampledPixels"].asUInt64() == expectedSampled );
    REQUIRE( result["outsideImagePixels"].asUInt64() == expectedOutside );
    REQUIRE( result["demNoDataPixels"].asUInt64() == 0ULL );
    REQUIRE( result["unresolvedGeometryPixels"].asUInt64() == 0ULL );
    REQUIRE( result["sourceNoDataPixels"].asUInt64() == 0ULL );

    // Provenance metadata rides on the output.
    GdalDatasetWrapper out;
    REQUIRE( out.open( tmp.filePath( "geocoded.tif" ) ) );
    GDALDatasetH h = static_cast<GDALDatasetH>( out.dataset() );
    REQUIRE( GDALGetMetadataItem( h, "SICNU_SAR_GEOMETRY_PRODUCT", nullptr )
             != nullptr );
    REQUIRE( std::string( GDALGetMetadataItem( h, "SICNU_SAR_GEOMETRY_PRODUCT", nullptr ) )
             == "geocoded_rd" );
    REQUIRE( std::string( GDALGetMetadataItem( h, "SICNU_SAR_GEOCODE_BANDS", nullptr ) )
             == "backscatter,gamma0,incidence,local_incidence,layover_shadow" );
}

TEST_CASE( "rs:sar_geocode tilted DEM: layover class and gamma0 area factor from real geometry",
           "[sar][geocoding][operator][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const OrbitSegment orbit = makeCircularOrbit();

    std::vector<float> sar( static_cast<size_t>( kSarW ) * kSarH, 2.0f );
    const QString sarPath = tmp.filePath( "sar.tif" );
    REQUIRE( writeSarScene( sarPath, sar, kSarW, kSarH, 1, true, orbit ) );

    // Ramp rising toward the sensor: the cells sit north of the equator
    // (sensor due south) and row order runs north→south, so z = +0.06
    // metres per row is a 6% grade rising toward the equator
    // (dz/dN = −0.06 m/m). The near-nadir beam (θ0 ≈ 1.3°) lays over on
    // every interior cell and gamma0 carries the sinθ0/sinθL area factor.
    const double dLatMetres = kDlatDeg * 111320.0;
    std::vector<float> dem( static_cast<size_t>( kDemW ) * kDemH );
    for ( int y = 0; y < kDemH; ++y )
        for ( int x = 0; x < kDemW; ++x )
            dem[static_cast<size_t>( y ) * kDemW + x] =
                static_cast<float>( 0.06 * dLatMetres * y );
    double gt[6];
    demGeoTransform( gt );
    const QString demPath = tmp.filePath( "dem_ramp.tif" );
    REQUIRE( writeDem( demPath, dem, kDemW, kDemH, gt, epsg4326Wkt(), true ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_geocode" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = sarPath.toStdString();
    params["dem"] = demPath.toStdString();
    params["output"] = tmp.filePath( "geocoded_ramp.tif" ).toStdString();
    params["resampling"] = "nearest";
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );

    const auto gamma0 = readBand( tmp.filePath( "geocoded_ramp.tif" ), 2 );
    const auto localIncidence = readBand( tmp.filePath( "geocoded_ramp.tif" ), 4 );
    const auto mask = readBand( tmp.filePath( "geocoded_ramp.tif" ), 5 );

    std::uint64_t layoverCells = 0;
    for ( int y = 0; y < kDemH; ++y )
        for ( int x = 0; x < kDemW; ++x )
        {
            const size_t idx = static_cast<size_t>( y ) * kDemW + x;
            const AnalyticFix fix = analyticFix( demCenterLat( y ), demCenterLon( x ) );
            if ( !fix.inImage )
                continue;
            if ( y == 0 || y == kDemH - 1 )
            {
                // The 1-pixel DEM halo replicate keeps interior cells
                // analytic; border rows have one-sided gradients — assert
                // only the strict interior.
                continue;
            }
            REQUIRE( mask[idx] == 1.0f );
            ++layoverCells;
            // gamma0 = sigma0 · sin θ0 / sin θL evaluated per pixel with
            // the kernel's own local incidence — pins the operator's RTC
            // wiring (right factor, right band, right multiplication).
            const double theta0 = fix.incidenceDeg * M_PI / 180.0;
            const double thetaL = localIncidence[idx] * M_PI / 180.0;
            REQUIRE( localIncidence[idx] > 0.0 );
            REQUIRE( gamma0[idx]
                     == Approx( 2.0f * static_cast<float>( std::sin( theta0 ) / std::sin( thetaL ) ) )
                            .epsilon( 1e-3 ) );
        }
    REQUIRE( layoverCells > 0 );
    // The class column and the RTC column agree with the incidence column:
    // a layover facet's normal leans away from the beam, so its local
    // incidence OPENS past the reference incidence (thetaL > theta0) and
    // the area factor compensates below 1.
    const auto incidence = readBand( tmp.filePath( "geocoded_ramp.tif" ), 3 );
    for ( int y = 1; y < kDemH - 1; ++y )
        for ( int x = 0; x < kDemW; ++x )
        {
            const size_t idx = static_cast<size_t>( y ) * kDemW + x;
            if ( mask[idx] == 1.0f )
                REQUIRE( localIncidence[idx] > incidence[idx] );
        }
}

TEST_CASE( "rs:sar_geocode NoData and counters: DEM gaps, source gaps, typed refusals",
           "[sar][geocoding][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const OrbitSegment orbit = makeCircularOrbit();

    std::vector<float> sar( static_cast<size_t>( kSarW ) * kSarH, 1.5f );
    const QString sarPath = tmp.filePath( "sar.tif" );
    REQUIRE( writeSarScene( sarPath, sar, kSarW, kSarH, 1, true, orbit ) );

    std::vector<float> dem( static_cast<size_t>( kDemW ) * kDemH, 10.0f );
    // A NoData column (sentinel declared on the band).
    for ( int y = 0; y < kDemH; ++y )
        dem[static_cast<size_t>( y ) * kDemW + 2] = -9999.0f;
    double gt[6];
    demGeoTransform( gt );
    const QString demPath = tmp.filePath( "dem_nodata.tif" );
    REQUIRE( writeDem( demPath, dem, kDemW, kDemH, gt, epsg4326Wkt(), true, -9999.0f, true ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_geocode" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = sarPath.toStdString();
    params["dem"] = demPath.toStdString();
    params["output"] = tmp.filePath( "geocoded_nodata.tif" ).toStdString();
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );
    REQUIRE( result["demNoDataPixels"].asUInt64() == static_cast<std::uint64_t>( kDemH ) );

    const auto backscatter = readBand( tmp.filePath( "geocoded_nodata.tif" ), 1 );
    for ( int y = 0; y < kDemH; ++y )
        REQUIRE( std::isnan( backscatter[static_cast<size_t>( y ) * kDemW + 2] ) );

    auto runExpectError = [&]( const QString &sarFile, const QString &demFile, bool corrupt,
                               bool wrongWindow ) {
        const QString badSar = tmp.filePath( sarFile );
        REQUIRE( writeSarScene( badSar, sar, kSarW, kSarH, 1, true, orbit, corrupt, wrongWindow ) );
        Json::Value p( Json::objectValue );
        p["input"] = badSar.toStdString();
        p["dem"] = demPath.toStdString();
        p["output"] = ( tmp.filePath( sarFile ) + ".out.tif" ).toStdString();
        RSOperatorContext c;
        REQUIRE_THROWS_AS( op->run( p, c ), RSOperatorError );
    };
    // Corrupt orbit segment.
    runExpectError( "sar_corrupt.tif", demPath, true, false );
    // Window contradicting the grid.
    runExpectError( "sar_window.tif", demPath, false, true );

    // DEM without CRS.
    const QString demNoCrs = tmp.filePath( "dem_nocrs.tif" );
    REQUIRE( writeDem( demNoCrs, dem, kDemW, kDemH, gt, epsg4326Wkt(), false ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = sarPath.toStdString();
        p["dem"] = demNoCrs.toStdString();
        p["output"] = tmp.filePath( "out_nocrs.tif" ).toStdString();
        RSOperatorContext c;
        REQUIRE_THROWS_AS( op->run( p, c ), RSOperatorError );
    }

    // Rotated DEM geotransform.
    std::vector<float> demFlat( static_cast<size_t>( kDemW ) * kDemH, 0.0f );
    double rotated[6] = { gt[0], gt[1], 0.0001, gt[3], 0.0002, gt[5] };
    const QString demRot = tmp.filePath( "dem_rot.tif" );
    REQUIRE( writeDem( demRot, demFlat, kDemW, kDemH, rotated, epsg4326Wkt(), true ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = sarPath.toStdString();
        p["dem"] = demRot.toStdString();
        p["output"] = tmp.filePath( "out_rot.tif" ).toStdString();
        RSOperatorContext c;
        REQUIRE_THROWS_AS( op->run( p, c ), RSOperatorError );
    }

    // Scene without any orbit contract.
    const QString sarNoContract = tmp.filePath( "sar_nocontract.tif" );
    REQUIRE( writeSarScene( sarNoContract, sar, kSarW, kSarH, 1, false, orbit ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = sarNoContract.toStdString();
        p["dem"] = demPath.toStdString();
        p["output"] = tmp.filePath( "out_nocontract.tif" ).toStdString();
        RSOperatorContext c;
        REQUIRE_THROWS_AS( op->run( p, c ), RSOperatorError );
    }
}
