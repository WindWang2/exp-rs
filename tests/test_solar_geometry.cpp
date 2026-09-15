// test_solar_geometry.cpp — radiometric-physics-11 work package B.
//
// Independent-oracle policy: the tests never re-use the production series.
// Expected values come from (a) published reference values for the seasonal
// extrema (declination ±23.44° at solstices, ~0 at equinoxes; equation of
// time extrema ≈ +16.4 min early November / −14.2 min mid-February;
// perihelion ≈ Jan 3, d ≈ 0.9833 AU; aphelion ≈ Jul 4, d ≈ 1.0167 AU),
// (b) Cooper's (1969) simple declination formula as a cross-formula oracle,
// and (c) exact trigonometric identities (noon elevation 90° − |φ − δ|,
// east/west mirror symmetry about solar noon). Tolerances cover the published
// series' own accuracy plus calendar drift of the quoted extrema dates.
//
// Negative tests pin the fail-closed contract: invalid dates, invalid
// lat/lon, out-of-range day-of-year, null outputs, and non-finite angles.

#include "processing/algorithms/solar_geometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QTemporaryDir>
#include <QTime>

#include <json/json.h>

#include <gdal_priv.h>

#include <cmath>
#include <limits>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "synthetic_raster_builder.h"

using namespace SolarGeometry;

namespace
{

constexpr double kPi = 3.14159265358979323846;

/// Cooper (1969): δ = 23.45°·sin(360°·(284+n)/365) — the classic simple
/// annual formula, deliberately different from Spencer's Fourier series.
double cooperDeclinationDeg( int dayOfYearNum )
{
    return 23.45 * std::sin( 2.0 * kPi * ( 284.0 + dayOfYearNum ) / 365.0 );
}

/// Solves the UTC time whose production hour angle is @p targetHaDeg at
/// (date, longitude 0) by inverting the module's own documented time algebra
/// (tst = 60·h + EoT ⇒ h = (4·(ha+180) − EoT)/60). Only used to *place* the
/// instant at solar noon; the verified quantities are identity-based.
QTime utcAtHourAngle( const QDate &date, double targetHaDeg, double eotMinutes )
{
    const double tst = 4.0 * ( targetHaDeg + 180.0 );
    const double hours = ( tst - eotMinutes ) / 60.0;
    int h = static_cast<int>( std::floor( hours ) );
    const int m = static_cast<int>( std::floor( ( hours - h ) * 60.0 ) );
    if ( h < 0 )
        h = 0;
    if ( h > 23 )
        h = 23;
    return QTime( h, m );
}

} // namespace

TEST_CASE( "dayOfYear basic calendar semantics", "[solar]" )
{
    REQUIRE( dayOfYear( QDate( 2023, 1, 1 ) ) == 1 );
    REQUIRE( dayOfYear( QDate( 2023, 12, 31 ) ) == 365 );
    REQUIRE( dayOfYear( QDate( 2024, 12, 31 ) ) == 366 ); // leap year
    REQUIRE( dayOfYear( QDate() ) == 0 );                 // invalid → 0
}

TEST_CASE( "earth-sun factor matches published perihelion/aphelion", "[solar]" )
{
    double e0 = 0.0;
    REQUIRE( earthSunFactor( 3, &e0 ) ); // Jan 3 ≈ perihelion
    CHECK( e0 > 1.0330 );
    CHECK( e0 < 1.0370 );

    REQUIRE( earthSunFactor( 185, &e0 ) ); // ~Jul 4 ≈ aphelion
    CHECK( e0 > 0.9645 );
    CHECK( e0 < 0.9685 );

    // Equinox days: factor within 0.7% of unity.
    REQUIRE( earthSunFactor( 80, &e0 ) );
    CHECK( e0 > 0.9930 );
    CHECK( e0 < 1.0070 );

    // Distance form is exactly 1/sqrt(E0) and inside the physical envelope.
    double d = 0.0;
    REQUIRE( earthSunDistanceAu( 3, &d ) );
    CHECK( d > 0.9815 );
    CHECK( d < 0.9850 );
    REQUIRE( earthSunDistanceAu( 185, &d ) );
    CHECK( d > 1.0155 );
    CHECK( d < 1.0185 );

    QString err;
    CHECK_FALSE( earthSunFactor( 0, &e0, &err ) );
    CHECK_FALSE( err.isEmpty() );
    CHECK_FALSE( earthSunFactor( 367, &e0, &err ) );
    CHECK_FALSE( err.isEmpty() );
    CHECK_FALSE( earthSunDistanceAu( -5, &d, &err ) );
    CHECK_FALSE( err.isEmpty() );
}

