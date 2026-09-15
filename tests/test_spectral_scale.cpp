// test_spectral_scale.cpp — 256–1024 band scale evidence for the Spectral
// Intelligence 11.0 kernels: bounded logical scale, independent closed-form /
// reference truths, determinism (bit-identical repeat runs), and memory-bound
// refusals. Wall-clock is never a gate here; the assertions are logical.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "processing/algorithms/endmember_analysis.h"
#include "processing/algorithms/spectral_hybrid_similarity.h"
#include "processing/algorithms/spectral_local_rx.h"
#include "processing/algorithms/spectral_sparse_unmixing.h"

using namespace SpectralLocalRx;
using namespace SpectralSparseUnmixing;
using namespace SpectralHybridSimilarity;
using namespace EndmemberAnalysis;
using Catch::Approx;

namespace
{
    constexpr int kHighBands = 1024;

    /// Build a 1024-band identity dictionary (orthogonal atoms — the sparse
    /// solution is the elementwise soft-threshold clamp, an exact oracle).
    std::vector<float> identityDictionary( int n )
    {
        std::vector<float> e( static_cast<size_t>( n ) * n, 0.0f );
        for ( int i = 0; i < n; ++i )
            e[static_cast<size_t>( i ) * n + i] = 1.0f;
        return e;
    }

    /// Deterministic pseudo-random values in [0, 1) (xorshift, no library
    /// randomness so the test is reproducible everywhere).
    class DeterministicRandom
    {
      public:
        explicit DeterministicRandom( uint64_t seed ) : m_state( seed | 1 ) {}
        double next()
        {
            m_state ^= m_state << 13;
            m_state ^= m_state >> 7;
            m_state ^= m_state << 17;
            return static_cast<double>( m_state % 1000000ull ) / 1000000.0;
        }

      private:
        uint64_t m_state;
    };
} // namespace

TEST_CASE( "Sparse unmixing at 1024 bands matches the closed form", "[scale][sparse]" )
{
    const int n = kHighBands;
    const std::vector<float> dictionary = identityDictionary( n );

    // A few deterministic pixels with positive and negative values.
    DeterministicRandom rng( 20260915u );
    const int pixelCount = 4;
    std::vector<float> pixels( static_cast<size_t>( pixelCount ) * n );
    for ( size_t i = 0; i < pixels.size(); ++i )
        pixels[i] = static_cast<float>( rng.next() * 1.2 - 0.1 );

    SpectralSparseUnmixing::Config config;
    config.lambda = 0.1;
    config.tolerance = 1e-10;
    config.maxIterations = 5000;

    SparseUnmixResult result;
    QString err;
    REQUIRE( unmixSparse( pixels.data(), pixelCount, n, dictionary.data(), n,
                          config, &result, &err ) );
    REQUIRE( result.abundances.size() == static_cast<size_t>( pixelCount ) * n );
    for ( int p = 0; p < pixelCount; ++p )
    {
        CHECK( result.converged[static_cast<size_t>( p )] == 1 );
        for ( int i = 0; i < n; i += 97 ) // stride sampling keeps runtime bounded
        {
            const double expected = std::max( 0.0,
                                              static_cast<double>( pixels[static_cast<size_t>( p ) * n + i] )
                                                  - config.lambda );
            CHECK( result.abundances[static_cast<size_t>( p ) * n + i]
                   == Approx( expected ).margin( 1e-4 ) );
        }
    }

    // Determinism: a second run must reproduce the abundances bit-for-bit.
    SparseUnmixResult repeat;
    REQUIRE( unmixSparse( pixels.data(), pixelCount, n, dictionary.data(), n,
                          config, &repeat, &err ) );
    CHECK( std::memcmp( result.abundances.data(), repeat.abundances.data(),
                        result.abundances.size() * sizeof( float ) )
           == 0 );
}

