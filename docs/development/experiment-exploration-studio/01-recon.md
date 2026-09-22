# Phase 0 — Recon: Experiment Exploration Studio

Baseline: `origin/master` @ `a9dc33fa` (2026-09-22 Asia/Shanghai).
Parallel OPEN PR: **#1237** `feat/undergrad-lab-cockpit` — **NEVER** modify
`src/app/teaching/**`, `src/teaching/**`, or teaching tests. Shared hotspots
(`main_window*`, `src/app/CMakeLists.txt`, root/`tests` CMake) use **semantic
union** — never ours/theirs wipe.

## Goal

Unify existing science cores into a teaching-facing **Experiment Exploration
Studio** (projection + Qt UI). Projection only — **no second sweep definition,
no private thread pool, no silent resample**.

Students see: why params change spatial results; where results differ; errors
that run but are scientifically wrong; first divergence step; uncertainty
sources; input vs param vs grid vs output.

## Dependency graph (what we project)

| Layer | Path | Authority | Wired? | Studio role |
|-------|------|-----------|--------|-------------|
| ParameterStudySpec / StudyBudget / SamplingStrategy | `src/study/study_spec.*` | Study document (versioned; unknown fields refused) | Yes `Sicnu::study` | **A** Study Designer emits this only |
| sampleStudyPoints / StudyPoint identity | `src/study/study_sampling.*` | Matrix cellId in `study:<id>` space; budget overflow = typed refuse | Yes | **B** point identity / combo explosion |
| StudyRunner + IStudyExecutionBackend | `src/study/study_runner.*`, `study_execution.h` | Windowed submit; TaskCenter owns concurrency | Yes | **B** Run Matrix; pause/cancel/retry |
| ExecutionPlaneStudyBackend | `src/study/bridge/study_execution_plane.*` | Production adapter → ExecutionPlane → TaskCenter | Yes `Sicnu::study_bridge` | **B** real runs (UI opt-in) |
| SensitivityCurve / UncertaintyEnvelope / Pareto / PointAggregate | `src/study/study_analysis.*` | Pure store projection; no parallel aggregate store | Yes | **D** sensitivity/uncertainty viz |
| SpatialDifferenceSummary / GdalRasterDifferenceSummarizer | `src/study/study_spatial.*` | Absolute-diff summary; **grid mismatch = `study.spatial_mismatch`** | Yes | **C** spatial compare; never silent resample |
| StudyReport / StudyRunRow | `src/study/study_export.*` | `sicnu.studyreport.v1`; atomic QSaveFile | Yes | **B/D/G** run table + export reload |
| FaultScenario / FaultSandbox / runner / diagnosis | `src/faultlab/*` | `sicnu.lab.faults/1`; sandbox-only mutation; diagnosis mirror | Yes `Sicnu::faultlab` | **E** predict→run→evidence→diagnose |
| FirstDivergenceAnalyzer / RunSnapshot / StepAligner | `src/experiment/debugger/*` | Pure read-side; closed verdicts; confidence from evidence completeness | Yes `Sicnu::experiment_debugger` | **F** Reference vs Student |
| ExperimentStore / MatrixLedger / ExperimentRun | `src/experiment/*` | Single run-truth | Yes | All tabs bind run ids |
| Verifier | `src/verify/*` | Pass/Fail/Indeterminate lattice | Sources; root may lack `add_subdirectory` | **E/G** evidence projection only |
| Grader | `src/grader/*` | Rubric/evidence/report | Yes | **E/G** evidence projection only |
| DatasetExperimentPanel | `src/app/workbench/dataset_experiment_panel.*` | Thin client over DatasetStore + ExperimentStore | Yes, docked | Seam: open stores / compare runs — Studio does **not** fork this |
| Map canvas / layers | QGIS canvas + workbench layer sections | GIS authority | Yes | **C** sync / swipe / side-by-side via existing canvas |
| Operator schema | `src/operators/framework/rs_schema.h` + `RSOperator::schema()` | Param types/ranges/determinism grade | Yes | **A** designer source (JSON schema projection) |
| TaskCenter / JobEngine | `src/jobs/*`, task_center | Admission/concurrency | Yes | Cancel/pause propagation only |

## Student questions → existing seams

| Student need | Seam to project (UI must only project) |
|--------------|----------------------------------------|
| Design a legal sweep | Operator schema → `ParameterStudySpec::validate` + `sampleStudyPoints` |
| Why so many points? | Sampler refusal `study.budget_exceeded` / `study.spec_*` |
| Run status / metric / output | `StudyRunRow` from `StudyReport` / MatrixLedger |
| Pause / cancel / retry | `StudyRunner` cancel + TaskCenter via execution backend |
| Compare two maps | Spatial summarizer + canvas layers; mismatch typed refuse |
| Param→metric curve | `SensitivityCurve` / `UncertaintyEnvelope` from analysis |
| Scientific fault teaching | FaultScenario → sandbox → runner → `diagnoseTransition` |
| Where did I diverge? | `FirstDivergenceAnalyzer::analyze` (aligned timeline, kind, confidence, gaps) |
| Export / reload offline | StudyReport JSON + fault/debugger report JSON + run ids / capsule refs |

