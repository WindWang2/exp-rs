# OWNERSHIP — ds41-workflow-durability-13

## Writable (this track owns)
- `src/workflow/pipeline_run_coordinator.{h,cpp}` — identity mode, cancel-aware hashing,
  affinity-marshalled mutators, destructor hardening, D17 provenance attempt layout.
- `src/workflow/workflow_run.{h,cpp}` — lineage envelope (attempt / resumeOf), serialization
  version 2 with legacy-1 acceptance.
- `src/workflow/workflow_checkpoint.{h,cpp}` — content-aware checkpoint election.
- `src/workflow/workflow_run_coordinator.{h,cpp}` — ONLY the two lineage seams: optional
  `resumeOf` parameter on `startTrackedPipeline` and the attempt increment after the resume swap.
- `src/workflow/workflow_provenance.{h,cpp}` — only if the attempt-layout change needs a helper.
- Tests: `tests/test_workflow_checkpoint_cache.cpp` (D17 lane, extend),
  `tests/test_workflow_recovery.cpp` (Engine-2.0 lane, extend),
  new light target `tests/test_workflow_durability_13.cpp` (Engine-2.0 envelope/election/fuzz,
  compiles workflow_checkpoint.cpp + workflow_run.cpp + workflow_run_lock.cpp +
  workflow_definition.cpp + fault_registry.cpp directly — no qgis/task_center chain).
- `tests/CMakeLists.txt` — append-only (new target block + append to existing blocks).
- `docs/adr/` — one new ADR for the durability contract (next free number).
- `.gitignore` — append-only planning-dir exception for this track.
- `.planning/ds41-workflow-durability-13/**`, `.goal-loop-ledger.md`.

## Read-only (other tracks own)
- `src/processing/**` except the two seams above (TaskCenter admission, execution plane).
- `src/app/**`, `src/operators/**`, `src/geospatial/**`, `src/runtime/**` (except compiling
  fault_registry.cpp into the new test target — read-only source reuse, no edits),
  `src/dataset/**`, `src/experiment/**`, `src/agent/**`, `src/sdk/**`.

## Shared append-only
- `tests/CMakeLists.txt` — new blocks only; the tail is a known textual conflict zone with
  sibling PRs (#1123-style append-only convention).

## Conflict hotspots
- `src/workflow/pipeline_run_coordinator.cpp` — historically touched by several parallel PRs
  (the `resumedDef` GCC break, all now merged). This track's edits are in the fingerprint
  helper, onNodeFinished, requestCancel, mutators and the destructor — no overlap with the
  merged hunks.
- `src/workflow/workflow_run.{h,cpp}` — serialization version bump; any concurrent PR adding
  checkpoint fields would conflict. None open.
