# Temporal Operators: Scientific Semantics Reference

> What every temporal operator's statistic means: denominators, valid
> observations, time axis, and references. Architecture/history live in
> `docs/temporal/`; the shared policy rules live in
> [nodata-and-statistics.md](nodata-and-statistics.md).

## Shared semantics

- **Time axis**: `t` = days since the collection reference epoch (first
  acquisition), from the real UTC acquisition instants — never array indices
  — for trend, harmonic, breakpoints, phenology and gap-fill. Smoothing
  kernels (Savitzky–Golay, Whittaker, moving average) treat samples as
  uniformly spaced (documented limitation; see below).
- **Valid observation** = finite value after the reader's normalization
  (declared finite NoData, non-finite values, and QA/SCL-masked samples →
  NaN). All statistics count the same set: **a denominator counts
  observations that contributed to the numerator**.
- **Undefined ≠ zero**: with no valid observations a statistic is NaN (or the
  operator refuses when its contract declares a minimum observation count).
- Quality weighting exists only in `rs:temporal_composite` best-pixel
  selection; everything else weighs valid observations equally.

## Per-operator contracts

| Operator | Statistic / denominator | Time handling | Notes |
|---|---|---|---|
| `rs:temporal_summary` | mean/stddev over finite samples; `valid_count` = finite obs | order only | stddev is population (÷N); optional exact median |
| `rs:temporal_composite` | per-group reduction (best_pixel / mean / median) | tie-break by day distance to target | `valid_count` per pixel; best-pixel uses QA scores |
| `rs:temporal_trend` | OLS slope/intercept; RMSE ÷(N−2) df-corrected; r² over valid | real day offsets | `n` = observations added; r² = 1 for zero-variance series (documented) |
| `rs:temporal_sen_trend` | Sen's median pairwise day slope; MK S with tie-corrected variance (Gilbert 1987 eq. 16.5), continuity-corrected z, two-sided p = erfc(\|z\|/√2) | real day offsets; equal-time pairs skipped | robust to outliers; needs ≥ 3 valid obs; p is a normal approximation (approximate below ~10 obs); monotonic trend only; var(S) presumes distinct acquisition instants — same-day duplicates under `duplicate_policy=keep_all` make the test conservative (use `reject` for exact inference) |
| `rs:temporal_smooth` | SG / Whittaker / MA per window | **index axis** (uniform spacing assumed) | SG leaves NaN where the window holds fewer valid samples than degree+1; Whittaker bridges interior gaps with w=0 |
| `rs:temporal_gap_fill` | time-weighted linear interpolation between bracketing valid samples | real day offsets | fills only within `max_gap_days`; no extrapolation; duplicate instants averaged |
| `rs:temporal_harmonic_fit` | weighted harmonic OLS; RMSE/r² over valid only | real day offsets, fixed 365.25-d period | optional IRLS Huber reweighting (robust); `fitted` NaN at masked dates |
| `rs:temporal_phenology` | threshold-fraction metrics; integral Σv·Δt over valid pairs | real days; doy from UTC instant | `minValidPerSeason` gate; wrapped seasons handle year end |
| `rs:temporal_breakpoints` | greedy RSS segmentation (BSFAST-lite); RMSE = √(ΣSSE / valid obs) — #759 | real day offsets | `minSegment` derived from `minSegmentDays` / mean spacing (`totalSpan/(N−1)`); all-NaN pixel → NaN RMSE, 0 breaks |
| `rs:temporal_decompose` | doy climatology over valid years; Whittaker trend | **index axis** (both the trend penalty and the trend step ignore real time) | seasonal+remainder NaN where y NaN |
| `rs:temporal_anomaly` | z-score (y − mean)/σ vs baseline; σ sample (N−1) | baseline window selection | degenerate baselines → NaN |
| `rs:temporal_index_series` | per-date spectral index through the same kernels as `rs:spectral_index` | n/a | NaN propagates through index arithmetic; validFraction reported |
| `rs:temporal_regularize` | per-calendar-point statistic; `validObservations` = observations contributing to that point | real day offsets; grid anchored on the collection epoch (first acquisition) | closes T-2: irregular → regular calendar (nearest / window_mean / linear / whittaker-on-grid); no method extrapolates past the observed span; `filled` flag marks synthetic values; whittaker `maxGapNodes` bounds penalty bridging |
| `rs:temporal_harmonic_breaks` | greedy seasonality-adjusted trend-break segmentation; per-segment harmonic + linear-trend OLS; magnitude = \|fitted jump\| at the break; RMSE = √(SSE / valid) | real day offsets | closes T-3: BFAST/CCDC-*inspired* greedy method, not the full algorithms; break days = offsets from the collection epoch; onset/recovery require `direction`; unrecovered recovery reports −1 |
| `rs:temporal_extract_regions` | per region × date mean/min/max/stddev (population)/median/valid_count | real day offsets in the table | closes C-2: batch points/polygons with caller-owned ids; streaming by date (each scene read once); rows written only where validCount > 0, `emptyCells` reports omitted cells; median degrades to NaN beyond `median_budget_mb` |
| `rs:temporal_region_features` | per-region feature row (quality, distribution, Sen/OLS trend, anomaly z, change features, per-cycle phenology medians across years) | real day offsets; phenology windows in doy | typed table + JSON schema sidecar `exp_rs_temporal_region_features/1`; join label tables by `region_id`; phenology medians need ≥ 3 valid samples per year × cycle window |
| `rs:temporal_seasonal_breaks` | per break: nested weighted-LS F attribution (trend change vs seasonal sin/cos change over the two neighbouring segments), day offset, fitted-level jump, seasonal-coefficient shift, harmonic-1 amplitude/phase change, F, p; optional seeded residual-bootstrap CI on the level jump (`compute_ci`) | real day offsets | Temporal Intelligence 11.0 (closes the ADR 0148 seasonal-attribution gap): BFAST-*inspired* seasonal-change testing, not full BFAST/CCDC; untestable breaks (side too short) report kind 4 with NaN statistics — never a guess; bootstrap CIs report NaN when < 60% of refits succeed; attribution always uses plain fits (segmentation `robust` does not bias the test) |
| `rs:temporal_model_select` | bounded candidate grid {harmonics 0..maxHarmonics} × {break budget 0..maxBreaks} scored by AICc/BIC/deterministic contiguous-block CV; winner + parameter count + score bands | real day offsets | Temporal Intelligence 11.0 (closes the ADR 0148 model-selection gap): fixed candidate enumeration, exact ties resolve to the earliest (smallest) candidate; AICc undefined (n−K−1 ≤ 0) rejects the candidate; degenerate pixels report NaN refusal, never a fabricated model; `block_cv` refits per fold (markedly slower, bounded by cvFolds ≤ 10) |
| `rs:temporal_phenology_multi` | automatic cycle windows from the pixel's own seasonal climatology (bounded peak candidates), per-window threshold metrics + quality flags {sampleCount, coverage, gapFraction, amplitudeRatio}; per cycle index median-of-seasons doy bands | real day offsets; doy/year from UTC instants | Temporal Intelligence 11.0: wrapped windows (start > end) count toward the HARVEST year (window-end year); refused windows (below sample floor, coverage/gap/amplitude gate) output no metrics — refusal semantics, no low-sample guessing; `cycle_count` = valid cycle indices (cropping-system indicator); `refusal_count` reports gated-out windows |

