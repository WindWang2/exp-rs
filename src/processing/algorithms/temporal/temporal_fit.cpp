// src/processing/algorithms/temporal/temporal_fit.cpp
#include "temporal_fit.h"

#include <algorithm>
#include <utility>
#include <cmath>
#include <limits>
#include <numeric>

namespace sicnu::temporal
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Solves the small symmetric-positive system A·x = b by Gaussian elimination
/// with partial pivoting. Returns false on a singular system.
bool solveSmall( std::vector<double> a, std::vector<double> b, int n,
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
        std::swap( a[static_cast<size_t>( col ) * n + k], a[static_cast<size_t>( pivot ) * n + k] );
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

/// One local polynomial fit (normal equations, degree <= 4) over the window
/// [lo, hi] of y/t; evaluates the fitted polynomial at xEval.
float localPolynomialAt( const std::vector<float> &y, const std::vector<double> &t,
                         int lo, int hi, int degree, double xEval )
{
  const int terms = degree + 1;
  std::vector<double> ata( static_cast<size_t>( terms ) * terms, 0.0 );
  std::vector<double> atb( terms, 0.0 );
  int count = 0;
  for ( int i = lo; i <= hi; ++i )
  {
    if ( !std::isfinite( y[i] ) )
      continue;
    double powers[5] = { 1.0, t[i], t[i] * t[i], t[i] * t[i] * t[i],
                         t[i] * t[i] * t[i] * t[i] };
    for ( int r = 0; r < terms; ++r )
    {
      atb[r] += powers[r] * y[i];
      for ( int c = 0; c < terms; ++c )
        ata[static_cast<size_t>( r ) * terms + c] += powers[r] * powers[c];
    }
    ++count;
  }
  if ( count < terms )
    return kNan;
  std::vector<double> coef;
  if ( !solveSmall( ata, atb, terms, &coef ) )
    return kNan;
  double value = 0.0;
  double xPow = 1.0;
  for ( int r = 0; r < terms; ++r )
  {
    value += coef[r] * xPow;
    xPow *= xEval;
  }
  return std::isfinite( value ) ? static_cast<float>( value ) : kNan;
}

/// Pentadiagonal LDLᵀ solve for A = W + λDᵀD (symmetric, bandwidth 2).
/// @a main (n), @a off1 (n-1), @a off2 (n-2) describe the symmetric matrix.
bool solvePentadiagonal( const std::vector<double> &main, const std::vector<double> &off1,
                         const std::vector<double> &off2, const std::vector<double> &rhs,
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
    return solveSmall( a, rhs, n, x );
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
} // namespace

std::vector<float> savitzkyGolay( const std::vector<float> &y, int window,
                                  int polynomialDegree )
{
  const int n = static_cast<int>( y.size() );
  std::vector<float> out( n, kNan );
  if ( n == 0 || window < 3 || ( window % 2 ) == 0 || polynomialDegree < 1 ||
       polynomialDegree > 4 )
    return out;
  const int half = window / 2;
  std::vector<double> t( n );
  std::iota( t.begin(), t.end(), 0.0 );
  for ( int i = 0; i < n; ++i )
  {
    const int lo = std::max( 0, i - half );
    const int hi = std::min( n - 1, i + half );
    out[i] = localPolynomialAt( y, t, lo, hi, polynomialDegree, static_cast<double>( i ) );
  }
  return out;
}

std::vector<float> whittakerSmooth( const std::vector<float> &y,
                                    const std::vector<float> &w, double lambda )
{
  const int n = static_cast<int>( y.size() );
  std::vector<float> out( n, kNan );
  if ( n == 0 || !( lambda > 0.0 ) )
    return out;
  const bool haveWeights = !w.empty() && static_cast<int>( w.size() ) == n;

  std::vector<double> main( n, 0.0 );
  std::vector<double> rhs( n, 0.0 );
  for ( int i = 0; i < n; ++i )
  {
    const double weight = std::isfinite( y[i] ) ? ( haveWeights ? w[i] : 1.0 ) : 0.0;
    main[i] = weight;
    rhs[i] = weight * ( std::isfinite( y[i] ) ? y[i] : 0.0 );
  }
  // A = W + λ·DᵀD. DᵀD (second-difference Gram) is pentadiagonal:
  //   main    = [1, 5, 6, …, 6, 5, 1]·λ
  //   off1    = [−2, −4, …, −4, −2]·λ
  //   off2    = [+1, …, +1]·λ
  // n < 3 special cases: DᵀD degenerates (identity interpolation).
  if ( n == 1 )
    return std::vector<float>{ y[0] };
  if ( n == 2 )
  {
    for ( int i = 0; i < n; ++i )
      out[i] = std::isfinite( y[i] ) ? y[i] : kNan;
    return out;
  }
  for ( int i = 0; i < n; ++i )
    main[i] += 6.0 * lambda;
  main[0] -= 5.0 * lambda;
  main[n - 1] -= 5.0 * lambda;
  main[1] -= 1.0 * lambda;
  main[n - 2] -= 1.0 * lambda;
  std::vector<double> off1( n - 1, -4.0 * lambda );
  off1[0] = -2.0 * lambda;
  off1[n - 2] = -2.0 * lambda;
  std::vector<double> off2( n - 2, lambda );

  std::vector<double> x;
  if ( !solvePentadiagonal( main, off1, off2, rhs, &x ) )
    return out;
  for ( int i = 0; i < n; ++i )
    out[i] = std::isfinite( x[i] ) ? static_cast<float>( x[i] ) : kNan;
  return out;
}

HarmonicFitResult harmonicFit( const std::vector<float> &y,
                               const std::vector<double> &tDays, int harmonics,
                               bool robust )
{
  HarmonicFitResult result;
  const int n = static_cast<int>( y.size() );
  const int terms = 1 + 2 * std::clamp( harmonics, 1, 6 );
  result.fitted.assign( n, kNan );
  if ( n == 0 || static_cast<int>( tDays.size() ) != n )
    return result;

  std::vector<double> weights( n, 0.0 );
  for ( int i = 0; i < n; ++i )
    weights[i] = std::isfinite( y[i] ) ? 1.0 : 0.0;

  for ( int iteration = 0; iteration < ( robust ? 4 : 1 ); ++iteration )
  {
    std::vector<double> ata( static_cast<size_t>( terms ) * terms, 0.0 );
    std::vector<double> atb( terms, 0.0 );
    int valid = 0;
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[i] <= 0.0 )
        continue;
      double design[13];
      design[0] = 1.0;
      for ( int k = 1; k <= harmonics; ++k )
      {
        const double omega = 2.0 * kPi * k * tDays[i] / 365.25;
        design[2 * k - 1] = std::sin( omega );
        design[2 * k] = std::cos( omega );
      }
      for ( int r = 0; r < terms; ++r )
      {
        atb[r] += weights[i] * design[r] * y[i];
        for ( int c = 0; c < terms; ++c )
          ata[static_cast<size_t>( r ) * terms + c] +=
            weights[i] * design[r] * design[c];
      }
      ++valid;
    }
    if ( valid < terms )
      return result; // underdetermined: NaN fit (documented contract)
    std::vector<double> coef;
    if ( !solveSmall( ata, atb, terms, &coef ) )
      return result;

    // Stats + fitted values.
    double sse = 0.0;
    double mean = 0.0;
    int count = 0;
    std::vector<float> fitted( n, kNan );
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[i] <= 0.0 )
        continue;
      double design[13];
      design[0] = 1.0;
      for ( int k = 1; k <= harmonics; ++k )
      {
        const double omega = 2.0 * kPi * k * tDays[i] / 365.25;
        design[2 * k - 1] = std::sin( omega );
        design[2 * k] = std::cos( omega );
      }
      double v = 0.0;
      for ( int r = 0; r < terms; ++r )
        v += coef[r] * design[r];
      fitted[i] = static_cast<float>( v );
      const double residual = y[i] - v;
      sse += weights[i] * residual * residual;
      mean += y[i];
      ++count;
    }
    result.coefficients = coef;
    result.validCount = count;
    result.rmse = count > 0 ? std::sqrt( sse / count ) : 0.0;
    double sst = 0.0;
    const double m = count > 0 ? mean / count : 0.0;
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[i] <= 0.0 )
        continue;
      const double d = y[i] - m;
      sst += d * d;
    }
    result.r2 = sst > 0.0 ? 1.0 - sse / sst : 0.0;

    if ( !robust || iteration == 3 )
    {
      result.fitted = fitted;
      return result;
    }
    // IRLS: Huber-style weights from 1.5·MAD scale.
    std::vector<double> residuals;
    residuals.reserve( count );
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[i] <= 0.0 )
        continue;
      residuals.push_back( std::abs( static_cast<double>( y[i] ) - fitted[i] ) );
    }
    std::sort( residuals.begin(), residuals.end() );
    const double mad = residuals.empty()
                         ? 0.0
                         : residuals[residuals.size() / 2];
    const double scale = 1.4826 * mad;
    const double delta = ( scale > 1e-9 ? 1.5 * scale : 1e6 );
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[i] <= 0.0 )
        continue;
      const double r = std::abs( static_cast<double>( y[i] ) - fitted[i] );
      weights[i] = r <= delta ? 1.0 : delta / r;
    }
  }
  return result;
}

