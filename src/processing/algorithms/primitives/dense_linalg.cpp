// dense_linalg.cpp — see dense_linalg.h
#include "dense_linalg.h"

#include <cmath>

namespace sicnu::primitives
{

bool invertDenseMatrixInPlace( std::vector<double> &m, int n )
{
    // Augmented with an identity worked on side-by-side; the identity half
    // becomes the inverse (Gauss-Jordan, partial pivoting).
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
                           m[static_cast<size_t>( col ) * n + c] );
                std::swap( inv[static_cast<size_t>( pivot ) * n + c],
                           inv[static_cast<size_t>( col ) * n + c] );
            }
        }

        const double diag = m[static_cast<size_t>( col ) * n + col];
        // Normalize the pivot row.
        for ( int c = 0; c < n; ++c )
        {
            m[static_cast<size_t>( col ) * n + c] /= diag;
            inv[static_cast<size_t>( col ) * n + c] /= diag;
        }
        // Eliminate all other rows.
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

bool invertDenseMatrix( const std::vector<double> &m, int n, std::vector<double> *inverse )
{
    std::vector<double> a = m;
    if ( !invertDenseMatrixInPlace( a, n ) )
        return false;
    *inverse = std::move( a );
    return true;
}

namespace
{

/// Dominant eigenvalue of a symmetric matrix via power iteration (same
/// deterministic recipe as SpectralAnomaly::conditionProxy: normalized ones
/// start, 1e-12 relative Rayleigh residual early exit, hard iteration cap).
/// Returns -1 when the iteration cannot produce a finite positive value.
double dominantEigenvalue( const std::vector<double> &m, int n )
{
    if ( n <= 0 || m.size() != static_cast<size_t>( n ) * n )
        return -1.0;
    std::vector<double> v( static_cast<size_t>( n ),
                           1.0 / std::sqrt( static_cast<double>( n ) ) );
    double lambda = -1.0;
    for ( int it = 0; it < 128; ++it )
    {
        std::vector<double> w( static_cast<size_t>( n ), 0.0 );
        double vw = 0.0;
        double norm2 = 0.0;
        for ( int i = 0; i < n; ++i )
        {
            const size_t rowOffset = static_cast<size_t>( i ) * n;
            double row = 0.0;
            for ( int j = 0; j < n; ++j )
                row += m[rowOffset + j] * v[static_cast<size_t>( j )];
            w[static_cast<size_t>( i )] = row;
            vw += v[static_cast<size_t>( i )] * row;
            norm2 += row * row;
        }
        if ( !std::isfinite( norm2 ) || !( norm2 > 0.0 ) || !std::isfinite( vw ) )
            return -1.0;
        lambda = vw;
        const double norm = std::sqrt( norm2 );
        if ( std::fabs( norm - lambda ) <= 1e-12 * std::max( 1.0, norm ) )
            break;
        for ( int i = 0; i < n; ++i )
            v[static_cast<size_t>( i )] = w[static_cast<size_t>( i )] / norm;
    }
    if ( !std::isfinite( lambda ) || !( lambda > 0.0 ) )
        return -1.0;
    return lambda;
}

} // namespace

double conditionNumber( const std::vector<double> &m, int n )
{
    const double lambdaMax = dominantEigenvalue( m, n );
    if ( !( lambdaMax > 0.0 ) )
        return -1.0;
    std::vector<double> inverse;
    if ( !invertDenseMatrix( m, n, &inverse ) )
        return -1.0;
    // λmax(A⁻¹) = 1/λmin(A) for a symmetric positive-definite A.
    const double lambdaMaxInverse = dominantEigenvalue( inverse, n );
    if ( !( lambdaMaxInverse > 0.0 ) )
        return -1.0;
    const double cond = lambdaMax * lambdaMaxInverse;
    if ( !std::isfinite( cond ) || !( cond >= 1.0 ) )
        return -1.0;
    return cond;
}

} // namespace sicnu::primitives
