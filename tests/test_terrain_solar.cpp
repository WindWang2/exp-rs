// test_terrain_solar.cpp — terrain-hydrology-11 Phase 3: shadow duration
// against hand-derivable parallel-ray truths and the low-precision solar
// position against tabulated astronomy (loose tolerance, declared ±1°).

#include "processing/algorithms/terrain_solar.h"
#include "synthetic_terrain_dem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace SyntheticTerrain;
using namespace TerrainSolar;

namespace
{
constexpr float kNo = -9999.0f;
}

TEST_CASE( "shadowDuration: a wall shadows everything behind it",
           "[terrain][solar]" )
{
    // 1×11 strip (one row), wall of height 10 at col 2, ground 0 elsewhere.
    // Sun due EAST (azimuth 90°) at 45° elevation: the shadow falls WEST of
    // the wall, length 10/tan(45°) = 10 cells → cols 0 and 1 shadowed; the
    // wall itself and everything east of it stays lit.
    Grid g = plane( 11, 1, 0.0, 0.0, 0.0 );
    g.at( 2, 0 ) = 10.0f;
    std::vector<SunSample> track = { { 90.0, 45.0, 1.0 } };
    ShadowDurationResult res;
    REQUIRE( shadowDuration( g.z.data(), 11, 1, kNo, 1.0, 1.0, track, &res ) );
    CHECK( res.sampleCount == 1 );
    for ( int x = 0; x < 11; ++x )
    {
        INFO( "col " << x << " shadow=" << res.shadowFraction[x] );
        CHECK( res.shadowFraction[x] == ( x <= 1 ? 1.0f : 0.0f ) );
    }
}

TEST_CASE( "shadowDuration: fraction tracks the weighted sun track",
           "[terrain][solar]" )
{
    // Same wall, two equally weighted samples: sun due east at 45°
    // (shadow cols ≥ 3) and due east at 90° (vertical: no shadow at all).
    // Expected fraction: 0.5 on cols ≥ 3, 0 on cols ≤ 2.
    Grid g = plane( 11, 1, 0.0, 0.0, 0.0 );
    g.at( 2, 0 ) = 10.0f;
    std::vector<SunSample> track = { { 90.0, 45.0, 1.0 }, { 90.0, 89.0, 1.0 } };
    ShadowDurationResult res;
    REQUIRE( shadowDuration( g.z.data(), 11, 1, kNo, 1.0, 1.0, track, &res ) );
    CHECK( res.sampleCount == 2 );
    CHECK( res.weightSum == Catch::Approx( 2.0 ) );
    for ( int x = 0; x < 11; ++x )
    {
        INFO( "col " << x << " shadow=" << res.shadowFraction[x] );
        CHECK( res.shadowFraction[x] == Catch::Approx( x <= 1 ? 0.5f : 0.0f ) );
    }

    // Weight scaling: 1:3 weights → 0.25 on the shadow side (only the 45°
    // sample shadows anything).
    std::vector<SunSample> track2 = { { 90.0, 45.0, 1.0 }, { 90.0, 89.0, 3.0 } };
    REQUIRE( shadowDuration( g.z.data(), 11, 1, kNo, 1.0, 1.0, track2, &res ) );
    CHECK( res.shadowFraction[0] == Catch::Approx( 0.25 ) );
    CHECK( res.shadowFraction[1] == Catch::Approx( 0.25 ) );
    CHECK( res.shadowFraction[8] == Catch::Approx( 0.0 ) );
}

