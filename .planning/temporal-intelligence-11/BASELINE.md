# BASELINE — Phase 0 audit (2026-09-15)

## GitHub / git facts at track start (all freshly fetched; prompt-generation snapshot was stale)

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` ("fix: fail-closed fixes for review issues #994–#999 (#1000)").
  - The prompt snapshot said `ebcafb4d` with #991/#992 open — **both have since been merged into master**:
    - `c5d4aafe` D18 Unified Mission Workbench (#991)
    - `1cea9892` D19 Dataset Foundry / Benchmark (#992)
  - D16 Temporal Phenology Timeline Studio (#986, `e8c4bf43`) is in master: adds `src/core/temporal_cube.*`, `src/processing/algorithms/{temporal_smoothing,phenology_metrics,breakpoint_detection,trend_analysis,spatiotemporal_filter}.*` (namespace `sicnu::temporal::d16`, hermetic static lib), D16 timeline widgets, `temporal_spatial_tools.*` agent tools.
- **Open PRs:** only #1008 `zcode/radiometric-spectral-workbench` (spectral/radiometric Day 13; mergeStateStatus=DIRTY).
  - Changed files overlap with this track: **none** in temporal scope. Shared-file overlap: `.gitignore`, `src/agent/CMakeLists.txt`, `src/analysis/CMakeLists.txt`, `src/app/CMakeLists.txt`, `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` — I keep my shared-CMake edits append-only/minimal and expect trivial rebase conflicts at worst.
- **Open issues:** #1001–#1007 — all dataset/workflow/georef/io/agent findings (R2 review residuals). **None touch temporal.** Not in my scope; ISSUES.md (old D3 backlog T-1/T-2/T-3/C-2) is historical and already closed by Platform 10.0.
- **Concurrent local (unpushed) worktrees/branches** (verified `git diff --name-only origin/master...<branch>`): `zcode/advanced-insar-platform-11`, `zcode/cn-eo-product-physics-11`, `zcode/execution-runtime-convergence-11`, `zcode/spectral-intelligence-11` — each touches only `.gitignore` among shared files, none touch temporal files.
- Local repo `master` is stale (`007e70cf`, 65 behind origin) — irrelevant: this track's worktree branches directly from `origin/master`.

## Temporal capability on origin/master (from read-only audit; file:line in audit notes)

Already present (do not rebuild):
- Kernels `src/processing/algorithms/temporal/` (namespace `sicnu::temporal`): `temporal_fit` (SG/Whittaker/harmonicFit IRLS/phenologyThreshold/phenologyCyclesPerYear/piecewiseLinearTrend/mannKendallSenSlope/seasonalDecompose), `temporal_change` (`fitSeasonalTrendBreaks` joint harmonic+trend segments, `disturbanceOnset`), `temporal_calendar` (regularize), `temporal_stream` (TemporalTileReader, sole NoData normalization point), `temporal_region_table` (parseRegionsJson/buildRegionGeometry/RegionDateReducer), `temporal_preflight`, `temporal_monitoring`, `temporal_gapfill`, `temporal_time`, `temporal_workspace`, `temporal_collection`, STAC adapter, contracts, band roles.
- 17 optical temporal operators registered in `rs_operators_init.cpp` (incl. `rs:temporal_harmonic_breaks`, `rs:temporal_phenology` cycles≤2, `rs:temporal_extract_regions`, `rs:temporal_region_features`).
- D16 hermetic library: `BreakpointDetector::detectHarmonicBreaks` (F-test+BIC+MOSUM), `PhenologyExtractor` (dynamic threshold, double logistic, multi-cycle derivative pairing), cube, STARFM — **library-only, unwired** (ADR 0161 declares production wiring as follow-up).
- UI: `TemporalAnalysisDialog` (6 hardcoded algorithms), workbench panel/scene model.
- Agent tools: temporal collection/workspace tools.

Declared debt matching this track (ADR 0148:67-69; temporal_change.h:6-16; ARCHITECTURE_V3 §5; docs/processing/temporal.md:21):
1. **Seasonal-component break detection** (today only trend breaks of seasonality-adjusted residual).
2. **Per-segment model selection** (harmonic order fixed by caller; no AIC/BIC/CV).
3. **Uncertainty/CI** (no CI anywhere; quality weighting only in composite best-pixel).
4. **Region-table GUI** (dialog does not surface region tables).
5. Fit kernels allocate Gram matrices per call (no scratch reuse).

## Build environment facts

- VS2022 MSVC 14.38, Ninja at `C:\Qt\Tools\Ninja`, Qt 6.8.0 at `C:\deps\Qt\6.8.0\msvc2022_64`, vcpkg toolchain `C:\deps\vcpkg` (manifest mode → `build-dev/vcpkg_installed`).
- Configure: `cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH="C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install" -DCMAKE_TOOLCHAIN_FILE=C:\deps\vcpkg\scripts\buildsystems\vcpkg.cmake`
- Resource bounds enforced via `build-ti11.cmd` / `test-ti11.cmd`: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, `-j2` max.
- `.planning/temporal-intelligence-11/*.md` whitelisted in `.gitignore` (append-only, matches repo convention).
