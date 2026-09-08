// primitives/raster_histogram.cpp — see raster_histogram.h for the contract.
//
// The otsu()/quantile() kernels are formula-identical ports of the historical
// ChangeDetection::otsuThresholdFromHistogram / percentileThresholdFromHistogram
// (change_detection.cpp @ 93a7fb0bbd); ChangeDetection now delegates here and
// its existing hand-derived tests pin the values bit-for-bit.

#include "raster_histogram.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::rs::primitives
{

namespace
{
/// Shared binning convention — must stay identical to
/// ChangeDetection::histogramBin (change_detection.h).
inline int histogramBin( double v, double minVal, double range, int bins )
{
  int bin = static_cast<int>( ( v - minVal ) / range * ( bins - 1 ) );
  return bin < 0 ? 0 : ( bin > bins - 1 ? bins - 1 : bin );
}
} // namespace

bool validHistogramRange( double minVal, double maxVal )
{
  return std::isfinite( minVal ) && std::isfinite( maxVal ) && maxVal > minVal;
}

bool otsuFromCounts( double minVal, double maxVal, const std::vector<double> &counts,
                     size_t finiteCount, double *threshold )
{
  if ( !threshold || counts.empty() || finiteCount == 0 )
    return false;
  const double range = maxVal - minVal;
  if ( range <= 0.0 )
  {
    *threshold = minVal;
    return true;
  }
  const int bins = static_cast<int>( counts.size() );

  // Otsu: maximize between-class variance over cumulative histogram sums.
  const double total = static_cast<double>( finiteCount );
  double sumAll = 0.0;
  for ( int b = 0; b < bins; ++b )
    sumAll += counts[static_cast<size_t>( b )] * b;

  double sumB = 0.0;
  double weightB = 0.0;
  double bestVariance = -1.0;
  // Bins inside an empty gap between two clusters all yield the identical
  // (bitwise-equal) between-class variance; averaging the tied maxima picks
  // the middle of the gap instead of its first edge — the robust convention
  // for well-separated bimodal distributions.
  double bestBinSum = 0.0;
  int bestBinCount = 0;
  for ( int b = 0; b < bins; ++b )
  {
    weightB += counts[static_cast<size_t>( b )];
    if ( weightB == 0.0 )
      continue;
    sumB += counts[static_cast<size_t>( b )] * b;
    const double weightF = total - weightB;
    if ( weightF == 0.0 )
      break;
    const double meanB = sumB / weightB;
    const double meanF = ( sumAll - sumB ) / weightF;
    const double between = weightB * weightF * ( meanB - meanF ) * ( meanB - meanF );
    if ( between > bestVariance )
    {
      bestVariance = between;
      bestBinSum = static_cast<double>( b );
      bestBinCount = 1;
    }
    else if ( between == bestVariance )
    {
      bestBinSum += static_cast<double>( b );
      ++bestBinCount;
    }
  }

  const double bestBin = ( bestBinCount > 0 ) ? bestBinSum / bestBinCount : 0.0;
  const double width = bins > 1 ? range / ( bins - 1 ) : range;
  *threshold = minVal + ( bestBin + 0.5 ) * width;
  return true;
}

bool quantileFromCounts( double minVal, double maxVal, const std::vector<double> &counts,
                         size_t finiteCount, double percentile, double *out )
{
  if ( !out || counts.empty() || finiteCount == 0 )
    return false;
  const double range = maxVal - minVal;
  if ( range <= 0.0 )
  {
    *out = minVal;
    return true;
  }
  const int bins = static_cast<int>( counts.size() );
  // Bin width must match the histogram builder, which bins with
  // (v - minVal) / range * (bins - 1) — i.e. width range/(bins-1), not
  // range/bins. The old /bins reconstruction biased every percentile low
  // by up to one bin (#700).
  const double binWidth = bins > 1 ? range / ( bins - 1 ) : range;
  const double p = std::clamp( percentile, 0.0, 100.0 );
  // Nearest-rank index over the sorted finite values (p == 0 -> minimum).
  const double rank = std::max( 1.0,
    std::ceil( p / 100.0 * static_cast<double>( finiteCount ) ) ) - 1.0;

  double cum = 0.0;
  for ( int b = 0; b < bins; ++b )
  {
    const double prev = cum;
    cum += counts[static_cast<size_t>( b )];
    if ( rank < cum || b == bins - 1 )
    {
      // The rank falls inside this bin; interpolate linearly from the bin's
      // lower edge. Histogram-estimated, so a value near the true sorted
      // percentile rather than an exact sample.
      const double frac = ( cum > prev )
        ? ( rank - prev ) / ( cum - prev )
        : 0.0;
      *out = minVal + ( static_cast<double>( b ) + std::clamp( frac, 0.0, 1.0 ) ) * binWidth;
      return true;
    }
  }
  *out = maxVal;
  return true;
}

RasterHistogram::RasterHistogram( int bins )
    : m_bins( std::clamp( bins, 1, 65536 ) )
    , m_hist( static_cast<size_t>( m_bins ), 0.0 )
{
}

void RasterHistogram::setRange( double minVal, double maxVal )
{
  m_minVal = minVal;
  m_maxVal = maxVal;
  m_range = maxVal - minVal;
  m_rangeValid = validHistogramRange( minVal, maxVal );
  m_finiteCount = 0;
  std::fill( m_hist.begin(), m_hist.end(), 0.0 );
}

void RasterHistogram::beginAutoRange()
{
  m_minVal = std::numeric_limits<double>::infinity();
  m_maxVal = -std::numeric_limits<double>::infinity();
  m_range = 0.0;
  m_rangeValid = false;
  m_finiteCount = 0;
  m_rangeCount = 0;
  std::fill( m_hist.begin(), m_hist.end(), 0.0 );
}

void RasterHistogram::addForRange( double v )
{
  if ( !std::isfinite( v ) )
    return;
  m_minVal = std::min( m_minVal, v );
  m_maxVal = std::max( m_maxVal, v );
  ++m_rangeCount;
}

bool RasterHistogram::beginHistogramFromRange()
{
  if ( m_rangeCount == 0 )
    return false;
  setRange( m_minVal, m_maxVal );
  return m_rangeValid;
}

void RasterHistogram::add( double v )
{
  if ( !m_rangeValid || !std::isfinite( v ) )
    return;
  ++m_hist[static_cast<size_t>( histogramBin( v, m_minVal, m_range, m_bins ) )];
  ++m_finiteCount;
}

double RasterHistogram::binWidth() const
{
  return m_bins > 1 ? m_range / ( m_bins - 1 ) : m_range;
}

double RasterHistogram::binLower( int b ) const
{
  return m_minVal + static_cast<double>( b ) * binWidth();
}

bool RasterHistogram::otsu( double *threshold ) const
{
  return otsuFromCounts( m_minVal, m_maxVal, m_hist, m_finiteCount, threshold );
}

bool RasterHistogram::quantile( double percentile, double *out ) const
{
  return quantileFromCounts( m_minVal, m_maxVal, m_hist, m_finiteCount, percentile, out );
}

bool RasterHistogram::meanStddev( double *mean, double *stddev ) const
{
  if ( !mean || !stddev || m_finiteCount == 0 || !m_rangeValid )
    return false;

  double sum = 0.0;
  double sumSq = 0.0;
  for ( int b = 0; b < m_bins; ++b )
  {
    const double count = m_hist[static_cast<size_t>( b )];
    if ( count == 0.0 )
      continue;
    const double center = binLower( b ) + 0.5 * binWidth();
    sum += count * center;
    sumSq += count * center * center;
  }
  const double n = static_cast<double>( m_finiteCount );
  const double m = sum / n;
  // Population convention (÷N): descriptive spread of the observed samples,
  // matching MathUtils::computeStats* (docs/processing/nodata-and-statistics.md §3).
  const double var = std::max( 0.0, sumSq / n - m * m );
  *mean = m;
  *stddev = std::sqrt( var );
  return true;
}

} // namespace sicnu::rs::primitives
