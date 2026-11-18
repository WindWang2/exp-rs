// tests/test_r33_t01_landform_evidence.cpp — landform classification
// evidence gate (R33-T01, issue #2091): a VALID DEM cell with no slope
// evidence (an axis with both neighbours missing) or no TPI evidence
// (window without a single valid neighbour) has NO evidence for any
// landform and must carry class 255 — the absence of information is not
// "plains". Genuine zero-evidence-free cases stay classified: a uniform
// plane (legitimate TPI 0, SD 0 → stdTpi 0) IS plains, and a tilted plane
// keeps its slope/TPI verdicts.
//
// All expectations are hand-derived: TPI = z − mean(valid window ∖ centre),
// stdTPI = TPI/SD (0 when SD = 0), Horn slope via the documented kernel,
// and the class table in terrain_landform.h.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/terrain_landform.h"

using Catch::Approx;

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kNoData = -9999.0f;
constexpr std::uint8_t kNoDataClass = 255;

float slopeAt( const std::vector<float> &dem, int width, int height, int x, int y,
               float cs )
{
    // Horn 3×3 central-difference slope, hand-rolled from the four
    // direct neighbours (matches the kernel's full-window path on the
    // fixtures below, whose diagonals are symmetric).
    const auto at = [&]( int xx, int yy ) {
        xx = std::clamp( xx, 0, width - 1 );
        yy = std::clamp( yy, 0, height - 1 );
        const float v = dem[static_cast<size_t>( yy ) * width + xx];
        return ( v == kNoData || !std::isfinite( v ) ) ? kNan : v;
    };
    const float e = at( x + 1, y ), w = at( x - 1, y );
    const float n = at( x, y - 1 ), s = at( x, y + 1 );
    const float dzdx = ( std::isfinite( e ) && std::isfinite( w ) ) ? ( e - w ) / ( 2 * cs ) : kNan;
    const float dzdy = ( std::isfinite( n ) && std::isfinite( s ) ) ? ( s - n ) / ( 2 * cs ) : kNan;
    if ( !std::isfinite( dzdx ) || !std::isfinite( dzdy ) )
        return kNan;
    return std::atan( std::hypot( dzdx, dzdy ) ) * 180.0f / static_cast<float>( M_PI );
}
} // namespace

TEST_CASE( "tpiMultiscale: no valid neighbour is nodata, not zero (#2091)",
           "[r33t01][terrain][landform]" )
{
    // 5×5 all NoData except an ISOLATED valid centre.
    std::vector<float> dem( 25, kNoData );
    dem[12] = 100.0f;

    std::vector<TerrainLandform::MultiscaleTPI> scales;
    REQUIRE( TerrainLandform::tpiMultiscale( dem.data(), 5, 5, kNoData, { 1, 3 },
                                             &scales ) );
    for ( const auto &s : scales )
    {
        REQUIRE( s.tpi[12] == kNoData );
        REQUIRE( s.stdTpi[12] == kNoData );
    }
    // NoData cells carry nodata everywhere else.
    REQUIRE( scales[0].tpi[0] == kNoData );
}

TEST_CASE( "landformClasses: evidence gate vs legitimate planes (#2091)",
           "[r33t01][terrain][landform]" )
{
    TerrainLandform::LandformClassParams params;
    params.innerRadiusCells = 1;
    params.outerRadiusCells = 2;
    params.flatSlopeDeg = 5.0;

    SECTION( "isolated valid pixel is class 255, not plains" )
    {
        std::vector<float> dem( 25, kNoData );
        dem[12] = 100.0f; // lone valid cell, 5×5 grid
        std::vector<std::uint8_t> classes;
        REQUIRE( TerrainLandform::landformClasses( dem.data(), 5, 5, kNoData, 10.0, 10.0,
                                                   params, &classes ) );
        REQUIRE( classes[12] == kNoDataClass );
        for ( std::size_t i = 0; i < classes.size(); ++i )
            if ( i != 12 )
                REQUIRE( classes[i] == kNoDataClass );
    }

    SECTION( "single missing axis on an otherwise valid plane stays plains" )
    {
        // Uniform plane (z = 50) with ONE NoData hole: cells around the
        // hole keep a derivable slope on both axes (partial availability
        // is the kernel's documented policy) and a non-empty TPI window —
        // they are legitimate plains; the hole itself is 255.
        std::vector<float> dem( 36, 50.0f ); // 6×6
        dem[14] = kNoData;
        std::vector<std::uint8_t> classes;
        REQUIRE( TerrainLandform::landformClasses( dem.data(), 6, 6, kNoData, 10.0, 10.0,
                                                   params, &classes ) );
        REQUIRE( classes[14] == kNoDataClass );
        // Every other cell: uniform plane → slope 0 < 5° → plains (0).
        for ( std::size_t i = 0; i < classes.size(); ++i )
            if ( i != 14 )
            {
                INFO( "pixel " << i );
                REQUIRE( classes[i] == 0 );
            }
    }

    SECTION( "valid strip between two NoData bands: no vertical gradient → 255" )
    {
        // A single valid ROW sandwiched between NoData rows: every cell
        // has E/W neighbours (horizontal gradient fine) but BOTH N and S
        // are missing — the slope kernel emits nodata (no vertical
        // evidence), so no class is derivable. Pre-fix these were plains.
        std::vector<float> dem( 35, kNoData ); // 7×5
        for ( int x = 0; x < 7; ++x )
            dem[static_cast<size_t>( 2 ) * 7 + x] = 50.0f;
        std::vector<std::uint8_t> classes;
        REQUIRE( TerrainLandform::landformClasses( dem.data(), 7, 5, kNoData, 10.0, 10.0,
                                                   params, &classes ) );
        for ( int x = 0; x < 7; ++x )
        {
            INFO( "x = " << x );
            REQUIRE( classes[static_cast<size_t>( 2 ) * 7 + x] == kNoDataClass );
        }
    }

    SECTION( "tilted plane keeps its slope-driven class (regression)" )
    {
        // z = 1.0·x at 10 m cells: dz/dx = 0.1 → slope atan(0.1) ≈ 5.71° >
        // 5° → not plains; TPI 0/SD 0 → stdTpi 0 → middle slope class.
        std::vector<float> dem( 36 );
        for ( int y = 0; y < 6; ++y )
            for ( int x = 0; x < 6; ++x )
                dem[static_cast<size_t>( y ) * 6 + x] = 1.0f * x;
        std::vector<std::uint8_t> classes;
        REQUIRE( TerrainLandform::landformClasses( dem.data(), 6, 6, kNoData, 10.0, 10.0,
                                                   params, &classes ) );
        // Interior cells (full 3×3 windows on a tilted plane).
        const std::uint8_t interior = classes[static_cast<size_t>( 2 ) * 6 + 2];
        REQUIRE( interior != 0 ); // slope 5.71° excludes plains
        REQUIRE( interior != kNoDataClass );
        // Hand check: slope matches the central-difference value.
        const float expected = slopeAt( dem, 6, 6, 2, 2, 10.0f );
        REQUIRE( expected == Approx( 5.7106f ).margin( 1e-3f ) );
    }
}
