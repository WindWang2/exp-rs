# DECISIONS — temporal-phenology-timeline (D16)

Unattended mode (`autonomy=full`): all architecture trade-offs are recorded here, no user interruptions.
Numbering continues the temporal-track convention (D-16x = D16 track).

## D-160-1 · New lightweight static library `sicnu_temporal_timeline`; no `qgis_core` pull

**Context**: disk has 27 GB free; a full heavy build-dev tree is ~14 GB; `qgis_core` alone is 980 cpp files
(`find src/core -name '*.cpp' | wc -l`). `sicnu_processing` PUBLIC-links `qgis_core`
(`src/processing/CMakeLists.txt:235`), and `sicnu_agent` links `qgis_core` + `sicnu_processing`
(`src/agent/CMakeLists.txt:179,183`). Any test linking those forces a multi-hour, multi-GB core build.

**Decision**: D16 sources compile into a new STATIC target `sicnu_temporal_timeline` declared in
`src/processing/CMakeLists.txt` (the closest sibling module that already owns `algorithms/temporal/*.cpp`).
Sources (all D16-spec seam paths): `src/core/temporal_cube.cpp`, `src/processing/algorithms/temporal_smoothing.cpp`,
`phenology_metrics.cpp`, `breakpoint_detection.cpp`, `trend_analysis.cpp`, `spatiotemporal_filter.cpp`,
`src/agent/spatial_tools/temporal_spatial_tools.cpp`, `src/agent/tools/temporal_tool.cpp`,
`src/app/widgets/timeline_scrubber_widget.cpp`, `src/app/workbench/temporal_timeline_widget.cpp`.
Links: `Qt6::Core/Gui/Widgets`, `GDAL::GDAL`, jsoncpp, `Sicnu::Geospatial` (Qt-free foundation —
`RasterReader` is the canonical pixel access path, ADR 0130/0134 lineage).
D16 tests link `sicnu_temporal_timeline` directly (light pattern of `tests/CMakeLists.txt:5138`).
Production wiring (operators/app consuming this library) is a declared follow-up, not D16 scope.
**Why not alternatives**: (a) compiling into `sicnu_processing` would force qgis_core into every D16 test build —
violates the hardware redline economics; (b) a new module directory would break the spec-mandated file paths;
(c) header-only would drag GDAL includes into a public core header — unacceptable layering.

## D-160-2 · `TemporalCube` layering: seam header in `src/core`, implementation via `Sicnu::Geospatial`

The D16 spec fixes the seam header at `src/core/temporal_cube.h` (namespace `sicnu::temporal`). The class is
declared there; its implementation (`src/core/temporal_cube.cpp`, compiled into `sicnu_temporal_timeline`)
reads pixels exclusively through `geospatial/raster/raster_reader.h` `RasterReader` (window reads, canonical
metadata, NoData handling). No GDAL API appears in any public D16 header.

## D-160-3 · Scene acquisition dates: filename parsing contract

`TemporalCubeConfig` carries no per-scene dates, so `open()` derives each scene's instant from its file name
using a self-contained ISO parser accepting `YYYY-MM-DD`, `YYYYMMDD`, and `YYYYDDD` (first match wins;
`QDate` validity-checked). Scenes without a parseable date produce a typed refusal at open (documented in the
header, honest failure — no silent guessing). This mirrors the repo's existing filename-parsing precedent in
`processing/algorithms/temporal/temporal_time.h` without linking that library.

## D-160-4 · Overload adjacency between `temporal_fit.h` and `temporal_smoothing.h`

Both headers live in `sicnu::temporal`. Existing `whittakerSmooth(y, w, λ)` (3-arg, `temporal_fit.h:31`) vs
new `whittakerSmooth(y, w, λ, d=2)` (D16 spec signature). Including both headers in one TU makes a 3-arg call
ambiguous. **Rule**: D16 translation units (all new tests, e2e) include only `temporal_smoothing.h`;
`temporal_fit.h` consumers are untouched. Documented here and in the header comment of `temporal_smoothing.h`.

