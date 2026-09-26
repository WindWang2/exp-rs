# Release Readiness — local verification evidence

* Generated (UTC): 2026-09-26T21:05:06.215035+00:00
* Git SHA: `774879cc28e02c2a3acce1763455311a03b9cb1a`
* Host: linux
* **Overall: ATTENTION**
* Counts: {"compiled": 46, "passed": 40, "failed": 5, "not_built": 0, "skipped": 1, "timeout": 0, "no_evidence": 0}

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
| fuzz-operator-schemas | `test_contract_fuzz_ops` | yes | passed |
| known-answer-corpus-8 | `test_known_answer_corpus_8` | yes | passed |
| trace-chain-8 | `test_trace_chain_8` | yes | passed |
| io-uri | `test_io_uri` | yes | passed |
| io-paths | `test_io_paths` | yes | passed |
| io-range-cache | `test_io_range_cache` | yes | passed |
| io-remote-range | `test_io_remote_range` | yes | passed |
| io-remote-validator | `test_io_remote_validator` | yes | passed |
| io-atomic-failures | `test_io_atomic_failures` | yes | failed |
| io-raster-contract | `test_io_raster_contract` | yes | passed |
| io-grid-descriptor | `test_io_grid_descriptor` | yes | passed |
| io-stac | `test_io_stac` | yes | passed |
| portable-fault-matrix | `test_fault_matrix` | yes | passed |
| sdk-ipc-contract | `test_exprs_ipc` | yes | passed |
| concurrency-stress | `test_concurrency_stress` | yes | passed |
| posix-fault-injection | `test_fault_injection` | yes | passed |
| worker-host-lifecycle | `test_worker_host` | yes | passed |
| visual-cartography | `test_mapspec` | yes | skipped |
| bench-quality7 | `benchmark_quality7` | yes | passed |
| bench-scale8 | `benchmark_scale8` | yes | passed |
| contract-graph | `test_contract_platform_9` | yes | failed |
| contract-operator-projection | `test_contract_projection_9` | yes | passed |
| contract-command-reference | `test_command_contract_9` | yes | failed |
| contract-diagnostics-census | `test_diagnostics_contract_9` | yes | failed |
| contract-capability-floors | `test_capability_contract_9` | yes | passed |
| bench-contract9 | `benchmark_contract9` | yes | passed |
| scientific-contract-10 | `test_scientific_contract_10` | yes | passed |
| drift-projection-10 | `test_drift_projection_10` | yes | passed |
| science-verification-10 | `test_science_verification_10` | yes | passed |
| contract-census-11 | `test_contract_census_11` | yes | passed |
| contract-determinism-11 | `test_contract_determinism_11` | yes | passed |
| metamorphic-11 | `test_verification_metamorphic_11` | yes | passed |
| numeric-reference-11 | `test_verification_numeric_reference_11` | yes | passed |
| mutation-kill-11 | `test_mutation_kill_11` | yes | passed |
| failure-contract-11 | `test_verification_failure_11` | yes | failed |
| cross-surface-11 | `test_contract_cross_surface_11` | yes | passed |

## Benchmarks (evidence snapshots, never gates)

_No benchmark artifacts found in the build tree._

## Platform caveats

* Windows/MSVC: documented from dev-workstation evidence (docs/verification/PLATFORM_EVIDENCE.md); NOT executed on this host unless it is Windows.
* macOS: GDAL 3.13 ladder exercised via PR #834 evidence; NOT executed on this host.
* Optional-feature omissions (OTB, Python worker, ONNX Runtime) follow the CMake option surface of this build tree; absent targets are reported as not-built, never as passing.

> This report equates nothing: `not-built`, `skipped`, `timeout`, `failed` and `passed` are distinct verdicts and stay distinct.