TEST_CASE( "Sparse unmixing accepts a 1024-band, 256-atom dictionary", "[scale][sparse]" )
{
    // Overcomplete + high-dimensional: 256 atoms over 1024 bands. Memory
    // bound: Gram 256^2 doubles = 0.5 MiB working state (documented bound,
    // asserted by successful construction, not wall-clock).
    const int bands = kHighBands;
    const int atoms = 256;
    std::vector<float> dictionary( static_cast<size_t>( atoms ) * bands, 0.0f );
    DeterministicRandom rng( 424242u );
    for ( size_t i = 0; i < dictionary.size(); ++i )
        dictionary[i] = static_cast<float>( rng.next() );
    // Make atoms pairwise well-separated: rescale each row to unit norm; the
    // random draw keeps the pairwise angles far above the 0.5 degree guard
    // with overwhelming probability — verified here by the build not refusing.
    for ( int a = 0; a < atoms; ++a )
    {
        double norm = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            const double v = dictionary[static_cast<size_t>( a ) * bands + b];
            norm += v * v;
        }
        norm = std::sqrt( norm );
        for ( int b = 0; b < bands; ++b )
            dictionary[static_cast<size_t>( a ) * bands + b] =
                static_cast<float>( dictionary[static_cast<size_t>( a ) * bands + b] / norm );
    }

    SpectralSparseUnmixing::Config config;
    config.lambda = 0.01;
    Dictionary prepared;
    QString err;
    REQUIRE( buildDictionary( dictionary.data(), bands, atoms, config, &prepared, &err ) );
    CHECK( prepared.lipschitz > 0.0 );
    CHECK( prepared.gram.size() == static_cast<size_t>( atoms ) * atoms );

    // One pixel solves and reports honestly.
    std::vector<float> pixel( bands, 0.1f );
    std::vector<double> abundances;
    int32_t used = 0;
    bool converged = false;
    REQUIRE( solveSparsePixel( pixel.data(), prepared, config, &abundances, &used,
                               &converged, &err ) );
    CHECK( *std::min_element( abundances.begin(), abundances.end() ) >= -1e-12 );
}

TEST_CASE( "Diagonal local RX at 1024 bands matches the reference and is deterministic", "[scale][localrx]" )
{
    // 24x24 grid, 1024 bands, checkerboard design background; verify a
    // diagonal-mode score against the test-local variance-only reference and
    // require bit-identical repeat runs (tile-agnostic determinism evidence
    // at the kernel level).
    constexpr int kWidth = 24;
    constexpr int kHeight = 24;
    constexpr int kBands = kHighBands;

    DeterministicRandom rng( 987654321u );
    std::vector<double> meanSpectrum( kBands );
    std::vector<double> amplitude( kBands );
    for ( int b = 0; b < kBands; ++b )
    {
        meanSpectrum[static_cast<size_t>( b )] = 0.3 + 0.2 * rng.next();
        amplitude[static_cast<size_t>( b )] = 0.01 + 0.04 * rng.next();
    }
    // Pixel value alternates mean +/- amplitude by a checkerboard sign so the
    // local mean is close to the global mean and variances are well-defined.
    std::vector<float> pixels( static_cast<size_t>( kWidth ) * kHeight * kBands );
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
        {
            const double sign = ( ( x + y ) % 2 == 0 ) ? 1.0 : -1.0;
            for ( int b = 0; b < kBands; ++b )
                pixels[( static_cast<size_t>( y ) * kWidth + x ) * kBands + b] =
                    static_cast<float>( meanSpectrum[static_cast<size_t>( b )]
                                        + sign * amplitude[static_cast<size_t>( b )] );
        }

    SpectralLocalRx::Config config;
    config.outerWindow = 5;
    config.innerWindow = 3;
    config.covarianceMode = CovarianceMode::Diagonal;
    config.loading = 1e-3;

    Result result;
    QString err;
    REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                           nullptr, nullptr, &result, &err ) );

    // Reference: recompute the center pixel's score from the enumerated
    // window-minus-guard background (test-local implementation).
    const int px = 12, py = 12;
    std::vector<double> mu( kBands, 0.0 );
    std::vector<double> var( kBands, 0.0 );
    size_t count = 0;
    for ( int y = py - 2; y <= py + 2; ++y )
        for ( int x = px - 2; x <= px + 2; ++x )
        {
            if ( std::abs( x - px ) <= 1 && std::abs( y - py ) <= 1 )
                continue;
            ++count;
            for ( int b = 0; b < kBands; ++b )
                mu[static_cast<size_t>( b )] +=
                    pixels[( static_cast<size_t>( y ) * kWidth + x ) * kBands + b];
        }
    for ( int b = 0; b < kBands; ++b )
        mu[static_cast<size_t>( b )] /= static_cast<double>( count );
    for ( int y = py - 2; y <= py + 2; ++y )
        for ( int x = px - 2; x <= px + 2; ++x )
        {
            if ( std::abs( x - px ) <= 1 && std::abs( y - py ) <= 1 )
                continue;
            for ( int b = 0; b < kBands; ++b )
            {
                const double d =
                    pixels[( static_cast<size_t>( y ) * kWidth + x ) * kBands + b]
                    - mu[static_cast<size_t>( b )];
                var[static_cast<size_t>( b )] += d * d;
            }
        }
    double trace = 0.0;
    for ( int b = 0; b < kBands; ++b )
    {
        var[static_cast<size_t>( b )] /= static_cast<double>( count - 1 );
        trace += var[static_cast<size_t>( b )];
    }
    const double load = config.loading * ( trace / kBands );
    double reference = 0.0;
    for ( int b = 0; b < kBands; ++b )
    {
        const double d =
            pixels[( static_cast<size_t>( py ) * kWidth + px ) * kBands + b]
            - mu[static_cast<size_t>( b )];
        reference += d * d / ( var[static_cast<size_t>( b )] + load );
    }
    const size_t centerIdx = static_cast<size_t>( py ) * kWidth + px;
    REQUIRE( result.scored[centerIdx] == 1 );
    CHECK( result.scores[centerIdx] == Approx( reference ).margin( 1e-3 ) );

    // Determinism: rerun and require bit-identical scores.
    Result repeat;
    REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                           nullptr, nullptr, &repeat, &err ) );
    CHECK( std::memcmp( result.scores.data(), repeat.scores.data(),
                        result.scores.size() * sizeof( float ) )
           == 0 );
}

