// test_terrain.cpp — Phase 11.2: Terrain analysis tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/terrain_analysis.h"

#include <cmath>
#include <vector>

using Catch::Approx;

static const float NODATA = -9999.0f;

// Helper: create a flat DEM (all same elevation)
static std::vector<float> flatDem( int w, int h, float elev = 100.0f )
{
    return std::vector<float>( w * h, elev );
}

// Helper: create a tilted DEM (elevation increases from left to right)
static std::vector<float> tiltedDem( int w, int h, float base = 100.0f, float step = 10.0f )
{
    std::vector<float> dem( w * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
            dem[r * w + c] = base + c * step;
    return dem;
}

// ===========================================================================
// Slope
// ===========================================================================

TEST_CASE( "Slope: flat DEM yields zero slope", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::slope( dem.data(), out.data(), W, H, 1.0f, NODATA ) );

    for ( int i = 0; i < W * H; ++i )
        REQUIRE( out[i] == Approx( 0.0f ).margin( 0.01f ) );
}

TEST_CASE( "Slope: tilted DEM yields positive slope", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = tiltedDem( W, H, 100.0f, 10.0f );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::slope( dem.data(), out.data(), W, H, 1.0f, NODATA ) );

    // Interior cells should have positive slope (gradient = 10/1 = 10)
    // slope = atan(10) ≈ 84.3°
    float centerSlope = out[5 * W + 5];
    REQUIRE( centerSlope > 80.0f );
    REQUIRE( centerSlope < 90.0f );
}

TEST_CASE( "Slope: nodata preserved", "[terrain]" )
{
    const int W = 5, H = 5;
    auto dem = flatDem( W, H );
    dem[2 * W + 2] = NODATA;
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::slope( dem.data(), out.data(), W, H, 1.0f, NODATA ) );
    REQUIRE( out[2 * W + 2] == NODATA );
}

TEST_CASE( "Slope: null input returns false", "[terrain]" )
{
    std::vector<float> out( 25 );
    REQUIRE_FALSE( TerrainAnalysis::slope( nullptr, out.data(), 5, 5, 1.0f, NODATA ) );
}

// ===========================================================================
// Aspect
// ===========================================================================

TEST_CASE( "Aspect: flat DEM yields -1 (undefined)", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::aspect( dem.data(), out.data(), W, H, 1.0f, NODATA ) );

    for ( int i = 0; i < W * H; ++i )
        REQUIRE( out[i] == Approx( -1.0f ) );
}

TEST_CASE( "Aspect: east-increasing DEM yields west-facing (270°)", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = tiltedDem( W, H, 100.0f, 10.0f );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::aspect( dem.data(), out.data(), W, H, 1.0f, NODATA ) );

    // Elevation increases to the right (east), so slope faces west (downhill compass azimuth = 270°).
    float centerAspect = out[5 * W + 5];
    REQUIRE( centerAspect > 260.0f );
    REQUIRE( centerAspect < 280.0f );
}

TEST_CASE( "Aspect: south-increasing DEM yields north-facing (~0° or ~360°)", "[terrain]" )
{
    const int W = 10, H = 10;
    // Elevation increases from top to bottom (south in image coords), so slope faces north (0°/360°).
    std::vector<float> dem( W * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            dem[r * W + c] = 100.0f + r * 10.0f;

    std::vector<float> out( W * H );
    REQUIRE( TerrainAnalysis::aspect( dem.data(), out.data(), W, H, 1.0f, NODATA ) );

    float centerAspect = out[5 * W + 5];
    REQUIRE( ( ( centerAspect >= 0.0f && centerAspect < 10.0f ) || centerAspect > 350.0f ) );
}

// ===========================================================================
// Hillshade
// ===========================================================================

TEST_CASE( "Hillshade: flat DEM yields constant value", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::hillshade( dem.data(), out.data(), W, H, 1.0f, NODATA,
                                         315.0f, 45.0f ) );

    // Flat surface → cos(zenith) = cos(45°) ≈ 0.707
    float expected = std::cos( 45.0f * M_PI / 180.0f );
    for ( int i = 0; i < W * H; ++i )
    {
        if ( out[i] != NODATA )
            REQUIRE( out[i] == Approx( expected ).margin( 0.05f ) );
    }
}

TEST_CASE( "Hillshade: output range [0, 1]", "[terrain]" )
{
    const int W = 20, H = 20;
    auto dem = tiltedDem( W, H, 100.0f, 5.0f );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::hillshade( dem.data(), out.data(), W, H, 1.0f, NODATA ) );

    for ( int i = 0; i < W * H; ++i )
    {
        REQUIRE( out[i] >= 0.0f );
        REQUIRE( out[i] <= 1.0f );
    }
}

// ===========================================================================
// Roughness
// ===========================================================================

TEST_CASE( "Roughness: flat DEM yields zero", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::roughness( dem.data(), out.data(), W, H, NODATA ) );

    for ( int i = 0; i < W * H; ++i )
        REQUIRE( out[i] == Approx( 0.0f ) );
}

TEST_CASE( "Roughness: step edge yields non-zero", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H, 100.0f );
    // Create a step: left half at 100, right half at 200
    for ( int r = 0; r < H; ++r )
        for ( int c = W / 2; c < W; ++c )
            dem[r * W + c] = 200.0f;

    std::vector<float> out( W * H );
    REQUIRE( TerrainAnalysis::roughness( dem.data(), out.data(), W, H, NODATA ) );

    // Boundary cells should have roughness = 100
    float boundaryRoughness = out[5 * W + 4]; // cell at boundary
    REQUIRE( boundaryRoughness == Approx( 100.0f ) );
}

// ===========================================================================
// TRI
// ===========================================================================

TEST_CASE( "TRI: flat DEM yields zero", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::tri( dem.data(), out.data(), W, H, NODATA ) );

    for ( int i = 0; i < W * H; ++i )
        REQUIRE( out[i] == Approx( 0.0f ) );
}

// ===========================================================================
// TPI
// ===========================================================================

TEST_CASE( "TPI: flat DEM yields zero", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H );
    std::vector<float> out( W * H );

    REQUIRE( TerrainAnalysis::tpi( dem.data(), out.data(), W, H, NODATA ) );

    for ( int i = 0; i < W * H; ++i )
        REQUIRE( out[i] == Approx( 0.0f ) );
}

TEST_CASE( "TPI: peak yields positive", "[terrain]" )
{
    const int W = 10, H = 10;
    auto dem = flatDem( W, H, 100.0f );
    dem[5 * W + 5] = 200.0f; // peak in center

    std::vector<float> out( W * H );
    REQUIRE( TerrainAnalysis::tpi( dem.data(), out.data(), W, H, NODATA ) );

    // Center cell (peak) should have positive TPI
    REQUIRE( out[5 * W + 5] > 0.0f );

    // Neighbor cells should have negative TPI (below peak)
    REQUIRE( out[4 * W + 5] < 0.0f );
}
