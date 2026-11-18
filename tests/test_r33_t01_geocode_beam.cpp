// tests/test_r33_t01_geocode_beam.cpp — beam-away ENU projection
// regression (R33-T01, issue #2126): the along-beam slope direction must be
// the FULL horizontal projection of −ŝ, including the shz·nz term in the
// north component. Dropping it zeroes the beam-away direction at low
// latitude and slants it everywhere the look has a strong radial
// component, misclassifying layover/shadow.
//
// Fixture: a 97°-inclined circular orbit (radius 7,000 km, period 5,880 s)
// whose zero-Doppler crossing for the target (50°N 20°E) sits at
// t ≈ 822.14 s with the sensor 2,203.6 km away — look elevation θe ≈ 7.46°,
// beam travelling EAST (gE ≈ +0.97) with a corrected southward gN ≈ −0.19.
// A north-rising slope (dzdN > 0, dzdE = 0) then FALLS along the beam:
// α = atan(dzdN·gN/gHoriz) < −θe for dzdN ≥ 0.8 → SHADOW. The legacy
// projection (gN without shz·nz ≈ −0.047) kept α ≈ −2.7° → Normal — the
// exact regression the issue demonstrated at the equator.
//
// The expected classes/angles are recomputed in-test from an independent
// ENU basis and the public orbit primitives (no shared helper with the
// code under test).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/sar/sar_geocoding.h"
#include "processing/algorithms/sar/sar_orbit.h"

using namespace sicnu::sar;
using Catch::Approx;

namespace
{

constexpr double kR = 7000000.0;               // orbit radius (m)
constexpr double kOmega = 2.0 * M_PI / 5880.0; // rad/s
constexpr double kInclination = 97.0;          // deg (retrograde)

/// Circular orbit in the plane inclined by kInclination around +Y:
/// P(t) = R·(cos ωt, sin ωt·cos i, sin ωt·sin i).
OrbitSegment makeInclinedOrbit()
{
    OrbitSegment orbit;
    const double inc = kInclination * M_PI / 180.0;
    for ( int i = 0; i <= 12; ++i ) // t ∈ [760, 880] s, 10 s steps
    {
        const double t = 760.0 + 10.0 * i;
        const double phase = kOmega * t;
        OrbitStateVector s;
        s.t = t;
        s.x = kR * std::cos( phase );
        s.y = kR * std::sin( phase ) * std::cos( inc );
        s.z = kR * std::sin( phase ) * std::sin( inc );
        s.vx = -kR * kOmega * std::sin( phase );
        s.vy = kR * kOmega * std::cos( phase ) * std::cos( inc );
        s.vz = kR * kOmega * std::cos( phase ) * std::sin( inc );
        orbit.states.push_back( s );
    }
    return orbit;
}

SarSceneContract makeContract()
{
    SarSceneContract contract;
    contract.orbit = makeInclinedOrbit();
    contract.azimuthStartSeconds = 820.0;
    contract.prfHz = 100.0;
    contract.rangeStartM = 2200000.0;
    contract.rangeSampleSpacingM = 30.0;
    contract.imageWidth = 8;
    contract.imageHeight = 8;
    return contract;
}

constexpr double kTargetLat = 50.0; // deg N
constexpr double kTargetLon = 20.0; // deg E

/// Independent zero-Doppler sensor state for the target (public solver).
bool targetSensorState( const SarSceneContract &contract, double *shOut,
                        double thetaEnu[3][3] )
{
    const GeodeticPoint p{ kTargetLat, kTargetLon, 0.0 };
    double azimuthTime = 0.0, slantRange = 0.0;
    if ( !forwardRangeDoppler( contract.orbit, p, &azimuthTime, &slantRange ) )
        return false;
    double sx, sy, sz;
    if ( !interpolateState( contract.orbit, azimuthTime, &sx, &sy, &sz,
                            nullptr, nullptr, nullptr ) )
        return false;
    double px, py, pz;
    Wgs84::geodeticToEcef( kTargetLat, kTargetLon, 0.0, &px, &py, &pz );
    const double losx = sx - px, losy = sy - py, losz = sz - pz;
    const double rho = std::sqrt( losx * losx + losy * losy + losz * losz );
    if ( !( rho > 0.0 ) )
        return false;
    shOut[0] = losx / rho;
    shOut[1] = losy / rho;
    shOut[2] = losz / rho;

    const double lat = kTargetLat * M_PI / 180.0;
    const double lon = kTargetLon * M_PI / 180.0;
    const double sinLat = std::sin( lat ), cosLat = std::cos( lat );
    const double sinLon = std::sin( lon ), cosLon = std::cos( lon );
    thetaEnu[0][0] = -sinLon;          thetaEnu[0][1] = cosLon;          thetaEnu[0][2] = 0.0;   // east
    thetaEnu[1][0] = -sinLat * cosLon; thetaEnu[1][1] = -sinLat * sinLon; thetaEnu[1][2] = cosLat; // north
    thetaEnu[2][0] = cosLat * cosLon;  thetaEnu[2][1] = cosLat * sinLon;  thetaEnu[2][2] = sinLat; // up
    return true;
}

/// Independent beam-away classification: the ENU components of
/// −(sh − (sh·u)·u), FULL dot products on every basis vector.
TerrainMaskClass oracleClass( const double sh[3], const double enu[3][3],
                              double dzdE, double dzdN, double lookElevationDeg )
{
    const double gx = -( sh[0] * enu[0][0] + sh[1] * enu[0][1] + sh[2] * enu[0][2] );
    const double gy = -( sh[0] * enu[1][0] + sh[1] * enu[1][1] + sh[2] * enu[1][2] );
    const double gHoriz = std::sqrt( gx * gx + gy * gy );
    if ( !( gHoriz > 1e-12 ) )
        return TerrainMaskClass::Normal;
    const double alphaDeg = std::atan( ( dzdE * gx + dzdN * gy ) / gHoriz ) * 180.0 / M_PI;
    if ( alphaDeg > 90.0 - lookElevationDeg )
        return TerrainMaskClass::Layover;
    if ( alphaDeg < -lookElevationDeg )
        return TerrainMaskClass::Shadow;
    return TerrainMaskClass::Normal;
}

} // anonymous namespace

