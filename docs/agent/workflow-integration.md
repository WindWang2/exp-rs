# Workflow Integration & Execution State Model (Harness 4.0)

## One authoritative engine

Agent plans never create a second workflow engine. The compile chain:

```
AgentPlan v2 (JSON, schema_version 2.0)
  → compilePlanToWorkflowJson()      src/agent/harness/agent_plan.cpp
  → WorkflowDefinition JSON
  → WorkflowRunCoordinator::startTrackedPipelineJson()
  → TaskCenter::submitPipeline()     (admission, fingerprints, cache)
  → RSOperator::run(params, ctx)     (the JSON param/result seam)
```

`WorkflowSession` (interactive wizard mode) is untouched; plan execution is
engine-scheduled, never LLM-scheduled. Multi-step plans get no unbounded
concurrency: TaskCenter's profile caps, RSS watermark, and RAM budget (ADR
0063 + the resource budget) own admission.

## Execution state model

Workflow Engine 2.0 run states (ADR 0123, `src/workflow/workflow_run.h`):

```
Created → Planning → Ready → Running → Completed
                          ↘ WaitingResource (admission hold, re-admitted)
        any non-terminal → Cancelling → Canceled
        Running → Failed (step failure rolls up)
        Interrupted (crash/startup recovery; resumable)
```

- **Persistence**: atomic per-transition checkpoints (`~/.rs_studio/
  checkpoints`, override via `WorkflowRunCoordinator::setCheckpointDirectory`),
  completed runs archived (50 kept).
- **Ownership**: one cross-process flock per executing run; a resume while
  another process owns the run is refused with the owner pid.
- **Resume**: only Interrupted/Failed/Canceled runs; completed steps whose
  output file still exists and is non-empty are NOT re-executed — their
  artifacts resolve into downstream `$step.port` placeholders.
- **Deterministic cache**: RFC 8785-canonicalized parameter fingerprint
  (SHA-256); identical steps serve cached outputs transactionally.
- **Cancel**: `TaskCenter::cancelPipeline` cascades to descendants; run rolls
  up to Canceled; scratch outputs are cleaned.

## Agent-facing observation

- `harness:execute_plan` → `{run_id, pipeline_id, status, next}`.
- `harness:run_status {run_id}` → state, progress, per-step status (incl.
  `cache_hit`, outputs, `task-<id>` execution ids), and the automatic
  verification block once terminal — see `result-verification.md`.
- `get_workflow_status` / `resume_workflow` (MCP meta tools) remain the
  generic surfaces over the same coordinator; `run:compare`, `result:inspect`,
  and `lineage:*` expose the governance view.

## Plan schema (v2)

```json
{
  "schema_version": "2.0",
  "kind": "execution_plan",
  "plan_id": "plan-harness.optical_vegetation",
  "goal": "Optical vegetation index map",
  "intent": "ndvi",
  "inputs":  [ { "name": "primary", "ref": "asset-3" } ],
  "steps":   [ { "id": "ndvi", "operator_id": "rs:spectral_index",
                 "params": { "input": "...", "index": "NDVI", "output": "..." },
                 "verification": "raster" } ],
  "outputs": [ { "name": "ndvi", "from_step": "ndvi", "port": "output",
                 "kind": "raster" } ],
  "verification": { "enabled": true },
  "map_output": { "from_step": "ndvi", "renderer_hint": "singleband_pseudocolor" }
}
```

v1 documents (3.0 `ExecutionPlan`: steps + wiring only) are accepted and gain
defaults — one reader, one compiler, two schema versions. Structural
validation (unique step ids, referential integrity, operator existence,
verification policy vocabulary) is a precondition for compilation.
