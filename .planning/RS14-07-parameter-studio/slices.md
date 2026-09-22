# Slices — RS14-07 Parameter Sensitivity & Uncertainty Studio

Each slice: RED (failing test for missing capability) → GREEN (minimal implementation) → REFACTOR → narrow test run → commit → progress.md update.

| Slice | Content | Test target (light unless noted) | Key oracles |
|---|---|---|---|
| A | `study_spec.{h,cpp}`: dimensions, budget, strategies, metrics, validation, versioned JSON roundtrip; `sicnu_study` CMake target + `sicnu_add_study_test` helper; layer guard | `test_study_spec` | foreign `schema_version` refused; every invalid field has a distinct typed code; budget cap = `kMaxMatrixCells` refusal |
| B | `study_sampling.{h,cpp}`: ladder building; Grid via `MatrixDescriptor::enumerateCells`; OAT (baseline = ladder median rule); seeded LHS (mt19937_64 + rejection sampling + stratified permutation); `matrixCellId` extracted in `experiment_matrix` | `test_study_spec` (extended) + `test_study_sampling` | grid pointIds == matrix cellIds (cross-authority oracle); LHS same-seed identical / different-seed different; each LHS stratum hit exactly once per dimension; OAT point count = 1 + Σ(step−1) |
| C | `study_execution.h` port + `study_runner.{h,cpp}` (windowed submissions, truthful recording, ledger links, progress, cancel) + `execution_plane_study_backend.cpp` production adapter + study output commit policy | `test_study_runner` (fake backend) | in-flight ≤ maxInFlight at all times; failure/cancel recorded truthfully; submit refusal → typed run failure, no phantom success; links present for every terminal run |
| D | `study_analysis.{h,cpp}`: point aggregates from store+ledger, curves (OAT & grid slices), envelopes across replicates, Pareto via `MatrixAggregator::paretoCellIds`, declaredBest gate | `test_study_analysis` | curve slices match hand-computed aggregates; envelope min/max across seeds; Pareto equals MatrixAggregator on a grid case; no declaredBest without objectiveMetric |
| E | `study_spatial.{h,cpp}` buffer summaries (ChangeDetection kernels) + GDAL adapter (alignment/CRS checks, typed mismatch) | `test_study_spatial` | identical buffers → 0 changed; NaN handling; epsilon thresholding; mismatched grids refused typed; GDAL synthetic roundtrip |
| F | `study_export.{h,cpp}`: `sicnu.studyreport.v1` document, atomic write, `StudyRunRow` DTO projection, mechanical trend triples, agent-consumption test | `test_study_export` | report parses standalone; status accounting sums honestly (no dropped points); trend labels match constructed data; version refusal on read |
| G | replay/cancel/budget hardening: determinism across two stores, cancel mid-window, per-run timeout, budget refusal path, persistence determinism (byte-stable report for fixed inputs) | folded into `test_study_runner` + `test_study_export` | two stores → identical pointIds & parameter docs; cancel leaves only terminal-truthful statuses; report bytes stable |
| E2E | full-stack: synthetic raster → real `rs:threshold_raster` sweep via `ExecutionPlaneStudyBackend` → store → report; teaching + agent assertions | `test_study_e2e` (heavy, built once) | committed outputs exist; metrics recorded; report consumable; exemplar spec loads |

## Commit plan

1. `feat(study): planning docs (recon/plan/slices)`
2. `feat(study): study spec, budget and versioned schema (Slice A)`
3. `feat(study): deterministic grid/OAT/LHS samplers on matrix identity (Slice B)`
4. `feat(study): windowed study runner over execution port (Slice C)`
5. `feat(study): sensitivity curves, envelopes, Pareto analysis (Slice D)`
6. `feat(study): spatial difference summaries (Slice E)`
7. `feat(study): versioned study report export + UI data model (Slice F)`
8. `test(study): replay/cancel/budget hardening oracles (Slice G)`
9. `test(study): full-stack teaching e2e (NDVI threshold exemplar)`
10. `docs(study): parameter studio guide + integration wiring points`

## Build discipline

- Configure once: `cmake` (Unix Makefiles, Debug, `SICNU_LAB_PROFILE=ON`, `ENABLE_TESTS=ON`, `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`) — before adding study targets, so a background baseline build (if any) never sees half-written sources.
- `CMAKE_BUILD_PARALLEL_LEVEL=1`, `CTEST_PARALLEL_LEVEL=1`; `-j1` always, `-j2` only for the small study targets when ≥30 GB is visibly free.
- Heavy e2e target built once, before final review.
