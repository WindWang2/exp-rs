// test_spectral_sparse_unmixing.cpp — sparse unmixing against closed-form
// oracles (identity dictionary => elementwise soft-threshold clamp; orthogonal
// dictionary => per-atom closed form), plus pure-pixel, overcomplete
// dictionary, collinearity refusals, sum-to-one penalty and honest
// non-convergence reporting.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/spectral_sparse_unmixing.h"

using namespace SpectralSparseUnmixing;
using Catch::Approx;

TEST_CASE( "Identity dictionary reduces to the elementwise soft-threshold clamp", "[sparse][kernel]" )
{
    // min 0.5*sum (x_i - a_i)^2 + lambda * sum a_i  s.t. a >= 0  has the
    // closed-form solution a_i = max(x_i - lambda, 0) for E = I.
    const int n = 6;
    std::vector<float> endmembers( static_cast<size_t>( n ) * n, 0.0f );
    for ( int i = 0; i < n; ++i )
        endmembers[static_cast<size_t>( i ) * n + i] = 1.0f;

    const std::vector<float> pixel = { 0.9f, 0.2f, -0.3f, 0.05f, 1.4f, 0.0f };
    const double lambda = 0.1;

    Config config;
    config.lambda = lambda;
    config.tolerance = 1e-12;
    config.maxIterations = 20000;

    SparseUnmixResult result;
    QString err;
    REQUIRE( unmixSparse( pixel.data(), 1, n, endmembers.data(), n, config, &result, &err ) );
    for ( int i = 0; i < n; ++i )
    {
        const double expected = std::max( 0.0, pixel[i] - lambda );
        CHECK( result.abundances[i] == Approx( expected ).margin( 1e-5 ) );
    }
    // Reconstruction error with E = I: RMSE of the thresholded residual.
    double sq = 0.0;
    for ( int i = 0; i < n; ++i )
    {
        const double a = std::max( 0.0, pixel[i] - lambda );
        sq += ( pixel[i] - a ) * ( pixel[i] - a );
    }
    CHECK( result.reconstructionError[0]
           == Approx( std::sqrt( sq / n ) ).margin( 1e-4 ) );
    // Reported abundance sum matches the closed-form sum.
    double sum = 0.0;
    for ( int i = 0; i < n; ++i )
        sum += std::max( 0.0, pixel[i] - lambda );
    CHECK( result.abundanceSums[0] == Approx( sum ).margin( 1e-4 ) );
    CHECK( result.converged[0] == 1 );
}

TEST_CASE( "Orthogonal dictionary has a per-atom closed form", "[sparse][kernel]" )
{
    // E = [[2,0],[0,3]] (2 bands, 2 atoms): the objective separates per atom,
    //   a_i = max( <x,e_i>/||e_i||^2 - lambda/||e_i||^2 , 0 ).
    const std::vector<float> endmembers = { 2.0f, 0.0f, 0.0f, 3.0f };
    const std::vector<float> pixel = { 0.6f, 0.9f };
    const double lambda = 0.05;

    Config config;
    config.lambda = lambda;
    config.tolerance = 1e-12;
    config.maxIterations = 20000;

    SparseUnmixResult result;
    QString err;
    REQUIRE( unmixSparse( pixel.data(), 1, 2, endmembers.data(), 2, config, &result, &err ) );
    const double expectedA = std::max( 0.0, ( 0.6 * 2.0 ) / 4.0 - lambda / 4.0 );
    const double expectedB = std::max( 0.0, ( 0.9 * 3.0 ) / 9.0 - lambda / 9.0 );
    CHECK( result.abundances[0] == Approx( expectedA ).margin( 1e-6 ) );
    CHECK( result.abundances[1] == Approx( expectedB ).margin( 1e-6 ) );
}

