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

// ---------------------------------------------------------------------------
// Slice 3: NaN sentinels, physical clamping, window truncation at edges.
// ---------------------------------------------------------------------------

TEST_CASE( "NaN inputs stay NaN, outputs stay physical, edges truncate",
           "[d16][starfm]" )
{
    const int w = 9, h = 9;
    StarfmOptions options;
    options.windowRadius = 3;

    SECTION( "NaN center pixel propagates to a NaN output" )
    {
        std::vector<float> fine0 = plane( w, h, 0.4f );
        std::vector<float> coarse0 = fine0;
        std::vector<float> coarseK = plane( w, h, 0.5f );
        fine0[4 * w + 4] = kNan;
        const auto out = SpatiotemporalFilter::predictStarfm( fine0.data(), coarse0.data(),
                                                              coarseK.data(), w, h, options );
        REQUIRE( out.size() == fine0.size() );
        REQUIRE( std::isnan( out[4 * w + 4] ) );
        REQUIRE( out[0] == Approx( 0.5 ).margin( 1e-5 ) );
    }

    SECTION( "reflectance range [0,1] is enforced" )
    {
        // 0.95 + 0.12 would be 1.07 unclamped.
        std::vector<float> fine0 = plane( w, h, 0.95f );
        std::vector<float> coarse0 = fine0;
        std::vector<float> coarseK = plane( w, h, 1.07f );
        const auto out = SpatiotemporalFilter::predictStarfm( fine0.data(), coarse0.data(),
                                                              coarseK.data(), w, h, options );
        REQUIRE( out[4 * w + 4] == Approx( 1.0 ).margin( 1e-6 ) );

        std::vector<float> low0 = plane( w, h, 0.03f );
        std::vector<float> lowK = plane( w, h, 0.0f );
        const auto outLow = SpatiotemporalFilter::predictStarfm( low0.data(), low0.data(),
                                                                 lowK.data(), w, h, options );
        REQUIRE( outLow[4 * w + 4] == Approx( 0.0 ).margin( 1e-6 ) );
    }

    SECTION( "corner windows truncate without out-of-bounds reads" )
    {
        // Distinct radial gradient so weights matter but all candidates are
        // homogeneous (|f − f_center| = 0 within the whole plane).
        std::vector<float> fine0( static_cast<std::size_t>( w ) * h );
        for ( int y = 0; y < h; ++y )
            for ( int x = 0; x < w; ++x )
                fine0[static_cast<std::size_t>( y ) * w + x] =
                    static_cast<float>( 0.3 + 0.001 * ( x + y ) );
        std::vector<float> coarse0 = fine0;
        std::vector<float> coarseK( fine0.size() );
        for ( std::size_t i = 0; i < fine0.size(); ++i )
            coarseK[i] = fine0[i] + 0.05f;

        const auto out = SpatiotemporalFilter::predictStarfm( fine0.data(), coarse0.data(),
                                                              coarseK.data(), w, h, options );
        REQUIRE( out.size() == fine0.size() );
        // On a gradient every candidate predicts its own value + 0.05, so
        // the weighted mean carries a gradient bias bounded by the fine
        // spread inside the truncated window (slope 0.001/pixel, radius 3
        // -> <= 0.006). Truncation changes the support, not the physics:
        // every output stays finite, physical, and within that bound.
        for ( std::size_t i = 0; i < out.size(); ++i )
        {
            REQUIRE( std::isfinite( out[i] ) );
            REQUIRE( out[i] >= 0.0f );
            REQUIRE( out[i] <= 1.0f );
            REQUIRE( std::abs( out[i] - ( fine0[i] + 0.05f ) ) <= 0.01 );
        }
    }

    SECTION( "NaN coarse candidate is skipped, not propagated" )
    {
        std::vector<float> fine0 = plane( w, h, 0.4f );
        std::vector<float> coarse0 = fine0;
        std::vector<float> coarseK = plane( w, h, 0.52f );
        coarseK[4 * w + 4] = kNan; // the center's own coarse cell
        const auto out = SpatiotemporalFilter::predictStarfm( fine0.data(), coarse0.data(),
                                                              coarseK.data(), w, h, options );
        // The center still predicts from its neighbors (fallback path).
        REQUIRE( std::isfinite( out[4 * w + 4] ) );
        REQUIRE( out[4 * w + 4] >= 0.0f );
        REQUIRE( out[4 * w + 4] <= 1.0f );
    }
}

TEST_CASE( "Fully heterogeneous window falls back to the coarse temporal change",
           "[d16][starfm]" )
{
    // Center differs from every neighbor beyond the threshold: no candidate
    // is admitted, so the documented fallback carries the coarse change.
    const int w = 9, h = 9;
    std::vector<float> fine0 = plane( w, h, 0.3f );
    fine0[4 * w + 4] = 0.5f; // heterogeneous center
    std::vector<float> coarse0 = fine0;
    std::vector<float> coarseK( fine0.size() );
    for ( std::size_t i = 0; i < fine0.size(); ++i )
        coarseK[i] = fine0[i] + 0.1f;

    StarfmOptions options;
    options.windowRadius = 2;
    options.spectralThreshold = 0.0f; // nothing homogeneous -> everything gated
    const auto out = SpatiotemporalFilter::predictStarfm( fine0.data(), coarse0.data(),
                                                          coarseK.data(), w, h, options );
    INFO( "center=" << out[4 * w + 4] );
    // Fallback: base value + mean coarse change (+0.1).
    REQUIRE( out[4 * w + 4] == Approx( 0.6 ).margin( 1e-5 ) );

    // Neighbors (homogeneous windows) predict exactly through the kernel.
    REQUIRE( out[0] == Approx( 0.4 ).margin( 1e-5 ) );
}
