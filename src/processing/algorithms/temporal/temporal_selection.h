// src/processing/algorithms/temporal/temporal_selection.h
// Seasonal-trend break ATTRIBUTION and bounded per-segment MODEL SELECTION
// (Temporal Intelligence 11.0; closes the ADR 0148 declared gaps
// "seasonal-component break detection (trend-only today)" and
// "per-segment model selection").
//
// Method, stated honestly (continues the platform's "…-inspired" naming
// rule): attribution tests, per detected break, whether the SEASONAL
// component (harmonic sin/cos coefficients) changed beyond the TREND change
// with a nested-model F comparison on the two neighbouring segments —
// BFAST-inspired seasonal-change testing, NOT the full BFAST (no iterative
// trend/seasonal alternation) and NOT CCDC (no L1, no online model update).
// Model selection searches a bounded candidate grid {harmonic order} ×
// {maximum breaks} with AICc / BIC / deterministic contiguous-block CV —
// an explicit, bounded, documented selection (still not CCDC's online
// per-element model updating).
//
// Determinism contract: fixed candidate enumeration, fixed evaluation
// order, no randomness (CV folds are contiguous index blocks), bit-exact
// grade. NaN = missing / undefined; a test that cannot be evaluated
// (segment too short for the unrestricted fit) reports kind Untestable
// with NaN statistics — never a fabricated answer.
#pragma once

#include <cstdint>
#include <vector>

#include "temporal_change.h"

