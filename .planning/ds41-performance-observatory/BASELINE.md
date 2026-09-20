# ds41-performance-observatory — BASELINE

> Live snapshot, refreshed at 2026-09-20 (track start). Not a substitute for a
> re-read; the workflow re-fetches before completion.

## Repository state at track start

| Item | Value |
|---|---|
| Repo | `https://github.com/WindWang2/exp-rs` |
| `origin/master` at fetch | `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` (`adf8f9895`) |
| Subject | `docs(agents): record Platform 5.0 audit request` |
| Track worktree | `C:/Users/wangj.KEVIN/projects/exp-rs-worktrees/ds41-performance-observatory` |
| Track branch | `agent/ds41-performance-observatory` |
| Open PRs | **0** |
| Open issues | **0** |
| Local checkout | clean at `adf8f9895` (worktree created by this track) |

## Recent master history (last 30, condensed)

```
adf8f9895 docs(agents): record Platform 5.0 audit request
fe7da0622 feat(agent): add StepFun preset provider profile
2761a6857 Merge PR #1115 fix: deep-review wave-2 investigation targets (#1097)
28d3ffcb9 fix: deep-review wave-2 investigation targets (#1097)
08911c838 Merge PR #1113 fix(ops/workflow/agent): issues #1076–#1080, #1095
8a52c5b47 Merge PR #1112 fix(app): openProject, edit commits, pipeline wires, georef
4faa6a4a5 Merge PR #1111 fix: packaging/processing/ops issues #1087–#1096
9a9d2e811 Merge PR #1110 fix(geospatial): mirror manifest I/O, range_cache TOCTOU, httpFetch truncated
0621e7577 fix(python): restart-timer id re-resolve; namespace SHM geometry keys
b46877130 fix(plugins): allow one execution after mid-run worker recovery
7a1fe7e15 fix(agent): require band for histogram_shape; guard empty walk
61a8c9b0d fix(workflow): unwedge resume/cancel state-machine dead ends
a5173d2f9 fix(workflow): include port wiring in lineage signatures
8f59d9ce3 fix(operators): scan only tile w*h in EVI/SAVI scale probe
c144696e8 fix(app): openProject fail-closed, edit-commit checks, wire tracking, georef view fixes
b161f1d1e fix(experiment): check QFile::write results for lab reports
d3ec14ddc fix(gpu): honor device pin and shared-session eviction safety
406ddbf28 fix(operators): close spectral output-contract gaps
1cc66ccb2 fix(processing): count pre-warmed workers in LocalWorkerPool::m_alive
2376c021c fix(processing): close TaskCenter id-keyed cleanup gaps
...
```

## Recent merged PRs relevant to a performance observatory

| PR | Title | Relevance |
|---|---|---|
| #1115 / #1113 / #1112 / #1111 / #1110 | fail-closed waves #1076–#1097 | No perf-harness work; correctness/lifetime only |
| #1009 | `feat(execution-11)`: execution runtime convergence — per-chunk telemetry, worker lease, resource governance | **Provides the telemetry substrate** (`ExecutionTelemetry`, `ChunkPipeline`). No cross-module benchmark harness |
| #980 | `feat(execution)`: large-scale execution / external-memory / multi-worker engine | Scale tests exist (`test_large_scale_execution_10`) but no shared catalog/format |
| #981 | `feat(models)`: EO AI model runtime | `tests/test_model_runtime_bench.cpp` with its own ad-hoc `model-runtime-bench/*` schema |
| #1023 | `feat(fabric)`: cloud data fabric + VSI-object range cache | `RemoteRangeCache::telemetryJson()` — the only cache-metrics API in the raster path |
| #1019 | `feat(packaging)`: offline bundle schema /2, env-doctor | Consumes the same perf records? No. Independent |
| #992 | `D19: Dataset Foundry & Scientific Benchmark Platform` | **Closest analogue** — but it is a *data foundry + benchmark dataset* track, not a *cross-module perf/memory observatory* |

## Existing perf assets in the repo (census result)

Already present — this track must **reuse**, not rebuild:

