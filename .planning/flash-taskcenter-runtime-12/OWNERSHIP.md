# OWNERSHIP — flash-taskcenter-runtime-12

## Writable (this track owns)

- `src/processing/framework/task_center.{h,cpp}` — scheduler core
- `src/processing/framework/task_resource_budget2.h` — wire dormant dims (header-only, additive)
- `src/processing/framework/execution_plane.{h,cpp}` — backward-compatible request/telemetry seams
- `src/jobs/job_engine.{h,cpp}` — scheduler-adjacent engine bookkeeping if needed
- `src/runtime/observability/execution_telemetry.{h,cpp}` — counters/events, append-only enum tails
- `src/processing/framework/algorithm_descriptor.h` — document new optional `execution.*` keys (additive only)
- `tests/test_taskcenter_runtime_12.cpp` — new stress/state-machine/telemetry suite
- `tests/CMakeLists.txt` — append-only test registration
- `.planning/flash-taskcenter-runtime-12/**` — track docs

## Read-only (never modify)

- `src/workflow/**` — Workflow Track owner; integrate only via public TaskCenter seams
- `src/processing/algorithms/**` — no broad algorithm changes
- `src/runtime/chunk/**`, `src/runtime/exec/**`, `src/runtime/gpu/**` — tile-level runtime is another domain; consume, don't modify
- `src/app/**`, `src/agent/**`, `src/geospatial/**`, `src/dataset/**`, `src/experiment/**`, `src/plugins/**`, `src/sdk/**`

## Shared / append-only

- `tests/CMakeLists.txt` — append at end; parallel tracks append too (keep hunk minimal, distinct context)
- `src/runtime/observability/execution_telemetry.h` — `Counter`/`EventKind` enum appends go at the tail; do not reorder existing values
- `CHANGELOG.md` — skip (avoid contention)

## Parallel tracks in flight (local worktrees)

- `glm53-mission-workbench-12`, `glm53-scientific-verification-12` — different domains; recheck `tests/CMakeLists.txt` and telemetry header overlap at PR time.
