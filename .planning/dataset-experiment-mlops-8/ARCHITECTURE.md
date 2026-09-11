# ARCHITECTURE — 8.0 additions (design decisions)

## D1. Where the lifecycle wiring lives (WP-A)

**Decision: a two-layer bridge with dependency inversion at the science-core
boundary.**

```
WorkflowRunCoordinator (authoritative execution; emits runStateChanged
                        with coordinator mutex held)
        ▲  Qt::QueuedConnection (same pattern as the #754 governance mirror)
        │
sicnu_experiment_bridge  (new static target)
  WorkflowExperimentMonitor (QObject)      WorkflowRun → ExecutionEvent
        │                                   conversion (pure function)
        ▼
sicnu_experiment
  ExperimentRunBridge (plain state machine over neutral ExecutionEvent)
        │
        ▼
  ExperimentRunRecorder → ExperimentStore (authoritative persistence)
```

Why not alternatives:

- *Coordinator calls the recorder directly*: would add edge
  `sicnu_workflow → sicnu_experiment`; the workflow stack deliberately has no
  science-core edge (cycle-adjacent, drags SQLite + dataset into every
  workflow consumer, and #828 owns that file).
- *Bridge inside ProjectContext only*: MCP `run_workflow` never constructs
  ProjectContext; wiring at the composition root would miss the primary
  headless surface.
- *Bridge inside sicnu_experiment linking sicnu_workflow*: keeps one target
  but gives the science core a hard link dependency on the execution stack —
  worse layering than a thin separate target that only consumers link.

The bridge lib is a leaf: nothing links it except the surfaces that want
auto-recording (agent, CLI, tests). If nothing enables it, behavior is
byte-identical to master (opt-in, zero overhead when detached).

## D2. Event vocabulary

`ExecutionEvent` (Qt-only struct, defined in sicnu_experiment):

- `executionRef` — workflow run id (survives resume: the coordinator swaps
  resumed submissions back under the ORIGINAL runId, so one experiment run
  tracks the whole Interrupted→Running→terminal story).
- `workflowId`, `state`, `startedMs`, `finishedMs`.
- `definition` — workflow definition JSON snapshot (replay evidence).
- `steps` — bounded JSON array: stepId, operatorId, status, error,
  output path/size/digest, resolved-params-present flag (NOT full params —
  boundedness; full resolved params already live in the checkpoint).
- `artifacts` — stepId → path map + digest/size when known.
- `errorMessage`.

State vocabulary is a CLOSED set: `Running`, `Completed`, `Failed`,
`Canceled`, `Interrupted`. Anything else is an error result
(`experiment.bridge_unknown_state`) — the bridge never guesses a mapping
(truthfulness law).

## D3. State mapping and truthfulness

| Workflow state | Experiment transition | Notes |
|---|---|---|
| Running (first event for ref) | `startRun` (Created→Running) | pins resolved at this point from the registry (below) |
| Running (ref already Interrupted) | Interrupted→Running | resume continuation, same experiment run id |
| Completed | `markSucceeded` (artifacts from event) | only if steps all completed per run aggregate |
| Failed | `markFailed("workflow.failed", run.errorMessage)` | error evidence stored in metrics doc by recorder |
| Canceled | `markCancelled(reason)` | truthful cancellation |
| Interrupted | NEW `recorder.markInterrupted(note)` | non-terminal; resume may still complete it |

Duplicate/late events are idempotent-safe: the store rejects illegal
transitions (`experiment.bad_transition`) and the bridge treats an already-
terminal run with the same outcome as a no-op success (at-least-once
delivery under queued connections).

## D4. Pins (dataset/split/model identity)

Auto-recorded runs must not invent pins. Pin resolution order:

1. **Explicit pin registry** keyed by workflow id (and overridable by
   executionRef): `datasetVersionId`, `splitManifestId`, `modelId`,
   `modelDigest`, `seed`, `determinismNote`. Set by the enabling surface
   (MCP args, GUI, test).
2. Pins may arrive BEFORE or AFTER the Running event (race between the
   queued signal and the MCP thread that knows the args): `attachPins` is a
   MERGE upsert onto the possibly-already-created run record; the store's
   transition-checked upsert path persists pin updates pre-terminal.
3. When a `DatasetStore` is wired, the recorder resolves/verifies dataset
   version + fingerprint (existing 7.0 behavior).
4. Unpinned stays unpinned: fields empty, determinism defaults to Strict
   per recorder contract. Never a fabricated identity.

## D5. Stale reconciliation policy (the "caller decides" made concrete)

The recorder only reports stale runs. The bridge implements the decision,
evidence-gated (see D7 for why this is safe):

- Non-terminal experiment run whose executionRef is in the live set → untouched.
- Whose checkpoint on disk shows Failed/Canceled → close Failed/Cancelled
  respectively with `workflow.stale_reconciled` evidence.
- Whose checkpoint shows Interrupted → advance to Interrupted (non-terminal;
  a later resume continues the story) — only if not already Interrupted.
- Whose checkpoint is missing entirely → REPORT ONLY (cannot prove outcome;
  completed checkpoints are archived, so absence is ambiguous for old runs).

## D6. MCP surface (opt-in "selected governed executions")

`run_workflow` gains OPTIONAL args (absent ⇒ behavior identical to master):

- `experiment_db` (path) — presence enables auto-recording for THIS submission.
- `experiment_id` (+ optional `experiment_name`/`objective` — created if
  missing via `upsertExperiment`).
- `dataset_db`, `dataset_version`, `split_manifest`, `model_id`,
  `model_digest`, `seed` — optional pins (verified against stores when given).

Implementation: the MCP server owns a process-wide `WorkflowExperimentMonitor`
(lazy singleton) and calls `monitor.recordSubmission(definition, pins,
experiment...)` around `startTrackedPipelineJson`. Read-side MCP tools
(`experiment:list/inspect`, `reproducibility:inspect`) then see the recorded
runs with no new read verbs required. A `workflow:simulate`-style dry run is
NOT recorded (it doesn't go through the coordinator).

## D7. Threading/lifetime contract

- The coordinator emits with its mutex held ⇒ the monitor MUST connect
  Queued (mirrors ProjectContext's governance mirror). Terminal events can
  therefore arrive after process-exit begins; the monitor exposes `flush()`
  and the enabling surfaces call it on shutdown where an event loop exists.
- The monitor holds the coordinator's run snapshot extraction behind
  `pipelineIdForRun`/`runForPipeline` (public, lock-safe).
- `ExperimentStore` is mutex-guarded per instance; the monitor owns its
  store instance (opened at enable time) — never shares the panel's/MCP
  tool's per-call instances.
- Monitor lifetime is bound to its owner (MCP server process / test fixture);
  disconnect before store close (Qt connection context object pattern).

## D8. Provenance completeness (WP-B) inside the same seam

`markSucceeded` artifacts carry path/role/digest/size from StepPlan
completion identity (#750). The run's metrics document additionally carries
a `workflow` evidence block: definition id/title, step summaries (status,
operatorId, output digest), and the execution ref — so a recorded run alone
answers "what ran, on what inputs identity, producing which digested
outputs" without reading checkpoints. Full resolved params stay in the
checkpoint (boundedness); the experiment record cites them.

## D9. Compatibility

- No schema changes to either store (schema v1 stays; additive payload
  content only inside existing JSON documents).
- `markInterrupted` is additive to the recorder; no existing signature
  changes.
- MCP args are additive/optional; CLI verbs unchanged; GUI unchanged
  (auto-recorded runs appear through existing read paths).
