# PR & Issue Cross-Reference Matrix (Direction C: Agent Execution & Runtime)

- **Audit Date**: 2026-10-13
- **Baseline Commit**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`

## Open PRs Semantic & File Cross-Matrix

| PR # | Track | Head Branch | Touched Files / Subsystems | Direction C Overlap / Action |
|---|---|---|---|---|
| #1475 | Science/Data (R9-A) | `hardening/r9-science-data-integrity` | `src/core/qgsdatasourceresolver.*`, `src/data/*`, `src/dataset/*`, `src/geospatial/*`, `src/operators/framework/model_catalog.*` | **AVOID TOUCHING** `model_catalog.*` and data fabric files. No conflict. |
| #1476 | UI/Teaching (R9-C) | `hardening/r9-ui-teaching-test-quality` | `src/app/widgets/guided_workflow_widget.*`, `src/app/widgets/rs_scan_pool.*`, `src/teaching_admin/*`, `src/faultlab/*` | UI domain. Zero file overlap. |
| #1477 | Agent/Runtime (R9-B) | `hardening/r9-agent-execution-reliability` | `src/sdk/exprs/ipc_channel.*`, `src/sdk/exprs/external_process.*`, `src/sdk/exprs/plugin_registry.*`, `src/operators/runtime/model_runtime.*`, `src/processing/framework/task_center.*`, `src/python/isolated/python_worker_process_pool.*` | **OPEN PR**. Addresses 6 specific bugs. Left documented residuals: (1) `PythonWorkerProcessPool` unsynchronized writes on `isRestarting`/`crashBudgetLeft`/`restartCount`; (2) `test_execution_plane_9` workflow ownership release vs `drainPersists` race; (3) Linux/macOS unverified. Direction C builds on master and provides targeted hardening without colliding or duplicating #1477 solutions. |
| #1478 | UI/Help (R10-B) | `hardening/r10-ui-teaching-help-consistency` | `src/app/workflow/pipeline_editor_dock.*`, `src/app/shell/workflow_session_controller.*`, `src/teaching_admin/batch_assessment.*`, `data/help/workbenches.json` | Qt UI / Help domain. Zero file overlap. |

## Open Issues Audit (#1447–#1468)

| Issue # | Domain / Summary | Status against Master & PRs | Direction C Action |
|---|---|---|---|
| #1447 | JSON LogicError untyped access | `ALREADY_IN_MASTER` (B-side) | Preserved. |
| #1448 | Stores fail-open false success | `STILL_BROKEN` (terminal persist failure releasing ownership) | **IN_SCOPE** for C3/C4/C9 (WorkflowRunCoordinator terminal persistence & ownership happens-before). |
| #1449 | Destruction windows | `ALREADY_IN_MASTER` (registry accessors), `OUT_OF_SCOPE` (sqlite stores) | Track B domain for SQLite. |
| #1450 | Single authority drift | `ALREADY_IN_MASTER` / `IN_OPEN_PR` #1475 | Track A/B domain. |
| #1453 | Shell cluster (watchTask, scan_pool) | `ALREADY_IN_MASTER` / `IN_OPEN_PR` #1476 | UI domain. |
| #1454 | TaskCenter cluster (cancel-vs-submit race, temp-retention, logBuffer) | Sub-items 1-4 `ALREADY_IN_MASTER`, sub-item 5 in #1477 | Target remaining TaskCenter lifecycle and execution plane guarantees (C4). |
| #1455 | Operators runtime/io cluster (corpse-session, CancelToken, OTB watchdog) | Corpse session fixed in #1477; CancelToken / session management in scope | **IN_SCOPE** for C7 (Model runtime admission, CancelToken wiring). |
| #1456 | Geospatial egress / SSRF | `OUT_OF_SCOPE` (Track B) | Avoid touching geospatial egress. |
| #1457 | Data provenance / store | `OUT_OF_SCOPE` (Track B) | Data store domain. |
| #1458 | Experiment portable bundles | `OUT_OF_SCOPE` | Experiment studio domain. |
| #1459 | Workflow/runtime cluster (optional deref, error substring fallback, IO under mutex) | Sub-items in master; IO under coordinator mutex still has gaps | **IN_SCOPE** for C3 (Workflow execution reliability & lock order). |
| #1460 | CLI/SDK cluster (validateArgv, PATHEXT, batch runner) | #1460-2 in #1477; batch runner and CLI validation | **IN_SCOPE** for C11 (CLI fail-closed validation, argument hygiene). |
| #1461 | Analysis cluster | `OUT_OF_SCOPE` (Track A) | Analysis operator domain. |
| #1462 | Python/plugins cluster (zombie worker node, unknown-method IPC, duplicate plugin op) | Sub-items in master & #1477; `PythonWorkerProcessPool` residual lock-free writes remain | **IN_SCOPE** for C6 (Python worker thread safety & restart budget). |
| #1463 | Agent Ops journal/bench/recipes | Eviction dropping goal normalization, prefix-compare bench, recipe scan races | **IN_SCOPE** for C8/C9 (Agent Ops journal retention & replay idempotency). |
| #1464 | Grader / faultlab | `OUT_OF_SCOPE` | Teaching domain. |
| #1465 | Help / science context broker cache key | Broker cache key dropping typed failure cause | **IN_SCOPE** for C8 (Science context broker typed failure propagation). |
| #1466 | Teaching / suitability | `OUT_OF_SCOPE` | Teaching domain. |
| #1467 | Core fork | `OUT_OF_SCOPE` | Core / GDAL domain. |
| #1468 | CI cluster / Windows exit | `OUT_OF_SCOPE` (CI/Windows platform teardown) | Host is Linux. |
