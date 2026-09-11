# GOAL — Dataset, Experiment, Reproducibility & Scientific MLOps 8.0

Turn the existing DatasetStore/ExperimentStore foundation into an end-to-end
scientific MLOps and reproducibility platform in which datasets, samples,
splits, executions, metrics, artifacts, and replayability are automatically
and truthfully connected to the authoritative execution plane.

Branch: `feat/dataset-experiment-mlops-8`
Worktree: `../exp-rs-dataset-experiment-mlops-8`
Base: origin/master `322dfd3876` (== planning snapshot; re-verified 2026-09-10)

## Hard laws (unchanged)

- Pi stays the single agent loop; no second agent framework.
- Execution stays `WorkflowRunCoordinator -> TaskCenter -> JobEngine ->
  Executor/RSOperator`; the recorder/bridge RECORDS, never executes.
- `RSOperatorRegistry`, `IModelRuntime`, QGIS, `DatasetStore`/`ExperimentStore`,
  `src/geospatial/**` ownership unchanged.
- New units require proof that no authoritative equivalent exists.

## Verified primary gap (see BASELINE.md)

`ExperimentRunRecorder` (7.0) has **zero production callers** — the workflow
execution plane and the experiment store are not connected. 7.0's own final
report lists "recorder workflow wiring" as the top known limitation. 8.0's
center of gravity is closing that wiring truthfully (states, pins,
provenance, stale reconciliation) plus the secondary gaps listed in
CAPABILITY_MATRIX.md.

## Definition of Done

- Baseline + overlap audit recorded (BASELINE.md, CAPABILITY_MATRIX.md).
- WP-A implemented with local build + test evidence (unit + E2E).
- Secondary gaps either implemented or explicitly refused with rationale.
- Adversarial review completed; no open P0/P1; P2/P3 fixed or justified.
- Docs/help/catalog synchronized; no unrelated churn; PR created (no merge).
