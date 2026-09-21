# Recon — RS14-06 Experiment Debugger / First Divergence Locator

- Track: `RS14-06-experiment-debugger`
- Branch: `agent/rs14-experiment-debugger` (worktree `../exp-rs-wt-rs14-exp-debugger`)
- Baseline at recon time: `origin/master = 4f6632e1f` (same as prompt baseline; re-check before PR)
- Method: dynamic dedup (`gh pr list` / `gh issue list`, 2026-09-22) + code recon (headers, CMake, tests, ADRs, docs).

## 1. Dynamic dedup results

### Open PRs (2026-09-22)
| PR | Track | Overlap with RS14-06 |
|---|---|---|
| #1188 | RS14-17 Reproducibility Capsule (sicnu.capsule v1) | None — packaging of replay inputs, not comparison. Consumes the same run identity pins; we must not alter them. |
| #1189 | Agent Benchmark & Evaluation Harness (trajectory grading) | Adjacent but distinct: grades *agent trajectories* against case packs; we locate *scientific state* divergence between two recorded runs. No shared types planned. |
| #1190 | RS14-18 Curriculum Pack | None. |
| #1191 | RS14-10 Unified Scientific Verifier (ADR 0172) | Adjacent: verifies result *correctness* (accept/reject). We explain *why two runs differ* (causal localization). Complementary; verifier is a potential future consumer of our typed diagnostic. No shared code required. |
| #1192 | RS14-08 Capability State Graph (ADR 0173) | None. |
| #1193 | RS14-09 Scientific Task Planner | None (planner is a *future consumer* of our agent adapter; interface stays DTO-level per integration notes). |

ADR numbers already claimed by open PRs: 0172 (verifier), 0173 (capability graph). **This track uses ADR 0174** and documents the union-renumber risk in the ADR header.

### Open issues — avoidance matrix
Per the track prompt, the following issue areas are out of scope; this track touches **none** of their write paths:

| Issue cluster | Why RS14-06 does not touch it |
|---|---|
| #1146/#1147/#1164/#1165 SAR correctness | We compare recorded evidence; we never compute SAR products. |
| #1148/#1149/#1168/#1169/#1170 Mission Runtime | We do not read or write mission authority/state. |
| #1152/#1158 Workflow cancellation | We are strictly read-only over finished/terminal run records. |
| #1153 ImportCenter, #1156/#1157 plugins, #1181 tool catalog | No plugin/registry interaction. |
| #1154/#1155 jsoncpp depth bombs | We only parse **locally produced, platform-written** provenance JSON via the existing strict `ProvenanceGraph::fromJson` (bounded, envelope-gated); we never parse remote/untrusted JSON. Note recorded, not fixed. |
| #1159/#1182 TaskCenter cancel/retry | No TaskCenter involvement. |
| #1160 VRAM ledger | No model runtime involvement. |
| #1161/#1171/#1172/#1173/#1184 Catalog/Dataset/Experiment consistency + 10k truncation | We use store **read paths only** (`runById`, `listRunsByCursor`); we do not modify write paths, prune, or rely on `Experiment.runIds`. #1172 (`runIds` never populated) observed: our reference-set resolution therefore never trusts `Experiment.runIds`; it takes explicit run ids instead. |
| #1162/#1163 geospatial mirror/cache | No mirror involvement. |
| #1174/#1175/#1178 atomic publish/sidecar/Windows | We write nothing except optional report JSON through the caller. |
| #1176 WBF/CatalogRecordStore perf | Untouched. |
| #1177 perf-observatory baseline loss | Untouched. |
| #1179 test oracle potency | Our new tests are written to fail on capability absence (see test strategy). |
| #1180 georeferencer UAF, #1185 CLI async hang, #1186 review batch | Untouched. |

## 2. Existing capabilities (the facts)