## D-160-5 · Whittaker solver: banded Cholesky, d ∈ {1,2}; Gilbert erratum in D16 spec §E

- d=2 → pentadiagonal SPD system solved by banded Cholesky in O(n) (Eilers' perfect-smoothing formulation);
  d=1 → tridiagonal Thomas; other d → empty return (documented).
- **Spec erratum (Package E)**: the D16 text asserts S = 43, Var(S) = 124.6667, Z = 3.7616 for the Gilbert (1987)
  case. From the definitions: the series {10,15,14,20,25,25,27,30,32,35} has 45 pairs, one tie pair (25,25 → 0),
  exactly one negative pair (15 → 14), so **S = 42**; the tie-corrected formula gives
  Var(S) = [10·9·25 − 2·1·9]/18 = **124.0** exactly; z = (S−1)/√124 = 41/√124 = **3.6818**;
  p = erfc(3.6818/√2) ≈ 2.1e-4. The spec's numbers form an internally consistent but hand-arithmetic
  erratum chain (its Z matches 42/√124.6667). Writing those numbers into tests would encode a false reference
  (anti-tautology rule cuts both ways: expected values must come from the authoritative formula).
  D16 tests assert the closed-form values (S 42, Var 124.0, Z 3.6818, p < 0.001, significant at 0.01),
  preserving every spec-intended property (tie correction exercised, strong significance). Expanded from the
  first cut of this decision during slice 2 when the reference loop exposed S = 42, not 43.

## D-160-6 · BFAST: greedy RSS breakpoint search + exact parametric significance (no asymptotic MOSUM bands)

- Joint harmonic + piecewise-linear regression per segment (T = 365.25 d, m = 3 harmonics), Bai–Perron style
  greedy splitting at the RSS-minimizing τ with `minSegmentSamples` bounds.
- **Significance**: per-break Chow-type F test converted to an exact p-value via the regularized incomplete
  beta (F CDF) — self-contained, exact for small n (n≈92), no asymptotic tables needed. The OLS-MOSUM moving-sum
  statistic (h = ⌊0.15·n⌋) is still computed and reported as the structural-stability screen (sup |M_t| emitted in
  `BreakpointCandidate.magnitude` context / diagnostics), but the accept/reject gate is the F-test p-value at
  `significanceAlpha`. **Why**: asymptotic MOSUM boundary-crossing constants (Andrews λ_α) are table-driven,
  normalization-dependent, and not reproducible offline; the F route keeps every claim closed-form and testable
  against textbook distributions.
- **BIC penalty** for model selection: BIC = n·ln(RSS/n) + p·ln(n); a break is kept only if BIC strictly decreases.

## D-160-7 · Lab 8 coexistence: courseware pair added, existing labspec untouched

`data/labs/lab8_temporal_analysis.labspec.json` (id `temporal_analysis`, D3a track) stays untouched. D16 adds
`data/labs/lab8_temporal_analysis.md` (中文 courseware, 4×25-point grading rubric) +
`lab8_temporal_analysis.lab.json` carrying a **distinct lab id** `temporal_phenology_timeline` so no lab
registry id-collides. The 100-point grading is executable headless via
`tests/test_d16_temporal_phenology_e2e.cpp` (TEST_CASE `Lab08 auto-grading`), keeping the manifest question
(the `.lab.json` legacy loader) isolated from the grading path.

## D-160-8 · Gap guard semantics (Package A)

A regularized grid value is NaN iff (a) no valid (unmasked) observation inside the ±W window (W = `maxWindowDays`),
or (b) the raw observations bracketing the grid center are more than 45 days apart (true acquisition hole —
interpolation would be fabrication). Cloud masking: scene band 2, when present, is the per-pixel cloud fraction
(0 = clear, 1 = opaque); single-band scenes are treated as clear. Compositing: BestPixel (max Q_i) or WeightedMean
per `TemporalCubeConfig` strategy; Q_i = (1−cloud_i)·exp(−(t_i−t_k)²/2σ_t²), σ_t = W/2.

## D-160-9 · Agent tool data path: injectable series provider

`TemporalSpatialTool` executes against a registered in-memory catalog
(`ingestSeries(metric, lon, lat, tDays/years, values)`, 0.25°-quantized keys); `toolSchema()` follows the
OpenAI/Anthropic function-calling JSON shape used by the repo's existing spatial tools (`src/agent/spatial_tools/`
ADR 0122 lineage). Self-consistency pre-checks: coordinate range, EOS<SOS reversal, non-finite series →
structured refusals (`status:"rejected", reason:"..."`), never hallucinated numbers.