## What UI must ONLY project (never recompute)

1. **Sweep definition** — only `ParameterStudySpec` + `sampleStudyPoints`. No second grid/LHS/OAT.
2. **Run truth** — ExperimentStore / StudyReport rows. UI does not invent statuses.
3. **Sensitivity / Pareto / uncertainty** — `study_analysis` aggregates only.
4. **Spatial delta** — `study_spatial` summaries; full-pixel scan for *previews* forbidden (use streaming/sampled summaries; tests use tiny synthetic rasters).
5. **Fault outcomes** — faultlab runner + diagnosis mirror; never mutate originals (sandbox).
6. **First divergence** — debugger analyzer results only; confidence downgrade when evidence incomplete (core already does this).
7. **Verifier/grader** — status lattice + reason slugs; indeterminate ≠ pass.

## Guardrails already in cores (Studio surfaces them)

| Guardrail | Core code / refusal |
|-----------|---------------------|
| Illegal ranges / duplicate dims / missing metrics | `study.spec_invalid_*`, `study.spec_no_metrics`, … |
| Combo explosion / budget | `study.budget_exceeded`, `study.spec_budget_over_cap`, `kMaxMatrixCells=1000` |
| In-flight bound | `kStudyMaxInFlightBound=8` |
| Grid mismatch | `study.spatial_mismatch` |
| Fault budget / sandbox | `faultlab.budget_exceeded`; `FaultSandbox::copyWithinBudget` |
| Incomplete debugger evidence | verdict `incomplete` / `non_comparable`; `CausalConfidence` downgrade |
| Non-reproducible operators | Designer gate on determinism grade / schema (Studio projection) |

## Resource policy (Studio hard caps — projection constants)

| Cap | Default | Notes |
|-----|---------|-------|
| Max study points (UI admit) | ≤ `budget.maxRuns` ≤ 1000 | Core already refuses over maxRuns |
| Max in-flight | ≤ 8 | Mirror `kStudyMaxInFlightBound` |
| Max disk budget (studio session) | configurable; default 512 MiB | Surfaces faultlab + study output dirs |
| Max per-run deadline | from `StudyBudget.perRunTimeoutMs` | Cancel propagates via submission |
| Spatial preview | sampled / report-level summary only | Never full-pixel UI scan |
| Run matrix table | virtualized / model; 100–1000 point baseline | Synthetic perf test |

## Owns / must not touch

**Owns:** `src/experiment_studio/**`, `src/app/experiment_studio/**`,
`docs/development/experiment-exploration-studio/**`,
`tests/test_experiment_studio_*`, append-only CMake + minimal
`main_window*` / `command_defs` registration (new command/dock only).

**Must not steal / modify:** `src/app/teaching/**`, `src/teaching/**`, teaching
tests; Parameter Studio / FaultLab / Debugger *compute* logic (project only);
Agent loop.

## Parallel #1237 union notes

Shared files we may touch append-only:
- `CMakeLists.txt` — add `src/experiment_studio` near study; do not remove teaching.
- `src/app/CMakeLists.txt` — add `experiment_studio/*` sources + link `Sicnu::experiment_studio`.
- `tests/CMakeLists.txt` — add `sicnu_add_experiment_studio_test`.
- `src/app/main_window.h`, `main_window_docks.cpp` / `main_window_workbench.cpp`,
  `main_window_menus.cpp`, `command_defs.cpp` — add Studio show command/dock
  beside DatasetExperiment / LabCockpit without rewriting their blocks.

## Demo path (DoD)

1. Pick operator `rs:threshold_raster` (or NDVI + threshold chain).
2. Study Designer: 1 dim `threshold` 0.1–0.9 / 5–20 points, metric `maskedPercent`.
3. Run Matrix via ExecutionPlaneStudyBackend / TaskCenter.
4. Sensitivity curve from analysis.
5. Spatial compare two recorded points (aligned grids).
6. Fault Injection teaching mode: predict → sandbox run → diagnosis vs system.
7. First Divergence: Reference vs Student from debugger report.
8. Export study/fault/debugger JSON + CSV + run ids (offline reload).

## Build notes

- Worktree: `/workspace/exp-rs-experiment-studio`
- Branch: `feat/experiment-exploration-studio` from `a9dc33fa`
- Own `build/`; Qt `/workspace/Qt/6.8.0/gcc_64`
- `CMAKE_BUILD_PARALLEL_LEVEL=1|2`, `ninja -j1|2`; no full clean; no concurrent
  heavy compiles with other agents.
