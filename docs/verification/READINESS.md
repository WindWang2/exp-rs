# Release Readiness — local verification evidence

* Generated (UTC): 2026-09-16T07:02:56.302663+00:00
* Git SHA: `d2868c7445f496c1742f04c94d23bc6e14bda3ec`
* Host: win32
* **Overall: ATTENTION**
* Counts: {"compiled": 26, "passed": 21, "failed": 5, "not_built": 19, "skipped": 0, "timeout": 1, "no_evidence": 0}

| Capability | Artifact | Compiled here | Executed status |
|---|---|---|---|
| header-self-containment | `sicnu_header_probes` | NO | passed |
| trace-contract | `test_trace_contract` | yes | passed |
| fault-registry | `test_fault_registry` | yes | passed |
| diagnostic-report | `test_diagnostic_report` | yes | passed |
| fuzz-resource-uri | `test_contract_fuzz_io` | yes | passed |
| fuzz-condition-ast | `test_contract_fuzz_lang` | yes | passed |
| fuzz-dataset-manifest | `test_contract_fuzz_data` | yes | passed |
| known-answer-corpus | `test_known_answer_corpus` | yes | passed |
| portability-contract | `test_portability_contract` | yes | passed |
| fuzz-worker-ipc-splits | `test_contract_fuzz_ipc` | yes | timeout |
| fuzz-operator-schemas | `test_contract_fuzz_ops` | NO | not_built |
| known-answer-corpus-8 | `test_known_answer_corpus_8` | yes | passed |
| trace-chain-8 | `test_trace_chain_8` | yes | passed |
| io-uri | `test_io_uri` | NO | not_built |
| io-paths | `test_io_paths` | NO | not_built |
| io-range-cache | `test_io_range_cache` | NO | not_built |
| io-remote-range | `test_io_remote_range` | NO | not_built |
| io-remote-validator | `test_io_remote_validator` | NO | not_built |
| io-atomic-failures | `test_io_atomic_failures` | NO | not_built |
| io-raster-contract | `test_io_raster_contract` | NO | not_built |
| io-grid-descriptor | `test_io_grid_descriptor` | NO | not_built |
| io-stac | `test_io_stac` | NO | not_built |
| portable-fault-matrix | `test_fault_matrix` | NO | not_built |
| sdk-ipc-contract | `test_exprs_ipc` | NO | not_built |
| concurrency-stress | `test_concurrency_stress` | NO | not_built |
| posix-fault-injection | `test_fault_injection` | NO | not_built |
| worker-host-lifecycle | `test_worker_host` | NO | not_built |
| visual-cartography | `test_mapspec` | NO | not_built |
| bench-quality7 | `benchmark_quality7` | NO | not_built |
| bench-scale8 | `benchmark_scale8` | NO | not_built |
| contract-graph | `test_contract_platform_9` | yes | failed |
| contract-operator-projection | `test_contract_projection_9` | yes | failed |
| contract-command-reference | `test_command_contract_9` | yes | failed |
| contract-diagnostics-census | `test_diagnostics_contract_9` | yes | failed |
| contract-capability-floors | `test_capability_contract_9` | yes | passed |
| bench-contract9 | `benchmark_contract9` | NO | not_built |
| scientific-contract-10 | `test_scientific_contract_10` | yes | passed |
| drift-projection-10 | `test_drift_projection_10` | yes | failed |
| science-verification-10 | `test_science_verification_10` | yes | passed |
| contract-census-11 | `test_contract_census_11` | yes | passed |
| contract-determinism-11 | `test_contract_determinism_11` | yes | passed |
| metamorphic-11 | `test_verification_metamorphic_11` | yes | passed |
| numeric-reference-11 | `test_verification_numeric_reference_11` | yes | passed |
| mutation-kill-11 | `test_mutation_kill_11` | yes | passed |
| failure-contract-11 | `test_verification_failure_11` | yes | passed |
| cross-surface-11 | `test_contract_cross_surface_11` | yes | passed |

## Benchmarks (evidence snapshots, never gates)

_No benchmark artifacts found in the build tree._

## Platform caveats

* Windows/MSVC: documented from dev-workstation evidence (docs/verification/PLATFORM_EVIDENCE.md); NOT executed on this host unless it is Windows.
* macOS: GDAL 3.13 ladder exercised via PR #834 evidence; NOT executed on this host.
* Optional-feature omissions (OTB, Python worker, ONNX Runtime) follow the CMake option surface of this build tree; absent targets are reported as not-built, never as passing.

> This report equates nothing: `not-built`, `skipped`, `timeout`, `failed` and `passed` are distinct verdicts and stay distinct.
