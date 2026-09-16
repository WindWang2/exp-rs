// tests/test_mosaic_seamline.cpp — F15 Package C oracle tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/mosaic_seamline.h"

#include <cmath>
#include <vector>

using namespace rs::mosaic;
using Catch::Approx;

namespace {

std::vector<float> zeros( int w, int h ) { return std::vector<float>( static_cast<size_t>( w ) * h, 0.0f ); }

} // namespace

TEST_CASE( "Seam: zero-cost column attracts the path", "[processing][mosaic][seamline]" )
{
    const int w = 5, h = 5;
    std::vector<float> cost( static_cast<size_t>( w ) * h, 10.0f );
    for ( int r = 0; r < h; ++r )
        cost[static_cast<size_t>( r ) * w + 2] = 0.0f;

    double total = -1;
    const auto path = computeSeamPath( cost, w, h, SeamOrientation::Vertical, &total );
    REQUIRE( path.size() == static_cast<size_t>( h ) );
    for ( int r = 0; r < h; ++r )
        CHECK( path[r] == 2 );
    CHECK( total == Approx( 0.0 ) );
}

TEST_CASE( "Seam: path detours around an expensive wall", "[processing][mosaic][seamline]" )
{
    const int w = 5, h = 5;
    std::vector<float> cost = zeros( w, h );
    for ( int r = 1; r <= 3; ++r )
        cost[static_cast<size_t>( r ) * w + 2] = 100.0f;

    double total = -1;
    const auto path = computeSeamPath( cost, w, h, SeamOrientation::Vertical, &total );
    REQUIRE( path.size() == h );
    CHECK( total == Approx( 0.0 ) );
    for ( int r = 1; r <= 3; ++r )
        CHECK( path[r] != 2 );
    // 3-connectivity: consecutive steps differ by at most 1.
    for ( int r = 1; r < h; ++r )
        CHECK( std::abs( path[r] - path[r - 1] ) <= 1 );
}

TEST_CASE( "Seam: horizontal orientation returns one row per column",
           "[processing][mosaic][seamline]" )
{
    const int w = 6, h = 4;
    std::vector<float> cost( static_cast<size_t>( w ) * h, 10.0f );
    for ( int c = 0; c < w; ++c )
        cost[static_cast<size_t>( 1 ) * w + c] = 0.0f;

    double total = -1;
    const auto path = computeSeamPath( cost, w, h, SeamOrientation::Horizontal, &total );
    REQUIRE( path.size() == static_cast<size_t>( w ) );
    for ( int c = 0; c < w; ++c )
        CHECK( path[c] == 1 );
    CHECK( total == Approx( 0.0 ) );
}

TEST_CASE( "Seam: deterministic tie-break picks the smallest index",
           "[processing][mosaic][seamline]" )
{
    const auto cost = zeros( 4, 4 );
    const auto path = computeSeamPath( cost, 4, 4, SeamOrientation::Vertical );
    REQUIRE( path.size() == 4 );
    // All-zero surface: the recorded predecessors and the final selection
    // must land on column 0 (strict-< comparisons over ascending scans).
    for ( int r = 0; r < 4; ++r )
        CHECK( path[r] == 0 );
}

TEST_CASE( "Seam: invalid inputs rejected", "[processing][mosaic][seamline]" )
{
    CHECK( computeSeamPath( {}, 0, 0, SeamOrientation::Vertical ).empty() );
    auto shortCost = zeros( 3, 3 );
    shortCost.pop_back();
    CHECK( computeSeamPath( shortCost, 3, 3, SeamOrientation::Vertical ).empty() );
    auto nanCost = zeros( 2, 2 );
    nanCost[0] = std::nanf( "" );
    CHECK( computeSeamPath( nanCost, 2, 2, SeamOrientation::Vertical ).empty() );
}