TEST_CASE( "Full covariance mode refuses absurd band counts instead of thrashing", "[scale][localrx]" )
{
    // The documented defensive bound: full local covariance refuses above
    // 8192 bands (B^2 doubles of transient state); diagonal remains valid.
    const int bands = 8193;
    const int width = 3;
    const int height = 3;
    std::vector<float> pixels( static_cast<size_t>( width ) * height * bands, 0.5f );
    SpectralLocalRx::Config config;
    config.outerWindow = 3;
    config.innerWindow = 1;
    config.covarianceMode = CovarianceMode::Full;
    Result result;
    QString err;
    CHECK_FALSE( dualWindowRx( pixels.data(), width, height, bands, config,
                               nullptr, nullptr, &result, &err ) );
    CHECK( err.contains( "8192" ) );

    config.covarianceMode = CovarianceMode::Diagonal;
    CHECK( dualWindowRx( pixels.data(), width, height, bands, config,
                         nullptr, nullptr, &result, &err ) );
}

TEST_CASE( "Hybrid similarity and angle matrix scale to 1024 bands", "[scale][hybrid][endmember]" )
{
    constexpr int kBands = kHighBands;
    DeterministicRandom rng( 555555u );
    std::vector<float> t( kBands ), r( kBands );
    for ( int b = 0; b < kBands; ++b )
    {
        t[static_cast<size_t>( b )] = static_cast<float>( 0.2 + 0.8 * rng.next() );
        r[static_cast<size_t>( b )] = t[static_cast<size_t>( b )]; // identical
    }
    SimilarityResult similarity;
    REQUIRE( SpectralHybridSimilarity::similarity( t.data(), r.data(), kBands, -9999.0f,
                                                   Form::ProductNormalized, &similarity ) );
    REQUIRE( similarity.defined );
    CHECK( similarity.hybrid == Approx( 1.0 ).margin( 1e-6 ) );

    // Endmember angle matrix on 1024-band rows: symmetric, zero diagonal.
    const int atoms = 8;
    std::vector<float> endmembers( static_cast<size_t>( atoms ) * kBands );
    DeterministicRandom rng2( 777u );
    for ( size_t i = 0; i < endmembers.size(); ++i )
        endmembers[i] = static_cast<float>( 0.1 + rng2.next() );
    std::vector<double> matrix;
    QString err;
    REQUIRE( angleMatrix( endmembers.data(), atoms, kBands, &matrix, &err ) );
    for ( int a = 0; a < atoms; ++a )
    {
        CHECK( matrix[static_cast<size_t>( a ) * atoms + a] == 0.0 );
        for ( int c = a + 1; c < atoms; ++c )
            CHECK( matrix[static_cast<size_t>( a ) * atoms + c]
                   == matrix[static_cast<size_t>( c ) * atoms + a] );
    }

    // Reduction keeps deterministic order: repeat run picks the same leaders.
    ReduceConfig reduceConfig;
    ReduceResult first;
    REQUIRE( reduceEndmembers( endmembers.data(), atoms, kBands, reduceConfig,
                               nullptr, &first, &err ) );
    ReduceResult second;
    REQUIRE( reduceEndmembers( endmembers.data(), atoms, kBands, reduceConfig,
                               nullptr, &second, &err ) );
    CHECK( first.representativeOf == second.representativeOf );
}
