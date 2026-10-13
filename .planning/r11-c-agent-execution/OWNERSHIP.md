# Direction C Ownership and Seam Boundaries

## In-Scope Directories and Components
- `pi/**` (Existing extensions, e.g. `pi/exp-rs-spatial.ts`)
- `src/agent/**`
- `src/agent_loop/**`
- `src/agent_ops/**` (Task recovery, journals, receipts, evidence recording)
- `src/science_context/**` (Context caching, typed failures, CRS/unit propagation)
- `src/workflow/**` (Workflow DAG execution, state transitions, journal integrity)
- `src/cli/**` (Agent and workflow execution endpoints)
- `src/sdk/exprs/**` (IPC channels, external process, plugin registry)
- `src/plugins/**`
- `src/runtime/**`
- `src/operators/runtime/**` (Model runtime, session management, VRAM accounting)
- `src/processing/framework/**` (TaskCenter, run ownership, execution planes)
- `src/python/isolated/**` (Python worker pool, process isolation, lifecycle)
- Corresponding tests in `tests/**` (e.g., `test_exprs_ipc`, `test_model_runtime_stress`, `test_execution_plane_9`, `test_runtime_python_channel_r4`, `test_agentops_*`, `test_workflow_*`)

## Out-of-Scope / Excluded Files
- `src/operators/framework/model_catalog.*` (Active in PR #1475)
- `src/core/radiometric_state.*` (Science domain, Track A)
- `src/data/**`, `src/dataset/**`, `src/geospatial/**` (Data domain, Track B)
- `src/app/**`, `src/teaching_admin/**`, `src/faultlab/**` (UI / Teaching domain, Track D)
- `CMakeLists.txt` global changes or CI workflow files (.github/workflows)

## Cross-PR Coordination
- **PR #1477** (`hardening/r9-agent-execution-reliability`):
  Avoid re-doing already solved items (IPC setEventSink reentrancy, Windows PATHEXT, record snapshot leak, same-key flight coalescing, TaskCenter catalog QPointer/destroyed connection, acquireWorker check-then-mark).
  Address remaining real residuals:
  1. `PythonWorkerProcessPool` unsynchronized writes to `isRestarting`, `crashBudgetLeft`, `restartCount`.
  2. `WorkflowRunCoordinator` happens-before between `releaseRunOwnership` and `drainPersists`.
  3. Linux test validation and regression verification.
- **PR #1475, #1476, #1478**: Zero overlap in touched source files.
