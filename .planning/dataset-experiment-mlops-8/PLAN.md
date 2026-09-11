# PLAN — implementation order

## M1 — Bridge core in sicnu_experiment (WP-A)

1. `run_recorder.{h,cpp}`: add `markInterrupted(runId, note)` (additive;
   truthful Interrupted, note stored in metrics doc).
2. `run_bridge.{h,cpp}`:
   - `ExecutionEvent` struct (closed state vocabulary).
   - `RunPins` struct + `attachPins(ref, pins)` / `setWorkflowPins(wfId, pins)`
     merge-upsert (pre/post Running safe).
   - `ExperimentRunBridge`:
     - ctor(`ExperimentStore&`, optional `DatasetStore*`).
     - `ensureExperiment(id, name, objective)` — idempotent create for opt-in.
     - `handleExecutionEvent(event)` → recorder transitions (D3 mapping;
       idempotent terminal re-delivery tolerated).
     - `reconcileStale(liveRefs, checkpointPolicy)` (D5; checkpoint
       snapshots supplied by the adapter layer — the core stays workflow-free).
     - `runIdForExecution(ref)`.
3. Tests: `tests/test_mlops8_bridge.cpp` (light target, experiment+dataset
   only): lifecycle truth matrix, resume continuation, duplicate delivery,
   unknown state refusal, pins merge timing, ensureExperiment idempotency,
   stale decisions (live untouched / failed→Failed / canceled→Cancelled /
   interrupted→Interrupted / missing→reported only), read-only store
   behavior.

## M2 — Workflow adapter target (WP-A)

1. `src/experiment/bridge/workflow_experiment_adapter.{h,cpp}`:
   - `workflowRunToExecutionEvent(const WorkflowRun&, state, startedMs,
     finishedMs)` — pure conversion incl. bounded step summaries + artifact
     digests (D2/D8).
   - `WorkflowExperimentMonitor` (QObject): attach(coordinator&), queued
     handler, pins registry delegation, `enableRecording(storePath, experiment
     ..., datasetStorePath)`, `flush()`.
2. `src/experiment/bridge/CMakeLists.txt` → `sicnu_experiment_bridge`
   (PUBLIC: Sicnu::experiment + sicnu_workflow + Qt6::Core; AUTOMOC).
   Root CMakeLists: add_subdirectory AFTER src/workflow.
3. E2E tests: `tests/test_mlops8_e2e.cpp` (mirror test_workflow_run_coordinator
   harness): success pipeline → Completed run with artifacts+digests; failing
   executor → Failed with error evidence; cancel → Cancelled; resume →
   Interrupted then Completed on the SAME experiment run id; unknown-state
   event ignored-with-error; monitor disabled ⇒ zero writes.

## M3 — MCP opt-in surface (WP-I slice)

1. `data_platform_tools`/`mcp_server`: optional run_workflow recording args
   (D6); process-wide monitor accessor; flush on server stop (if a stop hook
   exists — else best-effort flush after each tracked run).
2. Surface test: extend the existing data-platform surface test OR a new
   bounded case exercising the arg plumbing (store inspectable afterwards).

## M4 — Docs + catalog sync

1. `docs/adr/0143-workflow-experiment-auto-recording.md`.
2. `docs/experiments/auto-recording.md` + docs/experiments/README.md +
   CHANGELOG.md entry.
3. DOCS_LEDGER.md updated; help/catalog drift tests untouched unless the
   tool catalog changes shape (dataPlatformToolDefs gains nothing if run_workflow
   args are plain QVariantMap — verify; if tool defs change, update drift tests).

## M5 — Adversarial review + remediation

Two read-only subagents (budget cap): architecture/correctness + tests/
performance/portability. Findings → REVIEW_LOG.md; P0/P1 fixed; P2 fixed;
P3 fixed or justified; affected tests re-run.

## M6 — Integration + PR

Sync master, full diff inspection, bounded test sweep
(dataset_core, split_leakage, experiment_evaluation, platform7 library +
surface, workflow coordinator, mlops8 bridge + e2e, data_platform_surface),
FINAL_REPORT.md, push, PR.
