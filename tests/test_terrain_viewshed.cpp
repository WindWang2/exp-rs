// test_terrain_viewshed.cpp — terrain-hydrology-11 Phase 2: viewshed,
// cumulative viewshed and horizon profiles against independent closed-form
// truths (flat plane, linear ridge, cone horizon formula).

#include "processing/algorithms/terrain_viewshed.h"
#include "synthetic_terrain_dem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace SyntheticTerrain;
using namespace TerrainVisibility;

namespace
{
constexpr float kNo = -9999.0f;
}

TEST_CASE( "viewshed on a flat plane sees everything within the radius",
           "[terrain][viewshed]" )
{
    const int w = 21;
    const int h = 21;
    const Grid g = plane( w, h, 0.0, 0.0, 100.0 );
    ViewshedParams p;
    p.obsCol = 10;
    p.obsRow = 10;
    p.observerHeight = 2.0;
    p.targetHeight = 0.0;
    p.radius = 0.0; // full frame
    std::vector<std::uint8_t> vis;
    REQUIRE( viewshedR3( g.z.data(), w, h, kNo, 1.0, 1.0, p, &vis ) );

    // Every cell is visible: the observer is 2 m above a plane.
    std::size_t visible = 0;
    for ( std::size_t i = 0; i < vis.size(); ++i )
        visible += vis[i];
    CHECK( visible == vis.size() );

    // Radius cut: only cells within 5 map units.
    p.radius = 5.0;
    REQUIRE( viewshedR3( g.z.data(), w, h, kNo, 1.0, 1.0, p, &vis ) );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const double d = std::hypot( x - 10.0, y - 10.0 );
            CHECK( vis[y * w + x] == ( d <= 5.0 ? 1 : 0 ) );
        }
}

TEST_CASE( "viewshed hides the far side of a linear ridge",
           "[terrain][viewshed]" )
{
    // Ridge crest along the row direction through col 10. Observer on the
    // WEST flank at col 5, eye 2 m above the local surface (z=40 → 42).
    // Independent oracle on row 2 (hand-derived): the crest top (col 10,
    // z=50, distance 5) subtends the max path angle 1.6; every far-flank
    // cell (col c ≥ 11) subtends (28−2c)/(c−5) < 1.6 → hidden; every col
    // ≤ 10 is on ascending or flat-to-eye ground → visible.
    const int w = 31;
    const int h = 5;
    const Grid g = ridge( w, h, 10.0, 50.0, 2.0 );
    ViewshedParams p;
    p.obsCol = 5;
    p.obsRow = 2;
    p.observerHeight = 2.0;
    p.radius = 0.0;
    std::vector<std::uint8_t> vis;
    REQUIRE( viewshedR3( g.z.data(), w, h, kNo, 1.0, 1.0, p, &vis ) );
    for ( int x = 0; x < w; ++x )
    {
        INFO( "col " << x << " vis=" << vis[2 * w + x] );
        CHECK( vis[2 * w + x] == ( x <= 10 ? 1 : 0 ) );
    }
}

TEST_CASE( "viewshed curvature bends the visible range by the closed form",
           "[terrain][viewshed]" )
{
    // 1-D strip, observer eye 2 m over a zero plane. With curvature factor f
    // the apparent angle of cell d is −f·d − 2/d, which peaks at
    // d* = sqrt(2/f); beyond d* the surface bends away and cells are hidden.
    // f = 0.0683 → d* = 5.41: cells 1..5 visible, 6..∞ hidden (hand-derived).
    const int w = 201;
    const int h = 1;
    Grid g = plane( w, h, 0.0, 0.0, 0.0 );
    for ( int x = 100; x < w; ++x )
        g.at( x, 0 ) = 5.0f; // far raised half (above the eye) must not reappear
    ViewshedParams p;
    p.obsCol = 0;
    p.obsRow = 0;
    p.observerHeight = 2.0;
    p.curvatureFactor = 0.0683;
    p.radius = 0.0;
    std::vector<std::uint8_t> vis;
    REQUIRE( viewshedR3( g.z.data(), w, h, kNo, 1.0, 1.0, p, &vis ) );
    for ( int x = 1; x < w; ++x )
    {
        INFO( "col " << x << " vis=" << vis[x] );
        CHECK( vis[x] == ( x <= 5 ? 1 : 0 ) );
    }

    // Without curvature the eye-above plane (z=0, eye 2) stays visible
    // forever.
    p.curvatureFactor = 0.0;
    REQUIRE( viewshedR3( g.z.data(), w, h, kNo, 1.0, 1.0, p, &vis ) );
    CHECK( vis[50] == 1 );
    CHECK( vis[99] == 1 );
    // The raised far half (z=5, above the eye) occludes itself: its first
    // cell is visible, everything behind it is hidden.
    CHECK( vis[100] == 1 );
    CHECK( vis[101] == 0 );
    CHECK( vis[199] == 0 );
}

