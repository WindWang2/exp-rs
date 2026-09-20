// dense_linalg.cpp — see dense_linalg.h
#include "dense_linalg.h"

#include <algorithm>
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

// Cyclic Jacobi rotations for a symmetric n×n row-major matrix: each sweep
// annihilates every off-diagonal pair with the closed-form 2×2 rotation
// (tau = (aqq−app)/(2apq), t = sign(tau)/(|tau|+sqrt(tau²+1))), so the
// diagonal converges to the eigenvalues. Deterministic (fixed sweep order,
// no randomness) and exact to machine precision — deliberately NOT power
// iteration: a fixed start vector is structurally blind for the equiangular
// Gram matrices this diagnostic exists to measure ((1,…,1) is an exact
// eigenvector of [[1,c],…,[c,1]], so inverse iteration would report λmin as
// λmax(A⁻¹) and under-report the condition number).
std::vector<double> symmetricEigenvalues( std::vector<double> a, int n )
{
    if ( n <= 0 || a.size() != static_cast<size_t>( n ) * n )
        return {};
    for ( const double v : a )
    {
        if ( !std::isfinite( v ) )
            return {};
    }

    constexpr int kMaxSweeps = 64;
    for ( int sweep = 0; sweep < kMaxSweeps; ++sweep )
    {
        for ( int p = 0; p < n; ++p )
        {
            for ( int q = p + 1; q < n; ++q )
            {
                const double apq = a[static_cast<size_t>( p ) * n + q];
                if ( apq == 0.0 )
                    continue;
                const double app = a[static_cast<size_t>( p ) * n + p];
                const double aqq = a[static_cast<size_t>( q ) * n + q];
                const double theta = ( aqq - app ) / ( 2.0 * apq );
                const double sign = theta >= 0.0 ? 1.0 : -1.0;
                const double t = sign / ( std::fabs( theta ) + std::sqrt( theta * theta + 1.0 ) );
                const double c = 1.0 / std::sqrt( t * t + 1.0 );
                const double s = t * c;
                for ( int k = 0; k < n; ++k )
                {
                    if ( k == p || k == q )
                        continue;
                    const double akp = a[static_cast<size_t>( k ) * n + p];
                    const double akq = a[static_cast<size_t>( k ) * n + q];
                    const double nkp = c * akp - s * akq;
                    const double nkq = s * akp + c * akq;
                    a[static_cast<size_t>( k ) * n + p] = nkp;
                    a[static_cast<size_t>( p ) * n + k] = nkp;
                    a[static_cast<size_t>( k ) * n + q] = nkq;
                    a[static_cast<size_t>( q ) * n + k] = nkq;
                }
                a[static_cast<size_t>( p ) * n + p] = app - t * apq;
                a[static_cast<size_t>( q ) * n + q] = aqq + t * apq;
                a[static_cast<size_t>( p ) * n + q] = 0.0;
                a[static_cast<size_t>( q ) * n + p] = 0.0;
            }
        }
        double offNorm2 = 0.0;
        double diagNorm2 = 0.0;
        for ( int i = 0; i < n; ++i )
        {
            const double dii = a[static_cast<size_t>( i ) * n + i];
            diagNorm2 += dii * dii;
            for ( int j = i + 1; j < n; ++j )
            {
                const double dij = a[static_cast<size_t>( i ) * n + j];
                offNorm2 += dij * dij;
            }
        }
        if ( offNorm2 <= 1e-30 * std::max( 1.0, diagNorm2 ) )
            break;
    }

    std::vector<double> eigenvalues( static_cast<size_t>( n ) );
    for ( int i = 0; i < n; ++i )
        eigenvalues[static_cast<size_t>( i )] = a[static_cast<size_t>( i ) * n + i];
    std::sort( eigenvalues.begin(), eigenvalues.end() );
    return eigenvalues;
}

} // namespace

double conditionNumber( const std::vector<double> &m, int n )
{
    const std::vector<double> eigenvalues = symmetricEigenvalues( m, n );
    if ( eigenvalues.empty() )
        return -1.0;
    const double lambdaMin = eigenvalues.front();
    const double lambdaMax = eigenvalues.back();
    if ( !( lambdaMin > 0.0 ) || !std::isfinite( lambdaMax ) )
        return -1.0;
    const double cond = lambdaMax / lambdaMin;
    if ( !std::isfinite( cond ) || !( cond >= 1.0 ) )
        return -1.0;
    return cond;
}

} // namespace sicnu::primitives
