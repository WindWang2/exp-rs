// primitives/percentile.h — exact small-array quantiles with an explicit
// interpolation contract (Foundation 5.0, Milestone A).
//
// Two platform quantile semantics exist by design; callers must declare
// which one they mean instead of drifting:
//
//   * NearestRank — the sorted-array convention already used by
//     ChangeDetection::percentileThreshold and the histogram quantile:
//     rank = ceil(p/100·N) − 1 (0-based), no interpolation. Exact sample.
//   * Linear — the C=1 "linear interpolation between closest ranks"
//     convention (numpy default): h = (N−1)·p/100, result interpolates
//     between floor(h) and ceil(h). Use for continuous statistics surfaces
//     (zonal/focal summaries), never for threshold placement parity.
//
// Non-finite values (NaN/±Inf) are excluded before ranking, matching the
// missing-data policy. Empty valid set → false.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace sicnu::rs::primitives
{

enum class QuantileMethod
{
  NearestRank,
  Linear,
};

/// Exact quantile of the finite values in @a values (@a scratch is used as
/// the sorted working copy). @a percentile in [0, 100]. Returns false when
/// no finite value exists.
template <typename T>
bool quantileExact( const std::vector<T> &values, double percentile,
                    QuantileMethod method, double *out, std::vector<T> *scratch )
{
  if ( !out || !scratch )
    return false;
  scratch->clear();
  scratch->reserve( values.size() );
  for ( const T v : values )
  {
    if ( std::isfinite( static_cast<double>( v ) ) )
      scratch->push_back( v );
  }
  if ( scratch->empty() )
    return false;

  std::sort( scratch->begin(), scratch->end() );
  const double n = static_cast<double>( scratch->size() );
  const double p = std::clamp( percentile, 0.0, 100.0 );

  if ( method == QuantileMethod::NearestRank )
  {
    const size_t ceilRank = static_cast<size_t>(
      std::ceil( p / 100.0 * n ) );
    const size_t rank = ceilRank > 0 ? std::min( ceilRank, scratch->size() ) - 1 : 0;
    *out = static_cast<double>( ( *scratch )[rank] );
    return true;
  }

  // Linear: h = (N−1)·p/100, interpolate between floor(h) and ceil(h).
  const double h = ( n - 1.0 ) * p / 100.0;
  const size_t lo = static_cast<size_t>( std::floor( h ) );
  const size_t hi = static_cast<size_t>( std::ceil( h ) );
  const double frac = h - static_cast<double>( lo );
  *out = static_cast<double>( ( *scratch )[lo] ) *
           ( 1.0 - frac ) +
         static_cast<double>( ( *scratch )[hi] ) * frac;
  return true;
}

} // namespace sicnu::rs::primitives
