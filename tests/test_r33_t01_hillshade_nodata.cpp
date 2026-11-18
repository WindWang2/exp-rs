// tests/test_r33_t01_hillshade_nodata.cpp — multidirectional hillshade
// NoData fidelity (R33-T01, issue #2094): the 8-direction mean must carry
// the EXACT sentinel on invalid pixels instead of accumulating nodata/8
// eight times (float32 cannot represent e.g. -99999.9/8 — the rounding
// drift resurfaces the NoData pixel as a huge "valid" shade).
//
// Oracle: per-direction hillshade on the same fixture — valid pixels equal
// the mean of the 8 single-direction shades; invalid pixels are exactly
// nodata; legitimate shadow-0 and lit-255 extremes survive the averaging.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/terrain_analysis.h"

using Catch::Approx;

namespace
{
constexpr float kNoData = -9999.0f;

/// A 7×7 fixture with a centre peak: exercises lit, shadow and slope faces
/// plus a NoData hole. Sentinel chosen NOT divisible by 8 in float32 to
/// pin the drift regression (the issue's -99999.9 class of sentinels).
constexpr float kSentinel = -99999.9f;

std::vector<float> peakDem()
{
    constexpr int kN = 7;
    std::vector<float> dem( kN * kN, 10.0f );
    dem[static_cast<size_t>( 3 ) * kN + 3] = 80.0f;
    dem[static_cast<size_t>( 1 ) * kN + 1] = kSentinel; // hole
    return dem;
}
} // namespace

TEST_CASE( "hillshadeMultidirectional keeps exact NoData and the 8-way mean (#2094)",
           "[r33t01][terrain][hillshade]" )
{
    constexpr int kN = 7;
    const std::vector<float> dem = peakDem();

    std::vector<float> multi( kN * kN, 12345.0f ); // reused buffer, poisoned
    REQUIRE( TerrainAnalysis::hillshadeMultidirectional(
                 dem.data(), multi.data(), kN, kN, 10.0f, 10.0f, kSentinel, 45.0f ) );

    // Independent oracle: mean of the 8 per-direction shades on pixels
    // where EVERY direction has evidence; a valid centre lacking gradient
    // evidence in some direction (beside the hole) carries the sentinel.
    std::vector<float> direction( kN * kN );
    std::vector<double> mean( kN * kN, 0.0 );
    std::vector<unsigned char> fullEvidence( kN * kN, 1 );
    for ( int k = 0; k < 8; ++k )
    {
        REQUIRE( TerrainAnalysis::hillshade( dem.data(), direction.data(), kN, kN, 10.0f,
                                             10.0f, kSentinel, 45.0f * k, 45.0f ) );
        for ( int i = 0; i < kN * kN; ++i )
        {
            if ( dem[i] == kSentinel )
            {
                INFO( "direction " << k << " pixel " << i );
                REQUIRE( direction[i] == kSentinel );
                fullEvidence[i] = 0;
                continue;
            }
            if ( direction[i] == kSentinel )
                fullEvidence[i] = 0;
            else
                mean[i] += direction[i];
        }
    }

    for ( int i = 0; i < kN * kN; ++i )
    {
        if ( !fullEvidence[i] )
        {
            // EXACT sentinel — the /8 accumulation would drift by ULPs.
            REQUIRE( multi[i] == kSentinel );
        }
        else
        {
            INFO( "pixel " << i );
            REQUIRE( multi[i] == Approx( mean[i] / 8.0 ).margin( 1e-3 ) );
        }
    }

    // The fixture exercises non-trivial shading: the steep FACE west of
    // the peak averages darker than the far flat corner (the azimuth-linear
    // terms cancel over the 8 directions; the slope cosine term does not).
    // (3,2) is full-evidence: the hole at (1,1) is two cells away.
    REQUIRE( multi[static_cast<size_t>( 3 ) * kN + 2]
             < multi[static_cast<size_t>( 5 ) * kN + 5] - 1e-3f );

    // Outputs on full-evidence pixels are finite and within the hillshade
    // gamut; evidence-less pixels carry the sentinel instead.
    for ( int i = 0; i < kN * kN; ++i )
        if ( fullEvidence[i] )
        {
            REQUIRE( std::isfinite( multi[i] ) );
            REQUIRE( multi[i] >= 0.0f );
        }
        else
        {
            REQUIRE( multi[i] == kSentinel );
        }
}

TEST_CASE( "hillshadeMultidirectional: unrepresentable sentinel never leaks (#2094)",
           "[r33t01][terrain][hillshade]" )
{
    constexpr int kN = 3;
    // Isolated sentinel pixel whose /8 rounding cannot reassemble exactly.
    std::vector<float> dem( kN * kN, 10.0f );
    dem[4] = kSentinel;
    std::vector<float> multi( kN * kN );
    REQUIRE( TerrainAnalysis::hillshadeMultidirectional(
                 dem.data(), multi.data(), kN, kN, 10.0f, 10.0f, kSentinel, 45.0f ) );
    REQUIRE( multi[4] == kSentinel );

    // NaN input (non-finite invalid) carries the sentinel too.
    std::vector<float> nanDem( kN * kN, 10.0f );
    nanDem[4] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE( TerrainAnalysis::hillshadeMultidirectional(
                 nanDem.data(), multi.data(), kN, kN, 10.0f, 10.0f, kSentinel, 45.0f ) );
    REQUIRE( multi[4] == kSentinel );
}