1. `tests/test_execution_benchmarks.cpp` (1134 lines, 15 `[execution_bench]` cases,
   schema `execution-bench/1`). Emits wall/cpu/peak-RSS-delta/read/write bytes +
   cache hit/miss per workload into `$SICNU_EXEC_BENCH_OUT`.
   **Not built on Windows** (`if(NOT WIN32)` at `tests/CMakeLists.txt:2877`,
   because of the POSIX `mini_cog_server.h` remote-read fixtures).
2. `tests/test_perf_benchmarks.cpp` (operator-kernel benchmarks, `[benchmark]`).
3. `tests/benchmark_io.cpp` — manual I/O harness (`--out benchmarks/io-foundation-4.json`).
4. `tests/test_ui_scale_benchmark.cpp` — UI paging benchmark with **structural
   (non-timed) guards**; documented in `benchmarks/ui-scale-5.0.md`.
5. `tests/benchmark_contract9.cpp`, `benchmark_quality7.cpp`, `benchmark_temporal10.cpp`,
   `benchmark_scale8.cpp` — standalone Catch2 harnesses.
6. `scripts/perf_report.py` — merges/diffs `execution-bench/1` collections.
7. `scripts/run_perf_baseline.sh` — baseline runner.
8. `benchmarks/*.json` — 16 historical records, all ad-hoc schemas.

### Metrics already available in production code

| Metric | Source | Portability |
|---|---|---|
| RSS now | `sicnu::ResourceMonitor::currentRssMb()` | Linux/macOS/Windows |
| CPU time | `clock()` | portable |
| wall | `std::chrono::steady_clock` | portable |
| bytes read/written | `/proc/self/io` (harness-side only) | **Linux only** |
| cache hits/misses (result cache) | `ExecutionTelemetry::counters()` delta, or `resultPayload["cache"]` | portable |
| per-chunk telemetry | `sicnu::runtime::observability::ExecutionTelemetry` (`SICNU_TELEMETRY=1`) | portable |
| remote range-cache hits/misses | `RemoteRangeCache::telemetryJson()` | portable |
| queue wait | derived from `JobRecord{createdAtMs,startedAtMs,finishedAtMs}` via `JobEngine::snapshot()` | portable |
| SQL statements per page | none (see below) | — |

### Real gaps found (source-level, with evidence)

1. **No unified baseline record.** Five mutually incompatible JSON schemas
   exist (`execution-bench/1`, `model-runtime-bench/1`, `model-runtime-bench-ort/1`,
   `io-foundation-*`, ui-scale). No environment block is consistent.
2. **No portable harness.** The best harness (`test_execution_benchmarks`) is
   compiled out of Windows, so this track (developed on Windows/MSVC) cannot use
   it as a base. A new target must be socket-free and POSIX-free.
3. **`DataManager::publishSnapshot()` is O(N) per mutation**
   (`src/data/data_manager.cpp:210-224` — copies every `records` vector on each
   `registerSource`), giving **O(N²)** for N registrations. Measured: 20 000
   registrations = 2740 ms (`benchmarks/data_manager_register.json`).
4. **`DataManager::findByPath()` redoes identity work per record**
   (`src/data/data_manager.cpp:821-872`): `virtualPathAliases(stored)` allocates a
   `QStringList` per record, and `QFileInfo(stored).canonicalFilePath()` performs a
   **filesystem stat per record**. N assets ⇒ O(N) stats per probe. Measured:
   38.9 ms per probe at 20 000 assets (`benchmarks/data_manager_find_by_path.json`).
5. **No query-count seam on the SQL stores** (`GovernanceStore`,
   `WorkspaceCatalog`, `DatasetStore`) — each raw `sqlite3` C API, no counter.
6. **No regression rules** beyond `test_ui_scale_benchmark`'s inline structural
   guards: nothing measures complexity scaling or request-count ceilings.

## Local build environment (constraint)

- Windows 11 x64, MSVC 14.38.33130, **Debug**, generator Ninja.
- `C:\Qt\Tools\Ninja\ninja.exe`, `C:\Qt\Tools\CMake_64\bin\cmake.exe` (not on `PATH`).
- Deps from `build-dev/vcpkg_installed` (3 GB, vcpkg manifest mode, `x64-windows`).
  A new worktree build dir must share that installed tree (`VCPKG_INSTALLED_DIR`)
  or pay a full re-install.
- Already-built in-tree objects in `build-dev`: 5114 `.obj`; `sicnu_*` static libs present.
