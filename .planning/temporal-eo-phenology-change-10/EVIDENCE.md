# EVIDENCE — temporal-eo-phenology-change-10

Policy: every capability claim maps to a local command + exit code or is
explicitly `not-executed`.

## Phase 0

- `git fetch --all --prune && git checkout master && git pull --ff-only` → exit 0; `origin/master` = `7d78059d1a6d316d606656759a506d17bc5e3b55`.
- `git worktree add ../exp-rs-temporal-eo-phenology-change-10 -b zcode/temporal-eo-phenology-change-10 origin/master` → exit 0.
- `git check-ignore -v .planning/temporal-eo-phenology-change-10/BASELINE.md` → matched `!` rule; `git add -n` confirms trackability (exit 0).
- `gh pr list --state open` → empty (no open PRs; no conflict).
- Dedupe: temporal PRs #712/#732/#733/#738/#742 all MERGED → not re-implemented.
- CMake configure #1 (`cmake --preset dev-default`) → exit 1: pybind11 FetchContent download failed (offline network). Configure #2 with `-DFETCHCONTENT_SOURCE_DIR_PYBIND11=/home/kevin/projects/rs-studio/main/build-dev/_deps/pybind11-src` → exit 0 ("Generating done"). Recorded as D10.

## Phase 1-5 (implementation + verification)

- `cmake --build build-dev --target <7 baseline temporal test targets> -j2` → first run EXIT=2:
  (a) my rs_temporal_extract_regions/region_features operators referenced undeclared
  constants (fixed, commit 544faaa86d); (b) GCC internal compiler error (segfault in
  dwarf2out / qdebug.h) on `src/experiment/bridge/lab_run_recorder.cpp` and
  `src/gui/qgspropertyoverridebutton.cpp` under host load ~16 (concurrent track builds).
- Retry at `-j1` → EXIT=0, all 7 baseline targets built. ICE judged load-induced (different
  file each run, clean retry unchanged source).
- `cmake --build build-dev --target test_temporal_calendar test_temporal_change
  test_temporal_regions test_temporal_operators_10 benchmark_temporal10 -j2` → EXIT=0.
- New tests (QT_QPA_PLATFORM=offscreen, serial):
  `test_temporal_calendar` 76 assertions / 9 cases PASS;
  `test_temporal_change` 34 / 5 PASS;
  `test_temporal_regions` 46 / 4 PASS;
  `test_temporal_operators_10` 291 / 6 PASS.
- Baseline temporal regression (same session): core 367, fit 162, algorithms 791,
  workspace 267, agent_tools 234, spatiotemporal_contracts 103 — ALL PASS.
- `test_sar_temporal_stats`: not built in this tree's targeted batch → `not-executed`
  (SAR kernels are another track's ownership; scheduled into the final verification batch).
- `benchmark_temporal10 --out benchmarks/temporal10.json` (full tier) → EXIT=0; numbers in
  PERFORMANCE.md.

## OUT_OF_SCOPE findings

- `test_temporal_scene_model` "renders QA and unknown values truthfully" fails on master
  state too (files `src/app/workbench/temporal_scene_model.*` and the test are untouched by
  this track — `git diff --name-only origin/master...HEAD | grep app/` is empty). QA cloud
  rendering lives in the workbench-9 / app layer: recorded here, not fixed in this track.
