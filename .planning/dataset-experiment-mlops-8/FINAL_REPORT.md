# FINAL_REPORT — Dataset / Experiment / Reproducibility & Scientific MLOps 8.0

Branch: `feat/dataset-experiment-mlops-8`
Worktree: `exp-rs-dataset-experiment-mlops-8` (inside main/; separate build dir)
Base: origin/master `322dfd3876`

## Status: COMPLETE (implementation + verification + adversarial review)

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

## Verification (local, no CI; environment: Ninja Release, GCC 16.2.1, Qt6,
ccache, USE_PRECOMPILED_HEADERS=OFF, tests at -j1, host under concurrent
track builds)

Executed and passed on this branch:
- test_mlops8_bridge — 150 assertions / 17 cases ✓
- test_mlops8_scale — 19 907 assertions ✓ (20k runs seeded + 19.9k stale
  reconciled report-only; 13.4 s wall)
- test_mlops8_e2e — 6/6 cases ✓ (real tracked pipelines: success/fail/
  cancel/interrupt-resume/disabled/no-ghost; run individually under host
  load; TIMEOUT 600)
- test_mcp_server — 3 934 assertions / 20 cases ✓ (incl. 2 new recording
  surface cases)
- Regression: dataset_core 155 ✓, split_leakage 672 ✓,
  experiment_evaluation 162 ✓, platform7_library 228 ✓,
  data_platform_surface 101 ✓, workflow_run_coordinator 176 ✓,
  dataset_e2e_examples 57 ✓ (all pre-review; the review fixes touched only
  bridge/adapter/mcp_server/tests, and the affected suites were re-run
  green after remediation and again after the rebase to master 226adb8d02)

Build evidence: sicnu_experiment, sicnu_experiment_bridge, sicnu_agent, and
all listed test targets compile clean (only pre-existing warnings in vendored
QGIS core). Exact commands in PERFORMANCE.md.

Honest classification: nothing was "compiled but unexecuted" among the
targets above; CLI auto-recording is NOT wired (the CLI reads recorded
stores via existing verbs; wiring the runner is a documented follow-up).

## Adversarial review

Round 0 self-review: 2×P1 + 3×P2/P3 fixed (REVIEW_LOG.md).
Round 1 (two read-only subagents, full diff): P0 0, P1 4, P2 7, P3 19 —
ALL P1/P2 fixed, P3 fixed or justified (REVIEW_LOG.md round 1). During
remediation two additional critical defects were found and fixed: a data
race on the workflow definition (SIGSEGV, coredump-backed; conversion now
uses one locked run.toJson() snapshot) and the first-submission terminal
event loss (enable-before-submit). Affected suites re-run green after
remediation and after rebase.

## Known limitations

- Terminal transitions still queued at process exit are recorded either by
  owner-side `flush()` or by the next `enable()`'s stale reconciliation
  (checkpoint-evidence based).
- Cross-thread interleaved delivery between a resume swap and its ghost
  event can, in rare races, record a ghost that the next reconciliation
  reports (never closes as success).
- CLI auto-recording is not wired: `sicnu_geo_rs_cli` pipelines are not
  auto-recorded (the CLI reads recorded stores through existing verbs);
  wiring rs_pipeline_runner through the same monitor is a follow-up.
- resume_workflow does not yet accept recording arguments; a continuation
  can be opted in via WorkflowExperimentMonitor::optInResume().
- RUN_SERIAL on sicnu_add_test targets does not propagate to discovered
  tests (pre-existing house pattern; the new cases are self-contained).
