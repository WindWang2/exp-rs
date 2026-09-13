# Release Readiness — local verification evidence

* Generated (UTC): 2026-09-13T06:33:20.989892+00:00
* Git SHA: `f0f6869b274c336513e75296fd6d204fcd4b7cfb`
* Host: linux
* **Overall: READY**
* Counts: {"compiled": 36, "passed": 36, "failed": 0, "not_built": 0, "skipped": 0, "timeout": 0, "no_evidence": 0}

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
| io-atomic-failures | `test_io_atomic_failures` | yes | passed |
| io-raster-contract | `test_io_raster_contract` | yes | passed |
| io-grid-descriptor | `test_io_grid_descriptor` | yes | passed |
| io-stac | `test_io_stac` | yes | passed |
| portable-fault-matrix | `test_fault_matrix` | yes | passed |
| sdk-ipc-contract | `test_exprs_ipc` | yes | passed |
| concurrency-stress | `test_concurrency_stress` | yes | passed |
| posix-fault-injection | `test_fault_injection` | yes | passed |
| worker-host-lifecycle | `test_worker_host` | yes | passed |
| visual-cartography | `test_mapspec` | yes | passed |
| bench-quality7 | `benchmark_quality7` | yes | passed |
| bench-scale8 | `benchmark_scale8` | yes | passed |
| contract-graph | `test_contract_platform_9` | yes | passed |
| contract-operator-projection | `test_contract_projection_9` | yes | passed |
| contract-command-reference | `test_command_contract_9` | yes | passed |
| contract-diagnostics-census | `test_diagnostics_contract_9` | yes | passed |
| contract-capability-floors | `test_capability_contract_9` | yes | passed |
| bench-contract9 | `benchmark_contract9` | yes | passed |

## Benchmarks (evidence snapshots, never gates)

### quality7 (schema exp.bench.quality7.v1)

_tier: quick_
* `trace_id_generate`: 671040 ops/s (20000 iters)
* `trace_ndjson_encode`: 2094770.0 ops/s (20000 iters)
* `trace_emit_disabled`: 41228600.0 ops/s (50000 iters)
* `trace_emit_ring`: 602095 ops/s (20000 iters)
* `fault_probe_disarmed`: 302704000.0 ops/s (50000 iters)
* `condition_validate`: 1571980.0 ops/s (10000 iters)
* `condition_evaluate`: 895187 ops/s (20000 iters)
* `resource_uri_parse`: 2039400.0 ops/s (20000 iters)
* `job_dispatch_roundtrip`: 4502480.0 ops/s (200 iters)

### scale8 (schema exp.bench.scale8.v1)

_tier: quick_
* `schedule_1000`: 68975.9 ops/s (1000 iters)
* `dataset_metadata_insert_2000`: 37360.3 ops/s (2000 iters)
* `dataset_metadata_page_read`: 703.564 ops/s (2 iters)
* `raster_window_reads`: 172830 ops/s (64 iters)
* `trace_file_sink_overhead`: 558932 ops/s (2000 iters)

## Platform caveats

* Windows/MSVC: documented from dev-workstation evidence (docs/verification/PLATFORM_EVIDENCE.md); NOT executed on this host unless it is Windows.
* macOS: GDAL 3.13 ladder exercised via PR #834 evidence; NOT executed on this host.
* Optional-feature omissions (OTB, Python worker, ONNX Runtime) follow the CMake option surface of this build tree; absent targets are reported as not-built, never as passing.

> This report equates nothing: `not-built`, `skipped`, `timeout`, `failed` and `passed` are distinct verdicts and stay distinct.
