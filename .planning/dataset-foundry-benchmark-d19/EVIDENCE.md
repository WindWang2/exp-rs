# EVIDENCE — D19 local run

## Toolchain

| Check | Result |
|-------|--------|
| `which g++` | **absent** |
| `which cmake` | **absent** |
| Compile / ctest | **not-executed** (honest) |

No online CI dependency. Tests and CMake wiring were added for execution on a machine with the normal exp-rs toolchain.

## What landed (code reviewable)

### Slice 1 (prior)
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

### Slice 2 (this continuation)
- **Persist BenchmarkService** via ExperimentStore additive tables
  (`benchmark_definitions`, `benchmark_results`) + `BenchmarkResult::fromJson`
- **Agent tool wrappers** on `data_platform_tools`:
  `dataset:qa`, `dataset:sample_query`, `dataset:versions`, `dataset:splits`,
  `benchmark:list`, `benchmark:inspect`, `benchmark:compare`
- Capability knowledge (`data/agent/capabilities/tools.json`) + drift prefixes
- **Hermetic E2E** `test_d19_foundry_benchmark_chain` (foundry→QA→features→benchmark→experiment pins→agent tools; no Qt GUI)
- Persistence unit coverage in `test_d19_benchmark` (`[d19][benchmark][persist]`)

## Scale notes

Catalog test exercises 1000 logical rows with page limit 50 (bounded). Agent `dataset:sample_query` scans at most 10k rows per call. 100k+/1M stress remains for a toolchain-capable follow-up.

## cmake/ctest

**not-executed** on this authoring box (`g++`/`cmake` absent). CMake entries:

- `test_d19_dataset_foundry`
- `test_d19_benchmark`
- `test_d19_foundry_benchmark_chain`
