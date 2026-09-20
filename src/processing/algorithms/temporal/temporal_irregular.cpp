// src/processing/algorithms/temporal/temporal_irregular.cpp
#include "temporal_irregular.h"

#include "temporal_linalg_detail.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Shared input contract for the day-axis kernels: equal sizes, finite and
/// strictly increasing tDays. Duplicate instants have h = 0, which makes the
/// time metric degenerate (division by zero in the divided differences) —
/// typed refusal instead of a silent epsilon floor.
bool validTimeAxis( const std::vector<float> &y,
                    const std::vector<double> &tDays )
{
  if ( y.empty() || tDays.size() != y.size() )
    return false;
  for ( size_t i = 0; i < tDays.size(); ++i )
  {
    if ( !std::isfinite( tDays[i] ) )
      return false;
    if ( i > 0 && !( tDays[i] > tDays[i - 1] ) )
      return false;
  }
  return true;
}

/// Optional weight contract: empty = unweighted; otherwise exactly one
/// finite, non-negative weight per sample. A wrong-sized or invalid weight
/// vector is a caller error — refusing (all-NaN) beats silently ignoring it
/// or letting a negative diagonal make the normal equations indefinite.
bool weightsUsable( const std::vector<float> &w, int n )
{
  if ( w.empty() )
    return true;
  if ( static_cast<int>( w.size() ) != n )
    return false;
  for ( float wi : w )
  {
    if ( !( wi >= 0.0f ) )  // also rejects NaN
      return false;
  }
  return true;
}
} // namespace

std::vector<std::uint8_t> gapFillProvenance( const std::vector<float> &input,
                                             const std::vector<float> &output )
{
  if ( input.size() != output.size() )
    return {};
  std::vector<std::uint8_t> out( input.size(),
                                 static_cast<std::uint8_t>( SampleProvenance::Unavailable ) );
  for ( size_t i = 0; i < input.size(); ++i )
  {
    const bool inFinite = std::isfinite( input[i] );
    const bool outFinite = std::isfinite( output[i] );
    if ( inFinite && outFinite )
      out[i] = static_cast<std::uint8_t>( SampleProvenance::Observed );
    else if ( !inFinite && outFinite )
      out[i] = static_cast<std::uint8_t>( SampleProvenance::Interpolated );
    // NaN→NaN stays Unavailable; the anomalous finite→NaN case is also
    // reported as Unavailable rather than claiming an observation.
  }
  return out;
}

std::vector<float> movingAverageDays( const std::vector<float> &y,
                                      const std::vector<double> &tDays,
                                      double windowDays )
{
  const int n = static_cast<int>( y.size() );
  std::vector<float> out( static_cast<size_t>( n ), kNan );
  if ( !validTimeAxis( y, tDays ) || !( windowDays > 0.0 ) )
    return out;
  const double half = windowDays / 2.0;

  // Sliding window over the sorted axis: [lo, hi) holds all samples with
  // tDays[i] − half ≤ t < tDays[i] + half (upper bound inclusive via ≤).
  int lo = 0;
  int hi = 0;
  double sum = 0.0;
  int count = 0;
  for ( int i = 0; i < n; ++i )
  {
    const double hiBound = tDays[static_cast<size_t>( i )] + half;
    const double loBound = tDays[static_cast<size_t>( i )] - half;
    while ( hi < n && tDays[static_cast<size_t>( hi )] <= hiBound )
    {
      if ( std::isfinite( y[static_cast<size_t>( hi )] ) )
      {
        sum += y[static_cast<size_t>( hi )];
        ++count;
      }
      ++hi;
    }
    while ( lo < n && tDays[static_cast<size_t>( lo )] < loBound )
    {
      if ( std::isfinite( y[static_cast<size_t>( lo )] ) )
      {
        sum -= y[static_cast<size_t>( lo )];
        --count;
      }
      ++lo;
    }
    if ( count > 0 )
      out[static_cast<size_t>( i )] =
        static_cast<float>( sum / static_cast<double>( count ) );
  }
  return out;
}

