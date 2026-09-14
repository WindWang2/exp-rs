/***************************************************************************
  processing/algorithms/breakpoint_detection.h
  Temporal Phenology Timeline Studio (D16) — harmonic breakpoint detection
  (BFAST-simplified, ADR 0161 / DECISIONS D-160-6).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Joint model per segment [τ_k, τ_{k+1}): intercept + linear trend + m
  sin/cos harmonic pairs at the base period T = 365.25 days:

    y_t = α_k + β_k·t + Σ_j [γ_jk·sin(2πj t/T) + δ_jk·cos(2πj t/T)] + ε_t

  Breakpoints are placed greedily at the residual-sum-of-squares-minimizing
  split (Bai–Perron style), each accepted only when (a) both sides keep at
  least @a minSegmentSamples finite samples and (b) the split's Chow-type F
  test is significant at @a significanceAlpha (exact p-value via the
  regularized incomplete beta) and (c) BIC strictly decreases
  (BIC = n·ln(RSS/n) + p·ln(n)). The OLS-MOSUM moving-sum statistic
  (h = ⌊0.15·n⌋) is computed and reported as the structural-stability
  screen; the accept/reject gate is the F-test p-value.

  NaN samples are absent, never zero. Deterministic: fixed scan order,
  single-threaded.
 ***************************************************************************/

#ifndef SICNU_PROCESSING_ALGORITHMS_BREAKPOINT_DETECTION_H
#define SICNU_PROCESSING_ALGORITHMS_BREAKPOINT_DETECTION_H

#include <vector>

namespace sicnu::temporal
{

struct BreakpointCandidate
{
    int index = 0;             ///< series index of the segment boundary (first point of the new segment)
    double tDays = 0.0;        ///< breakpoint time on the caller's axis
    double magnitude = 0.0;    ///< fitted-level jump: intercept_{k+1} − intercept_k (harmonics shared)
    double pValue = 1.0;       ///< exact F-test p-value of the split
    double rssReduction = 0.0; ///< absolute RSS decrease from the split
};

struct BfastResult
{
    std::vector<BreakpointCandidate> breakpoints; ///< ascending by index
    std::vector<float> fittedTrend;               ///< piecewise intercept + trend component
    std::vector<float> fittedHarmonics;           ///< shared harmonic component
    std::vector<float> residuals;                 ///< y − fit (NaN where y is NaN)
    double overallRmse = 0.0;                     ///< sqrt(RSS / finite observations)
    double mosumMax = 0.0;                        ///< sup |M_t| of the MOSUM screen
    int mosumH = 0;                               ///< MOSUM window h = ⌊0.15·n⌋
    int breakCount = 0;                           ///< breakpoints.size()
    bool valid = false;                           ///< false on degenerate input
};

class BreakpointDetector
{
  public:
    /// Joint harmonic + piecewise-linear breakpoint detection.
    /// @param harmonics 1..6 sin/cos pairs (default 3), @param maxBreaks >= 0,
    /// @param minSegmentSamples >= 2 finite samples on each side of every
    /// break. Magnitude is the fitted-level jump at the split point (the two
    /// segments' intercept+trend+harmonic evaluations differ there); it equals
    /// the pure intercept step only when the seasonality is harmonic-exact.
    static BfastResult detectHarmonicBreaks( const std::vector<float> &y,
                                             const std::vector<double> &tDays,
                                             int harmonics = 3,
                                             int maxBreaks = 3,
                                             int minSegmentSamples = 23,
                                             double significanceAlpha = 0.05 );
};

} // namespace sicnu::temporal

#endif // SICNU_PROCESSING_ALGORITHMS_BREAKPOINT_DETECTION_H
