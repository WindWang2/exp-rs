// test_r24_a_terrain_viewshed.cpp — R24-A science track (WP-A4).
//
// RED provenance (origin/master@5fe642ec6): terrain_viewshed.cpp buckets
// cells into integer rings by ceil(map-unit distance), so the ring granularity
// is one MAP UNIT, not one CELL. Two real-world grids break the sweep:
//   * sub-metre grids (cellSize 0.5 m): inward neighbours of axial cells at
//     even offsets share the same ring → no strictly-smaller-ring predecessor
//     → orphan cells stay invisible on flat terrain (false-invisible wedges);
//   * geographic grids (cellSize ~9.3e-5°): every cell beyond the observer's
//     8-neighbourhood lands in ring 1 together with all of its neighbours →
//     orphans across the whole frame (mass false invisibility).
// Expected (oracle): on a flat DEM with no radius limit, every cell is
// visible from a centre observer regardless of cell size; a blocking wall
// must hide the cells behind it at ANY cell size.

#include "processing/algorithms/terrain_viewshed.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
constexpr float kNodata = -9999.0f;

int visibleCount( const std::vector<std::uint8_t> &v )
{
    int n = 0;
    for ( const auto b : v )
        n += ( b == 1 );
    return n;
}
} // namespace

TEST_CASE( "viewshedR3 flat DEM is fully visible at sub-metre cell size",
           "[processing][terrain][viewshed][r24_a]" )
{
    // 21×21 flat DEM, 0.5 m cells, observer at the centre: every cell must
    // be visible. RED on master: axial cells at even offsets (e.g. observer
    // ±4 cells, d = 2.0 → ring 2 whose inward neighbours are also ring 2)
    // stay invisible.
    constexpr int kN = 21;
    constexpr int kC = kN / 2;
    const std::vector<float> dem( static_cast<size_t>( kN ) * kN, 100.0f );
    TerrainVisibility::ViewshedParams p;
    p.obsCol = kC;
    p.obsRow = kC;
    p.observerHeight = 2.0; // eye above the plane: avoids the 0 > 0 tie
    std::vector<std::uint8_t> vis;
    REQUIRE( TerrainVisibility::viewshedR3( dem.data(), kN, kN, kNodata,
                                          0.5, 0.5, p, &vis ) );
    REQUIRE( vis.size() == static_cast<size_t>( kN ) * kN );
    // The specific orphan cells on the axes (offset ±4 cells → d = 2.0 m).
    CHECK( vis[static_cast<size_t>( kC ) * kN + kC + 4] == 1 );
    CHECK( vis[static_cast<size_t>( kC ) * kN + kC - 4] == 1 );
    CHECK( vis[static_cast<size_t>( kC + 4 ) * kN + kC] == 1 );
    CHECK( vis[static_cast<size_t>( kC - 4 ) * kN + kC] == 1 );
    CHECK( visibleCount( vis ) == kN * kN );
}

TEST_CASE( "viewshedR3 flat DEM is fully visible at degree cell size",
           "[processing][terrain][viewshed][r24_a]" )
{
    // ~9.3e-4° cells (≈100 m): a 21×21 SRTM-like window is well inside one
    // map unit, so on master every non-adjacent cell is an orphan.
    constexpr int kN = 21;
    constexpr int kC = kN / 2;
    const std::vector<float> dem( static_cast<size_t>( kN ) * kN, 100.0f );
    TerrainVisibility::ViewshedParams p;
    p.obsCol = kC;
    p.obsRow = kC;
    p.observerHeight = 2.0; // eye above the plane: avoids the 0 > 0 tie
    std::vector<std::uint8_t> vis;
    REQUIRE( TerrainVisibility::viewshedR3( dem.data(), kN, kN, kNodata,
                                          9.3e-4, 9.3e-4, p, &vis ) );
    CHECK( visibleCount( vis ) == kN * kN );
}

TEST_CASE( "viewshedR3 blocking wall hides cells behind it at degree cell size",
           "[processing][terrain][viewshed][r24_a]" )
{
    // Observer at (10,16); a 200 m wall at row 10 (6 cells north) must hide
    // the cells at rows 4..8 between it and the observer on the same column.
    constexpr int kN = 21;
    std::vector<float> dem( static_cast<size_t>( kN ) * kN, 100.0f );
    for ( int x = 0; x < kN; ++x )
        dem[static_cast<size_t>( 10 ) * kN + x] = 300.0f; // wall
    TerrainVisibility::ViewshedParams p;
    p.obsCol = 10;
    p.obsRow = 16;
    p.observerHeight = 2.0;
    std::vector<std::uint8_t> vis;
    REQUIRE( TerrainVisibility::viewshedR3( dem.data(), kN, kN, kNodata,
                                          9.3e-4, 9.3e-4, p, &vis ) );
    // Behind the wall along the column: rows 4..8 invisible.
    for ( int y = 4; y <= 8; ++y )
        CHECK( vis[static_cast<size_t>( y ) * kN + 10] == 0 );
    // The wall line itself and near-field cells either side stay visible.
    CHECK( vis[static_cast<size_t>( 10 ) * kN + 10] == 1 );
    CHECK( vis[static_cast<size_t>( 15 ) * kN + 10] == 1 );
}

TEST_CASE( "viewshedR3 unit-cell regression anchors stay exact",
           "[processing][terrain][viewshed][r24_a]" )
{
    // cellSize = 1 keeps the legacy ring semantics: a flat DEM is fully
    // visible and a wall blocks its shadow. Pins that the fix does not
    // disturb the unit-grid behaviour the existing suite relies on.
    constexpr int kN = 15;
    constexpr int kC = 7;
    std::vector<float> dem( static_cast<size_t>( kN ) * kN, 50.0f );
    TerrainVisibility::ViewshedParams p;
    p.obsCol = kC;
    p.obsRow = kC;
    p.observerHeight = 2.0; // eye above the plane: avoids the 0 > 0 tie
    std::vector<std::uint8_t> vis;
    REQUIRE( TerrainVisibility::viewshedR3( dem.data(), kN, kN, kNodata,
                                          1.0, 1.0, p, &vis ) );
    CHECK( visibleCount( vis ) == kN * kN );

    std::vector<float> walled( static_cast<size_t>( kN ) * kN, 50.0f );
    for ( int x = 0; x < kN; ++x )
        walled[static_cast<size_t>( 4 ) * kN + x] = 500.0f;
    std::vector<std::uint8_t> vis2;
    REQUIRE( TerrainVisibility::viewshedR3( walled.data(), kN, kN, kNodata,
                                          1.0, 1.0, p, &vis2 ) );
    for ( int y = 0; y <= 3; ++y )
        CHECK( vis2[static_cast<size_t>( y ) * kN + kC] == 0 );
    CHECK( vis2[static_cast<size_t>( 5 ) * kN + kC] == 1 );
}
