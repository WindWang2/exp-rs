# PR_BODY — Temporal Intelligence 11.0

## feat(temporal): seasonal-component break attribution, bounded model selection, uncertainty, phenology 2.0

Closes the ADR 0148 declared temporal follow-up debts on the Platform 10.0 lineage:
**seasonal-component break detection** (trend-only before), **per-segment model selection**
(none before), **fit uncertainty** (no CI anywhere before), and upgrades **phenology** to
automatic multi-cycle with cross-year windows and quality-gated refusal semantics.

## Baseline

- Branch created from `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  (fix: fail-closed fixes for review issues #994–#999 (#1000)).

## What is delivered

### Kernels (`src/processing/algorithms/temporal/`, namespace `sicnu::temporal`)
- **`temporal_selection.{h,cpp}`** —
  (A) `attributeSeasonalTrendBreaks`: for every break of the shared joint harmonic+trend
  segmentation, nested weighted-LS F comparisons on the two neighbouring segments decide
  whether the *seasonal basis* (harmonic sin/cos coefficients) changed beyond the *trend*
  change → per-break kind {none, trend, seasonal, both, untestable}, seasonal-coefficient
  shift norm, harmonic-1 amplitude/phase deltas, F statistic, p-value. Plain unrobust refits
  keep the test self-consistent even when the segmentation ran with IRLS. Honest naming:
  BFAST-*inspired* seasonal-change testing, not full BFAST, not CCDC.
  (B) `selectSeasonalTrendModel`: bounded candidate grid {harmonics 0..3} × {break budget
  0..4} scored by AICc / BIC / deterministic contiguous-block CV; fixed enumeration order;
  exact ties resolve to the earliest (smallest) candidate; AICc-undefined candidates are
  rejected; degenerate series report stable refusal codes
  ("insufficient_valid_samples" / "no_fittable_candidate") instead of a fabricated model.
- **`temporal_uncertainty.{h,cpp}`** (C) — analytic weighted-LS coefficient CIs
  (diag((XᵀWX)⁻¹)·σ̂², Acklam normal quantile) and a generic residual bootstrap
  (fixed design; residuals redrawn only at observed indices — missing/irregular sampling
  preserved; seeded mt19937 with a documented modulo index rule; bounded resamples ≤ 999;
  percentile intervals; < 60% refit success ⇒ valid=false refusal, never fabricated bounds).
  Quality weights propagate through the weighted fits (global weight rescaling leaves
  standard errors invariant — tested).
- **`phenologyMultiCycle`** in `temporal_fit.{h,cpp}` (D) — automatic cycle candidates from
  the pixel's own seasonal climatology (local maxima above an amplitude share, min-span
  merge, ≤ maxCyclesPerYear per calendar year), windows bounded by circular midpoints,
  wrapped windows assigned to the HARVEST year (window-end year), per-window quality flags
  {sampleCount, coverage, gapFraction, amplitudeRatio} with stable refusal codes
  ("low_window_samples" / "coverage_gap" / "edge_truncated_window" /
  "below_amplitude_threshold" / "threshold_crossing_failed"). Composition of the existing
  `seasonalDecompose` + `phenologyThreshold` kernels — no new fitting semantics, no
  low-sample guessing.
- **`temporal_design_detail.h`** — the harmonic+trend design row/evaluator extracted from
  `temporal_change.cpp` into the shared detail namespace (single authority; the local copy
  was deleted; arithmetic unchanged).
- `SeasonalTrendBreaksResult` gains `segmentCoefficients` (additive field; per-segment model
  coefficients exposed without a refit).

### Operators (`src/operators/rs/`)
- **`rs:temporal_seasonal_breaks`** — per-pixel segmentation + attribution; bands:
  breaks_count, break_kind_k (nominal code 0..4, documented), break_day_k, break_mag_k,
  break_seasonal_shift_k, break_pvalue_k, rmse, r2, valid_count; opt-in `compute_ci` adds
  mag_ci_lo_k / mag_ci_hi_k (pixel-scoped deterministic seeds = seed + pixel index).
- **`rs:temporal_model_select`** — bands: selected_harmonics, selected_max_breaks,
  selected_params, selected_score, valid_count; JSON result adds selection histograms.
- **`rs:temporal_phenology_multi`** — bands: cycle_count, refusal_count, and per cycle k:
  cycle{k}_sos/pos/eos/los/amplitude/valid (median-of-seasons doy; NaN = refused/absent).

### Region work (E)
- `rs:temporal_extract_series` polygon membership rewired onto the single authority
  `temporal_region_table::buildRegionGeometry` (behavior-preserving; the inline even-odd
  copy was deleted after verifying predicate parity). Batch region tables
  (`rs:temporal_extract_regions` / `rs:temporal_region_features`) unchanged.

### Performance (G)
- `temporal_change.cpp::fitSegment` and `temporal_fit.cpp::harmonicFit` reuse thread-local /
  hoisted scratch: the per-pixel loop no longer heap-allocates Gram/dense/solution buffers
  per call (~7 allocations per fit → ~2 solver copies). Accumulation and elimination order
  unchanged — the byte-stable determinism anchor (`test_temporal_algorithms` rerun test) is
  the regression gate. SIMD not adopted (see limitations).

### Surfaces
- `TemporalAnalysisDialog`: the three new algorithms with parameter pages (existing command
  seam; `tr()`/help tips per dialog conventions; no D18-owned files touched). CLI/MCP/agent
  surfaces are automatic via `RSOperatorRegistry::listSchemas()`.
- Capability knowledge sync: `data/agent/capabilities/temporal.json` (+3),
  `data/processing/algorithm_meta/capability/rs-temporal-{seasonal-breaks,model-select,
  phenology-multi}.json` (schema v2), `capability_catalog.cpp` familyMap (+3),
  `docs/processing/temporal.md` (+3 rows), CHANGELOG section.

### Tests (H + oracles)
- `tests/temporal_corpus.h`: deterministic closed-form scenario builders (trend/seasonal
  amplitude/seasonal phase/both breaks, harmonic order 2, double season, winter cross-year,
  no-change negative controls, seeded missing/spike injection). Expectations derive from the
  generators, never from the code under test.
- `test_temporal_selection.cpp` — Oracle 1 (a pure step classifies TrendOnly, an amplitude
  jump SeasonalOnly, a phase shift SeasonalOnly, step+amplitude Both; the stationary control
  never yields a seasonal attribution; short sides report Untestable with NaN statistics)
  and Oracle 2 (order-2 recovery, order-0 on seasonality-free series, break-budget pick-up,
  all three penalties bit-identical across reruns, −inf tie resolves to the smallest model,
  stable refusal codes).
- `test_temporal_uncertainty.cpp` — exact-series coefficient recovery, 95% bounds bracket
  the truth with width = 2·z·se, noise widens intervals, quality weights shrink intervals,
  weight-rescaling invariance, singular/short refusals, bootstrap determinism +
  seed-sensitivity + planted-magnitude coverage + low-success refusal + gap preservation.
- `test_temporal_phenology_multi.cpp` — Oracle 3: double-cropping shape yields two cycles
  per year with chronological cycle indices; winter shape produces wrapped windows counted
  toward the harvest year; a >100-day observation hole produces gap refusals with NO
  metrics (all invalid metric fields checked); sparse windows below the floor refused;
  degenerate inputs refuse with stable codes; full determinism across runs.
- `test_temporal_operators_ti11.cpp` — E2E through the real registry on synthetic GeoTIFF
  stacks: seasonal-kind vs trend-kind pixel separation, CI band shapes, model-order
  recovery + NaN refusal, double-season cycle_count, gapped-pixel refusal_count.

## Dedupe / parallel-track ownership

- Open PR #1008 (`zcode/radiometric-spectral-workbench`, spectral/radiometric): **zero**
  temporal-file overlap; my shared-file edits (`.gitignore`, CMakeLists) are append-only
  minimal blocks. At branch time #1008 was the only open PR; its merge state is its owner's
  concern.
- Concurrent unpushed local branches (`zcode/advanced-insar-platform-11`,
  `zcode/cn-eo-product-physics-11`, `zcode/execution-runtime-convergence-11`,
  `zcode/spectral-intelligence-11`): verified by `git diff --name-only origin/master...<b>`
  — no temporal-file overlap.
- D18 (#991) / D19 (#992) were merged into the baseline before branch creation; D18-owned
  mission-mount files untouched.
- Open issues #1001–#1007 (dataset/workflow/georef/io/agent): out of scope, not touched.

## Architecture decisions

See `.planning/temporal-intelligence-11/DECISIONS.md` (D-TI11-1..10): extend the 10.0
lineage rather than wiring the D16 hermetic library (no kernel-duplication growth; ADR 0161
include rule respected); nested-F attribution instead of a full BFAST; bounded-grid
selection with documented deterministic tie rules; opt-in bootstrap CIs with refusal
floors; harvest-year wrapped windows; single point-in-polygon authority; scratch reuse with
exact arithmetic-order preservation; additive operator naming; independent test oracles.

## Compatibility

- All changes additive: no existing operator renamed, no band removed, no schema broken.
  `SeasonalTrendBreaksResult` gains a field (source-compatible addition; ABI not a contract
  in this repo's in-tree build). `rs:temporal_extract_series` polygon output CSV/JSON
  format unchanged.
- New failure codes: none added to the closed `kFailureModeCodes` vocabulary (kernel-level
  refusal codes are strings in result structs, documented per header).

## Local evidence (no online CI dependency)

All verification is local and reproducible; commands and exits are recorded in
`.planning/temporal-intelligence-11/EVIDENCE.md` + `TEST_MATRIX.md`. Resource bounds:
`CMAKE_BUILD_PARALLEL_LEVEL=2`, all builds `-j2`, tests `-j1`, `QT_QPA_PLATFORM=offscreen`.
Final targeted validation is run twice consecutively before the PR is opened.

## Known limitations / follow-ups

- Full BFAST/CCDC parity remains a documented non-goal ("…-inspired" scope, per ADR 0148).
- Unifying the two coexisting BFAST-like kernels (`temporal_change` wired vs the D16
  library-only `breakpoint_detection`) is a cross-lineage refactor beyond this track's
  ownership; the D16 library remains unwired by design (ADR 0161).
- Per-region seasonal-break attribution columns on `rs:temporal_region_features` (schema
  bump) — follow-up (D-TI11-6).
- SIMD kernels: not adopted — small-matrix bound; would require a result-consistency
  harness to be worth it (D-TI11-8).
- Attribution F tests are asymptotic (Gaussian residual assumption) and the greedy
  segmentation is not a global optimum — both documented in operator metadata.