TEST_CASE( "Seam: binned builder finds the known corridor (cell answer)",
           "[processing][mosaic][seamline]" )
{
    // 32x32 overlap, binned to 4x4 cells. Left half identical, right half
    // shifted by 50 -> radiometric means: cells 0-1 = 0, cells 2-3 = 50.
    // Edge-distance pushes away from borders: expected seam column = 1.
    const int n = 32;
    std::vector<float> a( static_cast<size_t>( n ) * n );
    std::vector<float> b( static_cast<size_t>( n ) * n );
    for ( int r = 0; r < n; ++r )
        for ( int c = 0; c < n; ++c )
        {
            a[static_cast<size_t>( r ) * n + c] = static_cast<float>( 10 + r );
            b[static_cast<size_t>( r ) * n + c] =
                ( c < n / 2 ) ? a[static_cast<size_t>( r ) * n + c]
                              : a[static_cast<size_t>( r ) * n + c] + 50.0f;
        }

    SeamCostWeights weights; // strong edge weight pulls the seam off the border
    weights.edgeDistance = 100.0;
    BinnedSeamCost builder( n, n, weights, /*maxCells=*/4 );
    builder.addWindow( 0, 0, n, n, a, b );
    const SeamDecision d = builder.solve();
    REQUIRE( d.path.size() == 4 ); // one cell-row entry per cell row
    REQUIRE( builder.cellsX() == 4 );
    for ( size_t i = 0; i < d.path.size(); ++i )
        CHECK( d.path[i] == 1 );
}

TEST_CASE( "Seam: binned builder accumulates across windows identically",
           "[processing][mosaic][seamline]" )
{
    const int n = 16;
    std::vector<float> a( static_cast<size_t>( n ) * n );
    std::vector<float> b( static_cast<size_t>( n ) * n );
    for ( int r = 0; r < n; ++r )
        for ( int c = 0; c < n; ++c )
        {
            a[static_cast<size_t>( r ) * n + c] = static_cast<float>( c + r );
            b[static_cast<size_t>( r ) * n + c] = a[static_cast<size_t>( r ) * n + c] + 3.0f;
        }

    BinnedSeamCost oneShot( n, n, SeamCostWeights{}, 16 );
    oneShot.addWindow( 0, 0, n, n, a, b );
    const SeamDecision whole = oneShot.solve();

    BinnedSeamCost tiled( n, n, SeamCostWeights{}, 16 );
    for ( int y = 0; y < n; y += 8 )
        for ( int x = 0; x < n; x += 8 )
        {
            std::vector<float> wa( 64 ), wb( 64 );
            for ( int r = 0; r < 8; ++r )
                for ( int c = 0; c < 8; ++c )
                {
                    wa[static_cast<size_t>( r ) * 8 + c] = a[static_cast<size_t>( y + r ) * n + x + c];
                    wb[static_cast<size_t>( r ) * 8 + c] = b[static_cast<size_t>( y + r ) * n + x + c];
                }
            tiled.addWindow( x, y, 8, 8, wa, wb );
        }
    const SeamDecision parts = tiled.solve();

    REQUIRE( whole.path.size() == parts.path.size() );
    for ( size_t i = 0; i < whole.path.size(); ++i )
        CHECK( whole.path[i] == parts.path[i] );
    CHECK( whole.totalCost == Approx( parts.totalCost ).margin( 1e-6 ) );
}

TEST_CASE( "Seam: cloud penalty dominates the corridor choice",
           "[processing][mosaic][seamline]" )
{
    // Three zero-radiometric corridors (odd columns 1/3/5); columns 1 and 3
    // carry full cloud blocks. Radiometric and gradient terms are symmetric
    // across the corridors and the edge term is disabled — without the cloud
    // penalty the tie-break would pick column 1, so the cloud penalty is the
    // only force that can move the seam to column 5.
    const int w = 7, h = 4;
    std::vector<float> a( static_cast<size_t>( w ) * h, 10.0f );
    std::vector<float> b( static_cast<size_t>( w ) * h );
    std::vector<float> cloudA( static_cast<size_t>( w ) * h, 0.0f );
    std::vector<float> cloudB( static_cast<size_t>( w ) * h, 0.0f );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
        {
            const float diff = ( c % 2 == 1 ) ? 0.0f : 40.0f;
            b[static_cast<size_t>( r ) * w + c] = 10.0f + diff;
            if ( c == 1 || c == 3 )
                cloudB[static_cast<size_t>( r ) * w + c] = 1.0f;
        }

    SeamCostWeights weights;
    weights.radiometric = 1.0;
    weights.gradient = 0.0;
    weights.cloud = 10.0;
    weights.edgeDistance = 0.0;

    BinnedSeamCost builder( w, h, weights, /*maxCells=*/7 ); // exact per-pixel cells
    builder.addWindow( 0, 0, w, h, a, b, cloudA, cloudB );
    const SeamDecision d = builder.solve();
    REQUIRE( d.path.size() == static_cast<size_t>( h ) );
    for ( int r = 0; r < h; ++r )
        CHECK( d.path[r] == 5 ); // clear corridor furthest from the cloudy one
}
