# Baseline Snapshot — R12-A Science Numerics Hardening

## Git Baseline
- **Remote**: `WindWang2/exp-rs` (https://github.com/WindWang2/exp-rs)
- **Base Branch**: `master`
- **Base Commit SHA**: `28abef358217520dfbb802efdfdb11a2bf850465`
- **Worktree Path**: `/home/kevin/projects/rs-studio/exp-rs-r12-a-science`
- **Worktree Branch**: `hardening/r12-a-science-numerics`
- **Build Directory**: `/home/kevin/projects/rs-studio/exp-rs-r12-a-science/build-r12-a-science`

## Merged Predecessors (R11 & earlier)
- `#1479` (merged 2026-10-13T14:31:51Z): `hardening(r11-a): 存量遥感科学算法、数值契约与性能收敛` (b405395082 & e0d4b56b3b)
- `#1481` (merged 2026-10-13T14:32:26Z): `fix(io): harden geospatial data fabric, URI direct-open, atomic rollback, and offline network boundaries`
- `#1480` (merged 2026-10-13T14:34:22Z): `fix(agent,ipc,workflow): harden worker lifecycle, bounded queue backpressure and terminal run persistence`
- `#1482` (merged 2026-10-13T14:40:27Z): `hardening(r11-d): 存量 Qt/QGIS 工作台、交互状态、实验教学与帮助`

## Candidate Issues & Swarm Audit Items for R12-A
- **Issue #1461** (`bug(analysis)`):
  - Multiband NoData per-band sentinel in `rs_multires_segmenter.cpp:703` (partially touched in #1479, check completeness).
  - Predict-only class IDs > 255 / > 65535 in `rs_classification_pipeline.cpp:807` (widened in #1479, verify error handling on write).
  - Failed `OGR_G_Transform` silent fallback in `rs_training_data_extraction.cpp:500`.
  - Non-atomic GeoTIFF output creation in `rs_segment_map.cpp:161` (needs atomic rename via `atomic_fs`).
  - Unchecked `scaler.fit` return in `rs_object_classify.cpp:58`.
  - Missing feature-dimension check in `rs_classifier_statistical.cpp:71`.
  - Cross-layer relative include `../../processing/algorithms/math_utils.h` in `rs_segment_features.cpp:3`.
  - Dead loop and missing error channel in `rs_segment_features.cpp:260`.
- **Issue #1467** (`bug(core-fork)`):
  - Swallowed typed per-scene read failures in `src/core/temporal_cube.cpp:369`.
  - `tStart + tCount > sliceCount()` uint64 overflow check in `src/core/temporal_cube.cpp:141`.
  - Radiometric preflight URI handling in `src/core/radiometric_state.cpp:101`.
- **Issue #1450** (`quality(authority)`):
  - Single-authority radiometric vocabulary (`gamma0` vs `DigitalNumber`) across SAR operators and core.

## Constraints & Environment
- **Parallelism**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, `MAKEFLAGS=-j2`
- **Compiler Lock**: `flock /tmp/exp-rs-build-j2.lock`
- **Verification Rule**: Oracle -> Diagnose -> Minimal Patch -> RED -> GREEN -> GREEN -> Review -> PR.
