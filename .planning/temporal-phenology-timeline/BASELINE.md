# BASELINE — temporal-phenology-timeline (D16)

- **Baseline SHA**: `007e70cff6f43151aef6cf7e501c14bcb94a5090` (`origin/master`, verified `git rev-parse origin/master` == `git rev-parse HEAD` on 2026-09-14).
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-temporal-phenology-timeline` (master repo root strictly read-only during the track).
- **Branch**: `zcode/temporal-phenology-timeline` (created from `origin/master`; no prior branch with this name existed).
- **Concurrent 10.0 tracks observed** on other worktrees (uncommitted work possible there — shared-file edits stay append-only/narrow): `advanced-sar-polsar-insar-10`, `cloud-data-fabric-datacube-10`, `hyperspectral-spectral-intelligence-10`, `cn-eo-products-sensor-physics-10`, `scientific-agent-workflow-compiler-10`, `large-scale-execution-engine-10`, `eo-ai-model-runtime-foundation-10`, `professional-workbench-visual-cartography-10`, `scientific-contract-verification-10`.

## Host resources (verified at track start)

- 16 cores / 62 GB RAM; build discipline: `ninja -j2` / `CMAKE_BUILD_PARALLEL_LEVEL=2`, `ctest -j1`; drop to `-j1` when RSS > 70%. `-j$(nproc)` forbidden.
- Disk: `/` at 97% used, **27 GB free** — a full `build-dev` tree is ~14 GB (measured in `main/build-dev`). Consequence: **targeted builds only**; the D16 build plan deliberately avoids targets that pull `qgis_core` (980 cpp files) — see DECISIONS D-160-1.
- ccache present but cache full (3.0/3.0 GB) and not wired into the project launcher; treat every compile as cold.

## Verified environment facts (evidence `file:line` at baseline)

- Prior temporal platform (D10, PR #973) already ships in `src/processing/algorithms/temporal/`:
  `whittakerSmooth/whittakerSmoothRobust/savitzkyGolay` (`temporal_fit.h:31-49`), `harmonicFit` (`:60`),
  `phenologyThreshold` single season (`:79-83`), `phenologyCyclesPerYear` multi-cycle (`:105-112`),
  `piecewiseLinearTrend` BSFAST-lite (`:130-137`), `mannKendallSenSlope` Gilbert (`:170-176`), `seasonalDecompose`.
  These are **free functions** in `sicnu::temporal`; D16 adds the class-based public seams required by the D16 spec
  (`PhenologyExtractor`, `BreakpointDetector`, `TrendAnalyzer`, `SpatiotemporalFilter`) plus the temporal cube,
  widgets, and agent tools. Name-collision audit: no symbol collisions; the one overload-adjacency
  (`whittakerSmooth` 3-arg in `temporal_fit.h` vs 4-arg in new `temporal_smoothing.h`) is recorded in DECISIONS D-160-4.
- `src/geospatial/fabric/virtual_cube.h` (D-1005..D-1007) already exists as the window-read fabric cube — D16's
  `TemporalCube` is the *temporal regularization layer* above scene assets, implemented with the Qt-free
  `geospatial/raster/raster_reader.h` (`RasterReader::open/readWindow`, `raster_reader.h:96-130`).
- `rs_temporal_phenology_operator.h`, `rs_temporal_harmonic_breaks_operator.h`, `rs_temporal_sen_trend_operator.h`
  exist from D10 and stay untouched this track (single-season phenology, linear-only breakpoints there).
- Lab 8 temporal labspec already exists: `data/labs/lab8_temporal_analysis.labspec.json` (id `temporal_analysis`).
  D16 adds the courseware pair `lab8_temporal_analysis.md` + `lab8_temporal_analysis.lab.json` — coexistence rules in DECISIONS D-160-7.
- `.gitignore:119` ignores `.planning/*` with per-track whitelist; D16 adds `!.planning/temporal-phenology-timeline/` (markdown-only pattern).
- Test helper conventions: `sicnu_add_test` (tests/CMakeLists.txt:48) links the full heavy chain; the light pattern
  (`add_executable` + explicit links, tests/CMakeLists.txt:5138-5153) is what D16 tests use.
- Configure requires offline FetchContent workarounds (precedent `temporal-eo-phenology-change-10/EVIDENCE.md:13`):
  `-DFETCHCONTENT_SOURCE_DIR_PYBIND11=main/build-dev/_deps/pybind11-src -DFETCHCONTENT_SOURCE_DIR_CATCH2=main/build-dev/_deps/catch2-src`.
