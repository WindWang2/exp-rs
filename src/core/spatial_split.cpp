// src/core/spatial_split.cpp — D15 Package A: spatial leakage defense.
#include "spatial_split.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rs::core
{
namespace
{
  constexpr double kVarianceEpsilon = 1e-12;

  // Stateless uniform hash in [0, 1000) for a block coordinate pair.
  uint32_t blockBucket( int64_t bx, int64_t by, uint32_t seed )
  {
    uint32_t x = static_cast<uint32_t>( bx ) * 0x9E3779B9u;
    x ^= static_cast<uint32_t>( by ) + 0x79B9u;
    x ^= seed;
    x += 0x9E3779B9u;
    x = ( x ^ ( x >> 16 ) ) * 0x21F0AAADu;
    x = ( x ^ ( x >> 15 ) ) * 0x735A2D97u;
    return ( x ^ ( x >> 15 ) ) % 1000u;
  }

  double squaredDistance( const SpatialSamplePoint &a, const SpatialSamplePoint &b )
  {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
  }

  // swarm-review CORE-6: the block index is a floating-point quotient cast to
  // int64. A NaN/inf block size slips through `blockWidth <= 0.0` (every NaN
  // comparison is false), and an out-of-range quotient makes the cast itself
  // undefined behaviour. Refuse the configuration (empty report) instead.
  constexpr double kMaxSafeQuotient = 9.0e18; // well inside int64 (max ~9.22e18)

  bool blockIndex( double quotient, int64_t &out )
  {
    if ( !std::isfinite( quotient ) || quotient < -kMaxSafeQuotient
         || quotient > kMaxSafeQuotient )
      return false;
    out = static_cast<int64_t>( quotient );
    return true;
  }
} // namespace

SpatialSplitReport SpatialBlockPartitioner::partition( std::span<const SpatialSamplePoint> samples,
                                                       const SpatialBlockConfig &config ) const
{
  SpatialSplitReport report;
  // swarm-review CORE-6: finiteness first — `NaN <= 0.0` is false, so a NaN or
  // infinite block size used to pass this gate and reach the int64 cast below.
  // The same fail-open shape applies to the guard ring: a non-finite or
  // negative buffer distance silently switches the demotion off, which is the
  // leakage the seam exists to prevent; a non-finite ratio lands every block
  // in one fold (std::clamp passes NaN through) and silently empties the
  // others. Degenerate configs refuse (empty report), they never silently
  // degrade.
  if ( samples.empty() || !std::isfinite( config.blockWidth )
       || !std::isfinite( config.blockHeight )
       || config.blockWidth <= 0.0 || config.blockHeight <= 0.0
       || !std::isfinite( config.bufferDistance ) || config.bufferDistance < 0.0
       || !std::isfinite( config.trainRatio ) || !std::isfinite( config.valRatio ) )
    return report;

  double xmin = std::numeric_limits<double>::infinity();
  double ymin = std::numeric_limits<double>::infinity();
  double xmax = -xmin, ymax = -ymin;
  for ( const SpatialSamplePoint &s : samples )
  {
    if ( !std::isfinite( s.x ) || !std::isfinite( s.y ) )
      return SpatialSplitReport {};
    xmin = std::min( xmin, s.x );
    xmax = std::max( xmax, s.x );
    ymin = std::min( ymin, s.y );
    ymax = std::max( ymax, s.y );
  }

  const double trainCut = std::clamp( config.trainRatio, 0.0, 1.0 ) * 1000.0;
  const double valCut = std::clamp( config.trainRatio + config.valRatio, 0.0, 1.0 ) * 1000.0;
  const auto roleFor = [&]( int64_t bx, int64_t by )
  {
    const uint32_t bucket = blockBucket( bx, by, config.randomSeed );
    if ( bucket < trainCut )
      return SampleRole::Train;
    if ( bucket < valCut )
      return SampleRole::Validation;
    return SampleRole::Test;
  };

  report.assignments.assign( samples.size(), SampleRole::Unassigned );
  std::vector<size_t> trainIdx;
  std::vector<size_t> evalIdx;
  for ( size_t i = 0; i < samples.size(); ++i )
  {
    // swarm-review CORE-6: a quotient outside int64 (or non-finite, e.g. a
    // subnormal block size against a huge coordinate range) is refused as an
    // empty report rather than cast into UB.
    int64_t bx = 0;
    int64_t by = 0;
    if ( !blockIndex( ( samples[i].x - xmin ) / config.blockWidth, bx )
         || !blockIndex( ( samples[i].y - ymin ) / config.blockHeight, by ) )
      return SpatialSplitReport {};
    const SampleRole blockRole = roleFor( bx, by );
    if ( blockRole == SampleRole::Train )
    {
      report.assignments[i] = SampleRole::Train;
      trainIdx.push_back( i );
    }
    else
    {
      evalIdx.push_back( i );
      report.assignments[i] = blockRole; // provisional; demotion happens below
    }
  }

  // Guard-ring demotion: any evaluation sample within the buffer distance of
  // a train sample becomes ExcludedBuffer; the rest keep their block role.
  double minTrainEvalDistance = std::numeric_limits<double>::infinity();
  for ( const size_t k : evalIdx )
  {
    double best = std::numeric_limits<double>::infinity();
    for ( const size_t t : trainIdx )
      best = std::min( best, std::sqrt( squaredDistance( samples[k], samples[t] ) ) );
    if ( best <= config.bufferDistance )
    {
      report.assignments[k] = SampleRole::ExcludedBuffer;
      ++report.bufferCount;
    }
    else
    {
      minTrainEvalDistance = std::min( minTrainEvalDistance, best );
      if ( report.assignments[k] == SampleRole::Validation )
        ++report.valCount;
      else
        ++report.testCount;
    }
  }
  report.trainCount = trainIdx.size();

  const bool distancesMeaningful = report.trainCount > 0
                                   && ( report.valCount + report.testCount ) > 0
                                   && std::isfinite( minTrainEvalDistance );
  report.minTrainTestDistance = distancesMeaningful ? minTrainEvalDistance : 0.0;

  // Audit statistic over the label attribute; reach of two buffers covers
  // the block neighbourhood a leakage audit should see.
  std::vector<double> labels( samples.size() );
  for ( size_t i = 0; i < samples.size(); ++i )
    labels[i] = static_cast<double>( samples[i].label );
  report.spatialAutocorrelationMoranI = SpatialAutocorrelationAuditor::computeMoransI(
    samples, labels, std::max( 0.0, config.bufferDistance ) * 2.0 );
  return report;
}

double SpatialAutocorrelationAuditor::computeMoransI( std::span<const SpatialSamplePoint> points,
                                                      std::span<const double> attributeValues,
                                                      double spatialCutoffDistance )
{
  const size_t n = std::min( points.size(), attributeValues.size() );
  if ( n < 2 || !( spatialCutoffDistance > 0.0 ) )
    return 0.0;

  double mean = 0.0;
  for ( size_t i = 0; i < n; ++i )
    mean += attributeValues[i];
  mean /= static_cast<double>( n );

  // Spec-literal degeneracy guard: the ADR fixes the threshold on the
  // unnormalized centered sum of squares.
  double centeredSumSquares = 0.0;
  for ( size_t i = 0; i < n; ++i )
  {
    const double d = attributeValues[i] - mean;
    centeredSumSquares += d * d;
  }
  if ( centeredSumSquares < kVarianceEpsilon )
    return 0.0;

  // Ordered-pair sums: numerator = sum w_ij z_i z_j; S0 = sum w_ij.
  double weightedProduct = 0.0;
  double s0 = 0.0;
  for ( size_t i = 0; i < n; ++i )
  {
    for ( size_t j = i + 1; j < n; ++j )
    {
      const double d2 = squaredDistance( points[i], points[j] );
      if ( !( d2 > 0.0 ) || d2 > spatialCutoffDistance * spatialCutoffDistance )
        continue;
      const double w = 1.0 / std::sqrt( d2 );
      weightedProduct += w * ( attributeValues[i] - mean ) * ( attributeValues[j] - mean );
      s0 += w;
    }
  }
  if ( s0 <= 0.0 )
    return 0.0;

  weightedProduct *= 2.0;
  s0 *= 2.0;
  return ( static_cast<double>( n ) / s0 ) * ( weightedProduct / centeredSumSquares );
}

} // namespace rs::core
