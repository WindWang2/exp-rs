// test_r24_a_temporal_pins.cpp — R24-A science track.
//
// Golden-value pins for the banded Whittaker smoother at n == 3 and n == 4.
// The n == 3 diagonal is correct only through a double subtraction
// (main[1] and main[n-2] alias the same index, each removing 1λ from the
// 6λ default → the required 4λ of the single-row DᵀD Gram (1,4,1)). A
// future reader who "fixes" the aliasing without restoring 4λ would
// regress n == 3 by ~4×; these hand-derived pins (exact rational golden
// values, independent of the implementation's matrix assembly) close that
// hole. Audit provenance: an R24-A exploration pass mis-flagged this as a
// 5λ defect; hand solving the true objective below disproved it — the
// pins document the verified-correct values so the conclusion survives.
//
// Objective: minimize Σ (y_i − z_i)² + λ Σ (Δ²z)².
//   n == 3, y = (0,1,0), λ = 10: system [[11,−20,10],[−20,41,−20],[10,−20,11]]
//     → z = (20/61, 21/61, 20/61) ≈ (0.3279, 0.3443, 0.3279)
//   n == 4, y = (0,1,1,0), λ = 10 (symmetric): z = (10/21, 11/21, 11/21, 10/21)

#include "processing/algorithms/temporal/temporal_fit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using sicnu::temporal::whittakerSmooth;

TEST_CASE( "whittakerSmooth n==3 golden pin (aliased diagonal yields 4λ)",
           "[processing][temporal][whittaker][r24_a]" )
{
    const std::vector<float> y = { 0.0f, 1.0f, 0.0f };
    const std::vector<float> z = whittakerSmooth( y, {}, 10.0 );
    REQUIRE( z.size() == 3 );
    const double g0 = 20.0 / 61.0;
    const double g1 = 21.0 / 61.0;
    CHECK( z[0] == Catch::Approx( g0 ).epsilon( 1e-9 ).margin( 1e-6 ) );
    CHECK( z[1] == Catch::Approx( g1 ).epsilon( 1e-9 ).margin( 1e-6 ) );
    CHECK( z[2] == Catch::Approx( g0 ).epsilon( 1e-9 ).margin( 1e-6 ) );
}

TEST_CASE( "whittakerSmooth n==4 golden pin (standard [1,5,5,1]λ diagonal)",
           "[processing][temporal][whittaker][r24_a]" )
{
    const std::vector<float> y = { 0.0f, 1.0f, 1.0f, 0.0f };
    const std::vector<float> z = whittakerSmooth( y, {}, 10.0 );
    REQUIRE( z.size() == 4 );
    const double ga = 10.0 / 21.0;
    const double gb = 11.0 / 21.0;
    CHECK( z[0] == Catch::Approx( ga ).epsilon( 1e-9 ).margin( 1e-6 ) );
    CHECK( z[1] == Catch::Approx( gb ).epsilon( 1e-9 ).margin( 1e-6 ) );
    CHECK( z[2] == Catch::Approx( gb ).epsilon( 1e-9 ).margin( 1e-6 ) );
    CHECK( z[3] == Catch::Approx( ga ).epsilon( 1e-9 ).margin( 1e-6 ) );
}

TEST_CASE( "whittakerSmooth n<3 degenerate cases pass through",
           "[processing][temporal][whittaker][r24_a]" )
{
    const std::vector<float> one = whittakerSmooth( { 7.0f }, {}, 10.0 );
    REQUIRE( one.size() == 1 );
    CHECK( one[0] == 7.0f );
    const std::vector<float> two = whittakerSmooth( { 1.0f, 3.0f }, {}, 10.0 );
    REQUIRE( two.size() == 2 );
    CHECK( two[0] == 1.0f );
    CHECK( two[1] == 3.0f );
}
