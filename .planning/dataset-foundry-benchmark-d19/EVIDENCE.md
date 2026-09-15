# EVIDENCE — D19 local run

## Toolchain

| Check | Result |
|-------|--------|
| `which g++` | **absent** |
| `which cmake` | **absent** |
| Compile / ctest | **not-executed** (honest) |

No online CI dependency. Tests and CMake wiring were added for execution on a machine with the normal exp-rs toolchain.

## What landed (code reviewable)

- Planning: BASELINE, AUDIT_DATASET, DECISIONS, architecture/plan docs
- `DatasetRole` / `AuditVerdict` / `BenchmarkTaskFamily` vocabularies
- Manifest `role` (omitted when Unspecified — fingerprint-stable)
- `FeatureSet` + `joinFeaturesBySampleId` (ambiguous/stale refused)
- `SampleCatalog` bounded filter/page/summary
- `DatasetQaReport` multi-category PASS/WARN/FAIL/UNKNOWN
- `DatasetFoundryService` headless façade
- `BenchmarkDefinition` / `BenchmarkRunner` / `BenchmarkCompare` / `BenchmarkService`
- `ExperimentRun` optional `benchmark_definition_id` / version pins
- Tests: `test_d19_dataset_foundry`, `test_d19_benchmark`

## Scale notes

Catalog test exercises 1000 logical rows with page limit 50 (bounded). 100k+/1M stress remains for a toolchain-capable follow-up (existing `test_dataset_quality_scale` / mlops9_scale patterns).