SeasonalMetrics phenologyThreshold( const std::vector<float> &y,
                                    const std::vector<double> &tDays,
                                    const std::vector<int> &doyOf,
                                    int seasonStartDoy, int seasonEndDoy,
                                    double crossingFraction )
{
  SeasonalMetrics out;
  const int n = static_cast<int>( y.size() );
  if ( n == 0 || static_cast<int>( tDays.size() ) != n ||
       static_cast<int>( doyOf.size() ) != n )
    return out;
  if ( !( crossingFraction > 0.0 && crossingFraction <= 1.0 ) )
    return out;

  // Season extraction: when start <= end a plain filter; a wrapped season
  // (start > end) matches doy >= start || doy <= end.
  std::vector<int> idx;
  for ( int i = 0; i < n; ++i )
  {
    if ( !std::isfinite( y[i] ) )
      continue;
    const int doy = doyOf[i];
    const bool inSeason =
      seasonStartDoy <= seasonEndDoy
        ? ( doy >= seasonStartDoy && doy <= seasonEndDoy )
        : ( doy >= seasonStartDoy || doy <= seasonEndDoy );
    if ( inSeason )
      idx.push_back( i );
  }
  if ( idx.size() < 3 )
    return out;

  float minV = y[idx.front()];
  float maxV = y[idx.front()];
  int posIdx = idx.front();
  for ( int i : idx )
  {
    if ( y[i] < minV )
      minV = y[i];
    if ( y[i] > maxV )
    {
      maxV = y[i];
      posIdx = i;
    }
  }
  out.base = minV;
  out.amplitude = maxV - minV;
  out.pos = doyOf[posIdx];
  const double threshold = minV + crossingFraction * ( maxV - minV );

  int sosIdx = -1;
  int eosIdx = -1;
  for ( int k = 0; k < static_cast<int>( idx.size() ); ++k )
  {
    if ( y[idx[k]] >= threshold )
    {
      sosIdx = idx[k];
      break;
    }
  }
  for ( int k = static_cast<int>( idx.size() ) - 1; k >= 0; --k )
  {
    if ( y[idx[k]] >= threshold )
    {
      eosIdx = idx[k];
      break;
    }
  }
  if ( sosIdx < 0 || eosIdx < 0 )
    return out;
  out.sos = doyOf[sosIdx];
  out.eos = doyOf[eosIdx];

  // LOS in days over the t axis (season may wrap: negative span + 365.25).
  double span = tDays[eosIdx] - tDays[sosIdx];
  if ( span < 0.0 )
    span += 365.25;
  out.los = span;

  // Small integral: Σ v·Δt over the season's valid samples.
  out.integral = 0.0;
  for ( int k = 1; k < static_cast<int>( idx.size() ); ++k )
  {
    const double dt = tDays[idx[k]] - tDays[idx[k - 1]];
    if ( dt > 0.0 && dt < 120.0 ) // ignore year-boundary jumps in wrapped seasons
      out.integral += 0.5 * ( y[idx[k]] + y[idx[k - 1]] ) * dt;
  }
  out.valid = true;
  return out;
}

