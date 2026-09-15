## Summary

D19 Dataset Foundry & Scientific Benchmark Platform — headless slice on top of existing ADR 0134–0138 / MLOps 9 authorities. Extends dataset roles, feature-table identity joins, bounded sample catalog, multi-category QA, FoundryService, and formal Benchmark Definition/Runner/Compare/Service with ExperimentRun pins. Adds ExperimentStore persistence for benchmarks, agent tool wrappers, and hermetic foundry→benchmark E2E (no Qt GUI). Adds hermetic 100k catalog scale stress, version-evolution lineage, leakage-Fail refusal, and LeaveOne*/Temporal benchmark-mode pins. Does **not** redesign D18 Workbench/MissionContext.

## Baseline

- **baseline SHA:** `ebcafb4d` (`origin/master`)
- branch: `grok/dataset-foundry-benchmark-d19`
- parallel: D18 PR #991 (UI) — overlap minimized

## Existing capabilities reused

- DatasetStore version DAG, samples, annotations, LabelSchema
- Split engine + leakage audit (ADR 0136) — **not duplicated**
- Patch generator, sample promotion, composition/label QA
- EvaluationProtocol / ConfusionMatrix / metrics (evaluation.*)
- ExperimentStore / promotion / matrix / replay seams
- Platform 7.0 `data_platform_tools` MCP surface patterns

## Dataset authority decision

Committed `DatasetVersionRecord` + fingerprinted `DatasetManifest` in DatasetStore remains the sole scientific dataset authority. D19 adds typed `DatasetRole` and foundry façades — no second database.

## Version DAG architecture

Unchanged write path (draft → stage → commit); `createDerivedVersion` / ancestors / children retained. Roles distinguish source/derived/training/benchmark/evaluation on the same DAG. Hermetic Source→Derived→Benchmark lineage covered in tests.

## Sample model

Existing SampleKind payloads retained. New `SampleCatalogRow` + filter/page/summary for bounded 100k+ logical catalogs without loading heavy payloads into UI/agent context. Scale stress uses **N=100000** (1M skipped on constrained authoring RAM).

## Label schema

Existing versioned LabelSchema + annotation pseudo provenance retained. Benchmarks default `refusePseudoLabelsInTest=true`.

## Split/leakage design

Reuse only. QA maps LeakageReport → PASS/WARN/FAIL/UNKNOWN via `auditedChecks` + finding severities (Error → Fail refuses a clean claim). Cross-region/year/temporal modes are LeaveOneRegionOut / LeaveOneYearOut / Temporal configs pinned on BenchmarkDefinition metadata.

## Benchmark architecture

`BenchmarkDefinition` pins dataset/split/label/metrics/protocol/policies → headless `BenchmarkRunner` (predictions in) → `BenchmarkResult` + MetricResult envelope → `BenchmarkService` registry + compare/seed summary. Metrics formulas stay in evaluation.*.

## Experiment/MLOps integration

- Optional `benchmark_definition_id` / `benchmark_definition_version` on ExperimentRun (append-only JSON)
- **Persistence:** ExperimentStore additive tables `benchmark_definitions` / `benchmark_results` (schema_version remains `"1"`); `BenchmarkService` binds store for publish/record/hydrate
- Promotion continues to consume experiment evidence; Foundry/Benchmark services are consumable by D18 later

## Agent tools

Thin extensions on `data_platform_tools` (no Workbench wiring):

- `dataset:qa`, `dataset:sample_query`
- GOAL aliases: `dataset:versions`, `dataset:splits`
- `benchmark:list`, `benchmark:inspect`, `benchmark:compare`

## Tests

- `tests/test_d19_dataset_foundry.cpp` (incl. 100k catalog scale, version evolution, leakage Fail)
- `tests/test_d19_benchmark.cpp` (incl. ExperimentStore persistence)
- `tests/test_d19_foundry_benchmark_chain.cpp` (hermetic E2E + LeaveOne*/Temporal pins; no GUI)
- CMake wired via `sicnu_add_test` (foundry TIMEOUT 300)

## Scale evidence

| Case | N / bound |
|------|-----------|
| Catalog smoke | 1000 rows, page 50 |
| Catalog scale (hermetic) | **100000** rows, page clamp 500 |
| Agent sample_query | scan cap 10k |
| 1M | **not attempted** (~4 GiB MemAvailable on authoring box) |

Compile/ctest **not-executed** (no g++/cmake on this box).

## Review findings

Self-boundary checks only; formal dual-reviewer pass pending toolchain-green evidence.

## Known limitations

- Runner classification path primary; regression/detection task families accepted in definition but not fully exercised in runner yet
- No cmake/g++ on authoring box → compile/ctest **not-executed**
- Formal Reviewer A/B (GOAL §31) still pending green builds
- 1M catalog stress deferred to a toolchain/RAM-capable host

## Follow-ups

- Toolchain build + ctest evidence (incl. 100k scale)
- Optional 1M catalog stress on a high-RAM host
- D18 consumption of FoundryService/BenchmarkService
- Formal Reviewer A/B (GOAL §31)

## Evidence policy

Local evidence only; no online CI dependency.
