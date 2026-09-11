# CAPABILITY MATRIX — verification platform 8.0 (baseline @ 322dfd3876)

Legend: Implemented / Partial / Stub / Missing / Refused-by-contract /
Duplicated / Unverified (present but no local evidence on this host).

## A. Cross-platform compile & feature matrix

| Capability | Status | Evidence / gap |
|---|---|---|
| Linux GCC build+test | Implemented | CI tier1; local `build/` GCC Release exists |
| Linux Clang build | Partial | local `build-clang/` exists; no documented lane/matrix tooling |
| Windows MSVC | Unverified (host) | PLATFORM_EVIDENCE.md documents dev-workstation runs; not executable locally |
| macOS | Unverified (host) | mentioned only via Homebrew GDAL note in #834 |
| GDAL version compat | Partial | inline `#if` ladders in `range_cache.cpp` (7) + `raster_reader.cpp` (1); no consolidated first-party seam; no compile-time probe test |
| Compiler-strictness guards (F1) | Missing | no local lane catches missing includes before merge |
| Platform-conditional structure guards (F3) | Missing | no check that all platform branches parse on this host |
| Feature-toggle matrix | Partial | CMake options exist; no documented/automated toggle-combination evidence |

## B. Layered local verification runner

| Capability | Status | Evidence / gap |
|---|---|---|
| Tiered lane definition | Missing | tiers described in TEST_INFRA.md prose, no executable ladder |
| One-command ladder with machine-readable results | Missing | scripts are per-purpose (perf only) |
| Resumability | Missing | — |
| Resource bounding | Partial | ad-hoc `-j` notes in docs; not enforced by a runner |
| Readiness aggregation (not-run != pass) | Missing | WP-I |

## C. Known-answer scientific corpus

| Family | Status | Gap |
|---|---|---|
| Spectral (NDVI/SAVI) | Implemented | — |
| Change detection stats | Implemented | — |
| Terrain slope/aspect | Implemented | — |
| Radiometric calibration | Implemented | — |
| Transition matrix | Implemented | — |
| Temporal stats/regression | Implemented | — |
| Classification metrics (accuracy/kappa) | Implemented (existing test_accuracy_assessment) | corpus doc lists as [existing]; fine |
| Geospatial grid ops (window/readBlock/NoData padding, memory budget) | Missing | no known-answer for RasterReader contracts |
| Dataset split/leakage invariants | Missing | assignByRatio/SpatialBlock/leakage covered by issue tests but not in corpus form with formula facts separated |
| Cartographic structural results | Missing | layout solver/token resolution have contract tests but no closed-form structural corpus entries |
| SAR speckle/geometry | Implemented (existing suites) | corpus doc cross-references; keep |

## D. Deterministic contract fuzzing

| Seam | Status |
|---|---|
| ResourceUri + redaction | Implemented (fuzz_io) |
| MapSpec/condition AST + placeholders | Implemented (fuzz_lang, fuzz_agent) |
| Dataset manifest | Implemented (fuzz_data) |
| AgentPlan | Implemented (fuzz_agent) |
| Operator schemas / parameter projection | Missing |
| Model manifests | Missing |
| Worker/plugin IPC envelopes | Missing |
| JSON version migrations | Missing |
| Path containment (publish/staging paths) | Partial (resolveAgainst cage only) |
| Remote metadata / STAC pagination | Missing |
| Split manifests | Missing |

## E. Fault injection / failure matrix

| Seam | Status |
|---|---|
| OutputCommitter / ArtifactPool / WorkflowCheckpoint | Implemented (5 SICNU_FAULT_POINT sites + test_fault_matrix) |
| DatasetStore / ExperimentStore transactions | Missing |
| ExperimentRunRecorder | Missing |
| Range cache / remote | Refused-by-contract for HTTP live (owned by io track; deterministic cache-level probe possible) |
| Model provider boundary | Missing |
| TaskCenter terminal transitions | Partial (stress tests race paths; no injected failure at mark*) |
| Worker/plugin host | Implemented-POSIX (existing suites); portable typed-failure covered by test_worker_host |

## F. Unified observability

| Chain link | Adapter status |
|---|---|
| ExecutionPlane (harness entry) | Implemented |
| JobEngine (operator span) | Implemented |
| Workflow run lifecycle | Missing |
| TaskCenter submit/dispatch/terminal | Missing |
| OutputCommitter publish/rollback | Missing |
| Dataset/Experiment artifact registration | Missing |
| Model inference | Missing (via job_engine op id only) |

## G. Performance & scale baselines

| Measurement | Status |
|---|---|
| trace/fault/condition/uri/job_dispatch micro | Implemented (benchmark_quality7, quality7.json — Windows debug host) |
| Short-task scheduling 1k/10k/100k | Missing |
| Geospatial window/range reads | Partial (test_perf_benchmarks covers some IO; no documented complexity note) |
| Dataset 100k metadata | Missing |
| Model tiling / cartography compile / help search / worker startup | Missing or scattered |
| Linux evidence for the above | Missing (quality7.json is Windows-only so far) |

## H. Test integrity / anti-vacuity

| Item | Status |
|---|---|
| #814/#822 numerical-tolerance repairs | Implemented |
| Remaining non-null-JSON-only assertions | Unknown — sweep planned |
| Silent skip accounting for optional deps | Partial — ctest reports skips; no aggregate readiness view |

## I. Release readiness report

Missing entirely — WP-I.