BreakpointResult piecewiseLinearTrend( const std::vector<float> &y,
                                       const std::vector<double> &tDays,
                                       int maxBreaks, int minSegment,
                                       double minImprovement )
{
  BreakpointResult result;
  const int n = static_cast<int>( y.size() );
  if ( n < 2 || static_cast<int>( tDays.size() ) != n )
    return result;

  // Cumulative sums for O(1) segment OLS: Σ1, Σy, Σy², Σt, Σt², Σty.
  std::vector<double> c1( n + 1, 0.0 ), cy( n + 1, 0.0 ), cy2( n + 1, 0.0 );
  std::vector<double> ct( n + 1, 0.0 ), ct2( n + 1, 0.0 ), cty( n + 1, 0.0 );
  for ( int i = 0; i < n; ++i )
  {
    const double v = std::isfinite( y[i] ) ? y[i] : 0.0;
    const double w = std::isfinite( y[i] ) ? 1.0 : 0.0;
    c1[i + 1] = c1[i] + w;
    cy[i + 1] = cy[i] + w * v;
    cy2[i + 1] = cy2[i] + w * v * v;
    ct[i + 1] = ct[i] + w * tDays[i];
    ct2[i + 1] = ct2[i] + w * tDays[i] * tDays[i];
    cty[i + 1] = cty[i] + w * tDays[i] * v;
  }
  auto segmentRss = [&]( int a, int b, double *slopeOut, double *iceptOut ) -> double {
    // Segment [a, b) in index space.
    if ( b - a < 2 )
      return 0.0;
    const double c1s = c1[b] - c1[a];
    const double cys = cy[b] - cy[a];
    const double cts = ct[b] - ct[a];
    const double ct2s = ct2[b] - ct2[a];
    const double ctys = cty[b] - cty[a];
    const double cy2s = cy2[b] - cy2[a];
    const double denom = c1s * ct2s - cts * cts;
    if ( std::fabs( denom ) < 1e-9 )
    {
      if ( slopeOut )
        *slopeOut = 0.0;
      if ( iceptOut )
        *iceptOut = c1s > 0 ? cys / c1s : 0.0;
      // RSS about the mean.
      double rss = cy2s - cys * cys / std::max( c1s, 1e-9 );
      return std::max( 0.0, rss );
    }
    const double slope = ( c1s * ctys - cts * cys ) / denom;
    const double intercept = ( cys - slope * cts ) / c1s;
    const double rss = cy2s - 2.0 * slope * ctys - 2.0 * intercept * cys +
                       slope * slope * ct2s + 2.0 * slope * intercept * cts +
                       c1s * intercept * intercept;
    if ( slopeOut )
      *slopeOut = slope;
    if ( iceptOut )
      *iceptOut = intercept;
    return std::max( 0.0, rss );
  };

  std::vector<int> breaks;
  std::vector<std::pair<int, int>> segments;
  segments.push_back( { 0, n } );
  const int maxSeg = std::clamp( maxBreaks, 0, 16 );
  const int minSeg = std::max( 3, minSegment );

  for ( int iter = 0; iter < maxSeg; ++iter )
  {
    double bestGain = 0.0;
    std::pair<int, int> bestSegment{ -1, -1 };
    int bestSplit = -1;
    double bestRssParent = 0.0;
    for ( const auto &seg : segments )
    {
      const int a = seg.first;
      const int b = seg.second;
      if ( b - a < 2 * minSeg )
        continue;
      const double rssParent = segmentRss( a, b, nullptr, nullptr );
      for ( int s = a + minSeg; s <= b - minSeg; ++s )
      {
        const double rssL = segmentRss( a, s, nullptr, nullptr );
        const double rssR = segmentRss( s, b, nullptr, nullptr );
        const double gain = rssParent - ( rssL + rssR );
        if ( gain > bestGain )
        {
          bestGain = gain;
          bestSegment = seg;
          bestSplit = s;
          bestRssParent = rssParent;
        }
      }
    }
    if ( bestSplit < 0 )
      break;
    if ( bestRssParent <= 0.0 || bestGain / bestRssParent < minImprovement )
      break;
    segments.erase( std::find( segments.begin(), segments.end(), bestSegment ) );
    segments.push_back( { bestSegment.first, bestSplit } );
    segments.push_back( { bestSplit, bestSegment.second } );
    breaks.push_back( bestSplit );
  }

  std::sort( breaks.begin(), breaks.end() );
  // Re-fit segments in order.
  std::vector<std::pair<int, int>> ordered;
  int start = 0;
  for ( int b : breaks )
  {
    ordered.push_back( { start, b } );
    start = b;
  }
  ordered.push_back( { start, n } );
  double totalSse = 0.0;
  long totalValid = 0;
  for ( const auto &seg : ordered )
  {
    double slope = 0.0;
    double intercept = 0.0;
    totalSse += segmentRss( seg.first, seg.second, &slope, &intercept );
    result.slopes.push_back( slope );
    result.intercepts.push_back( intercept );
    totalValid += seg.second - seg.first;
  }
  result.breakIndices = breaks;
  result.rmse = totalValid > 0 ? std::sqrt( totalSse / totalValid ) : 0.0;
  return result;
}

