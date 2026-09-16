// test_terrain_landform.cpp — terrain-hydrology-11 Phase 3: multiscale TPI
// and Weiss classes, and geomorphon patterns against closed-form/structural
// truths on synthetic surfaces.

#include "processing/algorithms/terrain_landform.h"
#include "synthetic_terrain_dem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace SyntheticTerrain;
using namespace TerrainLandform;

namespace
{
constexpr float kNo = -9999.0f;
}

TEST_CASE( "multiscale TPI is exactly zero on a plane at every scale",
           "[terrain][landform]" )
{
    const Grid g = plane( 13, 13, 0.3, -0.7, 42.0 );
    std::vector<MultiscaleTPI> scales;
    REQUIRE( tpiMultiscale( g.z.data(), 13, 13, kNo, { 2, 5 }, &scales ) );
    REQUIRE( scales.size() == 2 );
    // A plane has zero TPI wherever the full window fits (interior cells);
    // truncated edge windows on a ramp legitimately deviate from zero.
    for ( std::size_t s = 0; s < scales.size(); ++s )
    {
        const int r = scales[s].radiusCells;
        for ( int y = r; y < 13 - r; ++y )
            for ( int x = r; x < 13 - r; ++x )
            {
                INFO( "scale " << r << " cell (" << x << "," << y
                               << ") tpi=" << scales[s].tpi[y * 13 + x] );
                CHECK( scales[s].tpi[y * 13 + x] == Catch::Approx( 0.0 ).margin( 1e-3 ) );
            }
    }
}

TEST_CASE( "multiscale TPI on a V-ridge matches the hand-computed box mean",
           "[terrain][landform]" )
{
    // Ridge z = 50 − |col − 10|, radius 1 (3×3 box, centre excluded).
    // Crest cell (10,y): the 8 neighbours are {49×6 (cols 9/11 × 3 rows),
    // 50×2 (col 10, rows ±1)} → mean 49.25 → TPI = +0.75 (hand-computed).
    // Flank cell (8,y): locally planar → TPI 0.
    const int w = 21;
    const int h = 9;
    const Grid g = ridge( w, h, 10.0, 50.0, 1.0 );
    std::vector<MultiscaleTPI> scales;
    REQUIRE( tpiMultiscale( g.z.data(), w, h, kNo, { 1 }, &scales ) );
    const int y = 4;
    CHECK( scales[0].tpi[y * w + 10] == Catch::Approx( 0.75 ).epsilon( 1e-4 ) );
    CHECK( scales[0].tpi[y * w + 8] == Catch::Approx( 0.0 ).epsilon( 1e-4 ) );
    CHECK( scales[0].tpi[y * w + 12] == Catch::Approx( 0.0 ).epsilon( 1e-4 ) );
    // Standardised TPI on the crest: var{49×6,50×2} = 0.1875 → SD = 3/8·√…
    // SD = sqrt(0.1875) = √3/4 → stdTPI = 0.75/(√3/4) = √3 (hand-computed).
    CHECK( scales[0].stdTpi[y * w + 10]
           == Catch::Approx( std::sqrt( 3.0 ) ).epsilon( 1e-3 ) );
}

TEST_CASE( "Weiss classes: crest is upper slope/peak, flanks middle",
           "[terrain][landform]" )
{
    // Broad ridge, low flat threshold → crest classified peak or upper
    // slope (inner window also crest-dominated), flanks middle slope.
    const int w = 41;
    const int h = 9;
    const Grid g = ridge( w, h, 20.0, 30.0, 1.0 );
    LandformClassParams params;
    params.innerRadiusCells = 2;
    params.outerRadiusCells = 8;
    params.flatSlopeDeg = 2.0;
    std::vector<std::uint8_t> classes;
    REQUIRE( landformClasses( g.z.data(), w, h, kNo, 1.0, 1.0, params, &classes ) );
    const int y = 4;
    // Col 19 (one cell off the crest): stdTPI_outer ≈ 1.29 > 1σ,
    // stdTPI_inner ≈ 0.39 ≤ 1σ → upper slope (4), hand-derived.
    const std::uint8_t upper = classes[y * w + 19];
    CHECK( upper == 4 );
    // Col 16: hand-derived stdTPI_outer ≈ 0.32 (∈ ±1σ), stdTPI_inner = 0
    // → middle slope (3). (Col 14 is lower slope: inner std ≈ −1.07.)
    const std::uint8_t midflank = classes[y * w + 16];
    CHECK( midflank == 3 );
    // Plains: a constant grid is flat → code 0 everywhere.
    const Grid flat = plateau( 9, 9, 7.0 );
    REQUIRE( landformClasses( flat.z.data(), 9, 9, kNo, 1.0, 1.0, params, &classes ) );
    for ( const std::uint8_t c : classes )
        CHECK( c == 0 );
}

