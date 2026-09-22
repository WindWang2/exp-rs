# Recon & Current-State Matrix — temporal-change-phenology (19/20)

Baseline: `origin/master` = `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01`
(2026-09-23 fetch; identical to the 2026-09-22 recon seed).

## 1. Concurrent-work dedup (as of branch point)

- Open PRs: #1237 `feat/undergrad-lab-cockpit`, #1238
  `feat/experiment-exploration-studio`, #1239 `feat/teaching-admin-console`.
  All touch teaching/app shell files only; **zero overlap** with
  `src/processing/algorithms/temporal/**`, `breakpoint_detection.*`,
  `phenology_metrics.*`, `src/operators/rs/rs_temporal_*`.
- Open issues: 0.
- `agent/rs14-*` branches: fully merged, no unique commits.
- `agent/flash-*` branches: stale baselines, mined as defect hints only
  (no direct ports).
- `rs14-unified-verifier`: stale parallel-implementation branch; not touched.

## 2. Ownership / call graph (who owns what)

| Component | Authority | Callers | Error model | Tests |
|---|---|---|---|---|
| `temporal_time.{h,cpp}` | acquisition-time parsing (ISO/DOY/filename), UTC | collection input, workspace | fail-closed (`valid=false`) | test_temporal_core/calendar |
| `temporal_calendar.{h,cpp}` | regular calendar grids + `regularizeSeries` | regularize operator, workspace | refused points = NaN, counts truthful | test_temporal_calendar |
| `temporal_fit.{h,cpp}` | harmonicFit (IRLS Huber), whittakerSmooth(+Robust), phenologyThreshold, piecewiseLinearTrend, mannKendallSenSlope, seasonalDecompose, phenologyMultiCycle | smooth/trend/phenology/breakpoints operators | NaN fit on refusal; NaN rmse (#759) | test_temporal_fit, test_temporal_algorithms, test_d16_temporal_trend |
| `temporal_irregular.{h,cpp}` | day-axis kernels (movingAverage/SG/WhittakerTime), gapFillProvenance | irregular-smooth operators | typed refusal (all-NaN) on invalid axis | test_temporal_irregular |
| `temporal_gapfill.{h,cpp}` | gapFillSeries linear/nearest, counts | rs_temporal_gap_fill operator | NaN stays NaN; one-sided gap NaN | test_temporal_irregular |
| `temporal_change.{h,cpp}` | fitSeasonalTrendBreaks, disturbanceOnset | harmonic_breaks/seasonal_breaks/breakpoints operators | NaN segments honest | test_temporal_change |
| `breakpoint_detection.{h,cpp}` | detectHarmonicBreaks (BFAST-simplified) | **tests only** (e2e) | honest all-NaN failure | test_bfast_harmonic_breaks, test_d16 e2e |
| `phenology_metrics.{h,cpp}` | extractDynamicThreshold, fitDoubleLogistic, extractMultiCycle | tests + `src/agent/spatial_tools` (extractDynamicThreshold); fitDoubleLogistic/extractMultiCycle have NO production caller on this master (tests only) | invalid metrics refused | test_phenology_extraction |
| `temporal_monitoring.{h,cpp}` | cusum/ewma steps, seasonalMannKendall | monitor operator | NaN z on degenerate | test_temporal_algorithms |
| `temporal_uncertainty.{h,cpp}` | analytic coefficient CIs, residual bootstrap CI | model_select / uncertainty surfaces | refusalReason strings | test_temporal_uncertainty |
| `temporal_smoothing.{h,cpp}` (d16) | regular-axis Whittaker/SG (D16 studio) | timeline studio | empty vector on refusal | test_d16_temporal_trend |

Shared detail headers (single-source, no second authority):
`temporal_linalg_detail.h` (solveSmallDense/solvePentadiagonal/normalQuantile/
localPolynomialAt/madScale), `temporal_design_detail.h` (design rows).

## 3. Recent history already banked (regression-only, do NOT re-implement)

- #1229 (`5f71942f9`, 2026-09-22): 365.25 `doyOf` with day-366 bucket;
  `doyAt` yearLen-366 leap unwrap; MOSUM σ√h standardization; true MAD via
  `detail::madScale` in 4 kernels; interpolated SOS/EOS crossings.
- #1200 (`82308915d`): seasonalDecompose trend on the day axis; gap-fill
  provenance consumed by downstream statistics; capability JSON sync.
- #1166-era: irregular time-axis kernels, opt-in CI bands, SAR fusion.

## 4. Findings (source-level review of this master)

### F1 (P1, correctness) — `BreakpointCandidate.index` breaks contract under NaN/unsorted input
`breakpoint_detection.cpp:376` stores `bp.index = best.split`, the index into
the **sorted finite sub-vector**, while `breakpoint_detection.h:37` documents
"series index of the segment boundary (first point of the new segment)".
`tDays` is mapped to the caller axis but `index` is not. Any NaN in the input
before the break (or unsorted input) makes `index` point at the wrong input
sample. Tests never cover NaN inputs so this is invisible today. Existing
callers: tests only → behavior-compatible fix is to map through
`originSorted[best.split]` (identity for complete ascending inputs, i.e. all
existing expectations preserved).

### F2 (P2, correctness/consistency) — day-366 samples dropped by multi-cycle phenology
`phenology_metrics.cpp` `doyOf()` (post-#1229) emits DOY ∈ [1, 366]; the
leap bucket is day 366. `extractMultiCycle` (line ~592) and the
`fitDoubleLogistic` initializer (line ~320) call
`extractDynamicThreshold(..., 1, 365)` — excluding day-366 samples from the
season. The convention everywhere else (`phenologyThreshold` callers in
`temporal_fit.cpp:1242`, header docs) is the widest window `1..366`.
Effect: interior season samples 0.07% of the axis can shift SOS/EOS/POS
marginally or break bracketing; inconsistent with the documented day axis.

### F3 (P3, docs) — stale comment in `madScale`
`temporal_linalg_detail.h:243-244` says "even counts take the upper middle",
but the code takes the true median (mean of the two middle). The convention
comment referenced (temporal_smoothing.cpp) computes the true median too.

### F4 (P3, observed) — `phenologyThreshold` dead wrap branch
`temporal_fit.cpp:386`: `span < 0 → span += 365.25` is unreachable on
ascending-time input (tSos < tEos by construction). Documented, harmless;
no change (avoid touching a freshly-merged surface without failing evidence).

> Final authoritative numbering for fixes/oracles lives in `02-test-ledger.md`
> and the PR body; the F-numbers below were the working notes and are kept
> for provenance.

### F5 (performance, evidence-first)
`detectHarmonicBreaks` greedy split search is O(n²·p²) per accepted break
(OLS re-fit per split candidate). Consumers are currently tests/e2e only;
the operator-facing `fitSeasonalTrendBreaks` already uses O(1)-per-candidate
cumulative sums. No operator-level hot path on this master → **no change**
per "don't fabricate micro-optimizations" (recorded in PR known-limits).

## 5. Not-done items (classified)

- Owned by other tracks: teaching/UI temporal panels (#1237/#1238/#1239),
  QGIS-vendored `src/core/*temporal*` (upstream).
- Not reproducible on this master: MAD/MOSUM/SOS-EOS/leap defects from the
  seed (fixed by #1200/#1229) — regression tests only.
- Needs real platform environment: none identified (kernels are
  platform-independent pure C++).
- Explicit future direction: in-place solveSmallDense for fitSegment scratch
  (recorded follow-up in temporal_change.cpp comments).
