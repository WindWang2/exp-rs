// src/processing/algorithms/spectral_anomaly.cpp — RX anomaly detector
#include "spectral_anomaly.h"

#include <cmath>
#include <vector>

namespace SpectralAnomaly
{

/// Inverts an n x n symmetric positive-definite matrix (given in column-major
/// row order) via Gauss-Jordan with partial pivoting and a ridge added by the
/// caller. Returns false when singular.
bool invertMatrix( const std::vector<double> &m, int n, std::vector<double> *inverse )
{
    // Augmented matrix [A | I].
    std::vector<double> a = m;
    std::vector<double> inv( static_cast<size_t>( n ) * n, 0.0 );
    for ( int i = 0; i < n; ++i )
        inv[static_cast<size_t>( i ) * n + i ] = 1.0;

    for ( int col = 0; col < n; ++col )
    {
        int pivot = col;
        double best = std::abs( a[static_cast<size_t>( col ) * n + col] );
        for ( int r = col + 1; r < n; ++r )
        {
            const double v = std::abs( a[static_cast<size_t>( r ) * n + col] );
            if ( v > best )
            {
                best = v;
                pivot = r;
            }
        }
        if ( best < 1e-12 )
            return false;
        if ( pivot != col )
        {
            for ( int c = 0; c < n; ++c )
            {
                std::swap( a[static_cast<size_t>( pivot ) * n + c],
                           a[static_cast<size_t>( col ) * n + c ] );
                std::swap( inv[static_cast<size_t>( pivot ) * n + c],
                           inv[static_cast<size_t>( col ) * n + c ] );
            }
        }

        const double diag = a[static_cast<size_t>( col ) * n + col];
        // Normalize the pivot row.
        for ( int c = 0; c < n; ++c )
        {
            a[static_cast<size_t>( col ) * n + c] /= diag;
            inv[static_cast<size_t>( col ) * n + c] /= diag;
        }
        // Eliminate all other rows.
        for ( int r = 0; r < n; ++r )
        {
            if ( r == col )
                continue;
            const double factor = a[static_cast<size_t>( r ) * n + col];
            if ( factor == 0.0 )
                continue;
            for ( int c = 0; c < n; ++c )
            {
                a[static_cast<size_t>( r ) * n + c] -=
                    factor * a[static_cast<size_t>( col ) * n + c];
                inv[static_cast<size_t>( r ) * n + c] -=
                    factor * inv[static_cast<size_t>( col ) * n + c];
            }
        }
    }
    *inverse = std::move( inv );
    return true;
}

void accumulateMean( const float *pixels, size_t count, int bands,
                     BackgroundStats *stats, bool skipNonFinite,
                     const float *noDataBands,
                     const uint8_t *hasNoDataBands )
{
    if ( !pixels || !stats || count == 0 || bands <= 0 )
        return;
    if ( stats->mean.size() != static_cast<size_t>( bands ) )
    {
        stats->mean.assign( bands, 0.0 );
        stats->count = 0;
    }
    // Same accumulation order as rxDetector's mean loop (band-inner) so a
    // multi-tile pass reproduces the full-raster mean (the sum is order-tolerant
    // for this accumulator; values match within FP rounding).
    for ( size_t p = 0; p < count; ++p )
    {
        if ( skipNonFinite )
        {
            bool valid = true;
            for ( int b = 0; b < bands; ++b )
            {
                const float v = pixels[p * static_cast<size_t>( bands ) + b];
                // Invalid: non-finite, or equal (within tolerance) to the band's
                // declared NoData value (@a noDataBands, when provided and hasNoData is true).
                const bool checkNd = noDataBands && ( !hasNoDataBands || hasNoDataBands[b] );
                if ( !std::isfinite( v )
                     || ( checkNd && ( v == noDataBands[b] || std::isnan( v ) ) ) )
                {
                    valid = false;
                    break;
                }
            }
            if ( !valid )
                continue;
        }
        for ( int b = 0; b < bands; ++b )
            stats->mean[b] += pixels[p * static_cast<size_t>( bands ) + b];
        ++stats->count;
    }
}

void finalizeMean( BackgroundStats *stats )
{
    if ( !stats || stats->count == 0 )
        return;
    for ( double &v : stats->mean )
        v /= static_cast<double>( stats->count );
    stats->count = 0; // reset for the covariance pass
}

void accumulateCovariance( const float *pixels, size_t count, int bands,
                           BackgroundStats *stats, bool skipNonFinite,
                           const float *noDataBands,
                           const uint8_t *hasNoDataBands )
{
    if ( !pixels || !stats || count == 0 || bands <= 0 )
        return;
    if ( stats->covariance.size() != static_cast<size_t>( bands ) * bands )
    {
        stats->covariance.assign( static_cast<size_t>( bands ) * bands, 0.0 );
        stats->count = 0;
    }
    // Upper-triangle centered outer product.
    std::vector<double> d( bands );
    for ( size_t p = 0; p < count; ++p )
    {
        if ( skipNonFinite )
        {
            bool valid = true;
            for ( int b = 0; b < bands; ++b )
            {
                const float v = pixels[p * static_cast<size_t>( bands ) + b];
                // Invalid: non-finite, or equal (within tolerance) to the band's
                // declared NoData value (@a noDataBands, when provided and hasNoData is true).
                const bool checkNd = noDataBands && ( !hasNoDataBands || hasNoDataBands[b] );
                if ( !std::isfinite( v )
                     || ( checkNd && ( v == noDataBands[b] || std::isnan( v ) ) ) )
                {
                    valid = false;
                    break;
                }
            }
            if ( !valid )
                continue;
        }
        for ( int b = 0; b < bands; ++b )
            d[b] = pixels[p * static_cast<size_t>( bands ) + b] - stats->mean[b];
        for ( int i = 0; i < bands; ++i )
        {
            const double di = d[i];
            const size_t rowOffset = static_cast<size_t>( i ) * bands;
            for ( int j = i; j < bands; ++j )
                stats->covariance[rowOffset + j] += di * d[j];
        }
        ++stats->count;
    }
}

void finalizeCovariance( BackgroundStats *stats )
{
    if ( !stats || stats->count == 0 )
        return;
    const int bands = static_cast<int>( stats->mean.size() );
    const double invCount = 1.0 / static_cast<double>( stats->count );
    for ( int i = 0; i < bands; ++i )
    {
        for ( int j = i; j < bands; ++j )
        {
            const double v = stats->covariance[static_cast<size_t>( i ) * bands + j] * invCount;
            stats->covariance[static_cast<size_t>( i ) * bands + j] = v;
            stats->covariance[static_cast<size_t>( j ) * bands + i] = v;
        }
    }
}

bool invertCovariance( const std::vector<double> &covariance, int bands,
                       std::vector<double> *inverse )
{
    if ( bands <= 0 || static_cast<int>( covariance.size() ) != bands * bands )
        return false;
    double trace = 0.0;
    for ( int i = 0; i < bands; ++i )
        trace += covariance[static_cast<size_t>( i ) * bands + i];
    const double kRidge = std::max( 1e-9, ( trace / bands ) * 1e-7 );
    std::vector<double> covRidge = covariance;
    for ( int i = 0; i < bands; ++i )
        covRidge[static_cast<size_t>( i ) * bands + i] += kRidge;
    return invertMatrix( covRidge, bands, inverse );
}

float rxScore( const float *spectrum, const std::vector<double> &mean,
               const std::vector<double> &inverseCov, int bands )
{
    static thread_local std::vector<double> threadScratch;
    return rxScore( spectrum, mean, inverseCov, bands, &threadScratch );
}

float rxScore( const float *spectrum, const std::vector<double> &mean,
               const std::vector<double> &inverseCov, int bands,
               std::vector<double> *scratch )
{
    // Reuse the caller-provided scratch (resized once) to avoid a per-pixel heap
    // allocation in tight streaming loops (perf goal §2c).
    static thread_local std::vector<double> fallback;
    std::vector<double> &d = scratch ? *scratch : fallback;
    if ( d.size() < static_cast<size_t>( bands ) )
        d.resize( bands );
    for ( int b = 0; b < bands; ++b )
    {
        if ( !std::isfinite( spectrum[b] ) )
            return std::numeric_limits<float>::quiet_NaN();
        d[b] = static_cast<double>( spectrum[b] ) - mean[b];
    }

    double rx = 0.0;
    for ( int i = 0; i < bands; ++i )
    {
        double row = 0.0;
        const size_t rowOffset = static_cast<size_t>( i ) * bands;
        for ( int j = 0; j < bands; ++j )
            row += inverseCov[rowOffset + j] * d[j];
        rx += d[i] * row;
    }
    if ( !std::isfinite( rx ) )
        return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>( std::max( 0.0, rx ) );
}

bool rxDetector( const float *pixels, size_t count, int bands,
                 std::vector<float> *rxValues, QString *errorMessage )
{
    if ( !pixels || !rxValues || count == 0 || bands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid RX detector arguments" );
        return false;
    }

    BackgroundStats stats;
    accumulateMean( pixels, count, bands, &stats );
    finalizeMean( &stats );
    accumulateCovariance( pixels, count, bands, &stats );
    finalizeCovariance( &stats );

    std::vector<double> invCov;
    if ( !invertCovariance( stats.covariance, bands, &invCov ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Background covariance is singular" );
        return false;
    }

    rxValues->resize( count );
    std::vector<double> scratch( bands );
    for ( size_t p = 0; p < count; ++p )
    {
        ( *rxValues )[p] =
            rxScore( pixels + p * static_cast<size_t>( bands ), stats.mean, invCov, bands, &scratch );
    }
    return true;
}

} // namespace SpectralAnomaly