TEST_CASE( "geomorphon: peak, pit, flat and ridge patterns are exact",
           "[terrain][landform]" )
{
    GeomorphonParams params;
    params.searchRadiusCells = 10;
    params.flatRadiusCells = 1;
    params.flatThreshDeg = 2.0;

    // Pit: every direction climbs → all +1 → form 2 (pit). Use the pit
    // grid; observer at the bottom.
    const int w = 31;
    const int h = 31;
    const Grid bowl = pit( w, h, 15.0, 15.0, 25.0, 1.0 );
    GeomorphonResult res;
    REQUIRE( geomorphon( bowl.z.data(), w, h, kNo, 1.0, 1.0, params, &res ) );
    // Direction order N,NE,…,NW with +1 = terrain higher. At the centre the
    // slope is 1:1 → every direction angle ≈ 45° ≫ flat threshold.
    const std::size_t c = 15 * w + 15;
    CHECK( res.form[c] == 2 );

    // Peak: all directions descend → all −1 → form 1. Invert the bowl.
    Grid mount = bowl;
    for ( std::size_t i = 0; i < mount.z.size(); ++i )
        if ( mount.z[i] != kNo )
            mount.z[i] = 50.0f - mount.z[i];
    REQUIRE( geomorphon( mount.z.data(), w, h, kNo, 1.0, 1.0, params, &res ) );
    CHECK( res.form[c] == 1 );

    // Flat: plateau → form 0.
    const Grid flat = plateau( 21, 21, 3.0 );
    REQUIRE( geomorphon( flat.z.data(), 21, 21, kNo, 1.0, 1.0, params, &res ) );
    CHECK( res.form[10 * 21 + 10] == 0 );
    // All-flat: every direction digit is code 0 → digit value 1 →
    // pattern = Σ 3^i = 3280.
    CHECK( res.pattern[10 * 21 + 10] == 3280 );

    // Ridge crest (N–S): E/W lower (−1), N/S flat-ish (0) → form 3.
    const Grid rg = ridge( 31, 31, 15.0, 20.0, 2.0 );
    REQUIRE( geomorphon( rg.z.data(), 31, 31, kNo, 1.0, 1.0, params, &res ) );
    const std::size_t crest = 15 * 31 + 15;
    CHECK( res.form[crest] == 3 );
    // Decode: N digit 0 (flat along crest), E digit 4·? E = digit index 2 →
    // code −1 → digit value 0; pattern must equal 0·N?  Actually N=0,
    // NE=−1, E=−1, SE=−1, S=0, SW=−1, W=−1, NW=−1 → digits (c+1):
    // [1,0,0,0,1,0,0,0] → Σ digit·3^i = 1 + 3^4 = 82.
    CHECK( res.pattern[crest] == 82 );
}

TEST_CASE( "geomorphon: slope flank classifies as slope, NoData passthrough",
           "[terrain][landform]" )
{
    GeomorphonParams params;
    params.searchRadiusCells = 8;
    params.flatRadiusCells = 1;
    params.flatThreshDeg = 2.0;

    // Uniform west-high planar flank (z = −x): W side higher (+1), E side
    // lower (−1), flat along the N/S crest line. Pattern digits
    // [1,0,0,0,1,2,2,2] (N,NE,E,SE,S,SW,W,NW) → one + arc (SW-W-NW) and
    // one − arc (NE-E-SE) → form 5 (slope).
    const int w = 25;
    const int h = 25;
    const Grid g = plane( w, h, -1.0, 0.0, 0.0 );
    GeomorphonResult res;
    REQUIRE( geomorphon( g.z.data(), w, h, kNo, 1.0, 1.0, params, &res ) );
    CHECK( res.form[12 * w + 12] == 5 );

    // NoData cells carry the sentinels.
    Grid gd = g;
    pokeNodata( gd, 6, 6 );
    REQUIRE( geomorphon( gd.z.data(), w, h, kNo, 1.0, 1.0, params, &res ) );
    CHECK( res.pattern[6 * w + 6] == 65535 );
    CHECK( res.form[6 * w + 6] == 255 );

    // Cancellation aborts.
    CHECK_FALSE( geomorphon( g.z.data(), w, h, kNo, 1.0, 1.0, params, &res,
                             [] { return true; } ) );
}
