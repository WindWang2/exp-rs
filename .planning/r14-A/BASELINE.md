# R14-A Baseline (science-processing track)

- Established: 2026-10-15, from a live `git fetch origin --prune`.
- BASE_SHA: `f72f0c11731361488a384a085797aae64efaf7ee` (= origin/master at fetch time; local master identical).
- Worktree: `../exp-rs-r14-A-science-processing`, branch `hardening/r14-A-science-processing`, clean at all times between commits.
- Host capability note: `/teamwork-preview` and `/goal-loop` are not registered skills in this host (ZCode). Per the prompt's own
  degradation clause the same analyze→implement→verify→independent-review loop runs on native subagents (read-only Explore auditors,
  a non-implementer Reviewer agent) plus these file checkpoints. No skill invocation was fabricated.

## Live repository state at start

- Open PRs: #1504 (`hardening/r12-b-geodata-durability` — owns src/geospatial/**, src/data/**, src/dataset/**, src/experiment/**,
  and holds shared `tests/CMakeLists.txt`), #1505 (`hardening/r13-j-security-concurrency-fixes` — owns src/core/temporal_cube.cpp,
  src/science_context/broker.*, src/python/isolated/*, src/runtime/gpu/*, src/geospatial/remote/*; new since the prompt snapshot).
- Open issues: 31 (#1447–#1499). Track-relevant: #1450 (radiometric vocabulary double-track + authority drift), #1467
  (radiometric preflight provider URIs; NaN composites in temporal_cube).
- Recently merged into BASE_SHA: #1479 (r11-a science), #1483 (r12-a science numerics), #1501 (29-issue sweep; in-src/operators/rs it
  touched only rs_sar_calibrate_operator.cpp per-band NoData + rs_spectral_band_select_operator.cpp NoData propagation — both verified
  present in BASE_SHA), #1500/#1502/#1503 (other tracks).

## Dedup verification performed against BASE_SHA code (not PR descriptions)

- #1450 radiometric vocabulary: FIXED_ON_MASTER — `src/core/radiometric_state.h` carries the single `vocabulary::kTokens` table
  (sigma0/gamma0/beta0/sar_pair_metric/sar_texture/indices/dn/...), FSM edges extended, and
  `tests/test_radiometric_vocabulary_contract.cpp` pins writer tokens. Pending: run it green locally.
- #1467 radiometric preflight GDALOpenEx of provider URIs: FIXED_ON_MASTER — `radiometric_state.cpp` routes through
  `local_source::isPlainLocalPath` before any GDALOpenEx.
- #1467 temporal_cube catch(...): being fixed by open PR #1505 (src/core/temporal_cube.cpp in its file list) — FIXED_IN_OPEN_PR,
  out of this track's writable scope either way.
- #1501 rs fixes (sar per-band sentinel; band-select NoData propagation): verified present in BASE_SHA source.

## Build environment (this host)

- Qt 6.11.2 (system), GDAL 3.13.3 + PROJ 9.8.1 + GEOS 3.15 from `/home/kevin/pwb-sdks/root/usr`, Unix Makefiles, Debug.
- Catch2 v3.7.1 vendored via FetchContent; network fetch fails on this host (git URL-scoped proxy down), so the build reuses the
  sibling worktree's already-populated Catch2 *source* via `-DFETCHCONTENT_SOURCE_DIR_CATCH2=...` (source-only reuse; no build-tree
  sharing). Build dir: `build-r14-A` (worktree-local, never shared).
- Resource discipline: all builds under `flock /tmp/exp-rs-r14-global-build.lock`, `CMAKE_BUILD_PARALLEL_LEVEL=2`,
  `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`. Tracks B and C are live on the same lock — A queues behind them.
