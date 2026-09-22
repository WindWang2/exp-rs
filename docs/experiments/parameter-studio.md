# Parameter Sensitivity & Uncertainty Studio (RS14-07)

The parameter studio turns "change a parameter and click run" into a
*controlled experiment*: a versioned spec describes the parameter space, a
bounded runner executes every sampled point through the existing execution
spine, and the result is one honest document that serves two audiences —
students who want the parameter → result → interpretation chain, and agents
who want machine-readable evidence.

Nothing here executes on its own or stores a second copy of the truth:

```
ParameterStudySpec ──► samplers ──► StudyRunner ──► ExecutionPlane / TaskCenter
  (schema_version 1)    grid/OAT/LHS   (bounded        (admission, RAM, lanes
                         └ identity =     in-flight       stay with TaskCenter)
                           matrix cellId) window)
                                │
                 ExperimentStore runs + MatrixLedger links   ← the ONLY truth
                                │
                    analyzeStudy ──► buildStudyReport ──► sicnu.studyreport.v1
              (curves / envelopes / Pareto / spatial diffs)   (atomic JSON)
```

Module: `src/study` (`Sicnu::study`, Qt Core only) + `src/study/bridge`
(`Sicnu::study_bridge`, the opt-in ExecutionPlane adapter). Tests:
`test_study_spec|sampling|runner|analysis|spatial|export|exemplars` (light,
offline) and `test_study_e2e` (full stack).

## The study spec

A spec is a value object: it describes a bounded sensitivity experiment and
never executes anything. The serialized document is versioned
(`"schema_version": 1` — unlike the report, the spec carries no
`document_type` marker; readers refuse foreign versions) and every key is
known — the reader REFUSES unknown fields, because a typo'd spec must never
silently change a study's meaning:

| Field | Meaning |
|---|---|
| `study_id`, `experiment_id`, `algorithm_id` | identities; runs record into the existing experiment |
| `base_parameters` | parameters shared by every point (`output` is runner-managed and refused here) |
| `sampling.strategy` | `grid` (full cartesian), `oat` (baseline + one-at-a-time), `lhs` (seeded Latin hypercube) |
| `dimensions[]` | swept parameter: `parameter_path` + numeric ladder `min_value…max_value`, `step_count` values |
| `budget` | `max_runs` (hard cap 1000, refusal never truncation), `max_in_flight` (1–8), `per_run_timeout_ms`, `seed_replicates`, `seed` |
| `metrics` | operator payload keys aggregated per point |
| `objective_metrics[]` + `objective_metric` | Pareto directions and the ONE declared task metric; **empty ⇒ no consumer may name a "best" point** |
| `spatial_comparison`, `spatial_epsilon` | declare run-vs-baseline spatial difference summaries |

Three exemplar studies ship under `examples/studies/` and are contract-tested
through the production reader (`test_study_exemplars`):

| File | Sweep | Teaching point |
|---|---|---|
| `ndvi-threshold.sicnu-study.json` | `rs:threshold_raster.threshold`, OAT, 9 runs | the canonical parameter → mask → trend chain; spatial differences ON; **no** declared best |
| `classification-training-budget.sicnu-study.json` | `rs:supervised_classification.maxSamplesPerClass`, grid, 5×3 replicates | training-budget sweep against the held-out accuracy pair (`testSplit: 0.3` makes the operator emit `overallAccuracy`/`kappa`); the ONE exemplar that declares an objective metric (`overallAccuracy`, maximize) so the report may name a `declared_best` |
| `change-threshold.sicnu-study.json` | `rs:change_detection.threshold`, seeded LHS, 6×2 runs | controlled-scale sampling: same seed replays, different seed explores |

## Running a study

The runner is invoked with an `IStudyExecutionBackend`; the e2e test wires
`ExecutionPlaneStudyBackend` (from `Sicnu::study_bridge`), which submits each
point as an ExecutionPlane request with source tag `"study"` and commits the
operator output atomically (staged rename) into
`<studyOutputDir>/<pointId>/output.tif`. Production surfaces (CLI/GUI/agent)
are future wiring — see [integration.md](../integration.md). No catalog asset
is registered — a sweep must not flood the catalog; the run records + report
are the provenance.

Every submitted point becomes exactly one of (all recorded in the existing
`ExperimentStore`, linked to its matrix-cell identity via `MatrixLedger`):

- **recorded** — metrics + committed output on the run;
- **failed** — typed error evidence (`study.operator_failed`,
  `study.run_submit_refused`, …);
- **cancelled** — cancellation recorded with its reason.

Points a cancellation prevented from submitting are not given fake runs; they
surface as **missing** in the report's status accounting. There is no silent
retry, no silent truncation, no silent fallback anywhere.

## The report (`sicnu.studyreport.v1`)

`buildStudyReport` projects the recorded truth (it never executes) and
`writeStudyReport` writes it atomically. One document, two audiences:

- **Teaching**: the run table, one sensitivity curve per (dimension, metric)
  on the OAT/grid reference slice, mechanical trend triples ("maskedPercent
  falls from … to … on the reference slice") — factual observations, never
  explanations or recommendations;
- **Agent**: `document_type: sicnu.studyreport.v1`, standalone-parseable,
  explicit status accounting that sums to the run table, Pareto set computed
  by the matrix authority's dominance logic, uncertainty bands across seed
  replicates, and `declared_best` **only** when the spec declared an
  objective metric (labeled as evidence with its basis, not a recommendation).

One honesty note on the envelope: replicate runs record DISTINCT seeds, but
the submitted parameter document is identical (the execution spine has no
seed field). A deterministic operator that ignores the recorded seed
therefore produces zero-width bands — a truthful null result, not noise.
Sweeps whose dimension genuinely moves the metrics (like the classification
exemplar's training budget) are where curves and `declared_best` carry
signal.

A report READER tolerates unknown fields (evidence must survive additive
evolution) but refuses foreign versions — deliberately asymmetric with the
spec reader.

## Boundaries (what this module deliberately is NOT)

- No AutoML and no automatic "best parameters" beyond the spec's explicit
  `objective_metric` declaration.
- No changes to operator algorithm logic, TaskCenter, the Processing Registry,
  or the experiment schema; the runner spawns no thread pool of its own.
- GUI panels and agent (`study:*`) tool registration are future wiring — see
  [integration.md](../integration.md) for the designed seams.