TEST_CASE( "geocodeGroundCell beam-away projection includes shz·nz (#2126)",
           "[r33t01][sar][geocoding]" )
{
    const SarSceneContract contract = makeContract();

    double sh[3] = {};
    double enu[3][3] = {};
    REQUIRE( targetSensorState( contract, sh, enu ) );

    const double shU = sh[0] * enu[2][0] + sh[1] * enu[2][1] + sh[2] * enu[2][2];
    const double thetaE = std::asin( std::clamp( shU, -1.0, 1.0 ) ) * 180.0 / M_PI;
    // Slant geometry pins: θe ≈ 7.46°, beam travels east with the corrected
    // gN ≈ −0.19 (the legacy gN ≈ −0.047 is what #2126 shipped).
    REQUIRE( thetaE == Approx( 7.4557 ).margin( 0.05 ) );

    GeocodeGeometry flat;
    REQUIRE( geocodeGroundCell( contract, kTargetLat, kTargetLon, 0.0, 0.0, 0.0, &flat ) );
    REQUIRE( flat.resolved );
    REQUIRE( flat.lookElevationDeg == Approx( thetaE ).margin( 1e-9 ) );
    REQUIRE( flat.maskClass == TerrainMaskClass::Normal );

    // The regression pin: north-rising slopes fall along the east-travelling
    // beam once gN carries its shz·nz term — Shadow from dzdN ≈ 0.8 up.
    // The legacy projection kept every one of these Normal (α ≈ −2.7°).
    const double shadowSlopes[] = { 0.8, 1.0, 1.2 };
    for ( const double dzdN : shadowSlopes )
    {
        GeocodeGeometry g;
        REQUIRE( geocodeGroundCell( contract, kTargetLat, kTargetLon, 0.0, 0.0, dzdN, &g ) );
        REQUIRE( g.resolved );
        INFO( "dzdN = " << dzdN );
        REQUIRE( g.maskClass == TerrainMaskClass::Shadow );
        // Independent-oracle agreement on the class and the elevation.
        REQUIRE( g.maskClass == oracleClass( sh, enu, 0.0, dzdN, thetaE ) );
        REQUIRE( g.lookElevationDeg == Approx( thetaE ).margin( 1e-9 ) );
    }

    // Mirror slopes (falling to the north) rise along the beam; far below
    // the layover threshold θe≈7.46° is nowhere near — class Normal.
    const double normalSlopes[] = { -0.8, -1.0, -1.2 };
    for ( const double dzdN : normalSlopes )
    {
        GeocodeGeometry g;
        REQUIRE( geocodeGroundCell( contract, kTargetLat, kTargetLon, 0.0, 0.0, dzdN, &g ) );
        INFO( "dzdN = " << dzdN );
        REQUIRE( g.maskClass == TerrainMaskClass::Normal );
        REQUIRE( g.maskClass == oracleClass( sh, enu, 0.0, dzdN, thetaE ) );
    }

    // Combined slopes: the direction (gE:gN ratio) drives the projection —
    // verified against the independent oracle, both flank orientations.
    const double combined[][2] = { { 0.3, 1.0 }, { -0.3, 1.0 }, { 0.3, -1.0 }, { -0.3, -1.0 } };
    for ( const auto &slope : combined )
    {
        GeocodeGeometry g;
        REQUIRE( geocodeGroundCell( contract, kTargetLat, kTargetLon, 0.0, slope[0], slope[1], &g ) );
        INFO( "dzdE = " << slope[0] << ", dzdN = " << slope[1] );
        REQUIRE( g.maskClass == oracleClass( sh, enu, slope[0], slope[1], thetaE ) );
        // localIncidence/RTC against independent facet math in the same ENU.
        const double inv = 1.0 / std::sqrt( slope[0] * slope[0] + slope[1] * slope[1] + 1.0 );
        const double fx = ( -slope[0] * enu[0][0] - slope[1] * enu[1][0] + enu[2][0] ) * inv;
        const double fy = ( -slope[0] * enu[0][1] - slope[1] * enu[1][1] + enu[2][1] ) * inv;
        const double fz = ( -slope[0] * enu[0][2] - slope[1] * enu[1][2] + enu[2][2] ) * inv;
        const double cosThetaL = fx * sh[0] + fy * sh[1] + fz * sh[2];
        REQUIRE( g.localIncidenceDeg
                 == Approx( std::acos( std::clamp( cosThetaL, -1.0, 1.0 ) ) * 180.0 / M_PI ).margin( 1e-9 ) );
    }
}