## Platform 10.0 kernel notes

- **Regular calendars** (`temporal_calendar.h`): the grid is generated from
  real UTC instants anchored on the collection epoch — `"<N>d"` steps from
  the epoch, monthly steps from the epoch's day-of-month (clamped). Whittaker
  regularization is *defined on the calendar grid* (Eilers 2003): observations
  map to their nearest node, unobserved nodes carry weight 0, and runs longer
  than `maxGapNodes` split the solve rather than bridge.
- **Joint change model** (`temporal_change.h`): each segment fits
  `[1, t, sin/cos(kωt)...]`; breaks are trend breaks of the
  seasonality-adjusted residual via the same greedy RSS kernel as
  `rs:temporal_breakpoints`; up to 3 refinement iterations. Honesty rule:
  descriptions say "BFAST/CCDC-inspired", never "BFAST"/"CCDC".
- **Robust Whittaker** (`whittakerSmoothRobust`): IRLS with Cauchy weights
  `1/(1+(r/3·1.4826·MAD)²)`; damps spikes instead of smearing them.
- **Multi-cycle phenology** (`phenologyCyclesPerYear`): per-year, per-cycle
  threshold metrics (windows declared in doy — hemisphere-neutral); the
  raster operator (`rs:temporal_phenology cycles=2`) reports climatological
  second-cycle bands (`c2_*` + `cycle_count`), the region-features operator
  reports per-cycle median-across-years values.

## References

- Gilbert, R.O. (1987). *Statistical Methods for Environmental Pollution
  Monitoring*. Van Nostrand Reinhold. — Mann-Kendall test (ch. 16), Sen's
  slope, tie correction.
- Sen, P.K. (1968). "Estimates of the Regression Coefficient Based on
  Kendall's Tau." *JASA* 63(324), 1379–1389.
- Savitzky, A., Golay, M.J.E. (1964). *Analytical Chemistry* 36(8),
  1627–1639. — smoothing/derivative filter.
- Eilers, P.H.C. (2003). "A Perfect Smoother." *Analytical Chemistry* 75(14),
  3631–3636. — Whittaker smoother.
- Verbesselt, J. et al. (2010). "Phenological change detection while
  accounting for abrupt and gradual trends" — seasonal-trend model context
  for the breakpoint method family (exp-rs implements a greedy BSFAST-lite
  variant, not the full BFAST algorithm; `rs:temporal_breakpoints` docs state
  the exact method).