namespace sicnu::temporal
{

// ---------------------------------------------------------------------------
// A. Break attribution: trend change vs seasonal-component change
// ---------------------------------------------------------------------------

enum class BreakKind
{
  None,          ///< neither test significant (break kept by the RSS rule only)
  TrendOnly,     ///< level/slope changed, seasonal basis did not
  SeasonalOnly,  ///< seasonal basis changed, trend did not
  Both,          ///< both changed
  Untestable,    ///< neighbouring segments too short for the nested test
};

struct AttributedBreak
{
  int index = 0;                  ///< segment boundary (first sample of new segment)
  double breakDays = 0.0;         ///< day offset of the break sample
  double trendMagnitude = 0.0;    ///< segmentation level jump |fitL−fitR|
                                  ///  (always reported from the input fit;
                                  ///  replaced by the plain-fit jump when
                                  ///  the nested tests run — i.e. it is
                                  ///  NaN only when the segmentation gave
                                  ///  NaN)
  double seasonalShift = 0.0;     ///< L2 norm of the sin/cos coefficient change (NaN untestable)
  double amplitudeChange1 = 0.0;  ///< harmonic-1 amplitude change, output units (NaN when < 1 harmonic)
  double phaseChange1 = 0.0;      ///< harmonic-1 phase change, wrapped to (−π, π] radians
  double fStatistic = 0.0;        ///< SEASONAL-change F statistic (the
                                  ///  trend-change F is used internally for
                                  ///  the kind verdict; NaN when untestable)
  double pValue = 0.0;            ///< F-test p-value (NaN when untestable)
  BreakKind kind = BreakKind::Untestable;
};

/// Polar-form seasonal parameters of one segment and harmonic
/// (s·sin(ωt) + c·cos(ωt) = amplitude·sin(ωt + phase)).
struct SeasonalParam
{
  double amplitude = 0.0;
  double phase = 0.0;  ///< radians in (−π, π]
  bool valid = false;
};

struct BreakAttributionOptions
{
  int harmonics = 2;    ///< attribution basis (1..3; must match the segmentation basis)
  double alpha = 0.05;  ///< two-sided significance level for both nested F tests
};

struct BreakAttributionResult
{
  std::vector<AttributedBreak> breaks;  ///< parallel to @a fit.breaks
  /// [segment][harmonic-1] polar seasonal parameters (valid=false when the
  /// segment has no fit or the harmonic is absent).
  std::vector<std::vector<SeasonalParam>> segmentSeasonal;
  int testedCount = 0;     ///< breaks with an evaluated F test
  int seasonalCount = 0;   ///< breaks whose kind is SeasonalOnly or Both
};

/// Attributes each break of an existing joint harmonic+trend segmentation.
/// For the break between segments L and R, two nested F comparisons are run
/// against the FULL separate model (separate intercept/slope AND separate
/// seasonal sin/cos per side, evaluated on the union span): (1) freeing the
/// TREND block (2 parameters) beyond free seasonals — a level/slope change;
/// (2) freeing the SEASONAL block (2·harmonics parameters) beyond a free
/// trend — an amplitude/phase change. The symmetric design keeps a pure
/// level step from leaking into the seasonal verdict and vice versa. All
/// fits are plain weighted-LS (self-consistent even when the segmentation
/// used IRLS). Attribution kind combines both p-values at @a options.alpha.
BreakAttributionResult attributeSeasonalTrendBreaks(
    const SeasonalTrendBreaksResult &fit, const std::vector<float> &y,
    const std::vector<double> &tDays, const BreakAttributionOptions &options );

// ---------------------------------------------------------------------------
// B. Bounded per-segment model selection
// ---------------------------------------------------------------------------

enum class SelectionPenalty
{
  AICc,     ///< small-sample-corrected Akaike criterion (default)
  BIC,      ///< Schwarz criterion (ln n per parameter)
  BlockCv,  ///< deterministic contiguous-block k-fold cross-validation MSE
};

struct ModelSelectionOptions
{
  int maxHarmonics = 2;         ///< harmonic-order candidates 0..maxHarmonics (clamped 0..3)
  int maxBreaks = 2;            ///< break-budget candidates 0..maxBreaks (clamped 0..4)
  int minSegment = 8;           ///< minimum samples per segment (kernel floors at 3)
  double minImprovement = 0.1;  ///< split acceptance ratio passed to the segmentation
  SelectionPenalty penalty = SelectionPenalty::AICc;
  int cvFolds = 4;              ///< BlockCv only (2..10); folds are contiguous index blocks
  bool robust = false;          ///< IRLS inside candidate segment fits (matches segmentation)
};

struct ModelCandidateScore
{
  int harmonics = 0;   ///< harmonic order of this candidate
  int maxBreaks = 0;   ///< requested break budget of this candidate
  int segments = 0;    ///< segments actually fitted (<= maxBreaks + 1)
  int paramCount = 0;  ///< fitted parameters incl. σ² (segments·(2+2·harmonics) + breaks + 1)
  double rss = 0.0;    ///< residual sum of squares over valid samples (NaN when not fittable)
  double score = 0.0;  ///< penalty score, lower = better (NaN when not fittable)
  bool fittable = false;
};

struct ModelSelectionResult
{
  std::vector<ModelCandidateScore> candidates;  ///< enumeration order:
                                                ///  harmonics ascending, then maxBreaks ascending
  int selectedHarmonics = 0;
  int selectedMaxBreaks = 0;
  int selectedParamCount = 0;
  double selectedScore = 0.0;  ///< NaN unless selected
  bool selected = false;       ///< false = degraded refusal, see @a degradedReason
  const char *degradedReason = nullptr;  ///< stable code, null when selected:
                                         ///  "insufficient_valid_samples" |
                                         ///  "no_fittable_candidate"
};

/// Bounded model selection over the harmonic+trend segmented family.
/// k = 0 candidates use the shared linear-only segmentation
/// (piecewiseLinearTrend); k >= 1 candidates use fitSeasonalTrendBreaks.
/// Ties on the score resolve to the earliest enumerated candidate
/// (harmonics ascending, then maxBreaks ascending) — a parsimony-leaning,
/// fully deterministic rule. BlockCv masks each contiguous fold to NaN,
/// refits, and predicts the fold from the refit segment models; folds that
/// cannot be refitted contribute no samples (documented, deterministic).
ModelSelectionResult selectSeasonalTrendModel(
    const std::vector<float> &y, const std::vector<double> &tDays,
    const ModelSelectionOptions &options );

} // namespace sicnu::temporal
