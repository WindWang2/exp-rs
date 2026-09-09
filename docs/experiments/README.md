# Experiment Foundation 5.0

`src/experiment` (library `Sicnu::experiment`) provides experiment/run
identity, typed metrics + evaluation protocols, comparability-first run
comparison, the joined lineage graph, allowlisted environment capture and
reproduction bundles (ADR 0137-0138).

An `ExperimentRun` RECORDS an execution performed through TaskCenter/
JobEngine/workflow - it never executes anything (no second scheduler).
It binds: dataset version + fingerprint, split manifest + fingerprint,
algorithm/workflow identity, canonical parameters, seed, model id+digest,
determinism grade, environment, artifacts, metrics.

CLI: `sicnu_geo_rs_cli experiment create|list|inspect|run|compare`,
`sicnu_geo_rs_cli reproduce export|validate|inspect`.

## Platform 7.0 additions

- **Run recorder** (`run_recorder.h`): the authoritative TaskCenter/Workflow
  → ExperimentRun adapter. `startRun` verifies the experiment + dataset
  version pins, stamps fingerprints, redacts the environment and advances the
  run to Running; `markSucceeded`/`markFailed`/`markCancelled` record
  truthful terminal states with evidence (failures store their error under
  the run's metrics). `reconcileStaleRuns` reports non-terminal runs whose
  execution is no longer live — a crash truth report; nothing is ever
  auto-closed as success.
- **Replay readiness** (`replay_readiness.h`): per-dependency checks
  (dataset version + fingerprint, split manifest + fingerprint, artifacts,
  model/algorithm via hooks) with an overall level that never overstates:
  missing/mismatched → Impossible, unknown → BestEffort, everything pinned →
  Exact. `equivalentRuns` finds historical runs with the same execution
  fingerprint (duplicate-execution detection).
- **Comparison extensions** (`comparison_ext.h`): evaluation-protocol
  compatibility (dataset/split/subset/ignore/mask/thresholds/aggregation),
  label-schema compatibility (added/removed/foreign verdicts) and
  `pairedRunComparison` — per-metric deltas with their supports; metrics
  below the support threshold are flagged `insufficient_support`, and no
  significance is ever fabricated.

MCP surface: `experiment:list`, `experiment:inspect`, `experiment:compare`,
`reproducibility:inspect`, `reproducibility:export`,
`reproducibility:validate` (see `src/agent/data_platform_tools.h`).