## D-160-10 · Test naming fixed by the acceptance ctest filter

Acceptance runs `ctest -R "test_d16_|test_whittaker|test_bfast|test_phenology|test_virtual_cube"`. Test targets:
`test_virtual_cube_memory` (A), `test_whittaker_smooth` (B), `test_phenology_extraction` (C),
`test_bfast_harmonic_breaks` (D), `test_d16_temporal_trend` (E unit), `test_d16_starfm` (F),
`test_timeline_scrubber_widget` + `test_temporal_profile_widget` (G), `test_temporal_agent_tools` (H),
`test_d16_temporal_phenology_e2e` (I, includes Lab08 grading). All run under `QT_QPA_PLATFORM=offscreen`, `-j1`.

## D-160-4 (revised in review) · Nested namespace for the D16 smoothing seam

The original "never include both headers in one TU" rule could not prevent the
link-time hazard: `whittakerSmoothRobust(y,w,λ,iters)` and
`savitzkyGolay(y,window,degree)` had IDENTICAL signatures and mangled names in
`temporal_fit.cpp` (shared sicnu_processing) and `temporal_smoothing.cpp`
(static sicnu_temporal_timeline) — ELF interposition would silently bind one
library's calls to the other implementation once both link together. All three
D16 smoothing kernels now live in the nested namespace `sicnu::temporal::d16`
(mangling-distinct, spec function names preserved); both headers may coexist
in one translation unit.

## D-160-11 · Spec-mandated namespaces retained (convention deviation noted)

The D16 spec fixes `namespace sicnu::gui` (timeline widgets) and
`namespace sicnu::agent` (temporal tools); repo convention would prefer
`sicnu::app` and `sicnu::agent::spatial_tools`. The spec-mandated names are
kept for this track; normalization is a mechanical follow-up before workbench
wiring.

## D-160-12 · Review remediation ledger (Phase 6)

- P0: BestPixel admitted Q=0 (fully-clouded) observations via the −1 sentinel —
  gate now requires q > 0 for both policies; all-clouded-window NaN test added.
- P1: LOS cross-year double-count (+365 on the absolute axis) removed —
  `los = eos − sos` always; header contract rewritten; cross-year test added.
- P1: trend_analysis invalid path now NaN-fills every field per header contract.
- P1: STARFM fallback now implements the documented two-level contract
  (base + mean coarse delta of all valid window cells, else NaN); previously
  unreachable dead branch removed.
- P1: agent climatology skips non-finite months; non-finite target month is a
  structured rejection (anti-hallucination contract).
- P1: `std::clamp` UB in the scrubber at tiny widths guarded; TemporalCube
  rejects complex pixel types at open and treats read failures as typed
  non-contributions.
- P2 (applied): rssAtSplit dead code, nearest-valley wording, MAD even-count
  median, refit-failure hygiene (no stale partial output), profile axis-range
  caching, CrossingTruth edge constant 708→352, GDAL PUBLIC redundancy, misc
  comment/unit fixes, DECISIONS source-list drift.
- P2 (accepted as-is): spec-mandated `numClasses` field kept (documented as
  unused by the single-pair kernel); wall-clock redline tests kept with the
  generous margins the spec prescribes; GilbertReference kept (the
  42/124/3.6818 hand anchors carry independence).
