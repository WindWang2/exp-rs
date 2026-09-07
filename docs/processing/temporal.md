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