std::vector<float> savitzkyGolayDays( const std::vector<float> &y,
                                      const std::vector<double> &tDays,
                                      double windowDays,
                                      int polynomialDegree )
{
  const int n = static_cast<int>( y.size() );
  std::vector<float> out( static_cast<size_t>( n ), kNan );
  if ( !validTimeAxis( y, tDays ) || !( windowDays > 0.0 ) ||
       polynomialDegree < 1 || polynomialDegree > 4 )
    return out;
  const double half = windowDays / 2.0;

  // Two-pointer window bounds: centers advance monotonically, so [lo, hi)
  // never shrinks backward — O(n) bound updates instead of an O(n) scan per
  // position. Gather buffers are hoisted out of the loop (per-tile pixel
  // counts make per-position allocations the dominant cost otherwise).
  std::vector<float> wy;
  std::vector<double> wt;
  wy.reserve( static_cast<size_t>( n ) );
  wt.reserve( static_cast<size_t>( n ) );
  int lo = 0;
  int hi = 0;
  for ( int i = 0; i < n; ++i )
  {
    const double center = tDays[static_cast<size_t>( i )];
    const double loBound = center - half;
    const double hiBound = center + half;
    while ( hi < n && tDays[static_cast<size_t>( hi )] <= hiBound )
      ++hi;
    while ( lo < n && tDays[static_cast<size_t>( lo )] < loBound )
      ++lo;
    // The polynomial is fitted in coordinates relative to the center
    // (xEval = 0) so the normal equations stay conditioned even for large
    // day ordinals (~19000 since epoch).
    wy.clear();
    wt.clear();
    for ( int j = lo; j < hi; ++j )
    {
      wy.push_back( y[static_cast<size_t>( j )] );
      wt.push_back( tDays[static_cast<size_t>( j )] - center );
    }
    if ( static_cast<int>( wy.size() ) < polynomialDegree + 1 )
      continue;
    out[static_cast<size_t>( i )] = detail::localPolynomialAt(
      wy, wt, 0, static_cast<int>( wy.size() ) - 1, polynomialDegree, 0.0 );
  }
  return out;
}

std::vector<float> whittakerSmoothTime( const std::vector<float> &y,
                                        const std::vector<double> &tDays,
                                        const std::vector<float> &w,
                                        double lambda )
{
  const int n = static_cast<int>( y.size() );
  std::vector<float> out( static_cast<size_t>( n ), kNan );
  if ( !validTimeAxis( y, tDays ) || !( lambda > 0.0 ) ||
       !weightsUsable( w, n ) )
    return out;
  const bool haveWeights = !w.empty();

  // Degenerate cases mirror whittakerSmooth: too few instants to define a
  // second difference → pass finite values through unchanged.
  if ( n == 1 )
    return std::vector<float>{ y[0] };
  if ( n == 2 )
  {
    for ( int i = 0; i < n; ++i )
      out[static_cast<size_t>( i )] =
        std::isfinite( y[static_cast<size_t>( i )] ) ? y[static_cast<size_t>( i )] : kNan;
    return out;
  }

  std::vector<double> main( static_cast<size_t>( n ), 0.0 );
  std::vector<double> rhs( static_cast<size_t>( n ), 0.0 );
  for ( int i = 0; i < n; ++i )
  {
    const double weight =
      std::isfinite( y[static_cast<size_t>( i )] )
        ? ( haveWeights ? w[static_cast<size_t>( i )] : 1.0 )
        : 0.0;
    main[static_cast<size_t>( i )] = weight;
    rhs[static_cast<size_t>( i )] =
      weight * ( std::isfinite( y[static_cast<size_t>( i )] )
                   ? y[static_cast<size_t>( i )]
                   : 0.0 );
  }

  // λ·DᵀCD accumulation. Row r of D is the second divided difference over
  // (t_r, t_{r+1}, t_{r+2}); C weights each row by the local cell width
  // (h_r + h_{r+1})/2 — a discretized ∫(z″)²dt that reduces to Σ(Δ²z)² at
  // unit spacing.
  std::vector<double> off1( static_cast<size_t>( n - 1 ), 0.0 );
  std::vector<double> off2( static_cast<size_t>( n - 2 ), 0.0 );
  for ( int r = 0; r + 2 < n; ++r )
  {
    const double h0 = tDays[static_cast<size_t>( r + 1 )] - tDays[static_cast<size_t>( r )];
    const double h1 = tDays[static_cast<size_t>( r + 2 )] - tDays[static_cast<size_t>( r + 1 )];
    const double span = h0 + h1;
    const double a = 2.0 / ( h0 * span );
    const double b = -2.0 / ( h0 * h1 );
    const double c = 2.0 / ( h1 * span );
    const double s = lambda * span / 2.0;
    main[static_cast<size_t>( r )] += s * a * a;
    off1[static_cast<size_t>( r )] += s * a * b;
    off2[static_cast<size_t>( r )] += s * a * c;
    main[static_cast<size_t>( r + 1 )] += s * b * b;
    off1[static_cast<size_t>( r + 1 )] += s * b * c;
    main[static_cast<size_t>( r + 2 )] += s * c * c;
  }

  std::vector<double> x;
  if ( !detail::solvePentadiagonal( main, off1, off2, rhs, &x ) )
    return out;
  for ( int i = 0; i < n; ++i )
    out[static_cast<size_t>( i )] =
      std::isfinite( x[static_cast<size_t>( i )] )
        ? static_cast<float>( x[static_cast<size_t>( i )] )
        : kNan;
  return out;
}