TEST_CASE( "declination hits published seasonal extrema", "[solar]" )
{
    // Noon evaluations on the canonical extrema dates. Tolerance 0.6° covers
    // Spencer series accuracy (0.035°) + Gregorian drift of the event date.
    SunPosition p;
    REQUIRE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ), 0.0, 0.0, &p ) );
    CHECK( p.declinationDeg == Catch::Approx( 23.44 ).margin( 0.6 ) );

    REQUIRE( solarPosition( QDate( 2023, 12, 21 ), QTime( 12, 0 ), 0.0, 0.0, &p ) );
    CHECK( p.declinationDeg == Catch::Approx( -23.44 ).margin( 0.6 ) );

    REQUIRE( solarPosition( QDate( 2023, 3, 20 ), QTime( 12, 0 ), 0.0, 0.0, &p ) );
    CHECK( std::abs( p.declinationDeg ) < 0.6 );

    REQUIRE( solarPosition( QDate( 2023, 9, 23 ), QTime( 12, 0 ), 0.0, 0.0, &p ) );
    CHECK( std::abs( p.declinationDeg ) < 0.6 );
}

TEST_CASE( "declination agrees with Cooper's formula within 1 degree", "[solar]" )
{
    // Cross-formula oracle sampled every 5 days over a full year.
    for ( int n = 1; n <= 365; n += 5 )
    {
        const QDate d = QDate( 2023, 1, 1 ).addDays( n - 1 );
        SunPosition p;
        REQUIRE( solarPosition( d, QTime( 12, 0 ), 0.0, 0.0, &p ) );
        INFO( "day " << n << " spencer=" << p.declinationDeg
                     << " cooper=" << cooperDeclinationDeg( n ) );
        CHECK( std::abs( p.declinationDeg - cooperDeclinationDeg( n ) ) < 1.0 );
    }
}

TEST_CASE( "equation of time matches published extrema", "[solar]" )
{
    SunPosition p;
    // Early November: EoT ≈ +16.4 min (published maximum).
    REQUIRE( solarPosition( QDate( 2023, 11, 3 ), QTime( 12, 0 ), 0.0, 0.0, &p ) );
    CHECK( p.equationOfTimeMin > 15.0 );
    CHECK( p.equationOfTimeMin < 17.5 );

    // Mid-February: EoT ≈ −14.2 min (published minimum).
    REQUIRE( solarPosition( QDate( 2023, 2, 11 ), QTime( 12, 0 ), 0.0, 0.0, &p ) );
    CHECK( p.equationOfTimeMin > -15.5 );
    CHECK( p.equationOfTimeMin < -12.8 );

    // Zero crossings mid-April / mid-June / early September / late December.
    for ( const auto &md : { std::make_pair( 4, 15 ), std::make_pair( 6, 13 ),
                             std::make_pair( 9, 1 ), std::make_pair( 12, 25 ) } )
    {
        REQUIRE( solarPosition( QDate( 2023, md.first, md.second ), QTime( 12, 0 ),
                                0.0, 0.0, &p ) );
        INFO( "zero-crossing " << md.first << "/" << md.second
                               << " eot=" << p.equationOfTimeMin );
        CHECK( std::abs( p.equationOfTimeMin ) < 2.0 );
    }

    // Hard bound of the series (published envelope ≈ ±16.5 min).
    for ( int n = 1; n <= 365; ++n )
    {
        REQUIRE( solarPosition( QDate( 2023, 1, 1 ).addDays( n - 1 ), QTime( 12, 0 ),
                                0.0, 0.0, &p ) );
        CHECK( std::abs( p.equationOfTimeMin ) < 17.0 );
    }
}

