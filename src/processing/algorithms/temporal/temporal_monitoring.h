// temporal/temporal_monitoring.h — per-pixel temporal monitoring kernels
// (CUSUM, EWMA, seasonal Mann-Kendall) — Foundation 5.0, Milestone E.
//
// Contracts:
//   * CUSUM / EWMA consume STANDARDIZED anomalies z_t = (x_t − μ)/σ. The μ/σ
//     convention is the caller's (the retrospective operator uses the full
//     series' Welford statistics; a baseline-window variant passes those
//     instead). z must be finite — callers skip invalid observations and the
//     state's `used` count reflects how many contributed.
//   * CUSUM: S_t = S_{t−1} + (z_t − drift), drift in σ units (0 = plain
//     cumulative sum of standardized anomalies). Tracks final S, max|S_t|
//     and the argmax observation index.
//   * EWMA: Z_t = λ·Z_{t−1} + (1−λ)·z_t, λ ∈ (0, 1]; Z_0 = 0. Same
//     tracking of final/max/argmax.
//   * seasonal Mann-Kendall: per-season (e.g. calendar-month) Kendall S over
//     chronologically ordered observations, combined as
//       S = Σ S_m,  Var = Σ Var_m,  Z = (S − sign(S)) / sqrt(Var),
//       tau = S / Σ (n_m choose 2)
//     with the standard tie correction in each Var_m. Seasons with < 2
//     valid observations are skipped; seasonsUsed reports how many
//     contributed. Z is NaN when Var == 0 (no discriminating pairs) or
//     fewer than two seasons... precisely: one season with n >= 2 is still
//     a valid (ordinary) MK; NaN only when no season has >= 2 observations
//     or every Var_m is 0 (all-equal values within seasons).
#pragma once

#include <cstdint>

namespace sicnu::temporal::monitoring
{

/// CUSUM accumulator (POD, one per pixel; reset by zero-initializing).
struct CusumState
{
    double s = 0.0;       ///< running cumulative sum
    double maxAbs = 0.0;  ///< max |S_t| seen so far
    int argmax = -1;      ///< 0-based observation index of maxAbs
    int used = 0;         ///< valid observations consumed
};

/// One CUSUM step with a finite standardized anomaly @a z (callers skip
/// invalid observations). @a index is the observation's 0-based sequence
/// number (argmax bookkeeping).
void cusumStep( CusumState *state, double z, double drift, int index );

/// EWMA accumulator.
struct EwmaState
{
    double z = 0.0;       ///< EWMA of standardized anomalies
    double maxAbs = 0.0;
    int argmax = -1;
    int used = 0;
};

/// One EWMA step; @a lambda must be in (0, 1] (caller-validated).
void ewmaStep( EwmaState *state, double zAnomaly, double lambda, int index );

/// Seasonal Mann-Kendall result.
struct SeasonalMkResult
{
    double s = 0.0;           ///< combined S
    double variance = 0.0;    ///< combined variance (tie-corrected)
    double z = 0.0;           ///< standardized statistic (NaN when undefined)
    double tau = 0.0;         ///< combined Kendall's tau (NaN when no pairs)
    int seasonsUsed = 0;      ///< seasons with >= 2 valid observations
};

/// Computes the seasonal Mann-Kendall statistic over one pixel's series.
/// @a times (ascending not required — pairs use time ordering, strict
/// equality counts as a tie), @a values, @a seasons (any integer season id,
/// e.g. calendar month), @a valid 0/1 per observation, @a count series
/// length. O(Σ_m n_m²) pairs — callers guard series length.
SeasonalMkResult seasonalMannKendall( const double *times, const double *values,
                                      const int *seasons, const std::uint8_t *valid,
                                      int count );

} // namespace sicnu::temporal::monitoring
