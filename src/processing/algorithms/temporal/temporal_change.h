// src/processing/algorithms/temporal/temporal_change.h
// Joint seasonal-trend change modeling (Temporal Platform 10.0, closes the
// T-3 gap: a single global harmonic fit cannot express regime changes, and
// the linear-only breakpoint segmentation cannot express seasonality).
//
// Method, stated honestly: greedy seasonality-adjusted trend-break
// segmentation with per-segment harmonic + linear-trend refit —
// BFAST/CCDC-inspired, NOT the full BFAST algorithm (no iterative trend/
// seasonal alternation, no median filtering) and NOT CCDC (no L1 penalty,
// no per-segment model selection). Each iteration (a) fits every segment
// with the model [1, t, sin/cos(k·2πt/365.25)...], (b) detects trend breaks
// in the fitted residual with the shared greedy RSS segmentation
// (piecewiseLinearTrend, temporal_fit.h), (c) re-splits and re-fits until
// the break set is stable, the break budget is spent, or the improvement
// ratio is exhausted. Deterministic: fixed order, no randomness, bit-exact
// regression anchors (tolerance-free).
//
// Numeric contract: NaN = missing; fits treat NaN samples as absent; a
// segment needing more valid samples than model terms yields no fit (its
// stats are NaN and it counts as a single no-break segment). All outputs
// are NaN-free-by-refusal: fewer valid samples than the global model
// requires returns a result with zero breaks and NaN rmse/r2.
#pragma once

#include <vector>

namespace sicnu::temporal
{

struct SeasonalTrendSegment
{
  int startIndex = 0;        ///< sample index space [startIndex, endIndex)
  int endIndex = 0;
  double startDays = 0.0;    ///< day offset of the first sample
  double endDays = 0.0;      ///< day offset of the last sample
  double slopePerDay = 0.0;  ///< linear trend of this segment
  double intercept = 0.0;    ///< at t = 0 (the series epoch)
  double rmse = 0.0;         ///< segment fit error
  int validCount = 0;        ///< finite samples in the segment
};

struct BreakEvent
{
  int index = 0;           ///< segment boundary (first sample of the new segment)
  double breakDays = 0.0;  ///< day offset of the break sample
  double magnitude = 0.0;  ///< |fitL(tBreak) − fitR(tBreak)| (fitted level jump)
};

struct SeasonalTrendBreaksResult
{
  std::vector<BreakEvent> breaks;                 ///< ascending by index
  std::vector<SeasonalTrendSegment> segments;     ///< ascending, covering [0, n)
  std::vector<float> fitted;                      ///< per-sample fitted values
                                                  ///  (NaN where the sample is
                                                  ///  missing or its segment has
                                                  ///  no fit)
  double rmse = 0.0;   ///< sqrt(SSE / valid observations); NaN when none
  double r2 = 0.0;     ///< 1 − SSE/SST; NaN when SST == 0 or no fit
  int validCount = 0;  ///< finite observations
  int iterations = 0;  ///< refinement iterations actually run (diagnostic)
  /// Per-segment model coefficients [intercept, t, sin1, cos1, ...] in the
  /// shared detail::harmonicTrendDesignRow basis; empty when the segment has
  /// no fit. Parallel to @a segments (Temporal Intelligence 11.0: exposes
  /// the seasonal basis for break attribution / polar-form reporting
  /// without a refit).
  std::vector<std::vector<double>> segmentCoefficients;
};

/// Segment-level joint harmonic+trend fit. @a harmonics in 1..3 (design
/// columns 1 + t + 2·harmonics); @a maxBreaks in 0..8; @a minSegment = the
/// fewest samples per segment (>= 3 enforced); @a minImprovement = the
/// relative RSS reduction a split must achieve (as in piecewiseLinearTrend);
/// @a robust adds up to 3 IRLS Huber reweightings inside each final segment
/// fit (dampens outliers, does not move breaks).
SeasonalTrendBreaksResult fitSeasonalTrendBreaks( const std::vector<float> &y,
                                                  const std::vector<double> &tDays,
                                                  int harmonics, int maxBreaks,
                                                  int minSegment, double minImprovement,
                                                  bool robust );

/// Disturbance semantics over a fitted series (operator seam). Direction
/// "decrease" models NDVI-like loss; "increase" gain. A break is an onset
/// when the fitted level jump crosses @a minMagnitude in the declared
/// direction. Recovery = first day after the onset where the fitted series
/// returns within @a recoveryTolerance of the pre-break level; -1.0 when it
/// never does (unbounded recovery is a result, not an error). Returns the
/// day offset of the FIRST qualifying onset (-1 when none) and writes the
/// recovery length in days to @a recoveryDaysOut (-1 when unrecovered).
/// @a fitted/@a tDays must be the same length.
double disturbanceOnset( const std::vector<float> &fitted,
                         const std::vector<double> &tDays,
                         const std::vector<BreakEvent> &breaks,
                         bool decrease, double minMagnitude,
                         double *recoveryDaysOut, double recoveryTolerance );

} // namespace sicnu::temporal
