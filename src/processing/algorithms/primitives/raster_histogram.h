// primitives/raster_histogram.h — shared streaming histogram and the
// threshold-selection kernels that consume it (Foundation 5.0, Milestone A).
//
// Platform histogram convention (bit-compatible with the historical
// ChangeDetection::histogramBin, which remains the inline reference):
//
//   bin(v) = clamp( (int)((v - minVal) / range * (bins - 1)), 0, bins - 1 )
//
// i.e. the maximum value lands exactly in the last bin and the bin width
// reconstructed from a bin index is range / (bins - 1) — never range / bins
// (the /bins reconstruction biased every histogram percentile low by up to
// one bin, #700). Bin edges are computed in double.
//
// All accumulation ignores non-finite input values: NaN and ±Inf are never
// binned (missing data, per docs/processing/nodata-and-statistics.md).
// Declared NoData sentinels are the caller's responsibility — resolve them
// through nodata_utils.h and skip them before add().
//
// Otsu and quantile selection here are formula-identical to the historical
// ChangeDetection::otsuThresholdFromHistogram / percentileThresholdFromHistogram
// (including the tied-maxima averaging for Otsu and the nearest-rank +
// in-bin linear interpolation for quantiles); ChangeDetection delegates to
// these kernels and tests/test_change_detection.cpp plus
// tests/test_primitives5.cpp pin the values.
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

class QDebug;

namespace sicnu::rs::primitives
{

class RasterHistogram
{
  public:
    /// @a bins is clamped to [1, 65536]; counts are doubles so weighted
    /// accumulation stays exact for integer weights up to 2^53.
    explicit RasterHistogram( int bins );

    /// Explicit-range mode: set the range before the first add(). Fails
    /// (returns false from finalize) when maxVal <= minVal.
    void setRange( double minVal, double maxVal );

    /// Auto-range mode: pass 1 collects [min,max] over the finite values,
    /// pass 2 bins. beginAutoRange() resets both passes.
    void beginAutoRange();
    void addForRange( double v );
    /// Seals the auto range and starts binning. Returns false when no finite
    /// value was seen (empty histogram; finalize() then reports the error).
    bool beginHistogramFromRange();

    /// Bins one finite value into the active range. Non-finite values are
    /// ignored (never binned). Values outside [minVal, maxVal] clamp into
    /// the edge bins (same convention as ChangeDetection::histogramBin).
    void add( double v );

    /// Total number of binned (finite) values so far — during the auto-range
    /// pass nothing is binned yet; it counts the add() pass only.
    size_t finiteCount() const { return m_finiteCount; }

    /// Number of finite values seen during the auto-range (addForRange) pass.
    size_t rangeCount() const { return m_rangeCount; }

    /// True when setRange/beginHistogramFromRange gave a usable range
    /// (maxVal > minVal).
    bool hasRange() const { return m_rangeValid; }

    int bins() const { return m_bins; }
    double minVal() const { return m_minVal; }
    double maxVal() const { return m_maxVal; }
    /// range/(bins-1) — the reconstruction width of the shared convention.
    double binWidth() const;
    /// Lower edge of bin @a b in value units.
    double binLower( int b ) const;

    /// Raw per-bin counts (size() == bins()).
    const std::vector<double> &counts() const { return m_hist; }

    /// Otsu's between-class variance threshold from the current counts.
    /// Same maximization and tied-maxima (empty-gap averaging) convention as
    /// the historical kernel; threshold = minVal + (bestBin + 0.5) * width.
    /// Returns false when no finite value was binned.
    bool otsu( double *threshold ) const;

    /// Nearest-rank percentile (0..100) from the current counts, linearly
    /// interpolated inside the rank's bin. Returns false when no finite
    /// value was binned. p == 0 yields minVal, p == 100 yields maxVal.
    bool quantile( double percentile, double *out ) const;

    /// Mean and population stddev over the binned values (histogram
    /// estimate; bin centers weighted by counts). Returns false when empty.
    bool meanStddev( double *mean, double *stddev ) const;

  private:
    int m_bins = 0;
    double m_minVal = 0.0;
    double m_maxVal = 0.0;
    double m_range = 0.0;
    bool m_rangeValid = false;
    size_t m_finiteCount = 0;
    size_t m_rangeCount = 0;
    std::vector<double> m_hist;
};

/// Validates a [min,max] histogram range (finite and maxVal > minVal).
bool validHistogramRange( double minVal, double maxVal );

/// Otsu's between-class variance threshold directly from precomputed counts
/// (the historical ChangeDetection::otsuThresholdFromHistogram contract,
/// including the tied-maxima averaging and the range<=0 → minVal case).
/// @a counts has one entry per bin over [minVal, maxVal].
bool otsuFromCounts( double minVal, double maxVal, const std::vector<double> &counts,
                     size_t finiteCount, double *threshold );

/// Nearest-rank percentile with in-bin linear interpolation directly from
/// precomputed counts (the historical
/// ChangeDetection::percentileThresholdFromHistogram contract, including the
/// range/(bins−1) reconstruction width and the range<=0 → minVal case).
bool quantileFromCounts( double minVal, double maxVal, const std::vector<double> &counts,
                         size_t finiteCount, double percentile, double *out );

} // namespace sicnu::rs::primitives
