# TEST MATRIX

## Invariant tests (§18)

| ID | Invariant | Test |
|---|---|---|
| I1 | fingerprint identity | existing `test_execution_fingerprint` + new digest-input cases |
| I2 | destination free | existing + `test_content_cache`: same fp served to 2 paths, byte-equal outputs, both identities valid |
| I3 | revision invalidation | `test_workflow_incremental_cache` + content-cache variant |
| I4 | cache ≡ fresh | `test_fused_chain` + `test_content_cache` payload equivalence |
| I5 | provenance parity fresh/cache/resume/worker/chunk | `test_provenance_parity` |
| I6 | resume ≡ fresh | `test_workflow_recovery` extensions + persistence2 |
| I7 | GC never reaps live | `test_artifact_store` GC cases + existing `test_workflow_artifact_gc` |

## Suite map (new)

- `test_chunk_graph` — ordering, backpressure, halo, cancel, failure propagation, progress
- `test_fused_chain` — eligibility, equivalence, materialization boundaries, checkpoints
- `test_artifact_store` — CRUD, versions, digests, refs, GC, crash (SQLite kill -9 reopen)
- `test_content_cache` — store/serve, digest validation, wrong-path, partial-copy, leases, cross-session
- `test_remote_tile_cache` — LRU bounds, ETag revalidation, coalescing, offline fallback (QTcpServer fake)
- `test_scheduler3` — multi-dim admission, aging, UI reserve, overrides
- `test_gpu_plane` — session reuse, VRAM admission, OOM ladder (fake backend)
- `test_workspace_catalog` — schema, migration, paged query, 100k perf, transactions
- `test_workflow_persistence2` — runs.sqlite, W1-W4 fixes, migration from v1 checkpoints
- `test_worker_host` — protocol, SIGKILL isolation, version mismatch, GPU-family routing
- `test_observability` — event capture, JSON dump, CLI summary, overhead cap
- `test_fault_injection` — FAILURE_MATRIX rows (serial)

## Existing suites that must stay green (regression fence)

test_job_engine, test_task_center, test_execution_plane, test_task_resource_budget,
test_execution_fingerprint, test_workflow_engine_v2, test_workflow_incremental_cache,
test_workflow_recovery, test_workflow_resume_provenance, test_workflow_run_coordinator,
test_workflow_cache_e2e, test_workflow_artifact_gc, test_data_manager (+ family),
test_temporal_workspace, test_catalog_size, test_agent_* (workflow executor/golden).

## Run discipline

- Focused per phase; `-j2`; serial for chaos/bench.
- Full relevant ctest at: mid-epic (after E), and final.
- Perf: benchmark harness before/after per phase C/F/I; numbers into PERF_BASELINE.md.
