// src/processing/algorithms/spectral_unmixing.cpp — linear spectral unmixing
#include "spectral_unmixing.h"

#include <cmath>
#include <limits>
#include <vector>

namespace SpectralUnmixing
{

namespace
{

/// Solve the n x n linear system A x = b by Gaussian elimination with partial
/// pivoting (in-place: A and b are modified). A has a ridge already added by
/// the caller. Returns false when the system is (still) singular.
bool solveLinearSystem( std::vector<double> &a, std::vector<double> &b, int n )
{
    for ( int col = 0; col < n; ++col )
    {
        // Partial pivot.
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
                std::swap( a[static_cast<size_t>( pivot ) * n + c],
                           a[static_cast<size_t>( col ) * n + c ] );
            std::swap( b[static_cast<size_t>( pivot )], b[static_cast<size_t>( col )] );
        }

        // Eliminate below.
        const double diag = a[static_cast<size_t>( col ) * n + col];
        for ( int r = col + 1; r < n; ++r )
        {
            const double factor = a[static_cast<size_t>( r ) * n + col] / diag;
            if ( factor == 0.0 )
                continue;
            for ( int c = col; c < n; ++c )
                a[static_cast<size_t>( r ) * n + c] -=
                    factor * a[static_cast<size_t>( col ) * n + c];
            b[static_cast<size_t>( r )] -= factor * b[static_cast<size_t>( col )];
        }
    }

    // Back substitution.
    for ( int r = n - 1; r >= 0; --r )
    {
        double sum = b[static_cast<size_t>( r )];
        for ( int c = r + 1; c < n; ++c )
            sum -= a[static_cast<size_t>( r ) * n + c] * b[static_cast<size_t>( c )];
        const double diag = a[static_cast<size_t>( r ) * n + r];
        if ( std::abs( diag ) < 1e-12 )
            return false;
        b[static_cast<size_t>( r )] = sum / diag;
    }
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
        std::vector<double> rhs( nEndmembers, 0.0 );
        for ( int e = 0; e < nEndmembers; ++e )
        {
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += static_cast<double>( endmembers[static_cast<size_t>( e ) * bands + b] )
                       * x[b];
            rhs[static_cast<size_t>( e )] = sum;
        }

        // Solve (G + ridge I) a = rhs; the RHS vector holds the solution after
        // the in-place back substitution.
        std::vector<double> system = gram;
        constexpr double kRidge = 1e-9;
        for ( int e = 0; e < nEndmembers; ++e )
            system[static_cast<size_t>( e ) * nEndmembers + e] += kRidge;

        std::vector<double> abundance = rhs;
        if ( !solveLinearSystem( system, abundance, nEndmembers ) )
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
        double errorSq = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            double est = 0.0;
            for ( int e = 0; e < nEndmembers; ++e )
                est += abundance[static_cast<size_t>( e )]
                       * endmembers[static_cast<size_t>( e ) * bands + b];
            const double diff = static_cast<double>( x[b] ) - est;
            errorSq += diff * diff;
        }
        result->reconstructionError[p] =
            static_cast<float>( std::sqrt( errorSq / bands ) );
    }
    return true;
}

} // namespace SpectralUnmixing
