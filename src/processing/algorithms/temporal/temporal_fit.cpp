// src/processing/algorithms/temporal/temporal_fit.cpp
#include "temporal_fit.h"

#include "temporal_irregular.h"
#include "temporal_linalg_detail.h"

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
    out[i] = detail::localPolynomialAt( y, t, lo, hi, polynomialDegree, static_cast<double>( i ) );
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
  if ( !detail::solvePentadiagonal( main, off1, off2, rhs, &x ) )
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

  // Scratch reused across IRLS iterations (Temporal Intelligence 11.0
  // performance pass): same accumulation order, no per-iteration allocation.
  std::vector<double> ata( static_cast<size_t>( terms ) * terms, 0.0 );
  std::vector<double> atb( terms, 0.0 );
  std::vector<float> fitted( n, kNan );
  for ( int iteration = 0; iteration < ( robust ? 4 : 1 ); ++iteration )
  {
    ata.assign( static_cast<size_t>( terms ) * terms, 0.0 );
    atb.assign( terms, 0.0 );
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
        atb[static_cast<size_t>( r )] += weights[i] * design[r] * y[i];
        for ( int c = 0; c < terms; ++c )
          ata[static_cast<size_t>( r ) * terms + c] +=
            weights[i] * design[r] * design[c];
      }
      ++valid;
    }
    if ( valid < terms )
      return result; // underdetermined: NaN fit (documented contract)
    std::vector<double> coef;
    if ( !detail::solveSmallDense( ata, atb, terms, &coef ) )
      return result;

    // Stats + fitted values.
    double sse = 0.0;
    double mean = 0.0;
    int count = 0;
    fitted.assign( n, kNan );
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
    // IRLS: Huber-style weights from 1.5·MAD scale (median |r − median(r)|).
    std::vector<double> residuals;
    residuals.reserve( count );
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[i] <= 0.0 )
        continue;
      residuals.push_back( static_cast<double>( y[i] ) - fitted[i] );
    }
    const double scale = detail::madScale( residuals );
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

