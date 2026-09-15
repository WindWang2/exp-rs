// src/processing/algorithms/temporal/temporal_fit.h
// Per-pixel time-series fitting primitives for Temporal Analysis 2.0
// (Platform 3.0, goal §7). Pure, header-light, unit-testable: every function
// operates on one float series (NaN = missing) with an optional weight vector
// (missing values implicitly get weight 0).
//
// Numeric contract: all fits treat NaN samples as absent (never as zero),
// produce NaN for series with fewer valid samples than the model requires,
// and are deterministic (single-threaded, fixed order — bit-exact regression
// anchors; only the banded Whittaker solver is tolerance-grade — its tests
// assert behavioral bounds at 1e-4 margins; a formal relative-ε lock is
// pending and tracked in docs/processing/validation-policy.md).
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

/// Robust Whittaker smoother: @a iterations (1..10, default 3) of IRLS with
/// Cauchy reweighting w_i ∝ 1 / (1 + (r_i / k)²), k = 3 · 1.4826 · MAD of
/// the residuals — damps spikes (undetected clouds, BRDF outliers) instead
/// of smearing them. Same contract as whittakerSmooth otherwise; final
/// result is the last solve. Tolerance-grade (documented 1e-4).
std::vector<float> whittakerSmoothRobust( const std::vector<float> &y,
                                          const std::vector<float> &w,
                                          double lambda, int iterations = 3 );

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

// --- Multi-cycle phenology (Temporal Platform 10.0) ---

/// One season window in day-of-year; wrapped windows (start > end) span the
/// year end. Hemisphere-neutral: the caller declares the windows (the
/// operator documents the northern-hemisphere default and the complement
/// rule for second cycles).
struct SeasonWindow
{
  int startDoy = 0;
  int endDoy = 0;
};

/// The doy window covering the complement of @a window inside the year
/// (second crop cycle when @a window is the first).
SeasonWindow complementSeasonWindow( const SeasonWindow &window );

struct SeasonYearMetrics
{
  int year = 0;                ///< calendar year of the season's samples
  int cycleIndex = 0;          ///< 0-based cycle inside the year
  SeasonalMetrics metrics;     ///< same thresholds semantics as phenologyThreshold
};

/// Per-year, per-cycle threshold phenology (multiple crop cycles). For every
/// calendar year present in @a yearOf and every window in @a windows (1 or 2
/// entries), the year's samples inside the window are extracted and scored
/// with the phenologyThreshold semantics (base/amplitude/POS from the
/// window's valid samples; SOS/EOS by the crossing fraction; LOS over the
/// year axis). Years with fewer than 3 valid samples in a window yield no
/// metrics for that cycle (the year still appears for its other cycles).
/// @a windows empty or > 2 returns an empty result.
std::vector<SeasonYearMetrics> phenologyCyclesPerYear(
  const std::vector<float> &y, const std::vector<double> &tDays,
  const std::vector<int> &doyOf, const std::vector<int> &yearOf,
  const std::vector<SeasonWindow> &windows, double crossingFraction );

// --- Phenology 2.0: automatic multi-cycle candidates with quality flags
//     (Temporal Intelligence 11.0) ---

/// Quality / refusal flags for one proposed cycle. A cycle that fails any
/// check reports valid=false with a stable @a refusalReason and NO metrics
/// — the platform refuses to guess phenology from sparse or gapped windows
/// (extends the phenologyCyclesPerYear no-guessing contract).
struct PhenologyQualityFlags
{
  bool valid = false;
  int sampleCount = 0;         ///< finite observations in the window
  double coverage = 0.0;       ///< observed span / window span (0..1; detects
                               ///  series-edge truncation)
  double gapFraction = 0.0;    ///< largest observation gap / window span (0..1)
  double amplitudeRatio = 0.0; ///< window amplitude / whole-series amplitude
  const char *refusalReason = nullptr;  ///< null when valid; stable codes:
                                        ///  "low_window_samples" |
                                        ///  "coverage_gap" |
                                        ///  "edge_truncated_window" |
                                        ///  "below_amplitude_threshold" |
                                        ///  "threshold_crossing_failed"
};

/// One automatic cycle: metrics and quality. The cycle's samples are
/// selected as the TIME range between the midpoints to the adjacent peaks —
/// a season that crosses the calendar-year boundary is one continuous
/// range, not a wrapped doy window.
struct PhenologyCycle
{
  int seasonYear = 0;    ///< harvest year = calendar year of the window END
                         ///  (a Dec→May season counts toward the year it
                         ///  ends in)
  int cycleIndex = 0;    ///< 0-based within @a seasonYear (chronological by peak)
  SeasonWindow window;   ///< reported doy metadata: first/last observed doy
                         ///  of the cycle's samples (informational)
  SeasonalMetrics metrics;  ///< meaningful only when quality.valid
  PhenologyQualityFlags quality;
};