TEST_CASE( "A pure pixel is recovered with all mass on its atom", "[sparse][kernel]" )
{
    // x equals endmember 1 exactly: the zero-objective feasible point a=e1
    // is optimal; L1 cannot beat it.
    const std::vector<float> endmembers = { 0.6f, 0.2f, 0.2f, 0.1f, 0.4f, 0.5f };
    const std::vector<float> pixel = { 0.6f, 0.2f, 0.2f };

    Config config;
    config.lambda = 0.01;
    config.sumToOnePenalty = 100.0; // hard-ish pull onto the simplex
    config.tolerance = 1e-12;
    config.maxIterations = 20000;

    SparseUnmixResult result;
    QString err;
    REQUIRE( unmixSparse( pixel.data(), 1, 3, endmembers.data(), 2, config, &result, &err ) );
    CHECK( result.abundances[0] == Approx( 1.0 ).margin( 1e-3 ) );
    CHECK( result.abundances[1] == Approx( 0.0 ).margin( 1e-3 ) );
    CHECK( result.abundanceSums[0] == Approx( 1.0 ).margin( 1e-3 ) );
    CHECK( result.reconstructionError[0] == Approx( 0.0 ).margin( 1e-3 ) );
}

TEST_CASE( "An overcomplete dictionary is accepted and produces sparse support", "[sparse][kernel]" )
{
    // 3 bands, 4 atoms (FCLS requires n <= bands; sparse unmixing does not).
    const std::vector<float> endmembers = {
        0.7f, 0.1f, 0.1f, // atom 0
        0.1f, 0.7f, 0.1f, // atom 1
        0.1f, 0.1f, 0.7f, // atom 2
        0.4f, 0.4f, 0.1f, // atom 3
    };
    const std::vector<float> pixel = { 0.7f, 0.1f, 0.1f }; // ~= atom 0

    Config config;
    config.lambda = 0.02;
    SparseUnmixResult result;
    QString err;
    REQUIRE( unmixSparse( pixel.data(), 1, 3, endmembers.data(), 4, config, &result, &err ) );
    for ( int e = 0; e < 4; ++e )
        CHECK( result.abundances[e] >= -1e-6 );
    // Sparsity: at most two atoms carry mass for a pure pixel.
    int support = 0;
    for ( int e = 0; e < 4; ++e )
        if ( result.abundances[e] > 1e-3 )
            ++support;
    CHECK( support <= 2 );
    CHECK( result.abundances[0] == Approx( result.abundanceSums[0] ).margin( 1e-3 ) );
}

TEST_CASE( "Near-collinear dictionaries refuse; the threshold is the escape hatch", "[sparse][kernel]" )
{
    // Exact collinearity (proportional atoms).
    const std::vector<float> collinear = { 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f };
    const std::vector<float> pixel = { 0.6f, 0.6f, 0.6f };
    Config config;
    SparseUnmixResult result;
    QString err;
    CHECK_FALSE( unmixSparse( pixel.data(), 1, 3, collinear.data(), 2, config, &result, &err ) );
    CHECK( err.contains( "near-collinear" ) );

    // Near-collinear: 0.2 degrees apart < default 0.5 degree threshold.
    const double eps = 0.2 * 3.14159265358979323846 / 180.0;
    const std::vector<float> near = { 1.0f, 0.0f,
                                      static_cast<float>( std::cos( eps ) ),
                                      static_cast<float>( std::sin( eps ) ) };
    CHECK_FALSE( unmixSparse( pixel.data(), 1, 2, near.data(), 2, config, &result, &err ) );

    // Disabling the guard (threshold 0) is the documented escape hatch.
    Config relaxed;
    relaxed.collinearAngleDegrees = 0.0;
    relaxed.maxIterations = 5000;
    REQUIRE( unmixSparse( pixel.data(), 1, 2, near.data(), 2, relaxed, &result, &err ) );
}

