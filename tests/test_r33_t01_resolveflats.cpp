// tests/test_r33_t01_resolveflats.cpp — resolveFlats output contract on
// degenerate surfaces (R33-T01, issue #2095): a successful return must
// leave @p filled initialized for EVERY cell — in particular an all-NoData
// DEM used to return with the caller's buffer untouched (zeros from a
// value-initialized std::vector), which downstream D8/D∞ then ingested as
// elevation 0 m. Also pins the working cases: plateau drainage keeps the
// epsilon raise (magnitude-aware), NaN/finite sentinels pass through, and
// a REUSED buffer cannot leave stale pixels from a previous call.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/terrain_hydrology.h"

using Catch::Approx;

namespace
{
constexpr float kNoData = -9999.0f;
}

TEST_CASE( "resolveFlats initializes the output on an all-NoData DEM (#2095)",
           "[r33t01][terrain][hydrology]" )
{
    constexpr int kN = 4;
    std::vector<float> dem( kN * kN, kNoData );

    // The exact caller pattern (rs_terrain_flow_operator): a
    // value-initialized buffer of zeros.
    std::vector<float> filled( kN * kN, 0.0f );
    TerrainHydrology::FlatResolutionReport report;
    REQUIRE( TerrainHydrology::resolveFlats( dem.data(), filled.data(), kN, kN, kNoData,
                                             &report ) );
    for ( float v : filled )
        REQUIRE( v == kNoData ); // the invalid surface, not caller zeros
    REQUIRE( report.epsilon == 0.0 );
    REQUIRE( report.raisedCells == 0 );

    // Mixed invalid values (NaN + sentinel) pass through VERBATIM.
    std::vector<float> mixed( kN * kN );
    mixed.assign( kN * kN, kNoData );
    mixed[5] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE( TerrainHydrology::resolveFlats( mixed.data(), filled.data(), kN, kN, kNoData,
                                             nullptr ) );
    for ( int i = 0; i < kN * kN; ++i )
    {
        if ( i == 5 )
            REQUIRE( std::isnan( filled[5] ) );
        else
            REQUIRE( filled[i] == kNoData );
    }
}

TEST_CASE( "resolveFlats reuses a dirty buffer without stale pixels (#2095)",
           "[r33t01][terrain][hydrology]" )
{
    constexpr int kN = 5;
    // A plateau at z = 100 surrounded by a drain at z = 0 (the border):
    // every interior cell must be raised by epsilon steps toward the
    // border; a reused buffer poisoned with 500s must not survive.
    std::vector<float> dem( kN * kN, 100.0f );
    for ( int x = 0; x < kN; ++x )
    {
        dem[x] = 0.0f;
        dem[static_cast<std::size_t>( kN - 1 ) * kN + x] = 0.0f;
    }
    for ( int y = 0; y < kN; ++y )
    {
        dem[static_cast<std::size_t>( y ) * kN] = 0.0f;
        dem[static_cast<std::size_t>( y ) * kN + kN - 1] = 0.0f;
    }

    std::vector<float> filled( kN * kN, 500.0f ); // dirty reuse
    TerrainHydrology::FlatResolutionReport report;
    REQUIRE( TerrainHydrology::resolveFlats( dem.data(), filled.data(), kN, kN, kNoData,
                                             &report ) );

    // Border drains keep z = 0; every interior cell sits in (100, 100+eps]
    // and STRICTLY above 100 only where raised — but never the stale 500.
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
        {
            const float v = filled[static_cast<std::size_t>( y ) * kN + x];
            REQUIRE( v != 500.0f );
            if ( x == 0 || y == 0 || x == kN - 1 || y == kN - 1 )
                REQUIRE( v == 0.0f );
            else
                REQUIRE( v >= 100.0f );
        }
    // The centre cell is strictly above the plateau (drainage gradient).
    REQUIRE( filled[static_cast<std::size_t>( 2 ) * kN + 2] > 100.0f );
    REQUIRE( report.raisedCells > 0 );
    REQUIRE( report.epsilon > 0.0 );
}

TEST_CASE( "resolveFlats high-altitude plateau epsilon survives float32 (#2095 context)",
           "[r33t01][terrain][hydrology]" )
{
    // PROC-A2-1 regression pin at z ≈ 4000 m: the plateau interior must
    // take REAL float steps (magnitude-aware epsilon). Geometry: north
    // border (y=0) drains at 3900; rows 1..3 and all borders sit at 4000.
    // Row 1 already drains downhill (no raise); the perimeter rows
    // (including y=3) are drain-seeded; only row 2 needs the epsilon lift.
    constexpr int kN = 4;
    std::vector<float> dem( kN * kN, 4000.0f );
    for ( int x = 0; x < kN; ++x )
        dem[x] = 3900.0f; // north border drains
    std::vector<float> filled( kN * kN );
    TerrainHydrology::FlatResolutionReport report;
    REQUIRE( TerrainHydrology::resolveFlats( dem.data(), filled.data(), kN, kN, kNoData,
                                             &report ) );
    const float row1 = filled[static_cast<std::size_t>( 1 ) * kN + 1];
    const float row2 = filled[static_cast<std::size_t>( 2 ) * kN + 1];
    const float row3 = filled[static_cast<std::size_t>( 3 ) * kN + 1];
    REQUIRE( row1 == 4000.0f );           // drains downhill — untouched
    REQUIRE( row2 > 4000.0f );            // REAL float step at z ≈ 4000
    REQUIRE( row3 == 4000.0f );           // perimeter-seeded
    REQUIRE( report.raisedCells > 0 );
    REQUIRE( report.epsilon > 0.0 );
}
