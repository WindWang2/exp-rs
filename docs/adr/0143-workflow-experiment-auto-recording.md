# ADR 0143: Workflow → Experiment auto-recording bridge

Date: 2026-09-10. Status: Accepted. Relates to ADR 0130 (data-plane
truthfulness), 0137 (experiment run identity), 0138 (lineage/bundles).

## Context

The 7.0 platform shipped `ExperimentRunRecorder` as the callable seam between
the execution plane and `ExperimentStore`, but **nothing in the execution
plane calls it**: experiment records were created only by tests or manual
CLI/MCP bookkeeping. The 7.0 final report names "recorder workflow wiring" as
its top known limitation. Meanwhile every tracked workflow run already
persists truthful lifecycle truth (checkpoints, per-step completion identity,
cross-process locks) on the `WorkflowRunCoordinator` seam, which emits
`runStateChanged` — the signal the governance mirror (`WorkspaceService::
recordRun`, issue #754) already consumes.

## Decision

1. **The bridge is a consumer, never a participant.** Recording attaches to
   the authoritative coordinator lifecycle via the existing signal + public
   read APIs. The coordinator does not learn about experiments; no second
   scheduler exists; the bridge cannot execute anything.

2. **Two layers, dependency-inverted.**
   - `sicnu_experiment` gains `ExperimentRunBridge` (`run_bridge.{h,cpp}`): a
     state machine over a neutral `ExecutionEvent` (closed state vocabulary:
     Running/Completed/Failed/Canceled/Interrupted) driving the recorder. It
     knows nothing about workflows, keeping the science core free of the
     execution stack.
   - A new leaf target `sicnu_experiment_bridge`
     (`src/experiment/bridge/workflow_experiment_adapter.{h,cpp}`) links
     `Sicnu::experiment` + `sicnu_workflow`: a pure `WorkflowRun →
     ExecutionEvent` conversion plus a `WorkflowExperimentMonitor` QObject
     (queued signal consumer). Consumers are opt-in surfaces only.

3. **Opt-in recording of selected governed executions.** A monitor records
   nothing until an explicit surface enables it with a store path + target
   experiment (MCP `run_workflow` recording arguments today). Default
   behavior is byte-identical to an unrecorded platform.

4. **Pins at submission.** The store enforces identity-pin immutability
   once a run started; the enabling surface therefore passes pins with the
   synchronous `recordSubmission()` call — bound to that run, immune to
   queued-signal reordering or later submissions of the same workflow.
   Post-start pin attachment with different identity is an honest typed
   refusal (`experiment.bridge_pins_late`), never a silent drop or a
   rewrite.

5. **Truthful interruption and stale reconciliation.** `Interrupted` is a
   first-class non-terminal record (`markInterrupted`); a resumed execution
   re-enters Running on the SAME record (the coordinator swaps resumed
   submissions back under the original run id). At enable time the monitor
   reconciles recorded-but-non-terminal runs against checkpoint evidence:
   dead executions close Failed/Cancelled per their checkpoints; Interrupted
   checkpoints stay resumable; completed checkpoints are reported, not
   closed (no fabricated output evidence); executions owned by ANY live
   process (flock probe) are never touched.

6. **Bounded evidence.** The event carries step summaries (id, operator,
   status, error, output path/size/digest), the definition snapshot and
   artifact identity — capped (≤256 steps, ≤512 JSON members). Full resolved
   parameters stay in the checkpoint; the experiment record cites them.

## Consequences

- A failed/cancelled/interrupted workflow can no longer surface as a
  successful experiment: impossible states are refused by the store's
  transition table, and the bridge never fabricates history (terminal
  events for unknown executions are typed errors).
- MCP `run_workflow` callers opt in with `experiment_db` + `experiment_id`
  (+ optional pins) and read results back through the existing read-only
  `experiment:`/`reproducibility:` tools — no new read verbs. Recording is
  bound to the SUBMISSION: the enabling surface calls `recordSubmission()`
  synchronously with the just-submitted run (pins included in the first
  transition), and signal-driven events for any other run are ignored.
- The governance mirror keeps its own signal subscription; both consumers
  are queued (the coordinator emits with its mutex held).
- Known limit: a terminal event still queued when the process exits is
  delivered if the owner flushes (`flush()` at shutdown — the MCP server
  does). Otherwise the next enable()'s stale reconciliation closes
  Failed/Cancelled/Interrupted records from checkpoint evidence; a
  COMPLETED checkpoint is only reported (closing as success without
  artifact evidence would fabricate output truth).