TEST_CASE( "Zero and non-finite dictionaries refuse; NaN pixels stay NaN", "[sparse][kernel]" )
{
    const std::vector<float> zeroAtom = { 0.5f, 0.5f, 0.0f, 0.0f };
    const std::vector<float> pixel = { 0.5f, 0.5f };
    Config config;
    SparseUnmixResult result;
    QString err;
    CHECK_FALSE( unmixSparse( pixel.data(), 1, 2, zeroAtom.data(), 2, config, &result, &err ) );
    CHECK( err.contains( "zero" ) );

    const std::vector<float> nanAtom = { 0.5f, 0.5f, 0.3f, std::numeric_limits<float>::quiet_NaN() };
    CHECK_FALSE( unmixSparse( pixel.data(), 1, 2, nanAtom.data(), 2, config, &result, &err ) );

    // NaN pixel: NaN abundances/error, not an error return (unmix convention).
    const std::vector<float> endmembers = { 1.0f, 0.0f, 0.0f, 1.0f };
    const std::vector<float> nanPixel = { std::numeric_limits<float>::quiet_NaN(), 0.5f };
    REQUIRE( unmixSparse( nanPixel.data(), 1, 2, endmembers.data(), 2, config, &result, &err ) );
    CHECK( std::isnan( result.abundances[0] ) );
    CHECK( std::isnan( result.abundances[1] ) );
    CHECK( std::isnan( result.reconstructionError[0] ) );
    CHECK( result.converged[0] == 0 );
}

TEST_CASE( "Iteration caps are reported honestly, not silently swallowed", "[sparse][kernel]" )
{
    const std::vector<float> endmembers = { 1.0f, 0.0f, 0.0f, 1.0f };
    const std::vector<float> pixel = { 0.8f, 0.3f };
    Config config;
    config.maxIterations = 1; // cannot converge
    config.tolerance = 1e-12;
    SparseUnmixResult result;
    QString err;
    REQUIRE( unmixSparse( pixel.data(), 1, 2, endmembers.data(), 2, config, &result, &err ) );
    CHECK( result.converged[0] == 0 );
    CHECK( result.iterations[0] == 1 );

    config.maxIterations = 20000;
    REQUIRE( unmixSparse( pixel.data(), 1, 2, endmembers.data(), 2, config, &result, &err ) );
    CHECK( result.converged[0] == 1 );
}

TEST_CASE( "Batch driver matches the per-pixel path", "[sparse][kernel]" )
{
    const std::vector<float> endmembers = { 1.0f, 0.0f, 0.0f, 1.0f };
    const std::vector<float> pixels = { 0.8f, 0.3f, 0.1f, 0.7f };
    Config config;
    SparseUnmixResult result;
    QString err;
    REQUIRE( unmixSparse( pixels.data(), 2, 2, endmembers.data(), 2, config, &result, &err ) );
    REQUIRE( result.abundances.size() == 4 );
    CHECK( result.abundances[0] == Approx( 0.8 ).margin( 1e-3 ) );
    CHECK( result.abundances[1] == Approx( 0.3 ).margin( 1e-3 ) );
    CHECK( result.abundances[2] == Approx( 0.1 ).margin( 1e-3 ) );
    CHECK( result.abundances[3] == Approx( 0.7 ).margin( 1e-3 ) );

    // Structural refusals.
    CHECK_FALSE( unmixSparse( pixels.data(), 0, 2, endmembers.data(), 2, config, &result, &err ) );
    CHECK_FALSE( unmixSparse( pixels.data(), 2, 0, endmembers.data(), 2, config, &result, &err ) );
    CHECK_FALSE( unmixSparse( pixels.data(), 2, 2, nullptr, 2, config, &result, &err ) );
    Config bad;
    bad.lambda = -1.0;
    CHECK_FALSE( unmixSparse( pixels.data(), 2, 2, endmembers.data(), 2, bad, &result, &err ) );
}

TEST_CASE( "buildDictionary rejects atoms beyond the defensive cap", "[sparse][kernel]" )
{
    // 2049 one-band atoms (each a distinct scalar) exceed kMaxAtoms.
    const std::vector<float> atoms( 2049, 1.0f );
    Config config;
    config.collinearAngleDegrees = 0.0; // identical atoms: disable that guard here
    Dictionary dictionary;
    QString err;
    CHECK_FALSE( buildDictionary( atoms.data(), 1, 2049, config, &dictionary, &err ) );
    CHECK( err.contains( "capped" ) );
    REQUIRE( buildDictionary( atoms.data(), 1, 2048, config, &dictionary, &err ) );
    CHECK( dictionary.lipschitz > 0.0 );
}
