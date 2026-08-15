// src/processing/algorithms/spectral_unmixing.cpp — linear spectral unmixing
#include "spectral_unmixing.h"

#include <cmath>
#include <limits>
#include <vector>

namespace SpectralUnmixing
{

namespace
{

/// Inverts the n x n matrix @a m in place (Gauss-Jordan with partial pivoting).
/// Returns false when the matrix is singular (threshold matches the solver).
/// The per-pixel unmixing hot loop then solves via one matrix-vector product
/// instead of a full elimination per pixel (O(E^3) once vs O(E^3) per pixel).
bool invertMatrixInPlace( std::vector<double> &m, int n )
{
    std::vector<double> inv( static_cast<size_t>( n ) * n, 0.0 );
    for ( int i = 0; i < n; ++i )
        inv[static_cast<size_t>( i ) * n + i] = 1.0;

    for ( int col = 0; col < n; ++col )
    {
        int pivot = col;
        double best = std::abs( m[static_cast<size_t>( col ) * n + col] );
        for ( int r = col + 1; r < n; ++r )
        {
            const double v = std::abs( m[static_cast<size_t>( r ) * n + col] );
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
                std::swap( m[static_cast<size_t>( pivot ) * n + c],
                           m[static_cast<size_t>( col ) * n + c ] );
                std::swap( inv[static_cast<size_t>( pivot ) * n + c],
                           inv[static_cast<size_t>( col ) * n + c ] );
            }
        }

        const double diag = m[static_cast<size_t>( col ) * n + col];
        for ( int c = 0; c < n; ++c )
        {
            m[static_cast<size_t>( col ) * n + c] /= diag;
            inv[static_cast<size_t>( col ) * n + c] /= diag;
        }

        for ( int r = 0; r < n; ++r )
        {
            if ( r == col )
                continue;
            const double factor = m[static_cast<size_t>( r ) * n + col];
            if ( factor == 0.0 )
                continue;
            for ( int c = 0; c < n; ++c )
            {
                m[static_cast<size_t>( r ) * n + c] -=
                    factor * m[static_cast<size_t>( col ) * n + c];
                inv[static_cast<size_t>( r ) * n + c] -=
                    factor * inv[static_cast<size_t>( col ) * n + c];
            }
        }
    }

    m.swap( inv );
    return true;
}

} // namespace

bool unmix( const float *pixels, size_t count, int bands,
            const float *endmembers, int nEndmembers,
            UnmixResult *result, QString *errorMessage )
{
    if ( !pixels || !endmembers || !result || count == 0 || bands <= 0
         || nEndmembers < 1 || nEndmembers > bands )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid unmixing arguments" );
        return false;
    }

    result->abundances.assign( count * static_cast<size_t>( nEndmembers ), 0.0f );
    result->reconstructionError.assign( count, 0.0f );

    // Gram matrix G = E^T E (nEndmembers x nEndmembers), reused per pixel.
    std::vector<double> gram( static_cast<size_t>( nEndmembers ) * nEndmembers, 0.0 );
    for ( int e = 0; e < nEndmembers; ++e )
    {
        for ( int f = 0; f < nEndmembers; ++f )
        {
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += static_cast<double>( endmembers[static_cast<size_t>( e ) * bands + b] )
                       * endmembers[static_cast<size_t>( f ) * bands + b];
            gram[static_cast<size_t>( e ) * nEndmembers + f] = sum;
        }
    }

    double gramTrace = 0.0;
    for ( int e = 0; e < nEndmembers; ++e )
        gramTrace += gram[static_cast<size_t>( e ) * nEndmembers + e];
    const double kRidge = std::max( 1e-9, ( gramTrace / nEndmembers ) * 1e-7 );

    // Precompute (G + ridge I)^-1 once: the per-pixel hot loop then solves the
    // normal equations with a single matrix-vector product (O(E^2) per pixel
    // instead of a full Gaussian elimination, O(E^3) per pixel).
    std::vector<double> invGram = gram;
    for ( int e = 0; e < nEndmembers; ++e )
        invGram[static_cast<size_t>( e ) * nEndmembers + e] += kRidge;
    const bool invertible = invertMatrixInPlace( invGram, nEndmembers );

    std::vector<double> rhs( nEndmembers, 0.0 );
    std::vector<double> abundance( nEndmembers, 0.0 );
    std::vector<double> est( bands, 0.0 );

    for ( size_t p = 0; p < count; ++p )
    {
        const float *x = pixels + p * static_cast<size_t>( bands );

        bool hasNan = false;
        for ( int b = 0; b < bands; ++b )
        {
            if ( std::isnan( x[b] ) ) { hasNan = true; break; }
        }
        if ( hasNan )
        {
            for ( int e = 0; e < nEndmembers; ++e )
                result->abundances[p * static_cast<size_t>( nEndmembers ) + e] =
                    std::numeric_limits<float>::quiet_NaN();
            result->reconstructionError[p] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }

        // Right-hand side: E^T x.
        for ( int e = 0; e < nEndmembers; ++e )
        {
            const float *emRow = &endmembers[static_cast<size_t>( e ) * bands];
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += static_cast<double>( emRow[b] ) * x[b];
            rhs[static_cast<size_t>( e )] = sum;
        }

        // Solve (G + ridge I) a = rhs via a = (G + ridge I)^-1 rhs.
        if ( !invertible )
        {
            // Singular even with the ridge: leave abundances at zero and the
            // reconstruction error as the pixel norm.
            double normSq = 0.0;
            for ( int b = 0; b < bands; ++b )
                normSq += static_cast<double>( x[b] ) * x[b];
            result->reconstructionError[p] =
                static_cast<float>( std::sqrt( normSq / bands ) );
            continue;
        }
        for ( int e = 0; e < nEndmembers; ++e )
        {
            double sum = 0.0;
            for ( int f = 0; f < nEndmembers; ++f )
                sum += invGram[static_cast<size_t>( e ) * nEndmembers + f]
                       * rhs[static_cast<size_t>( f )];
            abundance[static_cast<size_t>( e )] = sum;
        }

        // Clip to [0, 1] and renormalize to unit sum (approximate sum-to-one).
        double sum = 0.0;
        for ( int e = 0; e < nEndmembers; ++e )
        {
            abundance[static_cast<size_t>( e )] =
                std::clamp( abundance[static_cast<size_t>( e )], 0.0, 1.0 );
            sum += abundance[static_cast<size_t>( e )];
        }
        if ( sum > 1e-12 )
        {
            for ( int e = 0; e < nEndmembers; ++e )
                abundance[static_cast<size_t>( e )] /= sum;
        }

        for ( int e = 0; e < nEndmembers; ++e )
            result->abundances[p * static_cast<size_t>( nEndmembers ) + e] =
                static_cast<float>( abundance[static_cast<size_t>( e )] );

        // Reconstruction error: RMSE of x - E a over the bands.
        std::fill( est.begin(), est.end(), 0.0 );
        for ( int e = 0; e < nEndmembers; ++e )
        {
            const double ae = abundance[static_cast<size_t>( e )];
            if ( ae == 0.0 )
                continue;
            const float *emRow = &endmembers[static_cast<size_t>( e ) * bands];
            for ( int b = 0; b < bands; ++b )
                est[static_cast<size_t>( b )] += ae * static_cast<double>( emRow[b] );
        }

        double errorSq = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            const double diff = static_cast<double>( x[b] ) - est[static_cast<size_t>( b )];
            errorSq += diff * diff;
        }
        result->reconstructionError[p] =
            static_cast<float>( std::sqrt( errorSq / bands ) );
    }
    return true;
}

} // namespace SpectralUnmixing
