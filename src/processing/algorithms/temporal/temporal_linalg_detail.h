// src/processing/algorithms/temporal/temporal_linalg_detail.h
// Shared small dense solver for the temporal kernels (detail namespace, not
// public API). Extracted from temporal_fit.cpp so the joint harmonic+trend
// segmentation (temporal_change.h) reuses the same elimination instead of
// growing a second copy.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace sicnu::temporal::detail
{

/// Solves the small dense symmetric system A·x = b by Gaussian elimination
/// with partial pivoting (row-major @a a, n×n). Returns false on a singular
/// system (pivot < 1e-12).
inline bool solveSmallDense( std::vector<double> a, std::vector<double> b, int n,
                             std::vector<double> *x )
{
  for ( int col = 0; col < n; ++col )
  {
    int pivot = col;
    double best = std::fabs( a[static_cast<size_t>( col ) * n + col] );
    for ( int row = col + 1; row < n; ++row )
    {
      const double v = std::fabs( a[static_cast<size_t>( row ) * n + col] );
      if ( v > best )
      {
        best = v;
        pivot = row;
      }
    }
    if ( best < 1e-12 )
      return false;
    if ( pivot != col )
    {
      for ( int k = col; k < n; ++k )
        std::swap( a[static_cast<size_t>( col ) * n + k],
                   a[static_cast<size_t>( pivot ) * n + k] );
      std::swap( b[col], b[pivot] );
    }
    const double d = a[static_cast<size_t>( col ) * n + col];
    for ( int row = col + 1; row < n; ++row )
    {
      const double factor = a[static_cast<size_t>( row ) * n + col] / d;
      if ( factor == 0.0 )
        continue;
      for ( int k = col; k < n; ++k )
        a[static_cast<size_t>( row ) * n + k] -= factor * a[static_cast<size_t>( col ) * n + k];
      b[row] -= factor * b[col];
    }
  }
  x->assign( n, 0.0 );
  for ( int row = n - 1; row >= 0; --row )
  {
    double sum = b[row];
    for ( int k = row + 1; k < n; ++k )
      sum -= a[static_cast<size_t>( row ) * n + k] * ( *x )[k];
    ( *x )[row] = sum / a[static_cast<size_t>( row ) * n + row];
  }
  return true;
}

/// Pentadiagonal LDLᵀ solve for symmetric A with bandwidth 2 (e.g.
/// W + λ·DᵀD). @a main (n), @a off1 (n-1), @a off2 (n-2) describe the
/// matrix. Returns false when a diagonal pivot drops below 1e-12.
/// n < 5 falls back to solveSmallDense. Extracted from temporal_fit.cpp so
/// the irregular-time Whittaker (temporal_irregular.h) reuses the same
/// factorization.
inline bool solvePentadiagonal( const std::vector<double> &main,
                                const std::vector<double> &off1,
                                const std::vector<double> &off2,
                                const std::vector<double> &rhs,
                                std::vector<double> *x )
{
  const int n = static_cast<int>( main.size() );
  if ( n < 5 )
  {
    // Small systems: fall back to dense elimination.
    std::vector<double> a( static_cast<size_t>( n ) * n, 0.0 );
    for ( int i = 0; i < n; ++i )
    {
      a[static_cast<size_t>( i ) * n + i] = main[i];
      if ( i + 1 < n )
      {
        a[static_cast<size_t>( i ) * n + ( i + 1 )] = off1[i];
        a[static_cast<size_t>( i + 1 ) * n + i] = off1[i];
      }
      if ( i + 2 < n )
      {
        a[static_cast<size_t>( i ) * n + ( i + 2 )] = off2[i];
        a[static_cast<size_t>( i + 2 ) * n + i] = off2[i];
      }
    }
    return solveSmallDense( a, rhs, n, x );
  }
  std::vector<double> d( n, 0.0 ), l1( n, 0.0 ), l2( n, 0.0 );
  // Column-wise banded LDLᵀ for bandwidth 2:
  //   D[i]  = A[i][i] − L1[i−1]²·D[i−1] − L2[i−2]²·D[i−2]
  //   L1[i] = ( A[i+1][i] − L1[i−1]·L2[i−1]·D[i−1] ) / D[i]
  //   L2[i] = A[i+2][i] / D[i]
  for ( int i = 0; i < n; ++i )
  {
    double sum = main[i];
    if ( i >= 1 )
      sum -= l1[i - 1] * l1[i - 1] * d[i - 1];
    if ( i >= 2 )
      sum -= l2[i - 2] * l2[i - 2] * d[i - 2];
    if ( !( sum > 1e-12 ) )
      return false;
    d[i] = sum;
    if ( i + 1 < n )
    {
      double s1 = off1[i];
      if ( i >= 1 )
        s1 -= l1[i - 1] * l2[i - 1] * d[i - 1];
      l1[i] = s1 / d[i];
    }
    if ( i + 2 < n )
      l2[i] = off2[i] / d[i];
  }
  // Forward substitution (L y = b).
  std::vector<double> yy( n, 0.0 );
  for ( int i = 0; i < n; ++i )
  {
    double s = rhs[i];
    if ( i >= 1 )
      s -= l1[i - 1] * yy[i - 1];
    if ( i >= 2 )
      s -= l2[i - 2] * yy[i - 2];
    yy[i] = s;
  }
  // Diagonal solve + back substitution (Lᵀ x = D⁻¹ y).
  for ( int i = 0; i < n; ++i )
    yy[i] /= d[i];
  x->assign( n, 0.0 );
  for ( int i = n - 1; i >= 0; --i )
  {
    double s = yy[i];
    if ( i + 1 < n )
      s -= l1[i] * ( *x )[i + 1];
    if ( i + 2 < n )
      s -= l2[i] * ( *x )[i + 2];
    ( *x )[i] = s;
  }
  return true;
}

/// Acklam's rational-approximation inverse standard-normal CDF
/// (|ε| < 1.15e-9 relative); deterministic, allocation-free. Shared by the
/// analytic CI kernel (temporal_uncertainty.cpp) and the Sen-slope CI
/// (temporal_fit.cpp).
inline double normalQuantile( double p )
{
  p = p < 1e-15 ? 1e-15 : ( p > 1.0 - 1e-15 ? 1.0 - 1e-15 : p );
  constexpr double a[] = { -3.969683028665376e+01, 2.209460984245205e+02,
                           -2.759285104469687e+02, 1.383577518672690e+02,
                           -3.066479806614716e+01, 2.506628277459239e+00 };
  constexpr double b[] = { -5.447609879822406e+01, 1.615858368580409e+02,
                           -1.556989798598866e+02, 6.680131188771972e+01,
                           -1.328068155288572e+01 };
  constexpr double c[] = { -7.784894002430293e-03, -3.223964580411365e-01,
                           -2.400758277161838e+00, -2.549732539343734e+00,
                           4.374664141464968e+00,  2.938163982698783e+00 };
  constexpr double d[] = { 7.784695709041462e-03, 3.224671290700398e-01,
                           2.445134137142996e+00, 3.754408661907416e+00 };
  const double pLow = 0.02425;
  if ( p < pLow )
  {
    const double q = std::sqrt( -2.0 * std::log( p ) );
    const double num =
      ( ( ( ( c[0] * q + c[1] ) * q + c[2] ) * q + c[3] ) * q + c[4] ) * q + c[5];
    const double den = ( ( ( d[0] * q + d[1] ) * q + d[2] ) * q + d[3] ) * q + 1.0;
    return num / den;
  }
  if ( p <= 1.0 - pLow )
  {
    const double q = p - 0.5;
    const double r = q * q;
    const double num =
      ( ( ( ( a[0] * r + a[1] ) * r + a[2] ) * r + a[3] ) * r + a[4] ) * r + a[5];
    const double den = ( ( ( ( b[0] * r + b[1] ) * r + b[2] ) * r + b[3] ) * r +
                         b[4] ) * r + 1.0;
    return num * q / den;
  }
  const double q = std::sqrt( -2.0 * std::log( 1.0 - p ) );
  const double num =
    ( ( ( ( c[0] * q + c[1] ) * q + c[2] ) * q + c[3] ) * q + c[4] ) * q + c[5];
  const double den = ( ( ( d[0] * q + d[1] ) * q + d[2] ) * q + d[3] ) * q + 1.0;
  return -num / den;
}

/// Fits a polynomial of degree (terms−1 ≤ 4) in @a t to the finite samples of
/// @a y over indices [lo, hi] via least squares, and evaluates it at
/// @a xEval. Returns NaN when fewer than terms samples are finite or the
/// normal system is singular. Extracted from temporal_fit.cpp so
/// savitzkyGolayDays (temporal_irregular.h) shares the same implementation.
inline float localPolynomialAt( const std::vector<float> &y,
                                const std::vector<double> &t, int lo, int hi,
                                int polynomialDegree, double xEval )
{
  const float kNanF = std::numeric_limits<float>::quiet_NaN();
  const int terms = polynomialDegree + 1;
  std::vector<double> ata( static_cast<size_t>( terms ) * terms, 0.0 );
  std::vector<double> atb( terms, 0.0 );
  int count = 0;
  for ( int i = lo; i <= hi; ++i )
  {
    if ( !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const double ti = t[static_cast<size_t>( i )];
    double powers[5] = { 1.0, ti, ti * ti, ti * ti * ti,
                         ti * ti * ti * ti };
    for ( int r = 0; r < terms; ++r )
    {
      atb[static_cast<size_t>( r )] += powers[r] * y[static_cast<size_t>( i )];
      for ( int c = 0; c < terms; ++c )
        ata[static_cast<size_t>( r ) * terms + c] +=
          powers[r] * powers[c];
    }
    ++count;
  }
  if ( count < terms )
    return kNanF;
  std::vector<double> coef;
  if ( !solveSmallDense( ata, atb, terms, &coef ) )
    return kNanF;
  double value = 0.0;
  double xPow = 1.0;
  for ( int r = 0; r < terms; ++r )
  {
    value += coef[r] * xPow;
    xPow *= xEval;
  }
  return std::isfinite( value ) ? static_cast<float>( value ) : kNanF;
}


/// Robust scale σ̂ = 1.4826 · MAD(r) where MAD = median(|r − median(r)|).
/// Even counts take the mean of the two middle elements (same convention as
/// the Whittaker IRLS in temporal_smoothing.cpp). Empty input → 0.
inline double madScale( std::vector<double> residuals )
{
  if ( residuals.empty() )
    return 0.0;
  const auto medianOf = []( std::vector<double> values ) {
    const std::size_t m = values.size() / 2;
    std::nth_element( values.begin(), values.begin() + static_cast<std::ptrdiff_t>( m ),
                      values.end() );
    if ( values.size() % 2 == 1 )
      return values[m];
    const double upper = values[m];
    const double lower = *std::max_element( values.begin(), values.begin() + static_cast<std::ptrdiff_t>( m ) );
    return 0.5 * ( lower + upper );
  };
  const double med = medianOf( residuals );
  for ( double &r : residuals )
    r = std::fabs( r - med );
  return 1.4826 * medianOf( residuals );
}

} // namespace sicnu::temporal::detail