struct PhenologyMultiOptions
{
  int maxCyclesPerYear = 3;      ///< strongest peaks kept per calendar year (1..4)
  double crossingFraction = 0.5; ///< phenologyThreshold crossing fraction
  double trendLambda = 1e4;      ///< decomposition trend smoothing (Whittaker λ)
  int seasonalWindow = 15;       ///< decomposition climatology smoothing (days)
  int minValidPerSeason = 6;     ///< hard sample floor per window (kernel ≥ 3)
  double minPeakFraction = 0.25; ///< peak must exceed min + this × seasonal range
  int minCycleSpanDays = 45;     ///< adjacent peaks closer than this merge
  double maxGapFraction = 0.5;   ///< refusal: largest gap above this share
  double minCoverage = 0.5;      ///< refusal: observed span below this share
  double minAmplitudeRatio = 0.1; ///< refusal: window amplitude below this share
};

struct PhenologyMultiResult
{
  std::vector<PhenologyCycle> cycles;  ///< ascending seasonYear, then cycleIndex
  int cyclesPerYearMax = 0;  ///< max cycles observed in any season year
                             ///  (1 single / 2 double / 3+ triple cropping)
  bool valid = false;
  const char *refusalReason = nullptr;  ///< null when valid; stable codes:
                                        ///  "insufficient_series" |
                                        ///  "insufficient_valid_samples" |
                                        ///  "degenerate_seasonal_component"
};

/// Automatic multi-cycle phenology (Phenology 2.0). Proposes cycle windows
/// from the seasonal component (seasonalDecompose climatology): local maxima
/// above a fraction of the seasonal range that dominate their ±
/// minCycleSpanDays neighbourhood, merged by minimum cycle span, strongest
/// @a maxCyclesPerYear per calendar year. Each interior peak's window is the
/// TIME range between the midpoints to its adjacent peaks — a season that
/// crosses the calendar-year boundary is one continuous range counted toward
/// the harvest year (the year the window ends in); series-edge peaks are
/// never scored (their seasons are truncated, not guessed). Every window is
/// quality-gated (sample count, coverage, gap fraction, amplitude share) and
/// refused — never guessed — when the gate fails. Composition of the
/// existing decomposition + threshold kernels; no new fitting semantics.
PhenologyMultiResult phenologyMultiCycle(
  const std::vector<float> &y, const std::vector<double> &tDays,
  const std::vector<int> &doyOf, const std::vector<int> &yearOf,
  const PhenologyMultiOptions &options );

/// Greedy piecewise-linear trend segmentation (BSFAST-lite): repeated OLS on
/// segments, splitting at the point with the largest RSS reduction while the
/// reduction ratio (reduction / segment RSS) exceeds @a minImprovement and
/// both sides keep >= @a minSegment samples.
struct BreakpointResult
{
  std::vector<int> breakIndices;   ///< segment start indices of segments 2..k
  std::vector<double> slopes;      ///< per-day slopes per segment
  std::vector<double> intercepts;  ///< at t = 0 (series epoch)
  double rmse = 0.0;               ///< sqrt( RSS / valid observations ); NaN when none
  long validCount = 0;             ///< finite observations across all segments
};

BreakpointResult piecewiseLinearTrend( const std::vector<float> &y,
                                       const std::vector<double> &tDays,
                                       int maxBreaks, int minSegment,
                                       double minImprovement );

/// Non-parametric monotonic trend: Sen's median slope with the Mann-Kendall
/// test (Gilbert 1987, chapter 16; supporting references in
/// docs/processing/temporal.md). Robust to outliers and free of the OLS
/// normality assumption; suited to noisy vegetation-index series.
///
/// Definitions (t = @a tDays, valid = finite y):
///   S        = Σ_{i<j, t_i<t_j} sign(y_j − y_i)
///   var(S)   = [n(n−1)(2n+5) − Σ t_p(t_p−1)(2t_p+5)] / 18   (tie-corrected,
///              t_p = multiplicity of tied value groups)
///   z        = (S ∓ 1)/√var(S) with the ±1 continuity correction, 0 when S = 0
///   p        = erfc(|z|/√2)          (two-sided standard-normal tail)
///   slope    = median of pairwise (y_j − y_i)/(t_j − t_i) over t_i < t_j
///   intercept = median of (y_i − slope·t_i)
/// Pairs with equal times are skipped for the slope; both samples still take
/// part in S only through strictly-ordered-time pairs. All outputs are NaN
/// when fewer than 3 valid observations exist (no meaningful test) or when no
/// strictly time-ordered pair exists. Deterministic: fixed evaluation order,
/// bit-exact grade.
///
/// Caveat: var(S) is Gilbert's formula for one observation per instant. With
/// same-day duplicates (collection `duplicate_policy = keep_all`) the
/// denominator over-counts pairs that can never enter S, which biases |z|
/// toward 0 — the test is conservative, never anti-conservative. Pass
/// `duplicate_policy = reject` (or pre-aggregate) for exact inference.
/// Inputs must be in non-decreasing time order (the operator seam sorts).
struct SenTrendResult
{
  double slope = 0.0;      ///< Sen's slope per day (median pairwise slope)
  double intercept = 0.0;  ///< median of (y_i − slope·t_i)
  double z = 0.0;          ///< Mann-Kendall standardized statistic
  double pValue = 1.0;     ///< two-sided significance (small = significant trend)
  double variance = 0.0;   ///< tie-corrected var(S), for reference
  int validCount = 0;      ///< finite observations
};

SenTrendResult mannKendallSenSlope( const std::vector<float> &y,
                                    const std::vector<double> &tDays );

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
