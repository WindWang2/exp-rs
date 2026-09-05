// src/processing/algorithms/temporal/temporal_fit.h
// Per-pixel time-series fitting primitives for Temporal Analysis 2.0
// (Platform 3.0, goal §7). Pure, header-light, unit-testable: every function
// operates on one float series (NaN = missing) with an optional weight vector
// (missing values implicitly get weight 0).
//
// Numeric contract: all fits treat NaN samples as absent (never as zero),
// produce NaN for series with fewer valid samples than the model requires,
// and are deterministic (single-threaded, fixed order — bit-exact regression
// anchors; only the banded Whittaker solver is tolerance-grade with a locked
// 1e-5 relative bound documented in the tests).
#pragma once

#include <cstddef>
#include <vector>

namespace sicnu::temporal
{

/// Savitzky–Golay smoothing of one series. Window must be odd (>= 3),
/// 1 <= polynomialDegree <= 4. Boundary points use the same polynomial fit
/// over the largest available window (shrink-at-boundary); a fit needing more
/// valid neighbors than the model supports leaves NaN at that position.
/// Returns a vector the same size as @a y (NaN preserved for gaps that the
/// polynomial cannot bridge: no fit at position i when its window holds fewer
/// valid samples than degree + 1).
std::vector<float> savitzkyGolay( const std::vector<float> &y, int window,
                                  int polynomialDegree );

/// Whittaker smoother: minimize Σ w_i (y_i − z_i)² + λ Σ (Δ²z)².
/// Pentadiagonal banded solve, O(T). Weights default to 1 for finite samples
/// and 0 for NaN (when @a w empty). λ > 0; λ → 0 interpolates the input,
/// huge λ approaches a straight-line fit. Tolerance-grade (documented 1e-5).
std::vector<float> whittakerSmooth( const std::vector<float> &y,
                                    const std::vector<float> &w, double lambda );

/// One harmonic regressor column pair (sin/cos of 2π·k·t / period).
struct HarmonicFitResult
{
  std::vector<float> fitted;   ///< fitted values at each t (NaN where no fit)
  std::vector<double> coefficients; ///< [intercept, sin1, cos1, sin2, cos2, ...]
  double rmse = 0.0;           ///< over valid samples
  double r2 = 0.0;             ///< 1 − SSE/SST (0 when SST == 0)
  int validCount = 0;
};

/// Weighted harmonic regression over time offsets @a tDays (same size as y;
/// typically day offsets from the series epoch). @a harmonics = number of
/// sin/cos pairs (1..6). Robust option runs up to 3 IRLS reweightings with a
/// Huber-like weight (1.5 · MAD scale) to damp outliers.
HarmonicFitResult harmonicFit( const std::vector<float> &y,
                               const std::vector<double> &tDays, int harmonics,
                               bool robust = false );

/// Per-season phenology metrics computed on one series.
/// A season spans [seasonStartDoy, seasonEndDoy] (day-of-year, both inclusive;
/// seasons may wrap the year end when start > end). Metric days are
/// day-of-year doubles; `los` (length of season) is in days.
struct SeasonalMetrics
{
  double sos = -1.0;         ///< start of season (doy), -1 when undefined
  double pos = -1.0;         ///< peak of season (doy)
  double eos = -1.0;         ///< end of season (doy)
  double los = 0.0;          ///< length of season in days (eos - sos, wrapped)
  double amplitude = 0.0;    ///< max - min inside the season
  double base = 0.0;         ///< minimum value inside the season
  double integral = 0.0;     ///< Σ value·Δday over the season (small approx)
  bool valid = false;
};

/// Threshold-fraction phenology on one season window of a series:
/// SOS/EOS = first/last crossing of base + fraction·amplitude. @a tDays are
/// day offsets (season-agnostic); @a doyOf gives the day-of-year per sample
/// (for season extraction + metric reporting). @a crossingFraction in (0,1].
SeasonalMetrics phenologyThreshold( const std::vector<float> &y,
                                    const std::vector<double> &tDays,
                                    const std::vector<int> &doyOf,
                                    int seasonStartDoy, int seasonEndDoy,
                                    double crossingFraction );

/// Greedy piecewise-linear trend segmentation (BSFAST-lite): repeated OLS on
/// segments, splitting at the point with the largest RSS reduction while the
/// reduction ratio (reduction / segment RSS) exceeds @a minImprovement and
/// both sides keep >= @a minSegment samples.
struct BreakpointResult
{
  std::vector<int> breakIndices;   ///< segment start indices of segments 2..k
  std::vector<double> slopes;      ///< per-day slopes per segment
  std::vector<double> intercepts;  ///< at t = 0 (series epoch)
  double rmse = 0.0;
};

BreakpointResult piecewiseLinearTrend( const std::vector<float> &y,
                                       const std::vector<double> &tDays,
                                       int maxBreaks, int minSegment,
                                       double minImprovement );

/// Additive decomposition: trend (Whittaker with @a trendLambda), seasonal
/// (mean of detrended values grouped by day-of-year, circularly smoothed by
/// @a seasonalWindow days), remainder (y − trend − seasonal). NaN-safe: a
/// sample's contribution to its doy mean requires >= 1 valid year.
struct DecompositionResult
{
  std::vector<float> trend;
  std::vector<float> seasonal;
  std::vector<float> remainder;
};

DecompositionResult seasonalDecompose( const std::vector<float> &y,
                                       const std::vector<double> &tDays,
                                       const std::vector<int> &doyOf,
                                       double trendLambda, int seasonalWindow );

} // namespace sicnu::temporal
