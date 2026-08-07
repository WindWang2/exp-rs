// src/processing/algorithms/spectral_anomaly.cpp — RX anomaly detector
#include "spectral_anomaly.h"

#include <cmath>
#include <vector>

namespace SpectralAnomaly
{

namespace
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

} // namespace

bool rxDetector( const float *pixels, size_t count, int bands,
                 std::vector<float> *rxValues, QString *errorMessage )
{
    if ( !pixels || !rxValues || count == 0 || bands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid RX detector arguments" );
        return false;
    }

    // Sample mean.
    std::vector<double> mean( bands, 0.0 );
    for ( size_t p = 0; p < count; ++p )
        for ( int b = 0; b < bands; ++b )
            mean[static_cast<size_t>( b )] += pixels[p * static_cast<size_t>( bands ) + b];
    for ( int b = 0; b < bands; ++b )
        mean[static_cast<size_t>( b )] /= static_cast<double>( count );

    // Sample covariance (biased, /count).
    std::vector<double> cov( static_cast<size_t>( bands ) * bands, 0.0 );
    for ( size_t p = 0; p < count; ++p )
    {
        std::vector<double> d( bands );
        for ( int b = 0; b < bands; ++b )
            d[static_cast<size_t>( b )] =
                pixels[p * static_cast<size_t>( bands ) + b] - mean[static_cast<size_t>( b )];
        for ( int i = 0; i < bands; ++i )
            for ( int j = 0; j < bands; ++j )
                cov[static_cast<size_t>( i ) * bands + j] +=
                    d[static_cast<size_t>( i )] * d[static_cast<size_t>( j )];
    }
    for ( double &v : cov )
        v /= static_cast<double>( count );

    // Ridge for robustness against (near-)singular background covariance.
    constexpr double kRidge = 1e-9;
    std::vector<double> covRidge = cov;
    for ( int i = 0; i < bands; ++i )
        covRidge[static_cast<size_t>( i ) * bands + i] += kRidge;

    std::vector<double> invCov;
    if ( !invertMatrix( covRidge, bands, &invCov ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Background covariance is singular" );
        return false;
    }

    rxValues->resize( count );
    for ( size_t p = 0; p < count; ++p )
    {
        std::vector<double> d( bands );
        for ( int b = 0; b < bands; ++b )
            d[static_cast<size_t>( b )] =
                pixels[p * static_cast<size_t>( bands ) + b] - mean[static_cast<size_t>( b )];

        double rx = 0.0;
        for ( int i = 0; i < bands; ++i )
        {
            double row = 0.0;
            for ( int j = 0; j < bands; ++j )
                row += invCov[static_cast<size_t>( i ) * bands + j]
                       * d[static_cast<size_t>( j )];
            rx += d[static_cast<size_t>( i )] * row;
        }
        ( *rxValues )[p] = static_cast<float>( std::max( 0.0, rx ) );
    }
    return true;
}

} // namespace SpectralAnomaly