TEST_CASE( "cumulativeViewshed sums observers and respects NoData",
           "[terrain][viewshed]" )
{
    const int w = 15;
    const int h = 15;
    Grid g = plane( w, h, 0.0, 0.0, 10.0 );
    applyNodataCollar( g, 1 );
    ViewshedParams a;
    a.obsCol = 4;
    a.obsRow = 4;
    a.observerHeight = 2.0;
    ViewshedParams b = a;
    b.obsCol = 10;
    b.obsRow = 4;
    std::vector<std::uint16_t> counts;
    REQUIRE( cumulativeViewshed( g.z.data(), w, h, kNo, 1.0, 1.0, { a, b }, &counts ) );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * w + x;
            if ( g.z[i] == kNo )
                CHECK( counts[i] == 0xFFFF );
            else
            {
                INFO( "cell (" << x << "," << y << ") count=" << counts[i] );
                CHECK( counts[i] <= 2 );
            }
        }
    // Both observers see the entire plane (2 m above it).
    CHECK( counts[7 * w + 7] == 2 );
    // Empty observer list: counts 0 everywhere valid.
    REQUIRE( cumulativeViewshed( g.z.data(), w, h, kNo, 1.0, 1.0, {}, &counts ) );
    CHECK( counts[7 * w + 7] == 0 );
}

TEST_CASE( "horizonProfile in a bowl matches the closed-form horizon angle",
           "[terrain][viewshed]" )
{
    // Bowl z = −20 + 2r. Observer at the centre (the bottom): every ray
    // climbs the 1:1 cone wall, tan(angle) = 2t/t = 2 at every sample →
    // the horizon angle is exactly atan(2) ≈ 63.435° in all directions.
    const int w = 41;
    const int h = 41;
    const Grid g = pit( w, h, 20.0, 20.0, 20.0, 2.0 );
    HorizonProfile hp;
    REQUIRE( horizonProfile( g.z.data(), w, h, kNo, 1.0, 1.0, 20, 20, 0.0, 0.0,
                             0.0, 45.0, &hp ) );
    REQUIRE( hp.azimuths.size() == 8 );
    for ( const double a : hp.angles )
    {
        INFO( "sector angle " << a );
        CHECK( a == Catch::Approx( 63.435 ).margin( 1.0 ) );
    }

    // On a flat plane the horizon is open (−90 sentinel) except the observer
    // cell noise; with a 0-height observer every angle is ≤ 0 → max 0 at the
    // very first sample. Use a negative observer plane: angles exactly 0.
    const Grid flat = plane( 11, 11, 0.0, 0.0, 5.0 );
    HorizonProfile hp2;
    REQUIRE( horizonProfile( flat.z.data(), 11, 11, kNo, 1.0, 1.0, 5, 5, 0.0, 0.0,
                             0.0, 30.0, &hp2 ) );
    for ( const double a : hp2.angles )
        CHECK( a == Catch::Approx( 0.0 ).margin( 0.5 ) );

    // Observer errors must refuse cleanly.
    HorizonProfile hp3;
    CHECK_FALSE( horizonProfile( flat.z.data(), 11, 11, kNo, 1.0, 1.0, -1, 5, 0.0,
                                 0.0, 0.0, 30.0, &hp3 ) );
}

TEST_CASE( "viewshed refuses observers outside the grid or on NoData",
           "[terrain][viewshed]" )
{
    const Grid g = plane( 8, 8, 0.0, 0.0, 0.0 );
    Grid gd = g;
    pokeNodata( gd, 3, 3 );
    ViewshedParams p;
    p.obsCol = 3;
    p.obsRow = 3;
    std::vector<std::uint8_t> vis;
    CHECK_FALSE( viewshedR3( gd.z.data(), 8, 8, kNo, 1.0, 1.0, p, &vis ) );
    p.obsCol = 8;
    CHECK_FALSE( viewshedR3( gd.z.data(), 8, 8, kNo, 1.0, 1.0, p, &vis ) );
}
