# Release Readiness — local verification evidence

* Generated (UTC): 2026-09-10T21:43:07.554488+00:00
* Git SHA: `95307764f1e92e105e0587241e1c35143089b4e3`
* Host: linux
* **Overall: ATTENTION**
* Counts: {"compiled": 30, "passed": 25, "failed": 1, "not_built": 0, "skipped": 0, "timeout": 4, "no_evidence": 0}

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
| io-range-cache | `test_io_range_cache` | yes | timeout |
| io-remote-range | `test_io_remote_range` | yes | timeout |
| io-remote-validator | `test_io_remote_validator` | yes | timeout |
| io-atomic-failures | `test_io_atomic_failures` | yes | passed |
| io-raster-contract | `test_io_raster_contract` | yes | passed |
| io-grid-descriptor | `test_io_grid_descriptor` | yes | passed |
| io-stac | `test_io_stac` | yes | passed |
| portable-fault-matrix | `test_fault_matrix` | yes | passed |
| sdk-ipc-contract | `test_exprs_ipc` | yes | passed |
| concurrency-stress | `test_concurrency_stress` | yes | passed |
| posix-fault-injection | `test_fault_injection` | yes | passed |
| worker-host-lifecycle | `test_worker_host` | yes | passed |
| visual-cartography | `test_mapspec` | yes | failed |
| bench-quality7 | `benchmark_quality7` | yes | passed |
| bench-scale8 | `benchmark_scale8` | yes | timeout |

## Benchmarks (evidence snapshots, never gates)

### quality7 (schema exp.bench.quality7.v1)

* `trace_id_generate`: 314302 ops/s (200000 iters)
* `trace_ndjson_encode`: 341441 ops/s (200000 iters)
* `trace_emit_disabled`: 12598300.0 ops/s (500000 iters)
* `trace_emit_ring`: 177205 ops/s (200000 iters)
* `fault_probe_disarmed`: 20280000.0 ops/s (500000 iters)
* `condition_validate`: 187483 ops/s (100000 iters)
* `condition_evaluate`: 109410 ops/s (200000 iters)
* `resource_uri_parse`: 981256 ops/s (200000 iters)
* `job_dispatch_roundtrip`: 2310300.0 ops/s (2000 iters)

### scale8 (schema exp.bench.scale8.v1)

* `schedule_1000`: 26214.7 ops/s (1000 iters)
* `schedule_10000`: 388.946 ops/s (10000 iters)
* `schedule_20000`: 104.587 ops/s (20000 iters)
* `dataset_metadata_insert_20000`: 15006.6 ops/s (20000 iters)
* `dataset_metadata_page_read`: 44.5734 ops/s (20 iters)
* `raster_window_reads`: 152618 ops/s (256 iters)
* `trace_file_sink_overhead`: 220437 ops/s (10000 iters)

## Platform caveats

* Windows/MSVC: documented from dev-workstation evidence (docs/verification/PLATFORM_EVIDENCE.md); NOT executed on this host unless it is Windows.
* macOS: GDAL 3.13 ladder exercised via PR #834 evidence; NOT executed on this host.
* Optional-feature omissions (OTB, Python worker, ONNX Runtime) follow the CMake option surface of this build tree; absent targets are reported as not-built, never as passing.

> This report equates nothing: `not-built`, `skipped`, `timeout`, `failed` and `passed` are distinct verdicts and stay distinct.
