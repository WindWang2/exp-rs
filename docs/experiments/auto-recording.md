# Automatic execution → experiment recording (8.0)

Since Platform 8.0, a governed workflow execution can be recorded into the
`ExperimentStore` **automatically and truthfully** — no caller-side
bookkeeping, no second scheduler. The recording is a pure consumer of the
authoritative `WorkflowRunCoordinator` lifecycle (ADR 0143).

## What gets recorded

For every tracked workflow run of an enabled submission, one
`ExperimentRun` follows the execution truthfully:

| Execution event | Recorded state | Evidence stored |
|---|---|---|
| run starts | Created → Running | workflow id, definition (as canonical parameters), environment (allowlisted + redacted), execution ref |
| a step completes | (part of Running) | per-step output path + size + content digest (when within budget) |
| all steps completed | Completed | artifacts (role = producing step id), step summaries, timings |
| any step failed | Failed | `error.error_code = workflow.failed` + the step's error message |
| cancellation | Cancelled | `cancel_reason` |
| crash / startup recovery | Interrupted (non-terminal) | `interrupt_note`; a resumed execution re-enters Running on the SAME record |
| stale at next enable | per checkpoint evidence | Failed/Cancelled closed; Interrupted kept resumable; no-evidence and completed-checkpoint cases reported, never guessed (close a completed one through the recording path, with its artifacts) |

Records that were never started cannot be closed as successes; terminal
events for unknown executions are typed errors
(`experiment.bridge_unknown_execution`). Identity pins are immutable once a
run starts (store-enforced).

## Opting in (MCP)

`run_workflow` accepts optional recording arguments; without them behavior
is unchanged and nothing is written anywhere:

```json
{
  "tool": "run_workflow",
  "arguments": {
    "pipeline": { "id": "my-analysis", "steps": [ /* ... */ ] },
    "experiment_db": "/work/exp/experiments.db",
    "experiment_id": "6d2b3d52-1111-4111-8111-111111111111",
    "experiment_name": "Spring 2026 NDVI baseline",
    "experiment_objective": "Compare water/forest separability",
    "dataset_db": "/work/exp/datasets.db",
    "dataset_version": "<dataset-version-id>",
    "split_manifest": "<split-manifest-id>",
    "model_id": "segformer", "model_digest": "<sha256>",
    "seed": 42
  }
}
```

- `experiment_db` + `experiment_id` enable recording FOR THIS SUBMISSION;
  the experiment is created if missing (idempotent). Other workflow runs in
  the process are not recorded.
- `dataset_db` turns on pin verification: the dataset version must exist and
  its committed fingerprint is stamped; a split manifest pin resolves its
  fingerprint from the stored manifest when present (an unknown manifest id
  stays recorded with an honestly empty fingerprint, which replay readiness
  reports as a gap).
- Pins must be supplied per submission (they are part of the run's identity,
  which is immutable after start). A malformed pin (e.g. a negative seed) is
  a typed refusal, never a silent drop.

Read results back through the existing tools — `experiment:list`,
`experiment:inspect`, `experiment:compare`, `reproducibility:inspect`,
`reproducibility:export` — or the CLI verbs on the same store.

## Libraries

- `sicnu::experiment::ExperimentRunBridge` (`src/experiment/run_bridge.h`) —
  the recording state machine over neutral `ExecutionEvent`s.
- `sicnu::experiment::WorkflowExperimentMonitor`
  (`src/experiment/bridge/workflow_experiment_adapter.h`) — the
  workflow-side adapter (signal consumer, converter, stale reconciliation).
  Link `Sicnu::experiment_bridge` to attach it to your own surface.

## Guarantees and limits

- Failed/cancelled/interrupted executions can never surface as successful
  experiments (store-validated transitions; the recorder never auto-closes).
- The bridge records what the execution plane reports; it never executes,
  retries, or mutates a run.
- Evidence is bounded (≤256 steps summarized; artifact identity via
  path/size/digest; full resolved parameters stay in workflow checkpoints).
- A terminal transition still queued at process exit is drained by the MCP
  server teardown (`flush()`); without that, the next `enable()`'s stale
  reconciliation closes Failed/Cancelled/Interrupted records from
  checkpoint evidence — a COMPLETED checkpoint is reported instead of
  closed (closing it as success without artifact evidence would fabricate
  output truth).