std::vector<float> whittakerSmoothRobust( const std::vector<float> &y,
                                          const std::vector<float> &w,
                                          double lambda, int iterations )
{
  const int n = static_cast<int>( y.size() );
  if ( n == 0 || !( lambda > 0.0 ) )
    return std::vector<float>( static_cast<size_t>( n ), kNan );
  const bool haveWeights = !w.empty() && static_cast<int>( w.size() ) == n;
  std::vector<float> weights( static_cast<size_t>( n ), 0.0f );
  for ( int i = 0; i < n; ++i )
    weights[static_cast<size_t>( i )] =
      std::isfinite( y[i] ) ? ( haveWeights ? w[static_cast<size_t>( i )] : 1.0f ) : 0.0f;

  const int maxIter = std::clamp( iterations, 1, 10 );
  std::vector<float> z = whittakerSmooth( y, weights, lambda );
  for ( int iter = 1; iter < maxIter; ++iter )
  {
    // Residual scale from the current fit: σ̂ = 1.4826 · MAD(r).
    std::vector<double> residuals;
    residuals.reserve( static_cast<size_t>( n ) );
    for ( int i = 0; i < n; ++i )
    {
      if ( std::isfinite( y[i] ) && std::isfinite( z[static_cast<size_t>( i )] ) )
        residuals.push_back( static_cast<double>( y[i] ) -
                             z[static_cast<size_t>( i )] );
    }
    if ( residuals.empty() )
      break;
    const double k = std::max( 3.0 * detail::madScale( residuals ), 1e-9 );
    bool changed = false;
    for ( int i = 0; i < n; ++i )
    {
      if ( weights[static_cast<size_t>( i )] <= 0.0f )
        continue;
      const double r = std::abs( static_cast<double>( y[i] ) -
                                 z[static_cast<size_t>( i )] );
      const double cauchy = 1.0 / ( 1.0 + ( r / k ) * ( r / k ) );
      const double newW = ( haveWeights ? w[static_cast<size_t>( i )] : 1.0 ) * cauchy;
      if ( std::abs( newW - weights[static_cast<size_t>( i )] ) > 1e-6 )
        changed = true;
      weights[static_cast<size_t>( i )] = static_cast<float>( newW );
    }
    if ( !changed )
      break;
    z = whittakerSmooth( y, weights, lambda );
  }
  return z;
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

  // Limb + SOS/EOS crossings share one interpolated semantics (D16 / WP4):
  // locate the level on the real day axis inside the bracketing segment, then
  // map back to DOY. Quantized-to-sample SOS/EOS previously disagreed with the
  // interpolated midpoints; both now use the same crossingTime + doyAt path.
  const auto posOf = [&]( int sampleIdx ) -> int {
    for ( int k = 0; k < static_cast<int>( idx.size() ); ++k )
      if ( idx[static_cast<size_t>( k )] == sampleIdx )
        return k;
    return -1;
  };
  const int pk = posOf( posIdx );
  const int lastK = static_cast<int>( idx.size() ) - 1;

  // First upward (rising limb) or downward (falling limb) crossing of
  // @a level within idx range (loK, hiK]; returns the interpolated crossing
  // time in tDays units, NaN when absent. Bracket indices are reported for
  // the doy mapping below.
  const auto crossingTime = [&]( int loK, int hiK, double level, bool rising,
                                 int *i0Out, int *i1Out ) -> double {
    for ( int k = loK + 1; k <= hiK; ++k )
    {
      const double v0 = y[idx[static_cast<size_t>( k - 1 )]];
      const double v1 = y[idx[static_cast<size_t>( k )]];
      const bool cross = rising ? ( v0 < level && v1 >= level )
                                : ( v0 >= level && v1 < level );
      if ( !cross )
        continue;
      const double t0 = tDays[idx[static_cast<size_t>( k - 1 )]];
      const double t1 = tDays[idx[static_cast<size_t>( k )]];
      if ( !( t1 > t0 ) )
        continue; // duplicate instant: degenerate crossing time — skip
      const double frac = ( level - v0 ) / ( v1 - v0 );
      if ( i0Out ) *i0Out = idx[static_cast<size_t>( k - 1 )];
      if ( i1Out ) *i1Out = idx[static_cast<size_t>( k )];
      return t0 + frac * ( t1 - t0 );
    }
    return std::numeric_limits<double>::quiet_NaN();
  };
  // Maps an interpolated crossing time back to a day-of-year by linear
  // interpolation of the bracketing samples' doys. Year-boundary brackets use
  // yearLen 366 when either endpoint is day 366 (leap), else 365 — avoids the
  // prior +365 unwrap that collapsed leap Dec-31 brackets one day early.
  const auto doyAt = [&]( double t, int i0, int i1 ) -> double {
    const int d0 = doyOf[static_cast<size_t>( i0 )];
    const int d1raw = doyOf[static_cast<size_t>( i1 )];
    const int yearLen = ( d0 == 366 || d1raw == 366 ) ? 366 : 365;
    int d1 = d1raw;
    if ( d1 < d0 )
      d1 += yearLen;
    const double t0 = tDays[static_cast<size_t>( i0 )];
    const double t1 = tDays[static_cast<size_t>( i1 )];
    const double frac = ( t1 > t0 ) ? ( t - t0 ) / ( t1 - t0 ) : 0.0;
    int d = static_cast<int>( std::lround( d0 + frac * ( d1 - d0 ) ) );
    if ( d > yearLen )
      d -= yearLen;
    if ( d < 1 )
      d += yearLen;
    return static_cast<double>( d );
  };

  // SOS / EOS at the same crossingFraction used for the threshold (interpolated).
  int sosI0 = -1, sosI1 = -1, eosI0 = -1, eosI1 = -1;
  double tSos = std::numeric_limits<double>::quiet_NaN();
  double tEos = std::numeric_limits<double>::quiet_NaN();
  if ( pk >= 0 )
  {
    tSos = crossingTime( 0, pk, threshold, true, &sosI0, &sosI1 );
    tEos = crossingTime( pk, lastK, threshold, false, &eosI0, &eosI1 );
  }
  if ( !std::isfinite( tSos ) || !std::isfinite( tEos ) || sosI0 < 0 || eosI0 < 0 )
    return out;
  out.sos = doyAt( tSos, sosI0, sosI1 );
  out.eos = doyAt( tEos, eosI0, eosI1 );

  // LOS in days over the t axis (season may wrap: negative span + 365.25).
  double span = tEos - tSos;
  if ( span < 0.0 )
    span += 365.25;
  out.los = span;

  if ( pk >= 0 )
  {
    const double amp = maxV - minV;
    const double v20 = minV + 0.20 * amp;
    const double v50 = minV + 0.50 * amp;
    const double v80 = minV + 0.80 * amp;

    int i0 = -1, i1 = -1;
    if ( pk > 0 )
    {
      const double t20u = crossingTime( 0, pk, v20, true, nullptr, nullptr );
      const double t80u = crossingTime( 0, pk, v80, true, nullptr, nullptr );
      if ( std::isfinite( t20u ) && std::isfinite( t80u ) && t80u > t20u )
        out.greenUpRate = ( v80 - v20 ) / ( t80u - t20u );
      const double t50u = crossingTime( 0, pk, v50, true, &i0, &i1 );
      if ( std::isfinite( t50u ) )
        out.greenUpMidDoy = doyAt( t50u, i0, i1 );
    }

    if ( pk < lastK )
    {
      i0 = i1 = -1;
      const double t80d = crossingTime( pk, lastK, v80, false, nullptr, nullptr );
      const double t20d = crossingTime( pk, lastK, v20, false, nullptr, nullptr );
      if ( std::isfinite( t80d ) && std::isfinite( t20d ) && t20d > t80d )
        out.senescenceRate = ( v80 - v20 ) / ( t20d - t80d );
      const double t50d = crossingTime( pk, lastK, v50, false, &i0, &i1 );
      if ( std::isfinite( t50d ) )
        out.senescenceMidDoy = doyAt( t50d, i0, i1 );
    }
  }

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
  {
    // No series to fit: the error is undefined, never zero (#759 contract).
    result.rmse = kNan;
    return result;
  }

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
    const double segRss =
        segmentRss( seg.first, seg.second, &slope, &intercept );
    totalSse += segRss;
    result.slopes.push_back( slope );
    result.intercepts.push_back( intercept );
    // Per-segment slope SE (WP5): σ̂² = RSS/(n−2), var(β̂) = σ̂²/Sxx. Same
    // weighted cumsums as the fit — NaN slots are weight-0 everywhere.
    const double segN = c1[seg.second] - c1[seg.first];
    const double segT = ct[seg.second] - ct[seg.first];
    const double segT2 = ct2[seg.second] - ct2[seg.first];
    const double sxx = segN > 0.0 ? segT2 - segT * segT / segN : 0.0;
    result.slopeStdErrors.push_back(
        segN > 2.0 && sxx > 0.0
            ? std::sqrt( segRss / ( segN - 2.0 ) / sxx )
            : std::numeric_limits<double>::quiet_NaN() );
    // Count finite observations only, via the same weighted cumsum that feeds
    // the RSS numerator — NaN slots must not inflate the denominator (#759).
    totalValid += static_cast<long>( segN );
  }
  result.breakIndices = breaks;
  result.validCount = totalValid;
  // Zero valid observations: the fit error is undefined, not zero.
  result.rmse = totalValid > 0 ? std::sqrt( totalSse / totalValid ) : kNan;
  return result;
}

