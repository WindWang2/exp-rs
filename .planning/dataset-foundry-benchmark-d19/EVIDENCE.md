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

### Slice 2 (prior continuation)
- **Persist BenchmarkService** via ExperimentStore additive tables
  (`benchmark_definitions`, `benchmark_results`) + `BenchmarkResult::fromJson`
- **Agent tool wrappers** on `data_platform_tools`:
  `dataset:qa`, `dataset:sample_query`, `dataset:versions`, `dataset:splits`,
  `benchmark:list`, `benchmark:inspect`, `benchmark:compare`
- Capability knowledge (`data/agent/capabilities/tools.json`) + drift prefixes
- **Hermetic E2E** `test_d19_foundry_benchmark_chain` (foundry→QA→features→benchmark→experiment pins→agent tools; no Qt GUI)
- Persistence unit coverage in `test_d19_benchmark` (`[d19][benchmark][persist]`)

### Slice 3 (this continuation — review hardening)
- **Hermetic SampleCatalog scale stress** at **N=100000** logical rows:
  filter + page (hard cap 500) + deep offset + filtered summary
  (`[d19][foundry][catalog][scale][hermetic]`). **1M not used** on this box
  (~4 GiB MemAvailable shared with other agents; QString-heavy 1M rows risk
  OOM without proving a stronger paging contract than 100k).
- **Version evolution** Source→Derived→Benchmark lineage via FoundryService
  (`[d19][foundry][version][hermetic]`); `createDerivedVersion` still forks
  committed parents.
- **Leakage refusal**: Error findings → QA overall Fail; audited empty →
  leakage Pass (`[d19][foundry][leakage][hermetic]`).
- **LeaveOneRegionOut / LeaveOneYearOut / Temporal** as benchmark-mode config
  pins (metadata + `forbiddenLeakage`; no second split engine)
  (`[d19][e2e][split][leaveone][hermetic]`).
- CMake: `test_d19_dataset_foundry` TIMEOUT 300 for scale case.

## Scale notes

| Contract | Actual N / bound | Notes |
|----------|------------------|-------|
| Catalog unit (slice 1) | 1000 rows, page 50 | smoke |
| Catalog scale stress (slice 3) | **100000** rows, page clamp 500 | hermetic; authored |
| Agent `dataset:sample_query` | scan cap 10k | unchanged |
| 1M catalog | **not attempted** | box RAM headroom ~4 GiB; unnecessary for hard page-cap proof |

Compile/ctest of the 100k case remains **not-executed** here (no g++/cmake).

## cmake/ctest

**not-executed** on this authoring box (`g++`/`cmake` absent). CMake entries:

- `test_d19_dataset_foundry` (TIMEOUT 300)
- `test_d19_benchmark`
- `test_d19_foundry_benchmark_chain`
