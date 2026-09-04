# PHASES — Execution & Data Plane 3.0

Each phase: scope → key files → tests → commit(s). Loop: edit → target build
(-j2) → focused test → commit.

| Phase | Scope | New/changed code | Tests | Status |
|---|---|---|---|---|
| A | Benchmark Harness 2.0: repeatable JSON benchmarks w/ wall/cpu/peakRSS/IO + cache counters; fix determinism (fixed seeds, fixed sizes); before/after diff tool | `tests/test_execution_benchmarks.cpp`, `scripts/perf_report.py` | runs standalone + ctest (small) | done |
| B | Chunk contracts + runner: `src/runtime/chunk/` (TileId, TileWindow+halo, BoundedTileQueue, ChunkGraphRunner, cancel/progress/failure propagation); `chunkCapability()` on RSOperator | `src/runtime/chunk/*`, `src/operators/framework/rs_operator.h` | `test_chunk_graph` (unit: ordering, backpressure, cancel, halo, failure) | done |
| C | Materialization elimination: fused chain execution for eligible operator chains (elementwise); materialize only at boundaries listed in ARCHITECTURE; identity/provenance preserved (one fingerprint per logical step, fused executor replays semantics) | `src/processing/framework/fused_chain_executor.*`, TaskCenter integration | `test_fused_chain` (equivalence fused vs unfused; checkpoint/provenance) | done |
| D | ArtifactStore: SQLite meta + fs payloads, ArtifactId/Version/Digest, lineage, refs, lifecycle | `src/data/artifact_store.*` | `test_artifact_store` | done |
| E | Content-addressed cache: persistent object pool + entry DB, digest validation, leases/refcount, transactional serve, cross-session reuse | `src/data/execution_result_cache2.*`, TaskCenter wiring | `test_content_cache` (wrong-path, stale, partial-copy, GC-lease) | done |
| F | Remote COG tile cache: memory+disk LRU, ETag/LM revalidation, coalescing, bounded prefetch, offline fallback | `src/data/providers/remote_tile_cache.*` | `test_remote_tile_cache` (fake HTTP server via QTcpServer) | done |
| G | Resource Scheduler 3.0: ResourceRequest dims, latency classes, aging fairness, UI reserve, overrides, telemetry | `src/processing/framework/task_resource_budget2.*`, TaskCenter | `test_scheduler3` (fairness, no starvation, caps) | done |
| H | GPU plane: GPUDeviceManager, ModelSessionPool, VRAMBudget, batch queue, OOM ladder | `src/runtime/gpu/*` | `test_gpu_plane` (fake device backend) | done |
| I | Persistent WorkspaceCatalog + DataManager backend + paged model/view for panel | `src/data/workspace_catalog.*`, panel model | `test_workspace_catalog` (100k synthetic rows, paged, UI thread budget) | done |
| J | Persistent workflow runtime: runs.sqlite, task snapshots, resume hardening (W1-W4), migration | `src/workflow/run_store.*`, coordinator/checkpoint changes | `test_workflow_persistence2`, extend recovery tests | done |
| K | Worker host: `sicnu_worker` exe + protocol v1 + LocalWorkerHost executor + crash isolation | `src/runtime/worker/*`, `src/cli/` | `test_worker_host` (SIGKILL worker, protocol version mismatch) | done |
| L | Observability: ExecutionTelemetry ring + JSON dump + CLI summary + agent tool | `src/runtime/observability/*` | `test_observability` | done |
| M | Fault injection/chaos matrix (§17 list) | `tests/test_fault_injection.cpp` + hooks | chaos suite | done |
| R | Review passes 1-4 + fixes | — | rerun affected | — |
| S | Master sync, final full build + ctest + bench, push, PR | — | — | — |

## Risk control

- Any phase that risks destabilizing the spine lands behind an env flag default-off
  (e.g. fused chains behind `SICNU_FUSED_CHAIN=1` until equivalence proven, worker
  isolation opt-in per operator family).
- Each phase keeps `ctest -j2` on affected suites green before commit.
