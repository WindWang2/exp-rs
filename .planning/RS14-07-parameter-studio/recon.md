# Recon — RS14-07 Parameter Sensitivity & Uncertainty Studio

- Track: `RS14-07-parameter-studio`
- Baseline: `origin/master` @ `4f6632e1f6bb41f90729800d0c7bf569ff34edb3` (PR #1145 merge; re-verified by `git fetch` at campaign start)
- Worktree: `../exp-rs-wt-rs14-param-studio`, branch `agent/rs14-parameter-sensitivity-studio`
- Dynamic dedup at start: open PRs #1188/#1189/#1190/#1191/#1192 (capability graph, verifier, curriculum, agent-benchmark, reproducibility capsule) — **none overlaps** a parameter-sweep teaching studio. Open issues reviewed for boundary (matrix below).

## 1. Existing capabilities (authorities we extend, never duplicate)

| Authority | Location | What it gives this track |
|---|---|---|
| Bounded sweep descriptor | `src/experiment/experiment_matrix.h` | axes × values → cells; deterministic `cellId` (sha256 over RFC 8785-canonical identity JSON, `experiment_matrix.cpp:228`); hard cap `kMaxMatrixCells = 1000`; typed refusal when exceeded |
| Cell↔run ledger | `MatrixLedger` (`experiment_matrix.h:86`) | lineage-edge-backed `link(cellId, runId)`, `runsForCell` — the study point↔run link needs no new table |
| Aggregation + Pareto | `MatrixAggregator` (`experiment_matrix.h:142`), `paretoCellIds` (:155) | `MetricAggregate{runCount,mean,populationStdDev,min,max}`; non-dominated cell selection. Reused for grid studies and Pareto projection |
| Run recording | `ExperimentRunRecorder` (`run_recorder.h:65`) | `startRun/markSucceeded/markFailed/markCancelled`, identity hashes, redacted env, truthful terminal states; "executes nothing" by contract |
| Experiment store | `ExperimentStore` (`experiment_store.h:22`) | SQLite WAL, schema v1, `upsertExperiment`, batched run upsert, keyset paging, lineage edges; project-local at `.sicnu/lab/experiments.db` |
| Execution spine | `ExecutionPlane` (`src/processing/framework/execution_plane.h:182`) | single async-first entry (GUI/Agent/MCP/CLI/workflows): `submit(ExecutionRequest)` → TaskCenter admission (concurrency/RAM/pending gates, `LatencyClass` lanes) → JobEngine → operator; event-loop-free `await`; commit-once `awaitResult` payload with committed output path |
| TaskCenter resource bounds | `task_center.h:412-493` | global/profile concurrency, RAM watermark, pending-task bound (refusal, not queueing), interactive reserve. The study runner must NOT spawn its own pool |
| Raster diff/statistics kernels | `ChangeDetection` (`src/processing/algorithms/change_detection.h:13-32`) | `difference()`, `statistics() → ChangeStats`, NaN-propagation conventions; `StreamingMagnitudeStats` (`rs_change_streaming.h:51`); `RasterHistogram` (`raster_histogram.h:37`) |
| Metric deltas | `pairedRunComparison` (`comparison_ext.h:84`), `summarizeAcrossSeeds` (`benchmark_compare.h:55`) | precedent for run-vs-run comparison semantics |
| Repeat/dedup semantics | `RepeatExecutionClassifier` (`repeat_execution.h:28`) | New / SameExecution / SameIdentity / EquivalentRerun / Deviated |
| Versioned JSON house style | `kEvidenceSchemaVersion` (`evidence.h:31`) et al. | `inline constexpr int k…SchemaVersion = 1`; readers refuse foreign versions; atomic sidecar writes (`writeFileAtomic`, `atomic_fs.h:69`) |
| Agent tool precedent | `data_platform_tools.{h,cpp}`, `surface_registry.cpp:143`, `mcp_server.h:187` | `study:*` tool wiring point (deferred, see §3/interfaces) |
| UI data model precedent | `dataset_experiment_panel.h:33` (thin client over stores), `processing_history_model.h:99-183` (DTO + `QAbstractTableModel`, hard row cap + truthful truncation signal) | seam for a future study panel; this track delivers the DTO/projection layer only |
| Test infra | Catch2 + CTest; `sicnu_add_io_test` (`tests/CMakeLists.txt:102`) light-test pattern; `sicnu_add_test` (:57) full-stack | new light helper `sicnu_add_study_test` (Catch2 + Sicnu::experiment + sicnu_study) + one full-stack e2e test |

## 2. Gaps (what this track adds)

1. **`ParameterStudySpec`** — parameter space (typed numeric dimensions over operator parameter paths), sampling strategy, budget (maxRuns ≤ 1000, per-run timeout), metric selection, artifact policy. Nothing today describes a *study*; `MatrixDescriptor` only describes a cartesian product of string axes.
2. **Samplers** — grid (reuse `MatrixDescriptor::enumerateCells` identity), **one-at-a-time** (baseline + single-dimension variations) and **small Latin hypercube** (seeded, deterministic via `std::mt19937_64` + fixed-boundary stratification + self-implemented bounded permutation — `std::uniform_int_distribution` is intentionally NOT used because it is not cross-platform deterministic) do not exist anywhere (verified by grep: sweep/latin/hypercube/monte/sensitivity).
3. **Study runner** — submits points through `ExecutionPlane` with a bounded in-flight window (no threads of its own), records every point truthfully via `ExperimentRunRecorder` (`executionRef` = TaskCenter task id), links via `MatrixLedger`, enforces budget with typed refusals, cooperative cancellation.
4. **Analysis projections** — sensitivity curves (dimension value → aggregate), uncertainty envelope (min/max + mean±σ across seed replicates), Pareto comparison (reuses `paretoCellIds` dominance logic).
5. **Spatial difference summaries** — run output vs baseline raster: `SpatialDifferenceSummary` value object; production GDAL summarizer reusing `ChangeDetection` kernels; interface-injected so pure tests use fakes.
6. **Versioned export (`sicnu.study.v1`)** — machine-readable study report (run table, curves, envelope, Pareto, spatial summaries, explicit status accounting: recorded/missing/failed/in_progress) + teaching narrative fields (parameter → result → interpretation triple). Atomic write.
7. **Exemplars** — NDVI threshold (`rs:threshold_raster.threshold`), classification (`rs:kmeans_classification.k`), change threshold (`rs:change_detection.threshold`) spec files + tests.

## 3. Interfaces to the other 19 concurrent tracks (no cross-PR dependency)

- **Execution Plane / TaskCenter**: consume as-is (stable public headers). No modifications planned.
- **Agent/MCP tool surface (`study:*` tools)**: this track ships the `StudyService` facade + versioned report document; the `data_platform_tools` registration is documented as the wiring point in `docs/integration.md` (created by this track) and deliberately not implemented, so we don't collide with agent-surface tracks (e.g. #1189 benchmark tooling) or drift the capability mirror (#1151 — known red; NOT ours to fix).
- **UI**: `StudyRunRow` DTO + projection is the data model; the Qt widgets panel is documented as future wiring (`docs/ui-architecture.md` thin-client pattern). No `src/app` changes.
- **Experiment reproducibility capsule (#1188)**: our report document is a projection from `ExperimentStore` truth; a capsule can wrap it later — integration note in `docs/integration.md`.

## 4. Dedup matrix vs open issues (must NOT absorb or fix)

| Issue | Area | Relation to this track |
|---|---|---|
| #1146/#1147/#1164/#1165 | SAR correctness/domain/LUT | none — no SAR code touched |
| #1148/#1149/#1168-#1170 | Mission runtime | none |
| #1152/#1158 | Workflow cancellation | we add NEW cancellation paths (study-level); do not touch D17/workflow cancel code |
| #1153/#1156/#1157 | ImportCenter/plugin lifecycle | none |
| #1154/#1155 | jsoncpp depth bombs | we add no new jsoncpp parse of untrusted input (Qt JSON only); not our fix |
| #1150/#1183 | spectral NoData / background stats perf | our exemplars use `rs:threshold_raster`/`rs:change_detection`, not the matched-filter family |
| #1151/#1187 | capability mirror / contract projection red | we add no new operators → no mirror drift caused; we do not regenerate the mirror |
| #1159/#1182/#1185 | TaskCenter cancel deadline / retry / CLI async | we consume TaskCenter API only |
| #1160 | VRAM ledger | no model runtime usage (kmeans/svm CPU operators only) |
| #1161-#1173, #1184 | Catalog/Dataset/Experiment existing defects | we build on `ExperimentStore` public API; if a defect blocks us, minimal bypass + PR note (none identified so far) |
| #1174/#1175/#1178 | atomic publish / sidecar / Windows rename | our outputs go through the ExecutionPlane commit path (unchanged); report JSON written via existing `writeFileAtomic` |
| #1176 | WBF/CatalogRecordStore perf | none |
| #1177 | performance observatory baselines | none |
| #1179 | oracle potency | we ADD strong oracles; do not modify existing ones |
| #1180 | georeferencer UAF | none |
| #1186 | P3 review batch | none |

## 5. Risks

1. **Build weight**: full-stack e2e test pulls qgis_core + processing + task_center (vendored QGIS). Mitigation: pure-core slices link only `sicnu_study` (Qt Core + Sicnu::experiment); the e2e target is built ONCE at -j1 before final review. Configure with `SICNU_LAB_PROFILE=ON` (zero network egress), `ENABLE_TESTS=ON`, Unix Makefiles, `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr` (mirrors an existing working cache on this machine).
2. **Cross-platform determinism**: LHS must not use `std::uniform_int_distribution`; implement bounded rejection sampling over `std::mt19937_64` (standard-defined).
3. **Double hashing drift**: study point identity MUST be the matrix `cellId` algorithm. Mitigation: expose one additive public helper in `experiment_matrix` (single fact source) instead of reimplementing the canonical hash.
4. **Second-scheduler temptation**: the runner's in-flight window is a submission window over `ExecutionPlane`, never a thread pool; admission stays with TaskCenter.
5. **Scope creep into GUI/agent surfaces**: deferred by design (§3).

## 6. Not doing (boundaries)

- No AutoML / no "best parameter" auto-decision (report flags Pareto front only; interpretation is labeled as evidence, not recommendation, unless a spec declares an explicit task metric with direction).
- No changes to operator algorithm logic, TaskCenter, Workflow, Processing Registry internals.
- No new registry/store/provenance system; no new SQL tables (lineage edges + experiment tables suffice).
- No GUI widgets; no MCP tool registration (wiring documented only).
- No absorption of any open issue (§4).
