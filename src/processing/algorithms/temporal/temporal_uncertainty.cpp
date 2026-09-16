// src/processing/algorithms/temporal/temporal_uncertainty.cpp
#include "temporal_uncertainty.h"

#include "temporal_linalg_detail.h"
#include "temporal_design_detail.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace sicnu::temporal
{

namespace
{
constexpr double kNanD = std::numeric_limits<double>::quiet_NaN();
using detail::kMaxTerms;
using detail::harmonicTrendDesignRow;

/// Acklam's rational-approximation inverse standard-normal CDF
/// (|ε| < 1.15e-9 relative); deterministic, allocation-free.
double normalQuantile( double p )
{
  p = std::clamp( p, 1e-15, 1.0 - 1e-15 );
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
} // namespace

AnalyticCiResult harmonicTrendCoefficientCi(
    const std::vector<float> &y, const std::vector<double> &tDays, int a, int b,
    int harmonics, const std::vector<double> &weights, double ciLevel )
{
  AnalyticCiResult result;
  a = std::max( 0, a );
  b = std::min( b, static_cast<int>( y.size() ) );
  const int harmonicsClamped = std::clamp( harmonics, 1, 3 );
  const int terms = 2 + 2 * harmonicsClamped;
  const double level = std::clamp( ciLevel, 0.01, 0.999 );
  if ( b - a < terms + 1 ||
       tDays.size() != y.size() ||
       ( !weights.empty() && weights.size() != y.size() ) )
  {
    result.refusalReason = "insufficient_valid_samples";
    result.sigma2 = kNanD;
    return result;
  }

  // Weighted normal equations over the segment (same accumulation order as
  // the shared fit kernels).
  std::vector<double> ata( static_cast<size_t>( kMaxTerms ) * kMaxTerms, 0.0 );
  std::vector<double> atb( kMaxTerms, 0.0 );
  double design[kMaxTerms];
  int n = 0;
  for ( int i = a; i < b; ++i )
  {
    const double w =
      weights.empty() ? ( std::isfinite( y[static_cast<size_t>( i )] ) ? 1.0 : 0.0 )
                      : weights[static_cast<size_t>( i )];
    if ( !( w > 0.0 ) || !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;  // rejects w <= 0 AND NaN weights (NaN > 0 is false)
    const int m = harmonicTrendDesignRow( tDays[static_cast<size_t>( i )],
                                          harmonicsClamped, design );
    for ( int r = 0; r < m; ++r )
    {
      atb[static_cast<size_t>( r )] += w * design[r] * y[static_cast<size_t>( i )];
      for ( int c = 0; c < m; ++c )
        ata[static_cast<size_t>( r ) * kMaxTerms + c] += w * design[r] * design[c];
    }
    ++n;
  }
  if ( n < terms + 1 )
  {
    result.refusalReason = "insufficient_valid_samples";
    result.sigma2 = kNanD;
    return result;
  }
  const int df = n - terms;

  // Coefficients + diagonal of the inverse Gram (term-by-term solves with
  // the shared elimination — one solve per design column).
  std::vector<double> coef;
  {
    std::vector<double> aDense( static_cast<size_t>( terms ) * terms, 0.0 );
    std::vector<double> bDense( terms, 0.0 );
    for ( int r = 0; r < terms; ++r )
    {
      bDense[static_cast<size_t>( r )] = atb[static_cast<size_t>( r )];
      for ( int c = 0; c < terms; ++c )
        aDense[static_cast<size_t>( r ) * terms + c] =
          ata[static_cast<size_t>( r ) * kMaxTerms + c];
    }
    if ( !detail::solveSmallDense( aDense, bDense, terms, &coef ) )
    {
      result.refusalReason = "singular_system";
      result.sigma2 = kNanD;
      return result;
    }
  }
  std::vector<double> diagInv( static_cast<size_t>( terms ), kNanD );
  for ( int j = 0; j < terms; ++j )
  {
    std::vector<double> e( static_cast<size_t>( terms ), 0.0 );
    std::vector<double> aDense( static_cast<size_t>( terms ) * terms, 0.0 );
    for ( int r = 0; r < terms; ++r )
      for ( int c = 0; c < terms; ++c )
        aDense[static_cast<size_t>( r ) * terms + c] =
          ata[static_cast<size_t>( r ) * kMaxTerms + c];
    e[static_cast<size_t>( j )] = 1.0;
    std::vector<double> col;
    if ( !detail::solveSmallDense( aDense, e, terms, &col ) )
    {
      result.refusalReason = "singular_system";
      result.sigma2 = kNanD;
      return result;
    }
    diagInv[static_cast<size_t>( j )] = col[static_cast<size_t>( j )];
  }

  // σ̂² over the same weighted samples.
  double sse = 0.0;
  for ( int i = a; i < b; ++i )
  {
    const double w =
      weights.empty() ? ( std::isfinite( y[static_cast<size_t>( i )] ) ? 1.0 : 0.0 )
                      : weights[static_cast<size_t>( i )];
    if ( !( w > 0.0 ) || !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const int m = harmonicTrendDesignRow( tDays[static_cast<size_t>( i )],
                                          harmonicsClamped, design );
    double v = 0.0;
    for ( int r = 0; r < m; ++r )
      v += coef[static_cast<size_t>( r )] * design[r];
    const double d = y[static_cast<size_t>( i )] - v;
    sse += w * d * d;
  }
  result.sigma2 = sse / static_cast<double>( df );
  result.df = df;
  result.valid = true;

  const double z = normalQuantile( 0.5 + level / 2.0 );
  result.coefficients.resize( static_cast<size_t>( terms ) );
  for ( int j = 0; j < terms; ++j )
  {
    CoefficientInterval &ci = result.coefficients[static_cast<size_t>( j )];
    ci.estimate = coef[static_cast<size_t>( j )];
    const double var = result.sigma2 * diagInv[static_cast<size_t>( j )];
    if ( !( var >= 0.0 ) )
    {
      ci.valid = false;
      ci.stdError = kNanD;
      ci.lower = kNanD;
      ci.upper = kNanD;
      continue;
    }
    ci.stdError = std::sqrt( var );
    ci.lower = ci.estimate - z * ci.stdError;
    ci.upper = ci.estimate + z * ci.stdError;
    ci.valid = true;
  }
  return result;
}

BootstrapCi residualBootstrapCi(
    const std::vector<float> &y, const std::vector<float> &fitted,
    const std::function<double( const std::vector<float> & )> &statistic,
    const BootstrapOptions &options )
{
  BootstrapCi result;
  const int n = static_cast<int>( y.size() );
  result.lower = kNanD;
  result.upper = kNanD;
  result.estimate = kNanD;
  if ( n == 0 || fitted.size() != y.size() || !statistic )
  {
    result.refusalReason = "invalid_input";
    return result;
  }

  // Residual pool over observed (finite y AND fitted) indices.
  std::vector<double> residuals;
  for ( int i = 0; i < n; ++i )
  {
    if ( std::isfinite( y[static_cast<size_t>( i )] ) &&
         std::isfinite( fitted[static_cast<size_t>( i )] ) )
    {
      residuals.push_back( static_cast<double>( y[static_cast<size_t>( i )] ) -
                           fitted[static_cast<size_t>( i )] );
    }
  }
  if ( residuals.empty() )
  {
    result.refusalReason = "invalid_input";
    return result;
  }
  double mean = 0.0;
  for ( double r : residuals )
    mean += r;
  mean /= static_cast<double>( residuals.size() );
  for ( double &r : residuals )
    r -= mean;

  result.estimate = statistic( y );
  if ( !std::isfinite( result.estimate ) )
  {
    result.refusalReason = "invalid_input";
    return result;
  }

  BootstrapOptions opts = options;
  opts.resamples = std::clamp( opts.resamples, 1, 999 );
  opts.ciLevel = std::clamp( opts.ciLevel, 0.01, 0.999 );
  opts.minSuccessRate = std::clamp( opts.minSuccessRate, 0.01, 1.0 );

  std::mt19937 rng( opts.seed );
  const size_t pool = residuals.size();
  std::vector<float> resampled( static_cast<size_t>( n ),
                                std::numeric_limits<float>::quiet_NaN() );
  std::vector<double> stats;
  stats.reserve( static_cast<size_t>( opts.resamples ) );
  int successes = 0;
  for ( int b = 0; b < opts.resamples; ++b )
  {
    // Residuals are redrawn ONLY at observed indices (finite y AND fitted);
    // missing positions stay NaN so the sampling pattern is preserved.
    for ( int i = 0; i < n; ++i )
    {
      if ( !std::isfinite( y[static_cast<size_t>( i )] ) ||
           !std::isfinite( fitted[static_cast<size_t>( i )] ) )
        continue;
      // Documented index rule: uniform via modulo (bias negligible for
      // pool >> 1 and irrelevant to determinism).
      const size_t pick = static_cast<size_t>( rng() ) % pool;
      resampled[static_cast<size_t>( i )] = static_cast<float>(
        fitted[static_cast<size_t>( i )] + residuals[pick] );
    }
    const double s = statistic( resampled );
    if ( std::isfinite( s ) )
    {
      stats.push_back( s );
      ++successes;
    }
  }

  const double rate =
    static_cast<double>( successes ) / static_cast<double>( opts.resamples );
  if ( rate < opts.minSuccessRate || successes < 2 )
  {
    result.successes = successes;
    result.lower = kNanD;
    result.upper = kNanD;
    result.refusalReason = "low_success_rate";
    return result;
  }

  std::sort( stats.begin(), stats.end() );
  const double tail = ( 1.0 - opts.ciLevel ) / 2.0;
  auto quantileAt = [&]( double q ) {
    const double pos = q * static_cast<double>( stats.size() - 1 );
    const size_t lo = static_cast<size_t>( std::floor( pos ) );
    const size_t hi = static_cast<size_t>( std::ceil( pos ) );
    const double frac = pos - static_cast<double>( lo );
    return stats[lo] * ( 1.0 - frac ) + stats[hi] * frac;
  };
  result.lower = quantileAt( tail );
  result.upper = quantileAt( 1.0 - tail );
  result.successes = successes;
  result.valid = true;
  return result;
}

} // namespace sicnu::temporal