TEST_CASE( "solar noon elevation identity 90 - |lat - decl|", "[solar]" )
{
    // Place the instant at ha = 0 via the module's documented algebra, then
    // verify the exact spherical identity with the independently bounded
    // declination from the seasonal-extrema test above.
    struct Case
    {
        const char *name;
        double lat;
    };
    const Case cases[] = { { "equator", 0.0 },  { "mid-latitude", 45.0 },
                           { "boreal", 60.0 },  { "austral", -33.0 },
                           { "subtropic", 15.0 } };
    for ( const auto &c : cases )
    {
        SunPosition p;
        REQUIRE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ), 0.0, 0.0, &p ) );
        const double eot = p.equationOfTimeMin;
        const double noonDecl = p.declinationDeg;
        REQUIRE( solarPosition( QDate( 2023, 6, 21 ),
                                utcAtHourAngle( QDate( 2023, 6, 21 ), 0.0, eot ),
                                c.lat, 0.0, &p ) );
        INFO( c.name << " hourAngle=" << p.hourAngleDeg );
        CHECK( std::abs( p.hourAngleDeg ) < 0.05 );
        CHECK( p.elevationDeg == Catch::Approx( 90.0 - std::abs( c.lat - noonDecl ) )
                                   .margin( 0.05 ) );
    }
}

TEST_CASE( "east/west mirror symmetry about solar noon", "[solar]" )
{
    const QDate date( 2023, 10, 10 );
    SunPosition noon;
    REQUIRE( solarPosition( date, QTime( 12, 0 ), 0.0, 0.0, &noon ) );
    const double eot = noon.equationOfTimeMin;

    SunPosition am, pm;
    REQUIRE( solarPosition( date, utcAtHourAngle( date, -30.0, eot ), 40.0, 0.0, &am ) );
    REQUIRE( solarPosition( date, utcAtHourAngle( date, +30.0, eot ), 40.0, 0.0, &pm ) );

    // Same zenith; azimuths mirrored around due south (180°): am + pm = 360°.
    CHECK( am.zenithDeg == Catch::Approx( pm.zenithDeg ).margin( 1e-6 ) );
    CHECK( am.azimuthDeg + pm.azimuthDeg == Catch::Approx( 360.0 ).margin( 0.1 ) );
    CHECK( am.azimuthDeg < 180.0 );
    CHECK( pm.azimuthDeg > 180.0 );
}

TEST_CASE( "London solstice reference position", "[solar]" )
{
    // 2023-06-21 12:00 UTC, London (51.5N, 0E): solar-noon elevation
    // ≈ 90 − (51.5 − 23.4) ≈ 61.9°; sun due south; ha ≈ EoT/4 ≈ −0.4°.
    SunPosition p;
    REQUIRE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ), 51.5, 0.0, &p ) );
    CHECK( p.elevationDeg > 61.0 );
    CHECK( p.elevationDeg < 63.0 );
    CHECK( p.azimuthDeg == Catch::Approx( 180.0 ).margin( 3.0 ) );
    CHECK( p.sunAboveHorizon );

    // Same instant, Sydney (−33.87, 151.21E): late-night sun below horizon.
    REQUIRE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ), -33.87, 151.21, &p ) );
    CHECK_FALSE( p.sunAboveHorizon );
    CHECK( p.elevationDeg < 0.0 );
}

