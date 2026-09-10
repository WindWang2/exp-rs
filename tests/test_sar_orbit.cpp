// tests/test_sar_orbit.cpp — orbit state-vector contract and zero-Doppler
// geometry (Scientific Algorithms 7.0, capability package A).
//
// Every expectation is analytic: the synthetic orbit is a circular equatorial
// orbit P(t) = R·(cos ωt, sin ωt, 0), V(t) = Rω·(−sin ωt, cos ωt, 0). For that
// orbit the zero-Doppler plane at time t is the meridian plane of the
// sub-satellite longitude ωt, so geolocation/forward-RD/incidence have exact
// closed forms on the equator (the WGS84 normal is radial there, radius a).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QStringList>

#include "processing/algorithms/sar/sar_orbit.h"

#include <cmath>

using namespace sicnu::sar;
using Catch::Approx;

namespace
{
constexpr double kOrbitRadius = 7000000.0;     // m
constexpr double kOmega = 2.0 * M_PI / 5880.0; // ~98 min period, rad/s
constexpr double kSemiMajor = Wgs84::kSemiMajor;

// Builds the analytic segment sampled every 10 s over [0, 60].
OrbitSegment makeCircularOrbit()
{
    OrbitSegment orbit;
    for ( int i = 0; i <= 6; ++i )
    {
        const double t = 10.0 * i;
        const double phase = kOmega * t;
        OrbitStateVector s;
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
} // namespace

TEST_CASE( "Orbit parser: documented encoding round-trips, garbage is refused",
           "[sar][orbit]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    OrbitSegment parsed;
    REQUIRE( parseOrbitStates( encodeOrbit( orbit ), &parsed ) );
    REQUIRE( parsed.states.size() == orbit.states.size() );
    REQUIRE( parsed.states[3].t == Approx( orbit.states[3].t ).margin( 1e-12 ) );
    REQUIRE( parsed.states[3].x == Approx( orbit.states[3].x ).margin( 1e-6 ) );
    REQUIRE( parsed.isValid() );

    QString error;
    // Fewer than 2 states.
    REQUIRE_FALSE( parseOrbitStates( QStringLiteral( "0;1;2;3;0;0;0" ), &parsed, &error ) );
    REQUIRE( !error.isEmpty() );
    // Malformed field count.
    REQUIRE_FALSE(
        parseOrbitStates( QStringLiteral( "0;1;2;3;0;0;0|1;2;3;4;0;0" ), &parsed, &error ) );
    // Non-numeric field.
    REQUIRE_FALSE(
        parseOrbitStates( QStringLiteral( "0;1;2;3;0;0;0|1;x;3;4;0;0;0" ), &parsed, &error ) );
    // Descending time.
    REQUIRE_FALSE(
        parseOrbitStates( QStringLiteral( "10;1;2;3;0;1;0|0;1;2;3;0;1;0" ), &parsed, &error ) );
    // Non-finite velocity on every state (degenerate).
    REQUIRE_FALSE( parseOrbitStates( QStringLiteral( "0;1;2;3;0;0;0|10;1;2;3;0;0;0" ),
                                     &parsed, &error ) );
}

TEST_CASE( "Hermite interpolation recovers the circular orbit between knots",
           "[sar][orbit]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    // Mid-segment sample 5 s after a knot: the Hermite form of a smooth
    // circular arc with 10 s spacing is accurate far below a millimetre.
    const double t = 25.0;
    double x, y, z, vx, vy, vz;
    REQUIRE( interpolateState( orbit, t, &x, &y, &z, &vx, &vy, &vz ) );
    const double phase = kOmega * t;
    REQUIRE( x == Approx( kOrbitRadius * std::cos( phase ) ).margin( 1e-2 ) );
    REQUIRE( y == Approx( kOrbitRadius * std::sin( phase ) ).margin( 1e-2 ) );
    REQUIRE( z == Approx( 0.0 ).margin( 1e-6 ) );
    REQUIRE( vx == Approx( -kOrbitRadius * kOmega * std::sin( phase ) ).margin( 1e-6 ) );
    REQUIRE( vy == Approx( kOrbitRadius * kOmega * std::cos( phase ) ).margin( 1e-6 ) );
    // Exactly at a knot the interpolation is exact.
    double kx, ky, kz;
    REQUIRE( interpolateState( orbit, 10.0, &kx, &ky, &kz, nullptr, nullptr, nullptr ) );
    REQUIRE( kx == Approx( orbit.states[1].x ).margin( 1e-9 ) );
    // Outside the segment is a typed refusal.
    REQUIRE_FALSE( interpolateState( orbit, -1.0, &x, &y, &z, nullptr, nullptr, nullptr ) );
    REQUIRE_FALSE( interpolateState( orbit, 61.0, &x, &y, &z, nullptr, nullptr, nullptr ) );
}

TEST_CASE( "Forward range-Doppler: sub-satellite point has zero range offset",
           "[sar][orbit]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    const double t = 30.0;
    const double subLon = kOmega * t; // sub-satellite longitude (rad)
    GeodeticPoint p{ 0.0, subLon * 180.0 / M_PI, 0.0 };
    double azTime, range;
    REQUIRE( forwardRangeDoppler( orbit, p, &azTime, &range ) );
    REQUIRE( azTime == Approx( t ).margin( 1e-4 ) );
    REQUIRE( range == Approx( kOrbitRadius - kSemiMajor ).margin( 1e-3 ) );
}

TEST_CASE( "Geolocation and forward RD round-trip across the swath", "[sar][orbit]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    // Targets whose zero-Doppler crossing lies inside the segment: the
    // crossing time of longitude λ is t = λ/ω, so longitudes are anchored
    // at the t=35 crossing (±1° moves the crossing ±16 s — all inside the
    // 60 s segment). The zero-Doppler plane is the sub-satellite meridian,
    // so latitude does not move the crossing time.
    const double crossLonDeg = kOmega * 35.0 * 180.0 / M_PI;
    const std::vector<GeodeticPoint> targets = {
        { 0.0, crossLonDeg + 1.0, 0.0 },
        { 30.0, crossLonDeg, 0.0 },
        { 0.0, crossLonDeg - 1.5, 800.0 },
    };
    for ( const GeodeticPoint &p : targets )
    {
        double azTime, range;
        REQUIRE( forwardRangeDoppler( orbit, p, &azTime, &range ) );
        GeodeticPoint back;
        REQUIRE( geolocateZeroDoppler( orbit, azTime, range, p.heightM, &back ) );
        REQUIRE( back.latDeg == Approx( p.latDeg ).margin( 1e-7 ) );
        REQUIRE( back.lonDeg == Approx( p.lonDeg ).margin( 1e-7 ) );
    }
}

TEST_CASE( "Geolocation closed form: in-plane off-nadir chord range lands at latitude ±Δ",
           "[sar][orbit]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    // In the zero-Doppler plane (the sub-satellite meridian) the ground point
    // at geocentric angle Δ from nadir sits at latitude ±Δ on the SAME
    // meridian, and its exact chord range is
    // sqrt(R² + a² − 2Ra·cosΔ) (triangle sat–centre–target).
    const double t = 30.0;
    const double subLonDeg = kOmega * t * 180.0 / M_PI;
    const double deltaDeg = 2.0;
    // Range to the actual ellipsoidal shell point (geodetic lat Δ on the
    // sub-satellite meridian) — the geodetic-vs-geocentric distinction
    // matters at the 1e-2° level, so the anchor is the ellipsoid itself.
    double tx, ty, tz;
    Wgs84::geodeticToEcef( deltaDeg, subLonDeg, 0.0, &tx, &ty, &tz );
    double sx, sy, sz;
    REQUIRE( interpolateState( orbit, t, &sx, &sy, &sz, nullptr, nullptr, nullptr ) );
    const double expectedRange =
        std::sqrt( ( sx - tx ) * ( sx - tx ) + ( sy - ty ) * ( sy - ty )
                   + ( sz - tz ) * ( sz - tz ) );
    GeodeticPoint back;
    REQUIRE( geolocateZeroDoppler( orbit, t, expectedRange, 0.0, &back ) );
    REQUIRE( back.lonDeg == Approx( subLonDeg ).margin( 1e-7 ) );
    REQUIRE( std::fabs( back.latDeg ) == Approx( deltaDeg ).margin( 1e-7 ) );
}

TEST_CASE( "Incidence angle from real geometry: zero at nadir, delta degrees off-track",
           "[sar][orbit]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    const double t = 30.0;
    const double subLonDeg = kOmega * t * 180.0 / M_PI;
    // Sub-satellite point: line of sight is radial; at the equator the
    // ellipsoidal normal is radial → incidence exactly 0.
    REQUIRE( incidenceAngleDeg( orbit, t, { 0.0, subLonDeg, 0.0 } )
             == Approx( 0.0 ).margin( 1e-6 ) );
    // A point Δ degrees away ALONG TRACK (equator, same height): the
    // equatorial triangle sat–centre–target fixes the incidence angle at the
    // target: sin θi = R·sinΔ / |ST| with
    // |ST| = sqrt(R² + a² − 2Ra·cosΔ) (angle at the target between its
    // radial and the line of sight).
    const double deltaDeg = 2.0;
    const double delta = deltaDeg * M_PI / 180.0;
    const double st = std::sqrt( kOrbitRadius * kOrbitRadius + kSemiMajor * kSemiMajor
                                 - 2.0 * kOrbitRadius * kSemiMajor * std::cos( delta ) );
    const double expectedIncidence =
        std::asin( kOrbitRadius * std::sin( delta ) / st ) * 180.0 / M_PI;
    REQUIRE( incidenceAngleDeg( orbit, t, { 0.0, subLonDeg + deltaDeg, 0.0 } )
             == Approx( expectedIncidence ).margin( 1e-6 ) );
    // Outside the orbit segment → NaN, not a guess.
    REQUIRE( std::isnan( incidenceAngleDeg( orbit, 61.0, { 0.0, subLonDeg, 0.0 } ) ) );
}

TEST_CASE( "Geolocation refuses ranges that cannot reach the height shell",
           "[sar][orbit]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    GeodeticPoint back;
    // Range 5 km SHORT of the sub-satellite distance: no point on the h=0
    // shell exists → typed refusal, never a fabricated coordinate.
    REQUIRE_FALSE( geolocateZeroDoppler( orbit, 30.0, kOrbitRadius - kSemiMajor - 5000.0,
                                         0.0, &back ) );
    // Azimuth time outside the segment is refused before any solve.
    REQUIRE_FALSE( geolocateZeroDoppler( orbit, 61.0, kOrbitRadius - kSemiMajor, 0.0,
                                         &back ) );
}
