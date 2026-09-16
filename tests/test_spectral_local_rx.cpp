// test_spectral_local_rx.cpp — dual-window RX kernel against an independent
// reference implementation (the test enumerates windows and computes mean /
// sample covariance / scaled loading / Mahalanobis itself) plus injection,
// guard, NoData and validation cases.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/spectral_local_rx.h"

using namespace SpectralLocalRx;
using Catch::Approx;

namespace
{
    constexpr float kNoData = -9999.0f;
    constexpr int kWidth = 16;
    constexpr int kHeight = 16;
    constexpr int kBands = 4;

    // Deterministic checkerboard background: pixel(x,y,b) =
    // c[b] + s(x)*d[b] + t(y)*e[b] with s alternating in x and t in y.
    struct Design
    {
        std::vector<float> c{ 0.50f, 0.55f, 0.60f, 0.65f };
        std::vector<float> d{ 0.02f, 0.04f, 0.06f, 0.08f };
        std::vector<float> e{ 0.01f, 0.03f, 0.05f, 0.07f };

        float at( int x, int y, int b ) const
        {
            const float s = ( x % 2 == 0 ) ? 1.0f : -1.0f;
            const float t = ( y % 2 == 0 ) ? 1.0f : -1.0f;
            return c[b] + s * d[b] + t * e[b];
        }
    };

    std::vector<float> buildCube( const Design &design )
    {
        std::vector<float> pixels( static_cast<size_t>( kWidth ) * kHeight * kBands );
        for ( int y = 0; y < kHeight; ++y )
            for ( int x = 0; x < kWidth; ++x )
                for ( int b = 0; b < kBands; ++b )
                    pixels[( static_cast<size_t>( y ) * kWidth + x ) * kBands + b] =
                        design.at( x, y, b );
        return pixels;
    }

    bool validPixel( const std::vector<float> &pixels, size_t idx, int bands,
                     const std::vector<float> &noData, const std::vector<uint8_t> &hasNoData )
    {
        for ( int b = 0; b < bands; ++b )
        {
            const float v = pixels[idx * bands + b];
            if ( !std::isfinite( v ) )
                return false;
            if ( !hasNoData.empty() && hasNoData[b] && v == noData[b] )
                return false;
        }
        return true;
    }

