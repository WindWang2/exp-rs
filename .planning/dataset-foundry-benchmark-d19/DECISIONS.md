# DECISIONS — D19

## D1. Dataset authority stays DatasetStore / DatasetManifest

**Choice:** Extend ADR 0134–0136 authorities; do not invent a second dataset DB.

**Why:** Master already has version DAG, samples, labels, splits, leakage, QA fragments. Duplicating would fracture scientific identity.

## D2. Typed DatasetRole on Manifest (additive)

**Choice:** Add `DatasetRole` enum: `Unspecified | Source | Derived | Training | Benchmark | Evaluation`. Default `Unspecified`; omit from JSON when unspecified so existing fingerprints stay stable on rewrite-without-role.

**Why:** One versioned model with typed roles (GOAL §5) without five unrelated implementations.

## D3. FeatureSet as dataset-layer join contract

**Choice:** New `feature_table` module in `sicnu_dataset`: schema (`feature_set_id`, `schema_version`, `sample_key`, columns/units/domains, producer, input dataset version, digest) + join that refuses ambiguous/missing keys.

**Why:** Operators already produce tables; coupling by free-form CSV path is the anti-pattern GOAL §13 forbids. Join lives next to sample identity, not in operators.

## D4. Benchmark Definition + Runner live in sicnu_experiment

**Choice:** `BenchmarkDefinition` / `BenchmarkRunner` / `BenchmarkService` in `src/experiment`, reusing `EvaluationProtocol`, metrics, ExperimentStore, promotion.

**Why:** Benchmarks are evaluation+experiment contracts over pinned dataset/split/model — not a second metric engine. Dataset foundry supplies pins; experiment owns run evidence.

## D5. No second split / leakage / confusion-matrix implementation

**Choice:** Benchmark modes (cross-region/year/sensor) are **configuration + policy** over existing `SplitMethod` + leakage audit + EvaluationProtocol.

**Why:** D15/MLOps9 already own the algorithms; duplication is the #1 failure mode called out in GOAL.

## D6. DatasetFoundryService is a headless façade

**Choice:** Thin service over DatasetStore (inspect, versions, QA, splits, sample query, feature attach). D18 may consume later; D19 does not wire UI.

## D7. AuditVerdict vocabulary

**Choice:** `Pass | Warn | Fail | Unknown` for QA categories and leakage summaries at the foundry/benchmark boundary (maps from existing DiagnosticSeverity + leakage findings without collapsing to boolean).

## D8. Pseudo-label test protection

**Choice:** `BenchmarkDefinition.refusePseudoLabelsInTest` (default true). Runner fails with typed diagnostic if protected test subset contains Pseudo/Weak/ModelAssisted tips when policy forbids.

## D9. Toolchain honesty

**Choice:** When `g++`/`cmake` absent, ship code+tests+CMake wiring and record **not-executed** in EVIDENCE; never claim green CI.

## D10. Minimal shared-file changes

**Choice:** Prefer new files; append-only edits to CMakeLists / vocabulary / manifest serialization. Isolate unavoidable shared edits in dedicated commits.

## D11. BenchmarkService persistence rides ExperimentStore

**Choice:** Add additive `benchmark_definitions` / `benchmark_results` tables to ExperimentStore (CREATE IF NOT EXISTS on open; schema_version stays `"1"` so existing DBs are not forced read-only). `BenchmarkService` optionally binds an `ExperimentStore*` and persists on publish/record; `hydrateFromStore()` cold-loads.

**Why:** GOAL asked to persist beyond process-local *if* a natural MLOps store exists. ExperimentStore already owns MetricRecord / promotion / lineage for evaluation evidence — benchmarks are the same family. A second SQLite database would fracture authority.

## D12. Agent tools extend data_platform_tools (not SpatialTool GUI)

**Choice:** Add thin MCP handlers `dataset:qa`, `dataset:sample_query`, GOAL aliases `dataset:versions`/`dataset:splits`, and `benchmark:list|inspect|compare` on the existing `data_platform_tools` surface. Register capability knowledge entries. Do not touch Workbench/MissionContext.

**Why:** Reuses open-store-by-path + bounded page patterns already used by Platform 7.0; keeps D18 UI ownership clean.

## D13. Catalog scale evidence uses N=100k (not 1M) on constrained boxes

**Choice:** Hermetic SampleCatalog stress targets **100000** logical rows with page hard-cap 500. Document and skip 1M when shared-box MemAvailable is only a few GiB.

**Why:** GOAL asks bounded paging toward 100k+ without OOM. 100k proves filter/page/summary stay page-bounded; 1M mainly stresses allocator/RAM on the authoring host without changing the 500-row API contract.

## D14. Scientific benchmark modes are SplitMethod + metadata pins

**Choice:** Cross-region / cross-year / temporal holdout are `LeaveOneRegionOut` / `LeaveOneYearOut` / `Temporal` configs stored as SplitManifests and recorded on `BenchmarkDefinition.metadata` (`benchmark_mode`, `split_method`) plus `forbiddenLeakage`. No second split/leakage engine.

**Why:** Reaffirms D5 for hermetic E2E cheapness; matches SPLIT_MODEL.md.