TEST_CASE( "longitude drives the hour angle (UTC-based solar time)", "[solar]" )
{
    // At 12:00 UTC the sun crosses the meridian at longitude ≈ −EoT/4; 15°
    // of longitude shifts the hour angle by 60° (4 min per degree).
    SunPosition west, east;
    REQUIRE( solarPosition( QDate( 2023, 4, 10 ), QTime( 12, 0 ), 30.0, -15.0, &west ) );
    REQUIRE( solarPosition( QDate( 2023, 4, 10 ), QTime( 12, 0 ), 30.0, +15.0, &east ) );
    CHECK( west.hourAngleDeg > east.hourAngleDeg ); // west of the meridian: earlier solar time
    // The hour-angle difference between ±15° must be 120° (8 min of EoT is
    // common-mode and cancels).
    CHECK( ( west.hourAngleDeg - east.hourAngleDeg )
           == Catch::Approx( 120.0 ).margin( 1e-6 ) );
}

TEST_CASE( "geometry validators refuse non-physical angles", "[solar]" )
{
    QString err;
    CHECK( validateSunGeometryDeg( 30.0, 180.0, &err ) );
    CHECK_FALSE( validateSunGeometryDeg( 0.0, 180.0, &err ) );      // horizon
    CHECK_FALSE( validateSunGeometryDeg( -10.0, 180.0, &err ) );    // below horizon
    CHECK_FALSE( validateSunGeometryDeg( 90.0001, 180.0, &err ) );  // beyond zenith
    CHECK_FALSE( validateSunGeometryDeg( 30.0, 360.0, &err ) );     // azimuth wrap
    CHECK_FALSE( validateSunGeometryDeg( 30.0, -1.0, &err ) );
    CHECK_FALSE( validateSunGeometryDeg(
        std::numeric_limits<double>::quiet_NaN(), 180.0, &err ) );
    CHECK_FALSE( err.isEmpty() );

    CHECK( validateViewGeometryDeg( 0.0, 0.0, &err ) );     // nadir is valid
    CHECK( validateViewGeometryDeg( 70.0, 270.0, &err ) );
    CHECK_FALSE( validateViewGeometryDeg( 90.0, 0.0, &err ) );      // straight up
    CHECK_FALSE( validateViewGeometryDeg( -1.0, 0.0, &err ) );
    CHECK_FALSE( validateViewGeometryDeg( 30.0, 360.5, &err ) );
    CHECK_FALSE( validateViewGeometryDeg(
        std::numeric_limits<double>::infinity(), 0.0, &err ) );
}

TEST_CASE( "solarPosition fail-closed refusals", "[solar]" )
{
    SunPosition p;
    QString err;

    CHECK_FALSE( solarPosition( QDate(), QTime( 12, 0 ), 0.0, 0.0, &p, &err ) );
    CHECK_FALSE( err.isEmpty() );

    CHECK_FALSE( solarPosition( QDate( 2023, 6, 21 ), QTime(), 0.0, 0.0, &p, &err ) );
    CHECK_FALSE( err.isEmpty() );

    CHECK_FALSE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ), 90.5, 0.0, &p, &err ) );
    CHECK_FALSE( err.isEmpty() );

    CHECK_FALSE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ), 0.0, 180.5, &p, &err ) );
    CHECK_FALSE( err.isEmpty() );

    CHECK_FALSE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ),
                                std::numeric_limits<double>::quiet_NaN(), 0.0, &p, &err ) );
    CHECK_FALSE( err.isEmpty() );

    CHECK_FALSE( solarPosition( QDate( 2023, 6, 21 ), QTime( 12, 0 ), 0.0, 0.0, nullptr, &err ) );
    CHECK_FALSE( err.isEmpty() );
}

// ---------------------------------------------------------------------------
// Operator E2E (rs:solar_geometry): registry wiring, result schema, metadata
// stamping and typed refusals. The math itself is pinned above by the
// independent oracles; here we prove the operator surface.
// ---------------------------------------------------------------------------