### Whole-run comparison — ALREADY EXISTS (do not duplicate)
- `sicnu::experiment::RunComparison::compare(a,b)` (`src/experiment/experiment_types.h:305`) — pin-level structural comparability, verdicts `Comparable | ComparableWithDifferences | NotComparable`, dimensions dataset/split/model/algorithm/config/seed/environment.
- `RunComparison::metricDiff` — numeric leaf deltas for comparable runs.
- `ReplayDeviationAnalyzer` (`src/experiment/replay_deviation.{h,cpp}`, schema v1) — original-vs-replay verdict `Identical | Equivalent | Deviated | Incomplete` with typed deviation list and honest evidence gaps. **Whole-run only; has no step-level data.**
- `RepeatExecutionClassifier` (`repeat_execution.{h,cpp}`) — New/SameExecution/SameIdentity/EquivalentRerun/Deviated for a re-observed execution.
- `comparison_ext.{h,cpp}` — evaluation-protocol compatibility, label-schema compatibility, `pairedRunComparison` with per-class supports and `insufficient_support` honesty contract.
- `benchmark_compare.{h,cpp}` — benchmark-result deltas under matched benchmark/dataset/split pins.
- CLI `experiment compare --a --b` (`src/cli/cli_dataset_commands.cpp:551`) and Workbench `dataset_experiment_panel` (`compareSelectedRuns`) surface these.

### Per-step execution evidence — EXISTS as read-only sources (never re-record)
- `ProvenanceGraph` (`src/workflow/workflow_provenance.{h,cpp}`, envelope `d17_provenance` v"1.0", strict `fromJson` refusing dangling edges): nodes `run | nodeExec | artifact`, edges `consumed | produced | reusedFrom`. nodeExec attributes: state, `lineageSignature`, elapsed, cacheHit, originNodeId, errorMessage. artifact attributes: fingerprint + size. Persisted per finished pipeline run as `provenance_<runId>.json` (attempt 1 beside checkpoint; `attempt-<N>/` for later attempts) — `PipelineRunCoordinator::provenancePath()`.
- `NodeStatusSnapshot` (`pipeline_run_coordinator.h:91`) — per-node state, `artifactFingerprint` (`sha256fl:`/`sha256full:`), `lineageSignature`, cacheHit; persisted in run checkpoints.
- `StepPlan` (`workflow_run.h:53`) — per-step `resolvedParams`, `fingerprint` (lineage signature), `outputDigest` (SHA-256 content digest), `status`, `errorMessage`; checkpoint-persisted.
- Step evidence bridge contract: `run.metrics()["workflow"]["steps"][]` with per-step `status` (read by `EvidenceProjector`, `evidence.cpp:182–210`) — the ExperimentRun-embedded step record for bridge-recorded runs.
- `lineageSignature` semantics (`WorkflowPlanOptimizer::computeNodeSignature`, `plan_optimizer.cpp:38`): SHA-256 over `operatorId + canonicalJson(parameters) + incoming (targetPort, sourceNodeId, parentSignature)` — **the deterministic process identity of a step**. Equal signatures ⇒ identical process lineage; differing output digests under equal signatures ⇒ result divergence without process divergence.

### Digest / canonicalization authority (reuse, never re-implement)
- `canonicalizeJsonRfc8785` + `ExecutionFingerprint` (`src/data/execution_fingerprint.h`, contract v3).
- `artifactSha256Hex` (`src/operators/framework/artifact_digest.h`).
- `runConfigHash` / `runExecutionFingerprint` / `runResultFingerprint` (`experiment_types.h`).

### Typed diagnostic seams (reuse for agent adapter)
- `DiagnosticReport` envelope `exp.diag.v1` (`src/runtime/observability/diagnostic_report.h`) — code verbatim + component + recoverability + suggested_action + cause_chain; "never a second taxonomy".
- `sicnu::data::Diagnostic{code,message,severity}` + `Result<T>` (`src/data/data_result.h`) — the `experiment.*` dotted-code convention.
- `RSOperatorError` numeric codes (append-only), `harness_error` string taxonomy (agent), `diagnostic_catalog` unified lookup.

