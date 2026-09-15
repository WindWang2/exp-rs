# DECISIONS — temporal-intelligence-11

Format: D-TI11-N · decision · alternatives · rationale.

## D-TI11-1 · Extend the Platform 10.0 `sicnu::temporal` lineage; do not wire the D16 `d16` library

Alternatives: (a) extend 10.0 kernels; (b) wire D16 `BreakpointDetector`/`PhenologyExtractor` into
operators as ADR 0161's "declared follow-up"; (c) write a third kernel family.
Choice: **(a)**. The 10.0 lineage is what operators/regions/UI already consume (no new link
dependency from `sicnu_operators` to the hermetic `SICNU_TEMPORAL_TIMELINE` lib, which was
deliberately built qgis-free). The repo already suffered ELF interposition from duplicate
same-signature kernels (ADR 0161 D-160-4) and has 3 Mann-Kendall + 2 BFAST-like copies; adding a
fourth lineage or promoting the second BFAST-like kernel into the operator surface would grow the
duplication ARCHITECTURE_V3 §1 forbids. D16 remains library-only; its BIC/F-test ideas are
re-implemented *once* inside the 10.0 module with clear attribution in DECISIONS (this is a
consolidation of an unwired experimental kernel's ideas, not a runtime duplicate). Follow-up note
for maintainers recorded in PR_BODY (unifying the two BFAST-like kernels is a cross-lineage
refactor beyond this track's ownership).

## D-TI11-2 · Seasonal break detection method: coefficient-shift refit attribution, not a full BFAST

Alternatives: (a) full BFAST iterative trend/seasonal alternation; (b) joint two-segment harmonic+
trend refit with nested-model comparison for attribution; (c) MOSUM on seasonal-component residuals only.
Choice: **(b)**, scoped and honest: candidate break set from the existing trend machinery
(`piecewiseLinearTrend` on seasonality-adjusted residual, as `fitSeasonalTrendBreaks` does today);
for each candidate, refit a two-segment model with (i) trend coefficients only free and (ii) trend +
seasonal (sin/cos) coefficients free; an F-test with deterministic bounded search decides whether the
seasonal component changed beyond the trend change → per-break attribution
{none, trend, seasonal, both}. Amplitude/phase are reported via per-segment harmonic coefficient
polar form (A·sin(ωt+φ)). We explicitly document this is **not** BFAST (no iterative alternation,
no seasonal-trend re-routing loop) and **not** CCDC (no L1, no dynamic model per pixel time series
element) — continuing the repo's "…-inspired" naming rule.

## D-TI11-3 · Model selection: bounded grid with AICc/BIC/deterministic block CV

Candidates: harmonic order k ∈ [0, `maxHarmonics`] (default max 3), trend family
{none, linear}, segmentation {0, 1, 2} interior breaks at most (bounded). Penalty: AICc default
(small-sample corrected, well-defined for n−k−1>0), BIC optional, `cv` = deterministic contiguous
blocked k-fold (folds are time-ordered slices, no RNG; k=4 default, guard for tiny n by falling
back to AICc with a `degraded` reason). Selection is deterministic: scores computed in fixed
candidate order; ties broken by smaller model (parsimony), documented and tested. Degenerate
inputs (valid < terms+2, all-NaN) return the smallest candidate with `selected=false` +
reason flag instead of erroring (refusal semantics, Oracle 3).

## D-TI11-4 · Uncertainty: analytic weighted-LS CI + seeded residual bootstrap; opt-in only

Alternatives: (a) bootstrap everywhere; (b) analytic only; (c) both, opt-in.
Choice: **(c)**. Analytic CI for linear coefficients (from (XᵀWX)⁻¹ diag × σ̂², t or normal
quantile at `ci_level`) — cheap, closed-form, deterministic. Residual bootstrap (fixed-seed
`std::mt19937`, default 199 resamples, bounded) for quantities without closed form (break date,
magnitude, phenology metric CIs) — resample centered residuals over the *observed* (irregular)
time points, refit, take percentile intervals; missing/irregular sampling is handled by
resampling only observed indices and reporting per-quantity success counts; quality weights
propagate through the existing weighted fits (weights enter XᵀWX; bootstrap resamples stay
weighted). Low-success bootstrap (≥40% failed refits) → CI reported as NaN with quality flag,
never fabricated.

## D-TI11-5 · Phenology 2.0 in the 10.0 lineage via composition, not duplication

`phenology_multi` composes existing kernels: `seasonalDecompose` (or whittaker+climatology
already inside it) → peak counting on the seasonal component proposes cycle windows (bounded
`maxCycles` default 3, ≥ `minCycleSpanDays` apart); each window is scored by the existing
`phenologyThreshold`. Cross-year windows (startDoy > endDoy spanning Dec→May) are supported by
explicit wrapped-window assignment to the **harvest year** (year of window end), extending the
existing wrapped-window algebra (`complementSeasonWindow`). Per-metric quality flags
{valid, sampleCount, gapFraction, amplitudeRatio, coverage} with hard refusal (valid=false +
flag reason) when window coverage < `minValidPerSeason` or gap fraction exceeds threshold —
no low-sample guessing (continues temporal_fit's `valid=false` convention).

## D-TI11-6 · Region work: single membership authority; extract_series polygon path moved onto it

`temporal_region_table::buildRegionGeometry` becomes the only point-in-polygon-membership
implementation for temporal operators; `rs:temporal_extract_series`'s separate polygon scan is
rewired onto it (behavior-preserving; pixel-center even-odd semantics unchanged, rotation still
rejected). `rs:temporal_region_features` gains opt-in `seasonal_breaks=true` columns
(attribution/magnitude/CI) reusing the new kernel; disabled by default → default CSV byte-identical,
sidecar schema version only bumps when the opt-in columns are on.

## D-TI11-7 · UI via existing seams only

`TemporalAnalysisDialog::kAlgorithms[]` + stacked parameter pages get the three new operators
(the dialog is the app's temporal command surface, pre-dating D18). New strings via
`QT_TRANSLATE_NOOP`/`tr()` + `SicnuDialogHelp::tip` per dialog conventions. **No** D18
mission-context/mount files are touched. Revised: the originally planned bespoke
`SpatialToolRegistry` entry is NOT added — the `REGISTER_RS_OPERATOR` seam already
auto-surfaces every new operator to the CLI/MCP/agent command vocabulary
(`RSOperatorRegistry::listSchemas()` → agent tools), and a hand-rolled parallel agent tool
would be a second surface to keep in sync. D16's in-memory `temporal:*` catalog tools remain
untouched.

## D-TI11-8 · Performance: scratch reuse with bit-exact arithmetic order; no SIMD this track

`fitSegment`/`harmonicFit` per-call heap allocations are hoisted into caller-owned scratch
structs (reused across pixels/tiles). Arithmetic order (accumulation sequence in Gram build and
Gaussian elimination) is preserved exactly → the byte-stable determinism rerun anchor
(test_temporal_algorithms) remains the regression gate. SIMD is **not** adopted: the kernels are
small-matrix (≤15×15) bound where vectorization gains are marginal, and the required
result-consistency harness would exceed the value; documented as not-executed-with-reason
(follow-up possible). Tile batching stays as-is (TemporalTileReader contract already bounds memory);
we add `estimateWorkingSetBytes` reporting to the new operators instead.

## D-TI11-9 · Naming

Operators: `rs:temporal_seasonal_breaks`, `rs:temporal_model_select`, `rs:temporal_phenology_multi`
— additive, no renames of existing operators or bands. Kernel namespace: `sicnu::temporal`
(new files `temporal_selection.{h,cpp}`, `temporal_uncertainty.{h,cpp}`; extensions to
`temporal_change.h` for the attribution kernel to keep the break machinery in one module).
Capability JSON: extend `data/agent/capabilities/temporal.json`; `capability_catalog.cpp`
familyMap only if the guard test requires temporal ops there (verified before editing).
Failure codes: only from the closed `kFailureModeCodes` vocabulary.

## D-TI11-10 · Test oracle independence

Corpus scenarios generate from closed-form truths (analytic sinusoids + step/amp/phase changes +
gaps) with a fixed-seed mt19937 *only* for noise/masking, so expected breaks/selections/flags are
computable independently of the implementation. Reference implementations in tests (e.g., direct
two-segment OLS via independent code path in the test file) are used where feasible, mirroring
the D16 `GilbertReference` pattern. No test reads results from the code under test to build its
own expectation.
