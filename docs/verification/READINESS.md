# Release Readiness — local verification evidence

* Generated (UTC): 2026-09-13T19:46:19.589011+00:00
* Git SHA: `7cca494625c40f3c97646584ca0e9296a7f9b349`
* Host: linux
* **Overall: INSUFFICIENT-EVIDENCE**
* Counts: {"compiled": 23, "passed": 20, "failed": 0, "not_built": 16, "skipped": 3, "timeout": 0, "no_evidence": 0}

| Capability | Artifact | Compiled here | Executed status |
|---|---|---|---|
| header-self-containment | `sicnu_header_probes` | yes | passed |
| trace-contract | `test_trace_contract` | yes | passed |
| fault-registry | `test_fault_registry` | yes | passed |
| diagnostic-report | `test_diagnostic_report` | yes | passed |
| fuzz-resource-uri | `test_contract_fuzz_io` | yes | passed |
| fuzz-condition-ast | `test_contract_fuzz_lang` | yes | passed |
| fuzz-dataset-manifest | `test_contract_fuzz_data` | yes | passed |
| known-answer-corpus | `test_known_answer_corpus` | yes | passed |
| portability-contract | `test_portability_contract` | yes | passed |
| fuzz-worker-ipc-splits | `test_contract_fuzz_ipc` | yes | passed |
| fuzz-operator-schemas | `test_contract_fuzz_ops` | yes | skipped |
| known-answer-corpus-8 | `test_known_answer_corpus_8` | yes | passed |
| trace-chain-8 | `test_trace_chain_8` | yes | passed |
| io-uri | `test_io_uri` | NO | not_built |
| io-paths | `test_io_paths` | NO | not_built |
| io-range-cache | `test_io_range_cache` | NO | not_built |
| io-remote-range | `test_io_remote_range` | NO | not_built |
| io-remote-validator | `test_io_remote_validator` | NO | not_built |
| io-atomic-failures | `test_io_atomic_failures` | NO | not_built |
| io-raster-contract | `test_io_raster_contract` | yes | skipped |
| io-grid-descriptor | `test_io_grid_descriptor` | NO | not_built |
| io-stac | `test_io_stac` | NO | not_built |
| portable-fault-matrix | `test_fault_matrix` | yes | skipped |
| sdk-ipc-contract | `test_exprs_ipc` | NO | not_built |
| concurrency-stress | `test_concurrency_stress` | NO | not_built |
| posix-fault-injection | `test_fault_injection` | NO | not_built |
| worker-host-lifecycle | `test_worker_host` | NO | not_built |
| visual-cartography | `test_mapspec` | NO | not_built |
| bench-quality7 | `benchmark_quality7` | NO | not_built |
| bench-scale8 | `benchmark_scale8` | NO | not_built |
| contract-graph | `test_contract_platform_9` | yes | passed |
| contract-operator-projection | `test_contract_projection_9` | yes | passed |
| contract-command-reference | `test_command_contract_9` | yes | passed |
| contract-diagnostics-census | `test_diagnostics_contract_9` | yes | passed |
| contract-capability-floors | `test_capability_contract_9` | yes | passed |
| bench-contract9 | `benchmark_contract9` | NO | not_built |
| scientific-contract-10 | `test_scientific_contract_10` | yes | passed |
| drift-projection-10 | `test_drift_projection_10` | yes | passed |
| science-verification-10 | `test_science_verification_10` | yes | passed |

## Benchmarks (evidence snapshots, never gates)

_No benchmark artifacts found in the build tree._

## Platform caveats

* Windows/MSVC: documented from dev-workstation evidence (docs/verification/PLATFORM_EVIDENCE.md); NOT executed on this host unless it is Windows.
* macOS: GDAL 3.13 ladder exercised via PR #834 evidence; NOT executed on this host.
* Optional-feature omissions (OTB, Python worker, ONNX Runtime) follow the CMake option surface of this build tree; absent targets are reported as not-built, never as passing.

> This report equates nothing: `not-built`, `skipped`, `timeout`, `failed` and `passed` are distinct verdicts and stay distinct.
