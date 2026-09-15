## Summary

D19 Dataset Foundry & Scientific Benchmark Platform — first coherent headless slice on top of existing ADR 0134–0138 / MLOps 9 authorities. Extends dataset roles, feature-table identity joins, bounded sample catalog, multi-category QA, FoundryService, and formal Benchmark Definition/Runner/Compare/Service with ExperimentRun pins. Does **not** redesign D18 Workbench/MissionContext.

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

## Dataset authority decision

Committed `DatasetVersionRecord` + fingerprinted `DatasetManifest` in DatasetStore remains the sole scientific dataset authority. D19 adds typed `DatasetRole` and foundry façades — no second database.

## Version DAG architecture

Unchanged write path (draft → stage → commit); `createDerivedVersion` / ancestors / children retained. Roles distinguish source/derived/training/benchmark/evaluation on the same DAG.

## Sample model

Existing SampleKind payloads retained. New `SampleCatalogRow` + filter/page/summary for bounded 100k+ logical catalogs without loading heavy payloads into UI/agent context.

## Label schema

Existing versioned LabelSchema + annotation pseudo provenance retained. Benchmarks default `refusePseudoLabelsInTest=true`.

## Split/leakage design

Reuse only. QA maps LeakageReport → PASS/WARN/FAIL/UNKNOWN via `auditedChecks` + finding severities.

## Benchmark architecture

`BenchmarkDefinition` pins dataset/split/label/metrics/protocol/policies → headless `BenchmarkRunner` (predictions in) → `BenchmarkResult` + MetricResult envelope → `BenchmarkService` registry + compare/seed summary. Metrics formulas stay in evaluation.*.

## Experiment/MLOps integration

Optional `benchmark_definition_id` / `benchmark_definition_version` on ExperimentRun (append-only JSON). Promotion continues to consume experiment evidence; Foundry/Benchmark services are consumable by D18 later.

## Tests

- `tests/test_d19_dataset_foundry.cpp`
- `tests/test_d19_benchmark.cpp`
- CMake wired via `sicnu_add_test`

## Scale evidence

Catalog paging bounded (test: 1000 rows, page 50). Larger stresses not-executed on this box (no g++/cmake).

## Review findings

Self-boundary checks only; formal dual-reviewer pass pending toolchain-green evidence.

## Known limitations

- BenchmarkService is process-local (persist via ExperimentStore artifacts follow-up)
- Runner classification path primary; regression/detection task families accepted in definition but not fully exercised in runner yet
- Agent tool wrappers (`dataset:inspect`, …) deferred — service API ready
- No cmake/g++ on authoring box → compile/ctest **not-executed**

## Follow-ups

- Toolchain build + ctest evidence; scale catalog/join tests
- Persist benchmark definitions/results in ExperimentStore
- Agent tool seams; D18 consumption of FoundryService/BenchmarkService
- Cross-region/year/sensor E2E fixtures using existing LeaveOne* splits
- Formal Reviewer A/B (GOAL §31)

## Evidence policy

Local evidence only; no online CI dependency.
