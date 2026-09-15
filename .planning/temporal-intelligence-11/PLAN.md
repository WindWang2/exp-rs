# PLAN — temporal-intelligence-11

Fact source: BASELINE.md (origin/master `a5b11b7f`). One lineage extended: the Platform 10.0
`sicnu::temporal` kernels + operators (`temporal_change` / `temporal_fit` / `rs_temporal_*`).
The D16 hermetic `d16` library stays untouched (ADR 0161 include rule; no new link dependency
from operators). No kernel duplication (ARCHITECTURE_V3 §1).

## Architecture decisions (details in DECISIONS.md)

- **A+B+C live in one new kernel module** `src/processing/algorithms/temporal/temporal_selection.{h,cpp}`
  ("seasonal-trend model selection & attribution"): bounded candidate grid {harmonic order 0..3} ×
  {trend: none|linear} × {segmentation on/off} with AICc/BIC/deterministic-block-CV penalties;
  seasonal amplitude/phase break attribution reuses the `temporal_change` design-matrix + solve seam.
- **Operators**: 3 new additive operators
  - `rs:temporal_seasonal_breaks` — seasonal amplitude/phase break detection + trend/seasonal
    attribution + optional CI (packages A, C).
  - `rs:temporal_model_select` — per-segment bounded model selection with score table (package B).
  - `rs:temporal_phenology_multi` — automatic multi-cycle candidates, cross-year windows,
    per-metric quality flags, refusal semantics (package D).
- **C (uncertainty)**: new kernel `temporal_uncertainty.{h,cpp}` — analytic weighted-LS coefficient
  CI (σ²(XᵀWX)⁻¹ diag) + seeded deterministic residual bootstrap for break dates/magnitudes/phenology
  metrics; wired as opt-in params on the new operators (+ `rs:temporal_harmonic_breaks` gains
  `ci_level` opt-in? — only if trivially additive; otherwise left to new operators).
- **E (regions)**: refactor `rs:temporal_extract_series` polygon path onto the shared
  `buildRegionGeometry` membership kernel (single point-in-polygon authority); extend
  `rs:temporal_region_features` with opt-in per-region seasonal-break attribution columns
  (new opt-in params, sidecar version bump only when columns enabled).
- **F (UI)**: register the new algorithms in `TemporalAnalysisDialog` `kAlgorithms[]` + parameter
  pages (existing seam, `tr()` i18n); add one agent spatial tool `temporal:region_series_inspect`
  via `SpatialToolRegistry::registerBuiltinTools`. No D18-owned mount files.
- **G (perf)**: scratch-reuse refactor of `temporal_change` segment fits + `temporal_fit::harmonicFit`
  (preallocated Gram/work buffers passed in; arithmetic order preserved → bit-exact anchors
  `test_temporal_algorithms.cpp` rerun test must stay green). No SIMD unless consistency harness added
  (default: skip SIMD, document).
- **H (corpus)**: `tests/temporal_corpus.h` — deterministic (fixed-seed mt19937) scenario builder:
  trend-break-only / seasonal-amp-break / seasonal-phase-break / both / double-season /
  heavy-missing / spike-anomaly / no-change negative controls / cross-year season; consumed by the
  new kernel tests and one operator E2E.

## Phase mapping

| Phase | Content |
|---|---|
| 0 | audit + artifacts (done) |
| 1 | corpus helper + `temporal_selection` contracts (headers, options structs, result structs) |
| 2 | seasonal break attribution kernel + tests (Oracle 1) |
| 3 | model selection + uncertainty + phenology-multi kernels + tests (Oracle 2/3) |
| 4 | operators + capability JSON/docs + dialog + agent tool (surfaces) |
| 5 | scratch-reuse perf refactor (bit-exact), resource guards, cancellation checks |
| 6 | E2E corpus tests through registry, determinism rerun, region features columns |
| 7 | independent adversarial review (subagent #2) + P0/P1 fixes |
| 8 | double-run validation, rebase, push, PR |

## Verification gates

- Targeted test executables: `test_temporal_selection`, `test_temporal_uncertainty`,
  `test_temporal_phenology_multi`, `test_temporal_seasonal_breaks` (or consolidated into fewer
  executables matching repo norms), plus existing `test_temporal_change`, `test_temporal_fit`,
  `test_temporal_algorithms`, `test_temporal_operators_10`, `test_temporal_regions`,
  `test_capability_knowledge` (drift), `test_help_catalog` (if it pins operator docs).
- `QT_QPA_PLATFORM=offscreen`, `CTEST_PARALLEL_LEVEL=1`.
- Final: full targeted list run twice consecutively.
