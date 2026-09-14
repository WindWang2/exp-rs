// tests/test_d16_starfm.cpp — D16 Package F: STARFM-class spatiotemporal
// fusion. Truth: exact delta-reproduction identities on homogeneous scenes
// and hand-computed single-candidate weights.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/spatiotemporal_filter.h"

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using sicnu::temporal::SpatiotemporalFilter;
using sicnu::temporal::StarfmOptions;

namespace
{

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

std::vector<float> plane( int w, int h, float v )
{
    return std::vector<float>( static_cast<std::size_t>( w ) * h, v );
}

} // namespace

TEST_CASE( "Homogeneous scenes reproduce a uniform coarse step exactly",
           "[d16][starfm]" )
{
    const int w = 15, h = 15;
    const std::vector<float> fine0 = plane( w, h, 0.40f );
    const std::vector<float> coarse0 = fine0;
    std::vector<float> coarseK = plane( w, h, 0.52f ); // uniform +0.12 step

    StarfmOptions options;
    options.windowRadius = 2;
    const auto fineK =
        SpatiotemporalFilter::predictStarfm( fine0.data(), coarse0.data(), coarseK.data(), w, h,
                                             options );
    REQUIRE( fineK.size() == fine0.size() );

    // Every candidate prediction is (C_k + F_0 − C_0) = 0.52 regardless of
    // weights, so the output must equal the stepped value everywhere.
    for ( std::size_t i = 0; i < fineK.size(); ++i )
    {
        INFO( "i=" << i << " v=" << fineK[i] );
        REQUIRE( fineK[i] == Approx( 0.52 ).margin( 1e-5 ) );
    }
}

TEST_CASE( "Center pixel reproduces the coarse temporal change within 1e-4",
           "[d16][starfm]" )
{
    // Step scene: left half 0.3, right half 0.6 at base time. Coarse pair
    // identical to fine (perfect sensor). At t_k the coarse gains a uniform
    // +0.12 step: for a center pixel on the homogeneous left half, all
    // admitted left-half candidates share the same delta.
    const int w = 15, h = 15;
    std::vector<float> scene( static_cast<std::size_t>( w ) * h );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
            scene[static_cast<std::size_t>( y ) * w + x] = x < 8 ? 0.3f : 0.6f;
    std::vector<float> coarseK( scene.size() );
    for ( std::size_t i = 0; i < scene.size(); ++i )
        coarseK[i] = scene[i] + 0.12f;

    StarfmOptions options;
    options.windowRadius = 2;
    const auto fineK =
        SpatiotemporalFilter::predictStarfm( scene.data(), scene.data(), coarseK.data(), w, h,
                                             options );
    REQUIRE( fineK.size() == scene.size() );

    const std::size_t centerIdx = 7 * w + 4; // well inside the left half
    const float delta = fineK[centerIdx] - scene[centerIdx];
    INFO( "delta=" << delta );
    REQUIRE( delta == Approx( 0.12 ).margin( 1e-4 ) ); // D16 §F tolerance
    REQUIRE( fineK[centerIdx] >= 0.0f );
    REQUIRE( fineK[centerIdx] <= 1.0f );

    // The boundary pixel sees both halves; the prediction stays physical
    // and near its own change (+0.12 from its own coarse cell is exact for
    // the self-candidate, and the window average cannot invert the step).
    const std::size_t boundaryIdx = 7 * w + 7;
    REQUIRE( fineK[boundaryIdx] >= 0.3f );
    REQUIRE( fineK[boundaryIdx] <= 0.75f );
}
