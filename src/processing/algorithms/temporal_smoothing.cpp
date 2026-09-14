/***************************************************************************
  processing/algorithms/temporal_smoothing.cpp
  Temporal Phenology Timeline Studio (D16) — smoothing kernel implementations.
  ---------------------------
  See temporal_smoothing.h for the seam contract (ADR 0161).

  Whittaker core: (W + λDᵀD) z = W y solved by banded Cholesky in O(n) —
  half-bandwidth 2 for d = 2 (pentadiagonal, Eilers), 1 for d = 1 (Thomas
  shape). No dense matrices anywhere.
 ***************************************************************************/

#include "processing/algorithms/temporal_smoothing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sicnu::temporal
{

namespace
{

constexpr double kTiny = 1e-12;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

std::vector<double> effectiveWeights( const std::vector<float> &y, const std::vector<float> &w )
{
    std::vector<double> out( y.size(), 0.0 );
    for ( std::size_t i = 0; i < y.size(); ++i )
    {
        if ( !std::isfinite( y[i] ) )
            continue;
        out[i] = w.empty() ? 1.0 : std::max( 0.0, static_cast<double>( w[i] ) );
    }
    return out;
}

/// Banded Cholesky (A = L·Lᵗ) solve of A z = rhs. @param subA is A's first
/// sub-diagonal (A[i][i-1]); @param subB the second (A[i][i-2], unused when
/// @a bandwidth < 2). Returns false when A is not numerically positive
/// definite (caller contract: an empty result, never a silent guess).
bool bandedCholeskySolve( const std::vector<double> &diag, const std::vector<double> &subA,
                          const std::vector<double> &subB, int bandwidth,
                          const std::vector<double> &rhs, std::vector<float> &z )
{
    const std::size_t n = diag.size();
    std::vector<double> d( n, 0.0 );  // L[i][i]
    std::vector<double> a( n, 0.0 );  // L[i][i-1]
    std::vector<double> b( n, 0.0 );  // L[i][i-2]

    for ( std::size_t i = 0; i < n; ++i )
    {
        double bi = 0.0;
        if ( bandwidth >= 2 && i >= 2 )
        {
            bi = subB[i] / d[i - 2];
            if ( !std::isfinite( bi ) )
                return false;
        }
        double ai = 0.0;
        if ( i >= 1 )
        {
            ai = ( subA[i] - bi * a[i - 1] ) / d[i - 1];
            if ( !std::isfinite( ai ) )
                return false;
        }
        const double residual = diag[i] - ai * ai - bi * bi;
        if ( !( residual > kTiny ) )
            return false; // not positive definite
        d[i] = std::sqrt( residual );
        a[i] = ai;
        b[i] = bi;
    }

    // Forward substitution: L u = rhs.
    std::vector<double> u( n, 0.0 );
    for ( std::size_t i = 0; i < n; ++i )
    {
        u[i] = ( rhs[i] - ( i >= 1 ? a[i] * u[i - 1] : 0.0 ) -
                 ( bandwidth >= 2 && i >= 2 ? b[i] * u[i - 2] : 0.0 ) ) /
               d[i];
    }
    // Backward substitution: Lᵗ z = u.
    z.assign( n, 0.0f );
    for ( std::size_t i = n; i-- > 0; )
    {
        double v = u[i];
        if ( i + 1 < n )
            v -= a[i + 1] * z[i + 1];
        if ( bandwidth >= 2 && i + 2 < n )
            v -= b[i + 2] * z[i + 2];
        z[i] = static_cast<float>( v / d[i] );
    }
    return true;
}

} // namespace

std::vector<float> whittakerSmooth( const std::vector<float> &y, const std::vector<float> &w,
                                    double lambda, int d )
{
    const std::size_t n = y.size();
    if ( n == 0 )
        return {};
    if ( d != 1 && d != 2 )
        return {};
    if ( lambda < 0.0 || !std::isfinite( lambda ) )
        return {};
    if ( lambda == 0.0 )
        return y; // interpolation of the finite samples

    // No penalty possible when the difference order exceeds the series.
    if ( static_cast<std::size_t>( d ) >= n )
        return y;

    const std::vector<double> weights = effectiveWeights( y, w );

    // Documented contract: no finite sample -> no fit -> all-NaN (same size).
    const bool anyFinite = std::any_of( weights.begin(), weights.end(),
                                        []( double v ) { return v > 0.0; } );
    if ( !anyFinite )
        return std::vector<float>( n, kNan );

    // Assemble A = W + λDᵀD band entries (Eilers' pentadiagonal pattern).
    std::vector<double> diag( n, 0.0 );
    std::vector<double> subA( n, 0.0 );
    std::vector<double> subB( n, 0.0 );
    const double lam = lambda;
    if ( d == 1 )
    {
        diag[0] = weights[0] + lam;
        for ( std::size_t i = 1; i + 1 < n; ++i )
            diag[i] = weights[i] + 2.0 * lam;
        diag[n - 1] = weights[n - 1] + lam;
        for ( std::size_t i = 1; i < n; ++i )
            subA[i] = -lam;
    }
    else
    {
        diag[0] = weights[0] + lam;
        diag[1] = weights[1] + ( n == 3 ? 4.0 : 5.0 ) * lam;
        for ( std::size_t i = 2; i + 2 < n; ++i )
            diag[i] = weights[i] + 6.0 * lam;
        diag[n - 2] = weights[n - 2] + ( n == 3 ? 4.0 : 5.0 ) * lam;
        diag[n - 1] = weights[n - 1] + lam;
        subA[1] = -2.0 * lam;
        for ( std::size_t i = 2; i + 1 < n; ++i )
            subA[i] = -4.0 * lam;
        subA[n - 1] = -2.0 * lam;
        for ( std::size_t i = 2; i < n; ++i )
            subB[i] = lam;
    }

    // Weighted right-hand side: W y (weights are 0 exactly where y is not
    // finite — never multiply 0 by NaN, which would poison the solve).
    std::vector<double> rhs( n, 0.0 );
    for ( std::size_t i = 0; i < n; ++i )
        if ( weights[i] > 0.0 )
            rhs[i] = weights[i] * y[i];

    std::vector<float> z;
    if ( !bandedCholeskySolve( diag, subA, subB, d, rhs, z ) )
        return {};
    return z;
}

std::vector<float> whittakerSmoothRobust( const std::vector<float> &y, const std::vector<float> &w,
                                          double lambda, int iterations )
{
    // First pass: plain smoother.
    std::vector<float> z = whittakerSmooth( y, w, lambda, 2 );
    if ( z.empty() )
        return z;

    const std::size_t n = y.size();
    const std::vector<double> base = effectiveWeights( y, w );
    constexpr int kCauchyC = 3;

    for ( int it = 0; it < iterations; ++it )
    {
        // Residuals over finite, fitted samples.
        std::vector<double> residuals;
        residuals.reserve( n );
        for ( std::size_t i = 0; i < n; ++i )
            if ( std::isfinite( y[i] ) && std::isfinite( z[i] ) )
                residuals.push_back( static_cast<double>( y[i] ) - z[i] );
        if ( residuals.size() < 3 )
            break;

        // σ̂ = 1.4826 · MAD (median absolute deviation from the median).
        std::vector<double> sorted = residuals;
        const std::size_t mid = sorted.size() / 2;
        std::nth_element( sorted.begin(), sorted.begin() + mid, sorted.end() );
        const double median = sorted[mid];
        for ( double &r : sorted )
            r = std::abs( r - median );
        std::nth_element( sorted.begin(), sorted.begin() + mid, sorted.end() );
        const double sigma = 1.4826 * sorted[mid];
        if ( !( sigma > kTiny ) )
            break; // exact fit already

        // Cauchy reweighting: w0 / (1 + (r / (c·σ̂))²).
        std::vector<float> reweighted( n, 0.0f );
        for ( std::size_t i = 0; i < n; ++i )
        {
            if ( base[i] <= 0.0 )
                continue;
            const double r = static_cast<double>( y[i] ) - z[i];
            const double t = r / ( kCauchyC * sigma );
            reweighted[i] = static_cast<float>( base[i] / ( 1.0 + t * t ) );
        }
        std::vector<float> next = whittakerSmooth( y, reweighted, lambda, 2 );
        if ( next.empty() )
            break;
        z = std::move( next );
    }
    return z;
}

std::vector<float> savitzkyGolay( const std::vector<float> &y, int windowSize, int polynomialDegree )
{
    const std::size_t n = y.size();
    if ( n == 0 )
        return {};
    if ( windowSize < 3 || windowSize % 2 == 0 )
        return {};
    if ( polynomialDegree < 1 || polynomialDegree > 4 )
        return {};

    const int half = windowSize / 2;
    const int cols = polynomialDegree + 1;
    std::vector<float> out( n, kNan );

    for ( std::size_t i = 0; i < n; ++i )
    {
        if ( !std::isfinite( y[i] ) )
            continue;
        // Shrink-at-boundary window over finite samples only.
        const int lo = std::max<int>( 0, static_cast<int>( i ) - half );
        const int hi = std::min<int>( static_cast<int>( n ) - 1, static_cast<int>( i ) + half );
        std::vector<double> xs;
        std::vector<double> vs;
        for ( int j = lo; j <= hi; ++j )
        {
            if ( std::isfinite( y[j] ) )
            {
                xs.push_back( j - static_cast<int>( i ) );
                vs.push_back( y[j] );
            }
        }
        if ( xs.size() < static_cast<std::size_t>( cols ) )
            continue; // no fabricated bridging

        // Normal equations for the local polynomial, evaluated at x = 0.
        std::vector<double> ata( static_cast<std::size_t>( cols ) * cols, 0.0 );
        std::vector<double> atb( cols, 0.0 );
        for ( std::size_t s = 0; s < xs.size(); ++s )
        {
            const double x = xs[s];
            double rowPower = 1.0;
            for ( int r = 0; r < cols; ++r )
            {
                atb[r] += rowPower * vs[s];
                double colPower = 1.0;
                for ( int c = 0; c < cols; ++c )
                {
                    ata[static_cast<std::size_t>( r ) * cols + c] += rowPower * colPower;
                    colPower *= x;
                }
                rowPower *= x;
            }
        }

        // Gaussian elimination with partial pivoting.
        std::vector<double> sol( cols, 0.0 );
        bool ok = true;
        for ( int col = 0; col < cols && ok; ++col )
        {
            int pivot = col;
            for ( int r = col + 1; r < cols; ++r )
                if ( std::abs( ata[static_cast<std::size_t>( r ) * cols + col] ) >
                     std::abs( ata[static_cast<std::size_t>( pivot ) * cols + col] ) )
                    pivot = r;
            const double pivotValue = ata[static_cast<std::size_t>( pivot ) * cols + col];
            if ( !( std::abs( pivotValue ) > kTiny ) )
            {
                ok = false;
                break;
            }
            if ( pivot != col )
            {
                for ( int c = 0; c < cols; ++c )
                    std::swap( ata[static_cast<std::size_t>( pivot ) * cols + c],
                               ata[static_cast<std::size_t>( col ) * cols + c] );
                std::swap( atb[pivot], atb[col] );
            }
            for ( int r = col + 1; r < cols; ++r )
            {
                const double factor =
                    ata[static_cast<std::size_t>( r ) * cols + col] / pivotValue;
                for ( int c = col; c < cols; ++c )
                    ata[static_cast<std::size_t>( r ) * cols + c] -=
                        factor * ata[static_cast<std::size_t>( col ) * cols + c];
                atb[r] -= factor * atb[col];
            }
        }
        if ( !ok )
            continue;
        for ( int r = cols; r-- > 0; )
        {
            double v = atb[r];
            for ( int c = r + 1; c < cols; ++c )
                v -= ata[static_cast<std::size_t>( r ) * cols + c] * sol[c];
            sol[r] = v / ata[static_cast<std::size_t>( r ) * cols + r];
        }
        out[i] = static_cast<float>( sol[0] );
    }
    return out;
}

} // namespace sicnu::temporal
