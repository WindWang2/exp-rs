# OVERLAP MAP — parallel-track conflict avoidance

## Active tracks at baseline and their cores

| Track (branch) | Core directories | Relation to us |
|---|---|---|
| execution-concurrency-lifecycle-9 | `src/processing`, `src/app`, `src/workflow`, TaskCenter/JobEngine, help/operators remediation (#848–#882 wave) | Shares `src/workflow/workflow_run_coordinator.*` as a seam (CLI auto-record). We keep additive-only edits there; their defect wave owns the file otherwise. |
| geospatial-data-fabric-9 | `src/geospatial` | Disjoint from us (we consume datasets through stores). |
| scientific-algorithms-9 | `src/processing` | Disjoint. |
| (uncommitted edits in MAIN worktree) | same files as exec wave incl. `src/dataset/split.cpp` | NOT authoritative here. We branch from origin/master; #875 is fixed on OUR branch per ISSUE_TRIAGE. Rebase-time check: if their split.cpp fix lands first, keep ours as the superset (ours adds the full validation matrix + tests; a one-line `blockSize > 0` guard is a strict subset). |

## Known conflict points and resolution

1. `src/dataset/split.cpp` — main-worktree uncommitted edits exist. Risk:
   merge conflict when the exec track lands #875's fix. Resolution: our M0
   is the comprehensive superset (validation matrix, NaN/overflow guards,
   degenerate-fold refusal, spatiotemporal protocol, regression tests).
   At rebase we resolve in favor of the union, keeping our typed
   diagnostics and tests.
2. `src/workflow/workflow_run_coordinator.{h,cpp}` — CLI auto-record wiring
   (8.0 documented follow-up). Policy: additive hook only; if the exec
   track rewrites the coordinator heavily, defer CLI auto-record to a
   follow-up rather than双边重写 (goal §1.4). Decision recorded in M3/M5
   planning notes.
3. `CMakeLists.txt` (root + src/dataset + src/experiment + tests) —
   additive registration only; land in the same commits as the code they
   register (keeps conflicts textual and tiny).
4. `CHANGELOG.md` — single entry at the end.

## What this track does NOT duplicate

- No second execution scheduler / runner (matrix runs go through
  WorkflowRunCoordinator submissions).
- No second geospatial I/O path (bundle/availability checks go through
  existing stores and artifact stores).
- No second model registry (M8 is an evidence seam over the existing
  model catalog interface).
- No GUI work (8.0's panel already surfaces recorded runs).
