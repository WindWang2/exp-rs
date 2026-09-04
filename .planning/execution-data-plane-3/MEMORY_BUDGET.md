# MEMORY BUDGET — build & runtime resource discipline

## Host

62 GB RAM total (~33 GB available during work, other epics co-running),
16 logical CPUs, 224 GB free disk. Swap already in use ⇒ **be conservative**.

## Build

- `CMAKE_BUILD_PARALLEL_LEVEL=2`; `cmake --build build --parallel 2`.
- Target builds preferred: `cmake --build build --target sicnu_runtime
  test_chunk_graph -j2` etc. Full builds only at phase milestones.
- ccache (4.13.6) enabled; vendored qgis_core objects cached from baseline build.
- If RSS pressure observed (`free -h` available < 8 GB): drop to `-j1`.
- Never configure OTB/ITK vendored build (SICNU_BUILD_OTB=OFF); OTB tests skip.

## Tests

- Default `ctest -j2` (parallel 2). Heavy suites (`test_perf_benchmarks`,
  `python_*`, `test_workflow_resume_provenance`, chaos/fault suites) `RUN_SERIAL`
  or `ctest -j1`.
- Chaos tests that spawn workers or send SIGKILL must run serially to avoid
  interfering with neighbors.
- Watchdog: any single test > 5 min ⇒ investigate before continuing.

## Runtime budgets (code-level, enforced in Phase B/G)

- Chunk queue bounded (tiles in flight ≤ small constant; bytes cap via budget).
- Telemetry ring buffer fixed capacity (e.g. 64k events, overwrite oldest).
- Tile caches bounded by bytes (memory LRU) and disk quota with eviction.
- Catalog queries always paged (LIMIT/OFFSET or keyset); no unbounded QVector
  growth on 100k assets in GUI paths (paged fetch, model/view).
- Progress/log buffers: cap retained log lines per task (existing ADR 0052
  mechanism; add automatic prune at cap).
- GPU: VRAM admission before session create; batch window bounded.

## Observability overhead rule

Telemetry must be O(1) amortized per event, no allocation on hot path beyond
ring slot, no locks shared with execution critical path (single-writer).