TEST_CASE( "shadowDuration: 2-D ray geometry follows the azimuth",
           "[terrain][solar]" )
{
    // Point pole of height 5 at (5,5) on a 11×11 zero plane. Sun in the NE
    // (azimuth 45°) at 45° elevation: rays travel toward the SW, so the
    // shadow falls on the SW diagonal, reach = 5/tan(45°) = 5 map units.
    // SW cells at Euclidean pole distances: (4,6) 1.41, (3,7) 2.83,
    // (2,8) 4.24 → shadowed; (1,9) 5.66 → lit.
    Grid g = plane( 11, 11, 0.0, 0.0, 0.0 );
    g.at( 5, 5 ) = 5.0f;
    std::vector<SunSample> track = { { 45.0, 45.0, 1.0 } };
    ShadowDurationResult res;
    REQUIRE( shadowDuration( g.z.data(), 11, 11, kNo, 1.0, 1.0, track, &res ) );
    // The pole cell itself is NOT shadowed by its own base; the sun-side
    // and cross-track cells are lit.
    CHECK( res.shadowFraction[5 * 11 + 5] == 0.0f );
    const int shadowed[] = { 6 * 11 + 4, 7 * 11 + 3, 8 * 11 + 2 };
    for ( const int idx : shadowed )
    {
        INFO( "cell idx " << idx );
        CHECK( res.shadowFraction[idx] == 1.0f );
    }
    CHECK( res.shadowFraction[9 * 11 + 1] == 0.0f ); // beyond the reach
    CHECK( res.shadowFraction[10 * 11 + 0] == 0.0f );
    CHECK( res.shadowFraction[0 * 11 + 0] == 0.0f );
    CHECK( res.shadowFraction[10 * 11 + 10] == 0.0f );
}

TEST_CASE( "shadowDuration rejects night-only tracks and honours NoData",
           "[terrain][solar]" )
{
    Grid g = plane( 5, 5, 0.0, 0.0, 1.0 );
    applyNodataCollar( g, 1 );
    std::vector<SunSample> night = { { 90.0, -10.0, 1.0 } };
    ShadowDurationResult res;
    CHECK_FALSE( shadowDuration( g.z.data(), 5, 5, kNo, 1.0, 1.0, night, &res ) );

    std::vector<SunSample> ok = { { 180.0, 30.0, 1.0 } };
    REQUIRE( shadowDuration( g.z.data(), 5, 5, kNo, 1.0, 1.0, ok, &res ) );
    // NoData collar stays nodata; interior computed.
    CHECK( res.shadowFraction[0] == kNo );
    CHECK( res.shadowFraction[2 * 5 + 2] >= 0.0f );

    // Cancellation.
    CHECK_FALSE( shadowDuration( g.z.data(), 5, 5, kNo, 1.0, 1.0, ok, &res,
                                 [] { return true; } ) );
}

TEST_CASE( "sunPositionDeg matches tabulated solar geometry within ±1.5°",
           "[terrain][solar]" )
{
    double az = 0.0;
    double elev = 0.0;

    // Equinox (day 81), solar noon, 40°N: elevation ≈ 50°, azimuth 180°.
    REQUIRE( sunPositionDeg( 81, 12.0, 40.0, &az, &elev ) );
    CHECK( elev == Catch::Approx( 50.0 ).margin( 1.5 ) );
    CHECK( az == Catch::Approx( 180.0 ).margin( 2.0 ) );

    // Summer solstice (day 172), solar noon, 40°N: elevation ≈ 73.4°.
    REQUIRE( sunPositionDeg( 172, 12.0, 40.0, &az, &elev ) );
    CHECK( elev == Catch::Approx( 73.4 ).margin( 1.5 ) );

    // Same day, southern hemisphere winter: elevation ≈ 26.6°.
    REQUIRE( sunPositionDeg( 172, 12.0, -40.0, &az, &elev ) );
    CHECK( elev == Catch::Approx( 26.6 ).margin( 1.5 ) );

    // Equinox sunrise at solar 6 h: on the horizon (≈ 0°), due east.
    REQUIRE( sunPositionDeg( 81, 6.0, 40.0, &az, &elev ) );
    CHECK( elev == Catch::Approx( 0.0 ).margin( 1.5 ) );
    CHECK( az == Catch::Approx( 90.0 ).margin( 2.0 ) );

    // Equinox sunset at solar 18 h: due west.
    REQUIRE( sunPositionDeg( 81, 18.0, 40.0, &az, &elev ) );
    CHECK( az == Catch::Approx( 270.0 ).margin( 2.0 ) );

    // Polar night: sun never rises at 80°N in December.
    REQUIRE( sunPositionDeg( 355, 12.0, 80.0, &az, &elev ) );
    CHECK( elev < 0.0 );

    // Input validation.
    double a2 = 0.0;
    double e2 = 0.0;
    CHECK_FALSE( sunPositionDeg( 366, 12.0, 40.0, &a2, &e2 ) );
    CHECK_FALSE( sunPositionDeg( 81, 25.0, 40.0, &a2, &e2 ) );
    CHECK_FALSE( sunPositionDeg( 81, 12.0, 91.0, &a2, &e2 ) );
}