DecompositionResult seasonalDecompose( const std::vector<float> &y,
                                       const std::vector<double> &tDays,
                                       const std::vector<int> &doyOf,
                                       double trendLambda, int seasonalWindow )
{
  DecompositionResult out;
  const int n = static_cast<int>( y.size() );
  out.trend.assign( n, kNan );
  out.seasonal.assign( n, kNan );
  out.remainder.assign( n, kNan );
  if ( n == 0 || static_cast<int>( tDays.size() ) != n ||
       static_cast<int>( doyOf.size() ) != n )
    return out;

  // Trend: Whittaker on the raw series (missing values weighted 0).
  out.trend = whittakerSmooth( y, {}, trendLambda > 0.0 ? trendLambda : 1e4 );

  // Seasonal: doy climatology of the detrended series, smoothed circularly.
  std::vector<double> sumByDoy( 366, 0.0 );
  std::vector<int> countByDoy( 366, 0 );
  for ( int i = 0; i < n; ++i )
  {
    if ( !std::isfinite( y[i] ) || !std::isfinite( out.trend[i] ) )
      continue;
    const int doy = std::clamp( doyOf[i], 1, 366 );
    sumByDoy[doy - 1] += y[i] - out.trend[i];
    ++countByDoy[doy - 1];
  }
  std::vector<double> clim( 366, 0.0 );
  for ( int d = 0; d < 366; ++d )
  {
    if ( countByDoy[d] > 0 )
      clim[d] = sumByDoy[d] / countByDoy[d];
    else
      clim[d] = std::numeric_limits<double>::quiet_NaN();
  }
  // Fill missing doys by neighbor interpolation (circular).
  for ( int pass = 0; pass < 2; ++pass )
  {
    for ( int d = 0; d < 366; ++d )
    {
      if ( std::isfinite( clim[d] ) )
        continue;
      const double prev = clim[( d + 365 ) % 366];
      const double next = clim[( d + 1 ) % 366];
      if ( std::isfinite( prev ) && std::isfinite( next ) )
        clim[d] = 0.5 * ( prev + next );
      else if ( std::isfinite( prev ) )
        clim[d] = prev;
      else if ( std::isfinite( next ) )
        clim[d] = next;
    }
  }
  // Circular box smoothing with the given window (days).
  const int halfW = std::clamp( seasonalWindow, 1, 61 ) / 2;
  if ( halfW > 0 )
  {
    std::vector<double> smoothed( 366, 0.0 );
    for ( int d = 0; d < 366; ++d )
    {
      double s = 0.0;
      int c = 0;
      for ( int k = -halfW; k <= halfW; ++k )
      {
        const double v = clim[( d + k + 366 ) % 366];
        if ( std::isfinite( v ) )
        {
          s += v;
          ++c;
        }
      }
      smoothed[d] = c > 0 ? s / c : 0.0;
    }
    clim = smoothed;
  }
  for ( int i = 0; i < n; ++i )
  {
    if ( !std::isfinite( y[i] ) )
      continue;
    const int doy = std::clamp( doyOf[i], 1, 366 );
    out.seasonal[i] = static_cast<float>( clim[doy - 1] );
    out.remainder[i] = y[i] - out.trend[i] - out.seasonal[i];
  }
  return out;
}

} // namespace sicnu::temporal
