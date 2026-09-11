# OWNERSHIP — Execution Plane 8.0

## Architectural laws honored (no exceptions taken)

- Single agent loop: Pi. This track adds none.
- Single scheduling chain: `WorkflowRunCoordinator -> TaskCenter -> JobEngine ->
  Executor/RSOperator`. WP-A only replaces the INTERNAL data structures of the
  two existing schedulers (TaskCenter admission, JobEngine queue); no new
  scheduler, no competing queue surface.
- `RSOperatorRegistry` stays the operator surface (determinism gate consults it;
  no new registry).
- Model execution stays behind `IModelRuntime` / `runModelInference`; WP-F
  model identity rides descriptor metadata, no new model runtime.
- QGIS stays the rendering/GIS engine; untouched.
- `DatasetStore` / `ExperimentStore` / DataManager: WP-F consults the catalog
  through the existing `fingerprintInputsForOperatorParams` seam.
- Geospatial I/O stays in `src/geospatial/**`; WP-F consumes
  `remote_source_validator.h` through a narrow adapter in the processing layer,
  without editing geospatial internals.
- Publication stays on OutputCommitter / governed registration seams (WP-G).
- New modules introduced (all additive, each justified because no authoritative
  equivalent exists):
  - `src/processing/framework/worker_process_guard.{h,cpp}` — OS process-tree
    containment helper (Windows Job Object / POSIX process group). No existing
    seam owns child-process containment; `worker_process_io.h` owns frame I/O
    only.
  - TaskCenter internal ready-heap + active-set counters — private members,
    no new public scheduler surface.
  - Trace emit sites inside existing transition functions — no new telemetry
    subsystem.

## Files this track expects to modify (and why)

| File | WPs | Risk of conflict with other tracks |
|---|---|---|
| `src/processing/framework/task_center.{h,cpp}` | A,B,D,H,I | core file of this track; no other 8.0 track claims it |
| `src/jobs/job_engine.{h,cpp}` | A,D | core of this track |
| `src/processing/framework/local_worker_pool.{h,cpp}`, `local_worker_host.cpp` | C | core of this track |
| `src/cli/sicnu_worker_main.cpp` | C | heartbeat emit (worker side) |
| `src/runtime/worker/worker_protocol.h` | C | additive optional op only (wire-compatible) |
| `src/workflow/workflow_run.{h,cpp}` (StepPlan stamp), `workflow_run_coordinator.cpp` | E | core of this track |
| `src/processing/algorithms/temporal/temporal_workspace.cpp` | F | fingerprint collector wiring (narrow) |
| `src/data/execution_identity_resolver.*` | F | default resolver implementation |
| `tests/test_execution_plane_8.cpp` (new), `tests/CMakeLists.txt` | all | additive |
| `.planning/execution-plane-8/*` | docs | this track only |

## Explicit non-goals

- No wire-protocol version bump (v1 stays; heartbeat is an ignorable op).
- No persistence format changes beyond ADDITIVE optional checkpoint fields
  (operator impl stamp; read fail-closed: absent ⇒ re-execute).
- No GUI files except where WP-G's audit proves a bypass that can only be
  closed at the call site (goal: zero GUI edits; prefer framework seams).
- No resurrection of superseded 5.0/6.0 branches.
