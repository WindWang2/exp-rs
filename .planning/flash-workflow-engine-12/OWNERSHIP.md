# OWNERSHIP — flash-workflow-engine-12

## Writable (this track owns)
- `src/workflow/**` — all new + modified files
- `tests/test_workflow*`, `tests/test_pipeline*`, `tests/test_ir2*` — workflow test files
- `docs/adr/` — new ADR for the workflow kernel contract (next free number)
- `.gitignore` — append-only planning-dir exception (done)
- `.planning/flash-workflow-engine-12/**`, `.goal-loop-ledger.md` (worktree ledger)

## Read-only (other tracks own)
- `src/processing/**` (TaskCenter internals — called only via executor contract)
- `src/app/**` GUI pipeline scene (read-only; UI adaptation belongs to Workbench track)
- `src/operators/**`, `src/geospatial/**`, `src/runtime/**`, `src/dataset/**`, `src/experiment/**`

## Shared append-only (coordinate carefully)
- `tests/CMakeLists.txt` — append new test blocks only
- `src/workflow/CMakeLists.txt` — append new sources only

## Conflict hotspot
- `src/workflow/pipeline_run_coordinator.cpp` — PRs #1117–#1120 each rename the same
  `resumedDef` variable (GCC break workaround). Our canonical fix will conflict with
  whichever lands second; keep the rename minimal and isolated.
