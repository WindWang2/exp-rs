# OWNERSHIP — R10-A modification surface (locked before development)

## Owned by this track (may modify)

- `src/analysis/classification/**`
- `src/analysis/segmentation/**`
- `src/analysis/hyperspectral/**`
- `src/analysis/georeferencing/**`
- `src/analysis/atmospheric/**`
- `src/operators/rs/**`, `src/operators/gdal/**`, `src/operators/otb/**`, `src/operators/opencv/**`, `src/operators/io/**`
- `src/processing/algorithms/**` — EXCEPT `temporal/temporal_workspace.cpp` (#1475 owns it)
- `src/processing/providers/**`, `src/processing/features/**`, `src/processing/contracts/**`
- `src/runtime/chunk/**` — only operator data-window contracts used by the above
- `data/processing/algorithm_meta/**`, `data/processing/toolbox_manifest.json` — existing entries' contract consistency
- `tests/**` and `benchmarks/**` files matching the above domains
  (tests/CMakeLists.txt edits: minimal increments only, serial, main-agent only)
- `.planning/r10-algorithms/**`

## Hard-excluded (other tracks' in-flight changes; read-only)

Per prompt §6, with the PR that owns each path:

- #1475: `src/processing/algorithms/temporal/temporal_workspace.cpp`,
  `src/operators/framework/model_catalog.*`, `src/operators/framework/rs_json_params.cpp`,
  `src/geospatial/fabric/query_planner.cpp`, `src/core/radiometric_state*`,
  `src/data/**`, `src/dataset/**`, `src/core/qgsdatasourceresolver.cpp`, `src/suitability/**`
- #1477: `src/operators/runtime/model_runtime.*`, `src/processing/framework/task_center.*`,
  `src/sdk/exprs/**`, `src/python/isolated/**`
- #1476: `src/app/widgets/guided_workflow_widget.cpp`, `src/app/widgets/rs_scan_pool.h`,
  `src/faultlab/**`, `src/teaching_admin/**`
- Always out of scope: `src/agent/**`, `src/app/**`, `src/help/**`, `src/teaching*/**`,
  `resources/styles*`, `src/sdk/**`, `src/python/**`

## Shared hot files (avoid or minimal-increment only)

- `tests/CMakeLists.txt`, top-level `CMakeLists.txt`, `src/analysis/CMakeLists.txt`
- Cross-domain fixes needed outside ownership → document in `INTERFACE_HANDOFF.md`, do not edit.

## Subagent seat allocation (max 4 concurrent machine-wide)

- S1 source-review/RED fixtures; S2 domain implementation (TDD); S3 test-matrix/perf;
  S4 contract research → independent reviewer (never reviews own implementation).
- Main agent: worktree integration, serial conflict resolution, final review gate, PR.