std::vector<float> whittakerSmoothTimeRobust( const std::vector<float> &y,
                                              const std::vector<double> &tDays,
                                              const std::vector<float> &w,
                                              double lambda, int iterations )
{
  const int n = static_cast<int>( y.size() );
  if ( !validTimeAxis( y, tDays ) || !( lambda > 0.0 ) ||
       !weightsUsable( w, n ) )
    return std::vector<float>( static_cast<size_t>( n ), kNan );
  const bool haveWeights = !w.empty();
  std::vector<float> weights( static_cast<size_t>( n ), 0.0f );
  for ( int i = 0; i < n; ++i )
    weights[static_cast<size_t>( i )] =
      std::isfinite( y[static_cast<size_t>( i )] )
        ? ( haveWeights ? w[static_cast<size_t>( i )] : 1.0f )
        : 0.0f;

  const int maxIter = std::clamp( iterations, 1, 10 );
  std::vector<float> z = whittakerSmoothTime( y, tDays, weights, lambda );
  for ( int iter = 1; iter < maxIter; ++iter )
  {
    std::vector<double> absRes;
    absRes.reserve( static_cast<size_t>( n ) );
    for ( int i = 0; i < n; ++i )
    {
      if ( std::isfinite( y[static_cast<size_t>( i )] ) &&
           std::isfinite( z[static_cast<size_t>( i )] ) )
        absRes.push_back(
          std::abs( static_cast<double>( y[static_cast<size_t>( i )] ) -
                    z[static_cast<size_t>( i )] ) );
    }
    if ( absRes.empty() )
      break;
    std::sort( absRes.begin(), absRes.end() );
    const double mad = absRes[absRes.size() / 2];
    const double k = std::max( 3.0 * 1.4826 * mad, 1e-9 );
    bool changed = false;
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[static_cast<size_t>( i )] <= 0.0f )
        continue;
      const double r = std::abs( static_cast<double>( y[static_cast<size_t>( i )] ) -
                                 z[static_cast<size_t>( i )] );
      const double cauchy = 1.0 / ( 1.0 + ( r / k ) * ( r / k ) );
      const double newW =
        ( haveWeights ? w[static_cast<size_t>( i )] : 1.0 ) * cauchy;
      if ( std::abs( newW - weights[static_cast<size_t>( i )] ) > 1e-6 )
        changed = true;
      weights[static_cast<size_t>( i )] = static_cast<float>( newW );
    }
    if ( !changed )
      break;
    z = whittakerSmoothTime( y, tDays, weights, lambda );
  }
  return z;
}

} // namespace sicnu::temporal
