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

} // namespace sicnu::primitives