    // Independent reference: enumerate background pixels of the clamped
    // outer window minus the inner guard around (px, py), compute the sample
    // covariance (+ scaled loading) and the Mahalanobis score of the center.
    // Returns false when the reference also considers the pixel unscoreable.
    bool referenceScore( const std::vector<float> &pixels, int px, int py,
                         const Config &config,
                         const std::vector<float> &noData,
                         const std::vector<uint8_t> &hasNoData,
                         float *score, int32_t *backgroundCount )
    {
        const int outerRadius = config.outerWindow / 2;
        const int innerRadius = config.innerWindow / 2;
        const size_t centerIdx = static_cast<size_t>( py ) * kWidth + px;
        if ( !validPixel( pixels, centerIdx, kBands, noData, hasNoData ) )
            return false;

        const int x0 = std::max( 0, px - outerRadius );
        const int x1 = std::min( kWidth - 1, px + outerRadius );
        const int y0 = std::max( 0, py - outerRadius );
        const int y1 = std::min( kHeight - 1, py + outerRadius );

        std::vector<size_t> background;
        for ( int y = y0; y <= y1; ++y )
        {
            for ( int x = x0; x <= x1; ++x )
            {
                if ( std::abs( x - px ) <= innerRadius && std::abs( y - py ) <= innerRadius )
                    continue;
                const size_t idx = static_cast<size_t>( y ) * kWidth + x;
                if ( !validPixel( pixels, idx, kBands, noData, hasNoData ) )
                    continue;
                background.push_back( idx );
            }
        }
        *backgroundCount = static_cast<int32_t>( background.size() );
        const int minSamples = config.minBackgroundSamples > 0
                                   ? config.minBackgroundSamples
                                   : ( config.covarianceMode == CovarianceMode::Full
                                           ? 2 * kBands + 2
                                           : kBands + 1 );
        if ( static_cast<int>( background.size() ) < minSamples )
            return false;

        std::vector<double> mu( kBands, 0.0 );
        for ( size_t idx : background )
            for ( int b = 0; b < kBands; ++b )
                mu[b] += pixels[idx * kBands + b];
        for ( int b = 0; b < kBands; ++b )
            mu[b] /= static_cast<double>( background.size() );

        const double denom = static_cast<double>( background.size() - 1 );
        double rx = 0.0;
        if ( config.covarianceMode == CovarianceMode::Full )
        {
            std::vector<double> cov( static_cast<size_t>( kBands ) * kBands, 0.0 );
            for ( size_t idx : background )
            {
                for ( int i = 0; i < kBands; ++i )
                    for ( int j = i; j < kBands; ++j )
                    {
                        const double di = pixels[idx * kBands + i] - mu[i];
                        const double dj = pixels[idx * kBands + j] - mu[j];
                        cov[i * kBands + j] += di * dj;
                    }
            }
            double trace = 0.0;
            for ( int i = 0; i < kBands; ++i )
            {
                for ( int j = i; j < kBands; ++j )
                {
                    cov[i * kBands + j] /= denom;
                    cov[j * kBands + i] = cov[i * kBands + j];
                }
                // Trace of the ESTIMATED covariance (normalized first — the
                // loading contract is alpha * tr(Sigma)/B).
                trace += cov[i * kBands + i];
            }
            const double load = config.loading * ( trace / kBands );
            for ( int i = 0; i < kBands; ++i )
                cov[i * kBands + i] += load;
            // Solve cov * z = d via Gauss-Jordan (test-local implementation).
            std::vector<double> d( kBands );
            for ( int b = 0; b < kBands; ++b )
                d[b] = pixels[centerIdx * kBands + b] - mu[b];
            std::vector<double> aug( kBands * ( kBands + 1 ), 0.0 );
            for ( int i = 0; i < kBands; ++i )
            {
                for ( int j = 0; j < kBands; ++j )
                    aug[i * ( kBands + 1 ) + j] = cov[i * kBands + j];
                aug[i * ( kBands + 1 ) + kBands] = d[i];
            }
            for ( int col = 0; col < kBands; ++col )
            {
                int pivot = col;
                for ( int r = col + 1; r < kBands; ++r )
                    if ( std::abs( aug[r * ( kBands + 1 ) + col] )
                         > std::abs( aug[pivot * ( kBands + 1 ) + col] ) )
                        pivot = r;
                if ( pivot != col )
                {
                    // Manual row exchange: swap_ranges on identical ranges is UB.
                    for ( int j = col; j <= kBands; ++j )
                        std::swap( aug[pivot * ( kBands + 1 ) + j],
                                   aug[col * ( kBands + 1 ) + j] );
                }
                const double diag = aug[col * ( kBands + 1 ) + col];
                for ( int j = col; j <= kBands; ++j )
                    aug[col * ( kBands + 1 ) + j] /= diag;
                for ( int r = 0; r < kBands; ++r )
                {
                    if ( r == col )
                        continue;
                    const double factor = aug[r * ( kBands + 1 ) + col];
                    if ( factor == 0.0 )
                        continue;
                    for ( int j = col; j <= kBands; ++j )
                        aug[r * ( kBands + 1 ) + j] -= factor * aug[col * ( kBands + 1 ) + j];
                }
            }
            rx = 0.0;
            for ( int b = 0; b < kBands; ++b )
                rx += d[b] * aug[b * ( kBands + 1 ) + kBands];
        }
        else
        {
            std::vector<double> var( kBands, 0.0 );
            for ( size_t idx : background )
                for ( int b = 0; b < kBands; ++b )
                {
                    const double diff = pixels[idx * kBands + b] - mu[b];
                    var[b] += diff * diff;
                }
            double trace = 0.0;
            for ( int b = 0; b < kBands; ++b )
            {
                var[b] /= denom;
                trace += var[b];
            }
            const double load = config.loading * ( trace / kBands );
            for ( int b = 0; b < kBands; ++b )
            {
                const double diff = pixels[centerIdx * kBands + b] - mu[b];
                rx += diff * diff / ( var[b] + load );
            }
        }
        *score = static_cast<float>( std::max( 0.0, rx ) );
        return true;
    }
} // namespace

TEST_CASE( "Dual-window RX matches the independent reference at multiple positions", "[localrx][kernel]" )
{
    const Design design;
    std::vector<float> pixels = buildCube( design );

    for ( CovarianceMode mode : { CovarianceMode::Full, CovarianceMode::Diagonal } )
    {
        Config config;
        config.outerWindow = 5;
        config.innerWindow = 3;
        config.covarianceMode = mode;
        config.loading = 1e-3;
        Result result;
        QString err;
        REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                               nullptr, nullptr, &result, &err ) );

        // Interior, edge, and corner pixels exercise window clamping.
        for ( auto [px, py] : std::vector<std::pair<int, int>>{ { 2, 2 }, { 0, 7 }, { 15, 15 }, { 8, 0 } } )
        {
            float reference = 0.0f;
            int32_t referenceCount = 0;
            const bool refScored = referenceScore( pixels, px, py, config,
                                                   {}, {}, &reference, &referenceCount );
            const size_t idx = static_cast<size_t>( py ) * kWidth + px;
            if ( refScored )
            {
                REQUIRE( result.scored[idx] == 1 );
                CHECK( result.backgroundSamples[idx] == referenceCount );
                CHECK( result.scores[idx] == Approx( reference ).margin( 1e-4 ) );
            }
            else
            {
                CHECK( result.scored[idx] == 0 );
            }
        }
    }
}

