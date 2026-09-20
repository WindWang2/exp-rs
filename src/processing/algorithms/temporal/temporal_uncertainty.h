// src/processing/algorithms/temporal/temporal_uncertainty.h
// Fit uncertainty for the temporal kernels (Temporal Intelligence 11.0;
// closes the ADR 0148-era gap "no CI anywhere / quality weighting only in
// composite best-pixel").
//
// Two complementary, opt-in mechanisms:
//  1. ANALYTIC: coefficient standard errors for the weighted harmonic+trend
//     linear model, from diag((XᵀWX)⁻¹)·σ̂² with σ̂² = SSE/(n−terms) and a
//     Student-t quantile at df = n−terms (exact for Gaussian noise at any
//     df ≥ 1 — the normal approximation would be badly anticonservative for
//     small df). Deterministic, O(terms³).
//  2. RESIDUAL BOOTSTRAP: fixed-design residual resampling (centered
//     residuals drawn with replacement at the SAME time points) for any
//     scalar statistic without closed form — break dates, magnitudes,
//     phenology metrics. Preserves the irregular/missing sampling pattern
//     (only observed indices participate); deterministic via a seeded
//     mt19937 with a documented index rule. Quality weights enter through
//     the fitted values/weights the caller used; resampling itself is
//     weight-agnostic.
//
// Honesty rules: a CI that cannot be estimated (too few successful refits,
// singular refits) is reported valid=false with NaN bounds and a refusal
// flag — never fabricated. Success-rate and resample caps are caller-bounded.
#pragma once

#include <functional>
#include <stdint.h>
#include <vector>

namespace sicnu::temporal
{

/// One coefficient's interval (index order matches the design row:
/// [intercept, t, sin1, cos1, ...]).
struct CoefficientInterval
{
  double estimate = 0.0;
  double stdError = 0.0;
  double lower = 0.0;  ///< estimate + z_{(1−level)/2}·se (NaN when invalid)
  double upper = 0.0;
  bool valid = false;
};

struct AnalyticCiResult
{
  std::vector<CoefficientInterval> coefficients;
  double sigma2 = 0.0;  ///< residual variance estimate (NaN when invalid)
  int df = 0;           ///< n_valid − terms
  bool valid = false;
  const char *refusalReason = nullptr;  ///< null when valid; stable codes:
                                        ///  "insufficient_valid_samples" |
                                        ///  "singular_system"
};

/// Analytic weighted-LS coefficient CIs for the shared harmonic+trend design
/// over segment [a, b). Weights: @a weights[i] > 0 participates with that
/// weight (quality-band weights are valid); non-finite y is skipped.
/// @a ciLevel in (0, 1) (0.95 default); Student-t quantile at df = n−terms.
AnalyticCiResult harmonicTrendCoefficientCi(
    const std::vector<float> &y, const std::vector<double> &tDays, int a, int b,
    int harmonics, const std::vector<double> &weights, double ciLevel );

/// Same analytic coefficient CIs on the harmonicFit basis — the no-trend
/// design [1, sin/cos…] with harmonics clamped to [1, 6] (Temporal Phenology
/// 12.0: wires CIs into rs:temporal_harmonic_fit, whose model has no trend
/// column; using the trend design here would silently report CIs for a
/// different model).
AnalyticCiResult harmonicCoefficientCi(
    const std::vector<float> &y, const std::vector<double> &tDays, int a, int b,
    int harmonics, const std::vector<double> &weights, double ciLevel );

struct BootstrapOptions
{
  int resamples = 199;      ///< bounded (caller clamps; hard cap 999)
  uint32_t seed = 20260915u;  ///< default seed = track start date
  double ciLevel = 0.95;
  double minSuccessRate = 0.6;  ///< below this the CI is refused (valid=false)
};

struct BootstrapCi
{
  double estimate = 0.0;  ///< statistic on the original data (NaN when invalid)
  double lower = 0.0;  ///< NaN unless valid (set by the kernel)
  double upper = 0.0;  ///< NaN unless valid (set by the kernel)
  int successes = 0;
  bool valid = false;
  const char *refusalReason = nullptr;  ///< null when valid; stable codes:
                                        ///  "low_success_rate" (too many
                                        ///  failed refits) |
                                        ///  "invalid_input" (size mismatch,
                                        ///  no residuals, non-finite
                                        ///  estimate)
};

/// Percentile-interval residual bootstrap for a scalar statistic.
/// @a fitted must hold the model's fitted values (NaN where no fit);
/// residuals are formed at indices where y and fitted are both finite and
/// centered on their mean. @a statistic receives a resampled series
/// (same length, same time axis) and returns NaN for "refit failed". The
/// estimate is @a statistic(y) itself. Deterministic for fixed inputs.
BootstrapCi residualBootstrapCi(
    const std::vector<float> &y, const std::vector<float> &fitted,
    const std::function<double( const std::vector<float> & )> &statistic,
    const BootstrapOptions &options );

} // namespace sicnu::temporal
