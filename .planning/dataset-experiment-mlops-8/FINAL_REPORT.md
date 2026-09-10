# FINAL_REPORT — Dataset / Experiment / Reproducibility & Scientific MLOps 8.0

Branch: `feat/dataset-experiment-mlops-8`
Worktree: `exp-rs-dataset-experiment-mlops-8` (inside main/; separate build dir)
Base: origin/master `322dfd3876`

## Status: IMPLEMENTATION COMPLETE — local verification in progress

## Baseline (verified against execution-time master)

- No open PRs / issues; latest merges #823–#835; predecessor track #824
  (dataset/experiment 7.0) whose declared top limitation was "recorder
  workflow wiring is follow-up integration work".
- Verified: `ExperimentRunRecorder` had ZERO production callers on master.
- Work packages C/D/E/F/G/H largely already satisfied on master (details:
  `.planning/dataset-experiment-mlops-8/CAPABILITY_MATRIX.md`); 8.0 focused
  on the verified gaps instead of duplicating 7.0.

## Delivered (goal §A–§I mapping)

- **A. Automatic execution-to-experiment lifecycle wiring** (the headline
  gap): two-layer, dependency-inverted bridge —
  `ExperimentRunBridge` (workflow-free state machine in `sicnu_experiment`)
  + `sicnu_experiment_bridge` adapter (`WorkflowRun → ExecutionEvent`
  conversion + queued `WorkflowExperimentMonitor`). Truthful
  Created/Running/Succeeded/Failed/Cancelled/Interrupted; resumed
  executions continue the SAME record; resume-ghost Running events are
  filtered by checkpoint existence; terminal events for unknown executions
  are typed errors. The recorder/scheduler boundary is untouched: the bridge
  consumes the authoritative `runStateChanged` signal and never executes.
- **Interrupted/stale reconciliation**: `markInterrupted`/`markResumed` on
  the recorder; enable-time reconciliation decides from checkpoint evidence
  (Failed/Cancelled close truthfully; Interrupted stays resumable;
  completed checkpoints without artifact evidence are reported, never
  closed as success; flock probe protects live owners across processes).
- **B. Provenance completeness**: auto-recorded runs carry step-level
  evidence (operator, status, error, output path/size/content digest) +
  the definition snapshot as canonical parameters — bounded (≤256 steps);
  artifact roles carry the producing step id; unified lineage tombstones
  (7.0) unchanged.
- **C. Dataset version lifecycle**: already satisfied on master (verified);
  no duplication.
- **D. Sample/annotation governance**: already satisfied on master
  (promotion refuses unmapped classes; annotation chains; typed WKT).
- **E. Split & leakage**: already satisfied (12 methods, 13 leakage kinds,
  fold audit); near-duplicate remains honestly digest-based.
- **F. Evaluation/comparison**: `experiment:compare` now reports
  experiment_context (ids, names, tags) for both sides — baseline/treatment
  visibility; protocol/schema/paired machinery (7.0) unchanged.
- **G. Reproducibility/replay**: recorded runs carry verified pins
  (dataset/split fingerprints resolved from stores when provided) and
  artifact digests — replay readiness consumes them unchanged; hooks
  untouched.
- **H. Reproduction bundle 2.0**: unchanged (already satisfied); bundles
  gain auto-recorded runs for free.
- **I. Surfaces**: MCP `run_workflow` opt-in recording args
  (experiment_db/experiment_id/name/objective + dataset_db-verified pins);
  read side unchanged (existing experiment:/reproducibility: tools + CLI
  verbs see recorded runs).

## Verification (local, no CI)

(filled after builds/tests complete — see TEST_MATRIX.md / PERFORMANCE.md)

## Adversarial review

Round 0 self-review: 2×P1 (resume records could never complete; resume-ghost
Running dangle), 3×P2/P3 — all fixed (REVIEW_LOG.md).
Round 1 subagent review: (pending)

## Known limitations

- Terminal transitions still queued at process exit are recorded either by
  owner-side `flush()` or by the next `enable()`'s stale reconciliation
  (checkpoint-evidence based).
- Cross-thread interleaved delivery between a resume swap and its ghost
  event can, in rare races, record a ghost that the next reconciliation
  reports (never closes as success).
- Pins attach pre-submission keyed by workflow definition id; concurrent
  runs of the same definition share pin defaults (documented).
