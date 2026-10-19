# BASELINE — R10-A (algorithms / operators / processing quality)

## Remote & branch facts (collected 2026-10-13, read-only, via git + gh 2.89.0)

- Repository: `WindWang2/exp-rs` (public; gh authenticated as WindWang2, ADMIN).
- `origin/master` = `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
  ("Merge hardening/r6-digest-identity-convergence: MSVC configure unblock").
  Master HEAD has NOT moved since the prompt's 2026-10-13 warmup snapshot (identical SHA).
- Worktree: `C:\Users\wangj.KEVIN\projects\exp-rs-r10-algorithms`
  (new, created via `git worktree add -b hardening/r10-algorithm-processing-quality origin/master`
  from the shared main checkout `projects/exp-rs`, which stays untouched on master).
- Branch: `hardening/r10-algorithm-processing-quality` @ `8a0864276` (fresh, no prior owners).
- Sibling r9 worktrees (other agents' — read-only): `exp-rs-r9-science-data` (#1475),
  `exp-rs-r9-ui-teaching` (#1476), `exp-rs-r9-agent-runtime` (#1477).

## Open PRs at start (all base=master, MERGEABLE, not draft)

| PR | head | scope note |
|----|------|-----------|
| #1475 | hardening/r9-science-data-integrity | science/data integrity; touches `src/operators/framework/{model_catalog*,rs_json_params.cpp}`, `src/processing/algorithms/temporal/temporal_workspace.cpp`, `src/geospatial/fabric/query_planner.cpp`, `src/core/radiometric_state*`, `src/data`, `src/dataset`, `src/suitability` |
| #1476 | hardening/r9-ui-teaching-test-quality | ui/teaching/faultlab |
| #1477 | hardening/r9-agent-execution-reliability | sdk/exprs, python/isolated, `src/operators/runtime/model_runtime.*`, `src/processing/framework/task_center.*` |

Recently merged (context): #1469 (44 findings / 16 domains), #1470 agent/workflow,
#1471 geospatial/plugins/runtime, #1472 science/analysis closure, #1473 data/experiment,
#1474 ui/teaching, + rs14-unified-verifier and r6-digest-identity merges on master tip.

## Open issues (20): #1447–#1450, #1453–#1468

Domain-relevant mapping started in `PR_ISSUE_REVIEW_MATRIX.md`.

## Host & build environment

- Windows 10.0.26200 x64, MSVC 2022 (14.38 toolset), Ninja, CMake (Qt Tools).
- Qt 6.8.0 @ `C:\deps\Qt\6.8.0\msvc2022_64`; vcpkg toolchain @ `C:\deps\vcpkg`
  (manifest mode, per-build `vcpkg_installed`, binary cache ~800 MB);
  Catch2 source override `C:/deps/catch2-src`.
- RAM at start: 13.7 GB free of 31.2 GB (44% free) → `-j2` per CLAUDE.md cap
  (`CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`); drop to `-j1` if free RAM < ~25%.
- Disk: 519 GB free on C:.
- Build dir: `build-dev/` (Ninja, Debug, `ENABLE_TESTS=ON`), configure log `configure_r10.log`.

## Issue #1461 sub-item verification on master (do-not-repair proof)

All 8 sub-items verified FIXED_IN_MASTER on `8a0864276` (fix comments cite #1461 in-source):

| sub-item | master evidence |
|---|---|
| P1 multiband NoData first-band-only | `rs_multires_segmenter.cpp` per-band sentinel→NaN rewrite, comment cites #1461 |
| P1 >255 class ids → Byte clamp | `rs_classification_pipeline.cpp` maxClassId incl. `backend->classOrder()`, "never silently clamp" |
| P1 transform-failure raw-geometry fallback | `rs_training_data_extraction.cpp:505-512` fail-closed skip + `transformFailed` counter (file moved to classification/) |
| P1 non-atomic segment_map write | `rs_segment_map.cpp` staging + fsync + atomic publish, comments cite #1461/#1178 |
| P2 scaler.fit ignored | `rs_object_classify.cpp:62` fail-closed (file moved to segmentation/) |
| P2 statistical predict dim check | `rs_classifier_statistical.cpp:68,160` MinDistance/Mahalanobis dim guards |
| P2 cross-layer relative include | `rs_segment_features.cpp:3` now `#include "processing/algorithms/math_utils.h"` |
| P2 dead loop / no error channel | `rs_segment_features.cpp` block loop with `setExtractError` + validMask |
