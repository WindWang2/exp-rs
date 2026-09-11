// sar_temporal.h — multi-date SAR statistics in the linear-power domain
// (Scientific Processing 8.0, capability package B).
//
// Companion to sar_ratio.h (two-scene change). Where sar_ratio covers the
// bi-temporal case, this module summarizes N >= 2 co-registered scenes per
// pixel: robust (median-anchored) log-domain change magnitude and
// speckle-aware dispersion, with explicit linear/dB bookkeeping on both
// sides of every aggregate.
//
// Domain rule (same as the SAR radiometric families): kernels compute in
// LINEAR POWER. dB inputs are converted by the caller ONCE (dbToLinear)
// before streaming, so no output depends on the input declaration. A sample
// is VALID iff it is finite and strictly positive — nonpositive linear
// power is outside the physical domain (NaN, never clamped), matching
// sar_metadata.h's per-kernel NoData policy.
//
// Robust change semantics: the per-pixel baseline is the MEDIAN of the
// linear-power samples (for even counts the UPPER-median sample —
// nth_element at n/2, a deterministic single selection rather than an
// interpolated average); the change magnitude of date i is
// |10·log10(x_i) − 10·log10(median)| dB. The log domain makes the
// multiplicative speckle process additive, so the median baseline and the
// deviation statistics are speckle-robust (unlike linear mean-based
// deviations).
#pragma once

#include <limits>

namespace sicnu::sar
{

struct SarTemporalStats
{
    int validCount = 0;              ///< finite, positive samples
    double meanLinear = kNaN();      ///< mean linear power
    double stdDevLinear = kNaN();    ///< population stddev (÷N)
    double cv = kNaN();              ///< stdDevLinear / meanLinear (dispersion)
    double meanDb = kNaN();          ///< 10·log10(meanLinear) — dB reporting of the mean
    double minLinear = kNaN();
    double maxLinear = kNaN();
    int argminDate = -1;             ///< first occurrence of the minimum (0-based)
    int argmaxDate = -1;             ///< first occurrence of the maximum (0-based)
    double baselineDb = kNaN();      ///< 10·log10(median linear) — robust baseline
    double maxLogDeviationDb = kNaN();  ///< max_i |10·log10(x_i) − baseline|
    double meanLogDeviationDb = kNaN(); ///< mean_i |10·log10(x_i) − baseline|
    int changedDates = 0;            ///< dates whose deviation ≥ the change threshold

    static constexpr double kNaN() { return std::numeric_limits<double>::quiet_NaN(); }
};

/// Statistics over one pixel's series. @a values are LINEAR POWER samples
/// (NaN/inf/nonpositive are invalid). @a changeThresholdDb (>= 0) counts
/// dates whose |log deviation| from the median baseline reaches it (the
/// `changedDates` product; 6 dB is the documented bi-temporal default).
/// Needs at least one valid sample (false otherwise).
bool sarTemporalStats( const double *values, int n, SarTemporalStats *out,
                       double changeThresholdDb = 6.0 );

} // namespace sicnu::sar