TEST_CASE( "rs:solar_geometry computes and reports the London reference", "[solar][operator][e2e]" )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "rs:solar_geometry" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["date"] = "2023-06-21";
    params["utc_time"] = "12:00";
    params["latitude"] = 51.5;
    params["longitude"] = 0.0;

    sicnu::operators::RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    CHECK( result["sun_elevation"].asDouble() > 61.0 );
    CHECK( result["sun_elevation"].asDouble() < 63.0 );
    CHECK( result["sun_azimuth"].asDouble() == Catch::Approx( 180.0 ).margin( 3.0 ) );
    CHECK( result["declination"].asDouble() == Catch::Approx( 23.44 ).margin( 0.6 ) );
    CHECK( result["earth_sun_factor"].asDouble() > 1.03 );
    CHECK( result["metadata_written"].asBool() == false );
}

TEST_CASE( "rs:solar_geometry stamps SICNU_* metadata on a writable raster",
           "[solar][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString rasterPath = dir.filePath( "scene.tif" );
    sicnu::testing::RsSyntheticRasterBuilder builder( 4, 3, 1 );
    REQUIRE( !builder.writeToDisk( rasterPath ).isEmpty() );

    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "rs:solar_geometry" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["date"] = "2023-06-21";
    params["utc_time"] = "12:00:00";
    params["latitude"] = 51.5;
    params["longitude"] = 0.0;
    params["input"] = rasterPath.toStdString();
    params["write_metadata"] = true;

    sicnu::operators::RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    CHECK( result["metadata_written"].asBool() );

    // Read the stamped keys back through GDAL.
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const auto item = [&]( const char *key ) {
        const char *v = GDALGetMetadataItem( ds, key, nullptr );
        return v ? QString::fromUtf8( v ).toDouble() : std::numeric_limits<double>::quiet_NaN();
    };
    const double zenith = item( "SICNU_SUN_ZENITH" );
    const double azimuth = item( "SICNU_SUN_AZIMUTH" );
    const double elevation = item( "SICNU_SUN_ELEVATION" );
    const double factor = item( "SICNU_EARTH_SUN_FACTOR" );
    const double distance = item( "SICNU_EARTH_SUN_DISTANCE_AU" );
    GDALClose( ds );

    CHECK( zenith == Catch::Approx( 90.0 - result["sun_elevation"].asDouble() ).margin( 1e-9 ) );
    CHECK( azimuth == Catch::Approx( result["sun_azimuth"].asDouble() ).margin( 1e-9 ) );
    CHECK( elevation == Catch::Approx( result["sun_elevation"].asDouble() ).margin( 1e-9 ) );
    CHECK( factor == Catch::Approx( result["earth_sun_factor"].asDouble() ).margin( 1e-9 ) );
    CHECK( distance > 1.015 );
    CHECK( distance < 1.018 );
}

TEST_CASE( "rs:solar_geometry typed refusals", "[solar][operator][e2e]" )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "rs:solar_geometry" );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext context;

    Json::Value params( Json::objectValue );
    params["date"] = "2023-02-30"; // nonexistent date
    params["utc_time"] = "12:00";
    params["latitude"] = 0.0;
    params["longitude"] = 0.0;
    REQUIRE_THROWS_AS( op->run( params, context ), sicnu::operators::RSOperatorError );

    params["date"] = "2023-06-21";
    params["latitude"] = 91.0; // out of range
    REQUIRE_THROWS_AS( op->run( params, context ), sicnu::operators::RSOperatorError );

    params["latitude"] = 0.0;
    params["utc_time"] = "25:99"; // invalid time
    REQUIRE_THROWS_AS( op->run( params, context ), sicnu::operators::RSOperatorError );

    params["utc_time"] = "12:00";
    params["write_metadata"] = true; // requires 'input'
    REQUIRE_THROWS_AS( op->run( params, context ), sicnu::operators::RSOperatorError );
}
