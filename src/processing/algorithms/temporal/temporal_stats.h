// src/processing/algorithms/temporal/temporal_stats.h
// Numerically stable streaming accumulators for temporal algorithms.
//
// Mean/variance uses Welford's algorithm; linear trend uses West's online
// covariance update (centered sums, never sum(x²) − sum(x)²/n — the naive form
// suffers catastrophic cancellation on long series with a large baseline and
// small temporal variance).
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace sicnu::temporal::stats
{

/// Welford online mean / variance accumulator.
struct WelfordAccumulator
{
  std::uint64_t n = 0;
  double mean = 0.0;
  double m2 = 0.0;

  void add( double x )
  {
    ++n;
    const double delta = x - mean;
    mean += delta / static_cast<double>( n );
    m2 += delta * ( x - mean );
  }

  /// Sample variance (n−1 denominator); 0 for n < 2.
  double sampleVariance() const { return n > 1 ? m2 / static_cast<double>( n - 1 ) : 0.0; }
  /// Population variance (n denominator); 0 for n == 0.
  double populationVariance() const { return n > 0 ? m2 / static_cast<double>( n ) : 0.0; }
  double sampleStddev() const { return std::sqrt( sampleVariance() ); }
  double populationStddev() const { return std::sqrt( populationVariance() ); }
};

/// Online least-squares regression of y on t (West's centered update).
/// t must be real time intervals (e.g. days since the collection reference
/// epoch) — never array indices.
struct OnlineRegression
{
  std::uint64_t n = 0;
  double meanT = 0.0;
  double meanY = 0.0;
  double mtt = 0.0; // centered Σ(t−meanT)²
  double mty = 0.0; // centered Σ(t−meanT)(y−meanY)
  double myy = 0.0; // centered Σ(y−meanY)²

  void add( double t, double y )
  {
    ++n;
    const double dt = t - meanT;
    const double dy = y - meanY;
    meanT += dt / static_cast<double>( n );
    meanY += dy / static_cast<double>( n );
    mtt += dt * ( t - meanT );
    mty += dt * ( y - meanY );
    myy += dy * ( y - meanY );
  }

  /// A unique solution exists with ≥ 2 distinct t values.
  bool solvable() const { return n >= 2 && mtt > 0.0; }

  double slope() const { return solvable() ? mty / mtt : quietNan(); }
  double intercept() const
  {
    return solvable() ? meanY - ( mty / mtt ) * meanT : quietNan();
  }

  /// Coefficient of determination. Zero y-variance is a perfectly explained
  /// flat series (residual SS == total SS == 0), reported as 1.0.
  double r2() const
  {
    if ( n < 2 || !solvable() )
      return quietNan();
    if ( myy <= 0.0 )
      return 1.0;
    const double r = ( mty * mty ) / ( mtt * myy );
    return r;
  }

  /// Root mean square error of the fit; requires n > 2.
  double rmse() const
  {
    if ( n <= 2 || !solvable() )
      return quietNan();
    const double sse = myy - ( mty * mty ) / mtt; // ≥ 0 mathematically
    return std::sqrt( sse > 0.0 ? sse / static_cast<double>( n - 2 ) : 0.0 );
  }

private:
  static double quietNan() { return std::numeric_limits<double>::quiet_NaN(); }
};

} // namespace sicnu::temporal::stats