TEST_CASE( "Injected anomaly dominates and its guard window protects the score", "[localrx][kernel]" )
{
    const Design design;
    std::vector<float> pixels = buildCube( design );
    // Inject a strong anomaly at (9,8), Chebyshev distance 1 from the scored
    // center (8,8): inside the center's 3x3 guard, outside its 1x1 guard.
    const auto anomalyCell = std::pair<int, int>{ 9, 8 };
    const size_t anomalyIdx = ( static_cast<size_t>( anomalyCell.second ) * kWidth
                                + anomalyCell.first )
                              * kBands;
    pixels[anomalyIdx] += 0.5f; // ~6x the d[0] amplitude

    Config config;
    config.outerWindow = 5;
    config.innerWindow = 3;
    config.covarianceMode = CovarianceMode::Full;
    Result result;
    QString err;
    REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                           nullptr, nullptr, &result, &err ) );

    // The anomaly's own score must dominate all CLEAN pixels (Chebyshev
    // distance >= 3, whose backgrounds exclude the anomaly entirely).
    const size_t anomalyScoreIdx = static_cast<size_t>( anomalyCell.second ) * kWidth
                                   + anomalyCell.first;
    REQUIRE( result.scored[anomalyScoreIdx] == 1 );
    float bestClean = 0.0f;
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
        {
            if ( std::max( std::abs( x - anomalyCell.first ),
                           std::abs( y - anomalyCell.second ) )
                 < 3 )
                continue;
            const size_t idx = static_cast<size_t>( y ) * kWidth + x;
            if ( result.scored[idx] )
                bestClean = std::max( bestClean, result.scores[idx] );
        }
    CHECK( result.scores[anomalyScoreIdx] > 5.0f * bestClean );

    // Guard semantics, canonical property: the ANOMALY's own score is
    // highest when its guard window removes it (and its neighbours) from
    // its background. With a 1x1 guard the anomaly stays in its own
    // background: its band-0 deviation now sits on top of a polluted mean
    // and a ~20x inflated variance, so its score drops sharply.
    Config tightGuard;
    tightGuard.outerWindow = 5;
    tightGuard.innerWindow = 1;
    Result tightResult;
    REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, tightGuard,
                           nullptr, nullptr, &tightResult, &err ) );
    CHECK( result.scores[anomalyScoreIdx] > tightResult.scores[anomalyScoreIdx] );
    // And even the polluted variant still flags the anomaly above the clean
    // pixel population (the anomaly is detectable either way — the guard
    // just recovers the full contrast).
    CHECK( tightResult.scores[anomalyScoreIdx] > 5.0f * bestClean );
}

TEST_CASE( "NoData pixels are excluded and never scored", "[localrx][kernel]" )
{
    const Design design;
    std::vector<float> pixels = buildCube( design );
    std::vector<float> noData( kBands, kNoData );
    std::vector<uint8_t> hasNoData( kBands, 1 );

    // Invalidate pixel (2,1) and the center candidate (2,2).
    const auto invalidate = [&]( int x, int y )
    {
        const size_t idx = ( static_cast<size_t>( y ) * kWidth + x ) * kBands;
        for ( int b = 0; b < kBands; ++b )
            pixels[idx + b] = kNoData;
    };
    invalidate( 2, 1 );
    invalidate( 2, 2 );

    Config config;
    config.outerWindow = 5;
    config.innerWindow = 3;
    Result result;
    QString err;
    REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                           noData.data(), hasNoData.data(), &result, &err ) );

    // The NoData center is never scored.
    CHECK( result.scored[static_cast<size_t>( 2 ) * kWidth + 2] == 0 );
    CHECK( std::isnan( result.scores[static_cast<size_t>( 2 ) * kWidth + 2] ) );

    // A neighbor's background excludes the invalid pixels (reference agrees).
    float reference = 0.0f;
    int32_t referenceCount = 0;
    REQUIRE( referenceScore( pixels, 2, 3, config, noData, hasNoData,
                             &reference, &referenceCount ) );
    CHECK( result.backgroundSamples[static_cast<size_t>( 3 ) * kWidth + 2]
           == referenceCount );
    CHECK( result.scores[static_cast<size_t>( 3 ) * kWidth + 2]
           == Approx( reference ).margin( 1e-4 ) );
}