SenTrendResult mannKendallSenSlope( const std::vector<float> &y,
                                    const std::vector<double> &tDays,
                                    double ciLevel )
{
  SenTrendResult out;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  out.slope = out.intercept = out.z = out.pValue = out.variance = nan;

  const int n = static_cast<int>( y.size() );
  if ( n < 2 || static_cast<int>( tDays.size() ) != n )
    return out;

  // Valid (finite) samples, in time order.
  std::vector<int> idx;
  idx.reserve( n );
  for ( int i = 0; i < n; ++i )
    if ( std::isfinite( y[i] ) )
      idx.push_back( i );
  out.validCount = static_cast<int>( idx.size() );
  if ( out.validCount < 3 ) // no meaningful MK test below 3 observations
    return out;

  // S statistic and Sen slope pairs, over strictly time-ordered pairs.
  double s = 0.0;
  std::vector<double> pairwiseSlopes;
  pairwiseSlopes.reserve( static_cast<size_t>( out.validCount ) *
                          ( out.validCount - 1 ) / 2 );
  for ( size_t a = 0; a < idx.size(); ++a )
  {
    for ( size_t b = a + 1; b < idx.size(); ++b )
    {
      const int i = idx[a];
      const int j = idx[b];
      if ( !( tDays[j] > tDays[i] ) )
        continue; // equal or inverted instants: no direction, skip entirely
      const double dy = y[j] - y[i];
      s += dy > 0.0 ? 1.0 : ( dy < 0.0 ? -1.0 : 0.0 );
      pairwiseSlopes.push_back( dy / ( tDays[j] - tDays[i] ) );
    }
  }
  if ( pairwiseSlopes.empty() ) // all valid samples share one instant
    return out;

  // Sen slope: median of the pairwise slopes; intercept: median residual.
  std::sort( pairwiseSlopes.begin(), pairwiseSlopes.end() );
  const size_t m = pairwiseSlopes.size();
  out.slope = m % 2 == 1
                  ? pairwiseSlopes[m / 2]
                  : 0.5 * ( pairwiseSlopes[m / 2 - 1] + pairwiseSlopes[m / 2] );

  std::vector<double> residuals;
  residuals.reserve( idx.size() );
  for ( int i : idx )
    residuals.push_back( y[i] - out.slope * tDays[i] );
  std::sort( residuals.begin(), residuals.end() );
  const size_t rn = residuals.size();
  out.intercept = rn % 2 == 1 ? residuals[rn / 2]
                              : 0.5 * ( residuals[rn / 2 - 1] + residuals[rn / 2] );

  // Mann-Kendall test: tie-corrected variance of S (Gilbert 1987 eq. 16.5),
  // continuity-corrected z, two-sided normal-tail p-value.
  std::vector<double> sorted( idx.size() );
  for ( size_t a = 0; a < idx.size(); ++a )
    sorted[a] = y[idx[a]];
  std::sort( sorted.begin(), sorted.end() );

  const double nn = static_cast<double>( out.validCount );
  double variance = nn * ( nn - 1.0 ) * ( 2.0 * nn + 5.0 );
  for ( size_t a = 0; a < sorted.size(); )
  {
    size_t b = a;
    while ( b < sorted.size() && sorted[b] == sorted[a] )
      ++b;
    const double t = static_cast<double>( b - a );
    if ( t > 1.0 )
      variance -= t * ( t - 1.0 ) * ( 2.0 * t + 5.0 );
    a = b;
  }
  variance /= 18.0;
  out.variance = variance;

  if ( variance <= 0.0 )
  {
    out.z = 0.0;
    out.pValue = 1.0;
  }
  else
  {
    const double sd = std::sqrt( variance );
    out.z = s > 0.0 ? ( s - 1.0 ) / sd : ( s < 0.0 ? ( s + 1.0 ) / sd : 0.0 );
    out.pValue = std::erfc( std::fabs( out.z ) / std::sqrt( 2.0 ) );
  }

  // Gilbert (1987 §16) slope CI: the two-sided level-`ciLevel` interval is
  // bracketed by the sorted pairwise slopes at ranks (K ∓ Cα)/2 with
  // Cα = z_{1−α/2}·√var(S). Ranks are rounded to the nearest order
  // statistic and clamped inside [1, K].
  if ( ciLevel > 0.0 && ciLevel < 1.0 && variance > 0.0 && m >= 2 )
  {
    const double zA = detail::normalQuantile( 0.5 + ciLevel / 2.0 );
    const double cAlpha = zA * std::sqrt( variance );
    const auto rankOf = [&]( double r ) -> size_t {
      const long rr = std::lround( r );
      return static_cast<size_t>( std::clamp<long>( rr, 1, static_cast<long>( m ) ) - 1 );
    };
    out.slopeCiLo = pairwiseSlopes[rankOf( ( static_cast<double>( m ) - cAlpha ) / 2.0 )];
    out.slopeCiHi = pairwiseSlopes[rankOf( ( static_cast<double>( m ) + cAlpha ) / 2.0 + 1.0 )];
    out.slopeCiValid = out.slopeCiLo <= out.slopeCiHi;
  }
  return out;
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

  // Trend: TIME-AWARE Whittaker on the day-offset axis (#1166). The D16
  // kernel smoothed by sample index, so the effective trend bandwidth was
  // cadence-dependent (cloud-clustered acquisitions under-smoothed dense
  // stretches); the tDays parameter the operator accepts and documents is
  // what the penalty must be regularized on.
  out.trend = whittakerSmoothTime( y, tDays, {}, trendLambda > 0.0 ? trendLambda : 1e4 );

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


// --- Multi-cycle phenology (Temporal Platform 10.0) ---

SeasonWindow complementSeasonWindow( const SeasonWindow &window )
{
  // Complement of [s, e] (possibly wrapped) is [e+1, s-1] on the circular
  // doy axis; a full-year window has no complement (returns itself).
  SeasonWindow out;
  out.startDoy = window.endDoy + 1;
  out.endDoy = window.startDoy - 1;
  if ( out.startDoy > 366 )
    out.startDoy -= 366;
  if ( out.endDoy < 1 )
    out.endDoy += 366;
  return out;
}

namespace
{
bool doyInWindow( int doy, const SeasonWindow &window )
{
  return window.startDoy <= window.endDoy
           ? ( doy >= window.startDoy && doy <= window.endDoy )
           : ( doy >= window.startDoy || doy <= window.endDoy );
}
} // namespace

std::vector<SeasonYearMetrics> phenologyCyclesPerYear(
  const std::vector<float> &y, const std::vector<double> &tDays,
  const std::vector<int> &doyOf, const std::vector<int> &yearOf,
  const std::vector<SeasonWindow> &windows, double crossingFraction )
{
  std::vector<SeasonYearMetrics> out;
  const int n = static_cast<int>( y.size() );
  if ( n == 0 || static_cast<int>( tDays.size() ) != n ||
       static_cast<int>( doyOf.size() ) != n ||
       static_cast<int>( yearOf.size() ) != n ||
       windows.empty() || windows.size() > 2 )
    return out;
  if ( !( crossingFraction > 0.0 && crossingFraction <= 1.0 ) )
    return out;

  // Collect the distinct years (ascending, deterministic input order scan).
  std::vector<int> years;
  for ( int i = 0; i < n; ++i )
  {
    if ( std::isfinite( y[i] ) &&
         ( years.empty() || years.back() != yearOf[static_cast<size_t>( i )] ) )
      years.push_back( yearOf[static_cast<size_t>( i )] );
  }

  for ( int year : years )
  {
    // This year's valid samples, in time order.
    std::vector<int> yearIdx;
    for ( int i = 0; i < n; ++i )
    {
      if ( std::isfinite( y[i] ) && yearOf[static_cast<size_t>( i )] == year )
        yearIdx.push_back( i );
    }
    for ( size_t c = 0; c < windows.size(); ++c )
    {
      std::vector<int> windowIdx;
      for ( int i : yearIdx )
      {
        if ( doyInWindow( doyOf[static_cast<size_t>( i )], windows[c] ) )
          windowIdx.push_back( i );
      }
      SeasonYearMetrics entry;
      entry.year = year;
      entry.cycleIndex = static_cast<int>( c );
      if ( windowIdx.size() < 3 )
      {
        entry.metrics.valid = false;
        out.push_back( entry );
        continue;
      }
      // SeasonalMetrics on the year × window subset (tDays sub-vector keeps
      // the real time axis; doyOf drives the metric reporting).
      std::vector<float> subY;
      std::vector<double> subT;
      std::vector<int> subDoy;
      subY.reserve( windowIdx.size() );
      subT.reserve( windowIdx.size() );
      subDoy.reserve( windowIdx.size() );
      for ( int i : windowIdx )
      {
        subY.push_back( y[static_cast<size_t>( i )] );
        subT.push_back( tDays[static_cast<size_t>( i )] );
        subDoy.push_back( doyOf[static_cast<size_t>( i )] );
      }
      entry.metrics = phenologyThreshold( subY, subT, subDoy,
                                          windows[c].startDoy, windows[c].endDoy,
                                          crossingFraction );
      out.push_back( entry );
    }
  }
  return out;
}

PhenologyMultiResult phenologyMultiCycle(
  const std::vector<float> &y, const std::vector<double> &tDays,
  const std::vector<int> &doyOf, const std::vector<int> &yearOf,
  const PhenologyMultiOptions &options )
{
  PhenologyMultiResult result;
  const int n = static_cast<int>( y.size() );
  if ( n == 0 || static_cast<int>( tDays.size() ) != n ||
       static_cast<int>( doyOf.size() ) != n ||
       static_cast<int>( yearOf.size() ) != n )
  {
    result.refusalReason = "insufficient_series";
    return result;
  }
  PhenologyMultiOptions opts = options;
  opts.maxCyclesPerYear = std::clamp( opts.maxCyclesPerYear, 1, 4 );
  opts.seasonalWindow = std::clamp( opts.seasonalWindow, 1, 61 );
  opts.minValidPerSeason = std::max( 3, opts.minValidPerSeason );

  int validTotal = 0;
  for ( float v : y )
    if ( std::isfinite( v ) )
      ++validTotal;
  if ( validTotal < std::max( 2 * opts.minValidPerSeason, 8 ) )
  {
    result.refusalReason = "insufficient_valid_samples";
    return result;
  }

  // Seasonal component from the shared decomposition kernel.
  const DecompositionResult decomp =
    seasonalDecompose( y, tDays, doyOf, opts.trendLambda, opts.seasonalWindow );

  // Peak candidates on the seasonal component (local maxima over the valid
  // sample sequence, above a fraction of the seasonal range).
  std::vector<int> validIdx;
  for ( int i = 0; i < n; ++i )
    if ( std::isfinite( y[i] ) && std::isfinite( decomp.seasonal[i] ) )
      validIdx.push_back( i );
  if ( validIdx.size() < static_cast<size_t>( std::max( 2 * opts.minValidPerSeason, 8 ) ) )
  {
    result.refusalReason = "insufficient_valid_samples";
    return result;
  }
  float sMin = decomp.seasonal[validIdx.front()];
  float sMax = sMin;
  for ( int i : validIdx )
  {
    sMin = std::min( sMin, decomp.seasonal[i] );
    sMax = std::max( sMax, decomp.seasonal[i] );
  }
  const float seasonalRange = sMax - sMin;
  if ( !( seasonalRange > 0.0f ) )
  {
    result.refusalReason = "degenerate_seasonal_component";
    return result;
  }
  const float peakFloor = sMin + static_cast<float>( opts.minPeakFraction ) * seasonalRange;

  std::vector<int> peaks;
  for ( size_t k = 0; k < validIdx.size(); ++k )
  {
    const int i = validIdx[k];
    const float v = decomp.seasonal[i];
    if ( v < peakFloor )
      continue;
    const bool hasPrev = k > 0;
    const bool hasNext = k + 1 < validIdx.size();
    const float prevV = hasPrev ? decomp.seasonal[validIdx[k - 1]] : v - 1.0f;
    const float nextV = hasNext ? decomp.seasonal[validIdx[k + 1]] : v - 1.0f;
    // Deterministic plateau rule: strictly above the previous valid sample
    // and >= the next (first sample of a plateau wins).
    if ( v > prevV && v >= nextV )
      peaks.push_back( i );
  }
  if ( peaks.empty() )
  {
    result.refusalReason = "degenerate_seasonal_component";
    return result;
  }

  // ±span dominance filter: a candidate must be the highest seasonal value
  // within ±minCycleSpanDays (smoothing can leave small local maxima on the
  // rising/falling limbs; adjacent-sample checks alone let them through).
  {
    std::vector<int> dominant;
    for ( int p : peaks )
    {
      bool dominated = false;
      for ( int q : validIdx )
      {
        if ( q == p || std::fabs( tDays[q] - tDays[p] ) > opts.minCycleSpanDays )
          continue;
        if ( decomp.seasonal[q] > decomp.seasonal[p] ||
             ( decomp.seasonal[q] == decomp.seasonal[p] && q < p ) )
        {
          dominated = true;
          break;
        }
      }
      if ( !dominated )
        dominant.push_back( p );
    }
    peaks = dominant;
  }
  if ( peaks.empty() )
  {
    result.refusalReason = "degenerate_seasonal_component";
    return result;
  }

  // Merge peaks closer than minCycleSpanDays (keep the higher peak; ties
  // keep the earlier).
  {
    std::vector<int> merged;
    for ( int p : peaks )
    {
      if ( !merged.empty() &&
           tDays[p] - tDays[merged.back()] < opts.minCycleSpanDays )
      {
        if ( decomp.seasonal[p] > decomp.seasonal[merged.back()] )
          merged.back() = p;
        continue;
      }
      merged.push_back( p );
    }
    peaks = merged;
  }

  // Cap per calendar year: strongest maxCyclesPerYear peaks (ties earlier).
  {
    std::vector<int> keep;
    for ( size_t k = 0; k < peaks.size(); ++k )
    {
      const int year = yearOf[peaks[k]];
      size_t sameYear = 0;
      for ( int existing : keep )
        if ( yearOf[existing] == year )
          ++sameYear;
      if ( static_cast<int>( sameYear ) < opts.maxCyclesPerYear )
      {
        keep.push_back( peaks[k] );
        continue;
      }
      // Find the weakest kept peak of this year; replace when beaten.
      size_t weakest = keep.size();
      float weakestV = decomp.seasonal[peaks[k]];
      for ( size_t j = 0; j < keep.size(); ++j )
      {
        if ( yearOf[keep[j]] != year )
          continue;
        if ( decomp.seasonal[keep[j]] < weakestV )
        {
          weakestV = decomp.seasonal[keep[j]];
          weakest = j;
        }
      }
      if ( weakest < keep.size() )
        keep[weakest] = peaks[k];
    }
    std::sort( keep.begin(), keep.end() );
    peaks = keep;
  }

  // Windows are TIME ranges between the midpoints to the adjacent peaks —
  // the honest representation for seasons that can span the calendar-year
  // boundary (a circular doy window cannot express a ~1-year season: the
  // short arc between the two midpoint doys collapses it). A window is
  // proposed only for peaks that have BOTH neighbours: an edge peak's
  // season is truncated by the series boundary, and those samples stay
  // unscored rather than producing a season with fabricated coverage. A
  // single-peak series keeps one window spanning the whole series.
  struct ProposedWindow
  {
    int peakIdx = 0;
    double tStart = 0.0;  ///< inclusive midpoint to the previous peak
    double tEnd = 0.0;    ///< inclusive midpoint to the next peak
  };
  std::vector<ProposedWindow> proposals;
  proposals.reserve( peaks.size() );
  const int m = static_cast<int>( peaks.size() );
  for ( int k = 0; k < m; ++k )
  {
    const int cur = peaks[static_cast<size_t>( k )];
    ProposedWindow w;
    w.peakIdx = cur;
    if ( m == 1 )
    {
      w.tStart = tDays.front();
      w.tEnd = tDays.back();
    }
    else if ( k == 0 || k == m - 1 )
    {
      continue;  // edge peak: series-truncated season, never scored
    }
    else
    {
      const int prev = peaks[static_cast<size_t>( k - 1 )];
      const int next = peaks[static_cast<size_t>( k + 1 )];
      w.tStart = 0.5 * ( tDays[static_cast<size_t>( prev )] +
                         tDays[static_cast<size_t>( cur )] );
      w.tEnd = 0.5 * ( tDays[static_cast<size_t>( cur )] +
                       tDays[static_cast<size_t>( next )] );
    }
    proposals.push_back( w );
  }
  if ( proposals.empty() )
  {
    // Every peak sits at a series edge (or the single peak spans the whole
    // series but produced no scoreable window): honest refusal, not a
    // silent empty result.
    result.refusalReason = "edge_truncated_series";
    return result;
  }

  // Series amplitude for the amplitude-ratio flag.
  float yMin = 0.0f;
  float yMax = 0.0f;
  bool first = true;
  for ( float v : y )
  {
    if ( !std::isfinite( v ) )
      continue;
    if ( first )
    {
      yMin = v;
      yMax = v;
      first = false;
    }
    else
    {
      yMin = std::min( yMin, v );
      yMax = std::max( yMax, v );
    }
  }
  const float seriesRange = first ? 0.0f : yMax - yMin;

  // Median inter-sample spacing over the whole series (coverage estimate).
  std::vector<double> spacings;
  for ( size_t k = 1; k < validIdx.size(); ++k )
  {
    const double dt = tDays[validIdx[k]] - tDays[validIdx[k - 1]];
    if ( dt > 0.0 )
      spacings.push_back( dt );
  }
  double medianSpacing = 16.0;
  if ( !spacings.empty() )
  {
    std::sort( spacings.begin(), spacings.end() );
    medianSpacing = spacings[spacings.size() / 2];
  }

  // Materialize cycles: group by harvest season year, quality-gate, score.
  struct GroupedCycle
  {
    int seasonYear = 0;
    double peakT = 0.0;
    PhenologyCycle cycle;
  };
  std::vector<GroupedCycle> grouped;
  for ( const ProposedWindow &w : proposals )
  {
    const int peakI = w.peakIdx;

    // Samples of this cycle: the time range between the adjacent-peak
    // midpoints (no circularity — a season may cross the calendar-year
    // boundary and still be one continuous range).
    std::vector<int> inWindow;
    for ( int i = 0; i < n; ++i )
    {
      if ( !std::isfinite( y[i] ) )
        continue;
      if ( tDays[i] < w.tStart || tDays[i] > w.tEnd )
        continue;
      inWindow.push_back( i );
    }

    // Harvest year = calendar year of the window END: the last in-window
    // sample sits within half a cycle of the window end, so under regular
    // sampling its year IS the end year (the wrapped Dec-to-May season
    // counts toward the year it ends in).
    GroupedCycle entry;
    entry.seasonYear =
      inWindow.empty() ? yearOf[static_cast<size_t>( peakI ) ]
                       : yearOf[static_cast<size_t>( inWindow.back() )];
    entry.peakT = tDays[peakI];
    entry.cycle.seasonYear = entry.seasonYear;
    // Reported doy metadata: the window's first/last observed doy.
    entry.cycle.window =
      inWindow.empty()
        ? SeasonWindow{ std::clamp( doyOf[static_cast<size_t>( peakI )], 1, 366 ),
                        std::clamp( doyOf[static_cast<size_t>( peakI )], 1, 366 ) }
        : SeasonWindow{ std::clamp( doyOf[static_cast<size_t>( inWindow.front() )], 1, 366 ),
                        std::clamp( doyOf[static_cast<size_t>( inWindow.back() )], 1, 366 ) };

    PhenologyQualityFlags &q = entry.cycle.quality;
    q.sampleCount = static_cast<int>( inWindow.size() );
    const double spanDays = std::max( 1e-9, w.tEnd - w.tStart );
    if ( q.sampleCount < opts.minValidPerSeason )
    {
      q.refusalReason = "low_window_samples";
      grouped.push_back( entry );
      continue;
    }

    std::vector<int> ordered = inWindow;
    std::sort( ordered.begin(), ordered.end(),
               [&]( int a, int b ) { return tDays[a] < tDays[b]; } );
    {
      double maxGap = 0.0;
      double prevT = 0.0;
      for ( size_t k = 0; k < ordered.size(); ++k )
      {
        const double t = tDays[ordered[k]];
        maxGap = std::max( maxGap, k == 0 ? t - w.tStart : t - prevT );
        prevT = t;
      }
      maxGap = std::max( maxGap, w.tEnd - prevT );  // tail gap
      q.gapFraction = std::clamp( maxGap / spanDays, 0.0, 1.0 );
      const double observedSpan = ( prevT - tDays[ordered.front()] ) + medianSpacing;
      q.coverage = std::clamp( observedSpan / spanDays, 0.0, 1.0 );
    }

    float wMin = y[inWindow.front()];
    float wMax = wMin;
    for ( int i : inWindow )
    {
      wMin = std::min( wMin, y[i] );
      wMax = std::max( wMax, y[i] );
    }
    q.amplitudeRatio =
      seriesRange > 0.0f
        ? std::clamp( ( wMax - wMin ) / seriesRange, 0.0f, 1.0f )
        : 0.0f;

    if ( q.gapFraction > opts.maxGapFraction )
      q.refusalReason = "coverage_gap";
    else if ( q.coverage < opts.minCoverage )
      q.refusalReason = "edge_truncated_window";
    else if ( q.amplitudeRatio < static_cast<float>( opts.minAmplitudeRatio ) )
      q.refusalReason = "below_amplitude_threshold";
    else
    {
      // The sub-series is already cycle-scoped; pass the widest doy window
      // so the threshold kernel scores every sample it receives (metrics
      // are doy-reported; LOS runs on the real t axis, so seasons that
      // cross the calendar-year boundary are handled naturally).
      std::vector<float> subY;
      std::vector<double> subT;
      std::vector<int> subDoy;
      subY.reserve( inWindow.size() );
      subT.reserve( inWindow.size() );
      subDoy.reserve( inWindow.size() );
      for ( int i : inWindow )
      {
        subY.push_back( y[i] );
        subT.push_back( tDays[i] );
        subDoy.push_back( doyOf[i] );
      }
      entry.cycle.metrics = phenologyThreshold(
        subY, subT, subDoy, 1, 366, opts.crossingFraction );
      if ( entry.cycle.metrics.valid )
        q.valid = true;
      else
        q.refusalReason = "threshold_crossing_failed";
    }
    grouped.push_back( entry );
  }

  // Deterministic order: seasonYear ascending, then peak time ascending;
  // cycleIndex = rank within the season year.
  std::sort( grouped.begin(), grouped.end(),
             []( const GroupedCycle &a, const GroupedCycle &b ) {
               if ( a.seasonYear != b.seasonYear )
                 return a.seasonYear < b.seasonYear;
               return a.peakT < b.peakT;
             } );
  int lastYear = -1;
  int cycleIndex = 0;
  for ( GroupedCycle &entry : grouped )
  {
    if ( entry.seasonYear != lastYear )
    {
      lastYear = entry.seasonYear;
      cycleIndex = 0;
    }
    // Harvest-year cap: cycle indices beyond maxCyclesPerYear are not
    // scored (the per-calendar-year PEAK cap cannot see harvest-year
    // boundary effects, so this is the authoritative bound — reported
    // counts can never exceed maxCyclesPerYear).
    if ( cycleIndex >= opts.maxCyclesPerYear )
    {
      ++cycleIndex;
      continue;
    }
    entry.cycle.cycleIndex = cycleIndex;
    ++cycleIndex;
    result.cycles.push_back( entry.cycle );
  }
  if ( !result.cycles.empty() )
  {
    result.valid = true;
    // Cycles are sorted by seasonYear; the longest run is the observed max.
    int maxY = 0;
    int run = 0;
    int runYear = 0;
    for ( const PhenologyCycle &c : result.cycles )
    {
      if ( c.seasonYear != runYear )
      {
        runYear = c.seasonYear;
        run = 0;
      }
      ++run;
      maxY = std::max( maxY, run );
    }
    result.cyclesPerYearMax = maxY;
  }
  return result;
}

} // namespace sicnu::temporal