### Library / test infrastructure
- `sicnu_experiment` (alias `Sicnu::experiment`): GUI/network-free science core; layer guard forbids Widgets/qgis_gui/Network. Read APIs: `runById`, `listRunsByCursor`, `runIdsByExecutionFingerprint`, `metricRecordForRun`.
- `sicnu_experiment_bridge` (`src/experiment/bridge/`): sanctioned precedent for a leaf lib linking `Sicnu::experiment + sicnu_workflow` (no reverse edges anywhere).
- `sicnu_workflow` (SHARED core) contains `ProvenanceGraph`; links jsoncpp/sicnu_operators/sicnu_runtime PUBLIC.
- Tests: flat Catch2 executables in `tests/`, `sicnu_add_test(NAME)` helper + extra links (e.g. `test_mlops9_evidence` links `Sicnu::experiment Sicnu::dataset SQLite::SQLite3`, TIMEOUT 300). `QTemporaryDir` for temp state; fixtures synthesized at runtime (repo convention).

## 3. The gap this track fills

> No module joins step-level records across two runs, and nothing anywhere locates a first divergence. (recon bottom line)

Concretely missing:
1. A **normalized, comparable run snapshot** abstraction over heterogeneous read-only sources (provenance graph / bridge step evidence / run-only records), with a canonical digest.
2. **Step matching / graph alignment** between two runs' step graphs (same-plan and different-plan cases).
3. **First-divergence localization with a typed classification taxonomy** (different input state / missing preprocessing / parameter divergence / data subset divergence / geometry-alignment divergence / result divergence without process divergence / unknown-non-comparable) and **causal confidence** (honest, evidence-based, never fake certainty).
4. **Alternative-valid-path handling** (declared equivalence profiles + invariant-based references) so deliberately-different-but-correct student paths are not misjudged.
5. A Qt-free **UI diff model** (dual-run timeline + first significant divergence) ready for GUI/CLI/agent rendering.
6. A **typed agent adapter** aligned to `exp.diag.v1` for future planner replanning.

## 4. Extension seam decision

- New leaf library **`src/experiment/debugger/` → `sicnu_experiment_debugger` (alias `Sicnu::experiment_debugger`)**, mirroring the bridge-lib precedent: `PUBLIC Qt6::Core + Sicnu::dataset + Sicnu::experiment + sicnu_workflow`. Nothing links into it except tests/CLI/GUI consumers (future).
- Root `CMakeLists.txt`: exactly one `add_subdirectory(src/experiment/debugger)` line (minimal central delta).
- Snapshot inputs cross a **provider interface** (`IRunEvidenceSource`) so tests use in-memory fakes and production wires file/store locators — no dependency on another concurrent track's types.
- All emitted JSON carries `schema_version: 1` and kind strings `exp.debugger.snapshot.v1` / `exp.debugger.divergence.v1` / `exp.debugger.equivalence.v1`.

## 5. Risks

| Risk | Mitigation |
|---|---|
| Workflow provenance area is actively moving (#1145 union, checkpoint cpp changed 2026-09-21) | We consume only the stable read contract (`ProvenanceGraph::fromJson`, envelope-gated). No workflow code modified. |
| `Experiment.runIds` unreliable (#1172) | Never used; explicit run ids only. |
| Steps evidence absent for single-operator runs | Snapshot honestly records `stepEvidence: "absent"`; divergence report degrades to run-level verdict reuse (`RunComparison`), never guesses. |
| ADR number collision at union time (0174 may clash with another in-flight track) | ADR header documents renumber-on-union; filename carries track slug. |
| Other RS14 tracks may also add comparison-adjacent modules | Dedup re-check before PR; interface-first design keeps merge surface tiny (one CMake line + new directory). |
| Digest incomparability across evidence modes (`sha256fl` vs `sha256full`) | Snapshot records digest mode per artifact; comparisons of mixed modes are typed as `unknown_evidence`, not false-equal. |

## 6. What we are NOT doing

- No ExperimentStore write-path changes; no new recording of runs (read-only projections only).
- No workflow cancellation/persistence fixes (#1152/#1158 out of scope).
- No second provenance/store/registry (we parse via `ProvenanceGraph` and read via `ExperimentStore`).
- No GUI implementation in this track (Qt-free diff model only; GUI wiring = documented integration point).
- No new operator/algorithm behavior; no auto-correction of scientific state (report-only).
- No handling of the open issues listed in §1.