TEST_CASE( "Windows with too few valid samples stay unscored", "[localrx][kernel]" )
{
    const Design design;
    std::vector<float> pixels = buildCube( design );
    std::vector<float> noData( kBands, kNoData );
    std::vector<uint8_t> hasNoData( kBands, 1 );
    // Invalidate a 4x4 block so the pixel (2,2) window has < 2*B+2 background.
    for ( int y = 0; y < 4; ++y )
        for ( int x = 0; x < 4; ++x )
        {
            const size_t idx = ( static_cast<size_t>( y ) * kWidth + x ) * kBands;
            for ( int b = 0; b < kBands; ++b )
                pixels[idx + b] = kNoData;
        }

    Config config;
    config.outerWindow = 5;
    config.innerWindow = 3;
    Result result;
    QString err;
    REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                           noData.data(), hasNoData.data(), &result, &err ) );
    CHECK( result.scored[static_cast<size_t>( 2 ) * kWidth + 2] == 0 );
    CHECK( result.backgroundSamples[static_cast<size_t>( 2 ) * kWidth + 2] < 2 * kBands + 2 );

    // Far from the hole, scoring continues unaffected.
    CHECK( result.scored[static_cast<size_t>( 12 ) * kWidth + 12] == 1 );
}

TEST_CASE( "scorePixel agrees with the grid driver", "[localrx][kernel]" )
{
    const Design design;
    std::vector<float> pixels = buildCube( design );
    Config config;
    config.outerWindow = 5;
    config.innerWindow = 3;

    // Gather the same clamped window-minus-guard background for (8,8).
    const int px = 8, py = 8;
    std::vector<float> background;
    for ( int y = py - 2; y <= py + 2; ++y )
        for ( int x = px - 2; x <= px + 2; ++x )
        {
            if ( std::abs( x - px ) <= 1 && std::abs( y - py ) <= 1 )
                continue;
            const size_t idx = ( static_cast<size_t>( y ) * kWidth + x ) * kBands;
            background.insert( background.end(), pixels.begin() + idx,
                               pixels.begin() + idx + kBands );
        }
    const float *center = pixels.data() + ( static_cast<size_t>( py ) * kWidth + px ) * kBands;
    float score = 0.0f;
    QString err;
    REQUIRE( scorePixel( center, background.data(), background.size() / kBands,
                         kBands, config, nullptr, nullptr, &score,
                         nullptr, &err ) );

    Result result;
    REQUIRE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                           nullptr, nullptr, &result, &err ) );
    CHECK( score == Approx( result.scores[static_cast<size_t>( py ) * kWidth + px] ).margin( 1e-5 ) );

    // A too-small background refuses with the typed status (never message
    // text matching).
    float small = 0.0f;
    PixelScoreStatus status = PixelScoreStatus::InvalidArguments;
    CHECK_FALSE( scorePixel( center, background.data(), 4, kBands, config,
                             nullptr, nullptr, &small, &status, &err ) );
    CHECK( status == PixelScoreStatus::InsufficientBackground );
}

TEST_CASE( "Dual-window RX validation refusals", "[localrx][kernel]" )
{
    const Design design;
    std::vector<float> pixels = buildCube( design );
    Config config;
    Result result;
    QString err;

    CHECK_FALSE( dualWindowRx( nullptr, kWidth, kHeight, kBands, config,
                               nullptr, nullptr, &result, &err ) );
    CHECK_FALSE( dualWindowRx( pixels.data(), 0, kHeight, kBands, config,
                               nullptr, nullptr, &result, &err ) );
    CHECK_FALSE( dualWindowRx( pixels.data(), kWidth, kHeight, 0, config,
                               nullptr, nullptr, &result, &err ) );

    config.outerWindow = 4; // even
    CHECK_FALSE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                               nullptr, nullptr, &result, &err ) );
    CHECK( err.contains( "odd" ) );

    config.outerWindow = 5;
    config.innerWindow = 5; // inner >= outer
    CHECK_FALSE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                               nullptr, nullptr, &result, &err ) );

    config.innerWindow = 3;
    config.loading = -1.0;
    CHECK_FALSE( dualWindowRx( pixels.data(), kWidth, kHeight, kBands, config,
                               nullptr, nullptr, &result, &err ) );
    CHECK( err.contains( "Loading" ) );
}
