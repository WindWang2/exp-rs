# EVIDENCE — temporal-intelligence-11

Append-only. Every capability claim links to a command + exit code from this worktree.

## Phase 0

- 2026-09-15 `git fetch origin --prune` → origin/master `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
- `gh pr list --state open` → only #1008 (spectral/radiometric; no temporal file overlap; shared CMake/.gitignore only).
- `gh issue list --state open` → #1001–#1007, all dataset/workflow/georef/io/agent; zero temporal scope. OUT_OF_SCOPE for this track (recorded, not acted on).
- Read-only audit of all temporal kernels/operators/tests/docs on origin/master (5.6M-token Explore agent, read-only) → BASELINE.md.
- Concurrent local branches checked: `git diff --name-only origin/master...<branch>` per branch → only `.gitignore` shared; PARALLEL_OWNERSHIP.md.
- Configure (background, `configure-ti11.cmd`, exit 0 pending verify): preset-equivalent flags + `CMAKE_PREFIX_PATH=C:/deps/Qt/6.8.0/msvc2022_64;C:/deps/qca-install;C:/deps/kc-install` + vcpkg toolchain + winflexbison. First attempt failed (Qt6 missing → prefix path), second (BISON missing → `C:/deps/winflexbison`); both environment-discovery issues, not code.

## OUT_OF_SCOPE findings

- Issues #1001–#1007 (dataset/workflow/georef/io/agent fail-open residuals): real but owned by other tracks; not touched.
- Two coexisting BFAST-like kernels (`temporal_change` wired, D16 `breakpoint_detection` library-only): consolidation is a cross-lineage refactor beyond this track's ownership; D-TI11-1 records the reasoning; PR_BODY follow-up.

## Phase 1-4 implementation (commits 5fea5cee, 5bf20bd8)

- New kernels: `temporal_selection.{h,cpp}` (attribution + bounded selection),
  `temporal_uncertainty.{h,cpp}` (analytic CI + seeded bootstrap),
  `temporal_design_detail.h` (shared harmonic+trend basis — single authority with
  `temporal_change.cpp`, whose local copy was deleted), `phenologyMultiCycle` in
  `temporal_fit.{h,cpp}` (composition of seasonalDecompose + phenologyThreshold).
- New operators registered: `rs:temporal_seasonal_breaks`, `rs:temporal_model_select`,
  `rs:temporal_phenology_multi` (`src/operators/CMakeLists.txt` + `rs_operators_init.cpp`).
- Surfaces: `data/agent/capabilities/temporal.json` (+3 entries), capability sidecars
  `rs-temporal-{seasonal-breaks,model-select,phenology-multi}.json` (schema v2),
  `capability_catalog.cpp` familyMap (+3), `docs/processing/temporal.md` (+3 rows),
  CHANGELOG section, `TemporalAnalysisDialog` (+3 algorithms with parameter pages).
- Package E delta: `rs:temporal_extract_series` polygon membership rewired onto
  `temporal_region_table::buildRegionGeometry` (verified implementation parity first:
  identical map-coordinate even-odd pixel-center predicate).
- Package G delta: `temporal_change.cpp` fitSegment and `temporal_fit.cpp` harmonicFit use
  thread-local/reused scratch — per-pixel Gram allocations removed; accumulation order
  unchanged (bit-exact anchors are the regression gate).
- Package F decision: no bespoke SpatialTool; operators auto-surface to CLI/MCP/agent via
  `RSOperatorRegistry` (D-TI11-7 revised).
- Package E follow-up (descoped): per-region seasonal-break columns on
  `rs:temporal_region_features` (D-TI11-6) — needs per-region series retention; recorded as
  PR_BODY follow-up.

## not-executed items (with reason)

- SIMD kernels (package G): not adopted — the temporal fits are ≤15×15 small-matrix bound;
  a result-consistency harness would exceed the value at this track's scope (D-TI11-8).
- Region-features seasonal columns (package E extension): descoped, see D-TI11-6.
- Per-60s CPU/RSS sampling during builds: no portable per-process sampler in this Git Bash
  host (recorded once per envelope rule; `-j2` cap maintained, monitored via compiler process
  count / library milestones).
