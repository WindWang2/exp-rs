# Performance Observatory

A repeatable, machine-readable performance and memory observatory for the
raster I/O, TaskCenter, dataset, temporal and UI layers. The point is to
**quantify before optimizing**: every performance claim this repository makes
should be able to point at a record produced by one command.

It deliberately does not become a benchmark gate on absolute time. Shared CI
machines disagree with each other by more than the signals we care about, so a
millisecond budget produces flakes and then a disabled suite. What this
observatory *does* gate on is machine-independent:

- request counts (tasks dispatched, store queries issued, cache hits/misses)
- complexity exponents measured on a size ladder
- memory bounds expressed in structural units (tiles, pages, buffer slots)
- semantic equivalence against a reference value

## Layout

| Path | What it is |
|---|---|
| `tests/perf/perf_observatory.h` | shared measurement core: scale ladder, portable RSS/CPU/IO sampling, `Sample`, complexity helpers, record writer, structural rule helpers |
| `tests/test_perf_io_observatory.cpp` | raster-I/O and writer workloads (Qt-free, links `Sicnu::Geospatial` only) |
| `tests/test_perf_observatory.cpp` | dataset, governance-store paging, TaskCenter dispatch/DAG, temporal streaming, tiled inference |
| `scripts/bench/perf_observatory_report.py` | `validate` / `merge` / `compare` / `report` |
| `scripts/bench/perf_observatory_verify.py` | the whole oracle as one command |
| `benchmarks/perf-observatory-baseline.md` | the baseline and its hotspot reasoning |

## Running it

```sh
# build (see your platform's build script; the targets are
# test_perf_io_observatory and test_perf_observatory)
cmake --build <build> --target test_perf_io_observatory test_perf_observatory

# run, collecting records
SICNU_OBS_OUT=/tmp/obs-run ctest --test-dir <build> --output-on-failure \
    -R 'obs (io|dataset|governance|taskcenter|temporal|tiled)'
```

```sh
# the whole oracle end to end: two runs, schema validation, structural
# comparison across the runs, and the gate self-checks. One PASS/FAIL line
# per Oracle; the track is done only when every line is PASS twice in a row.
python3 scripts/bench/perf_observatory_verify.py --build <build>
```

Parameters:

| Variable | Meaning |
|---|---|
| `SICNU_OBS_SCALE` | `small` (default, ctest friendly), `mid`, `scale` |
| `SICNU_OBS_OUT` | destination for records; defaults to a private temp dir |
| `SICNU_OBS_SCRATCH` | fixture directory; defaults to the system temp dir |

Nothing is ever written into the repository. Records are plain JSON and
fixtures are deleted by the OS; the worktree stays clean after a run.

## Metrics

Every record carries three blocks.

`measurement` — the timings and resource counters:

| field | source | portability |
|---|---|---|
| `wall_ms` | `std::chrono::steady_clock` | portable |
| `cpu_ms` | `GetProcessTimes` on Windows, `getrusage` elsewhere | portable. Process CPU across ALL threads, so it can exceed `wall_ms` and is not comparable across machines |
| `peak_rss_mb` | peak over a workload, measured against a pre-workload baseline, via `VmRSS` / `GetProcessMemoryInfo` / `mach_task_basic_info` | portable. `null` with a reason when the watermark never rose above the 1 MiB / 2 ms resolution |
| `read_bytes`, `write_bytes` | `/proc/self/io` or `GetProcessIoCounters` | Linux + Windows |
| `io.available`, `io.unavailable_reason` | explicit honesty flag | — |

A metric that a platform cannot provide is recorded as `null` **with a reason**.
The observatory never writes a `0` in place of "unavailable" — a fake zero is
worse than a gap because it looks like a measurement.

`counts` — the structural indicators. These are what make a comparison
meaningful: two runs on the same build must agree on all of them.

| field | meaning |
|---|---|
| `tasks_dispatched` | units of work submitted |
| `pages_requested` | store/DB query requests issued |
| `cache_hits` / `cache_misses` | cache accounting |
| `files_written` | artefacts the workload produced |
| `rows_materialized` | the memory unit in use (rows, or tile height for streamed paths) |

`complexity` — present when a workload measures a two-point size ladder. The
exponent is `log2(t(2N)/t(N))`: `1.0` means linear, `2.0` quadratic. This is the
regression primitive. A workload that becomes O(N²) shows up immediately, and a
machine that is merely 30 % slower shows up not at all.

## Workload catalog

All fixtures are deterministic LCG rasters in a temporary directory, with the
seed baked into the workload definition. There is no wall-clock seed, no RNG
device and no network, so a record is reproducible offline on any lane.

| Workload | Module | Measures | Structural gate |
|---|---|---|---|
| `obs_io_windowed_scan` | raster I/O | tile-walked windowed read | tiles observed == expected; working set = one tile |
| `obs_io_windowed_scan_scaling` | raster I/O | complexity over raster area | exponent < 1.5 (measured 0.00 at these sizes) |
| `obs_io_full_materialize` | raster I/O | whole-raster read as the contrast case | value-equivalent to the windowed scan |
| `obs_io_write_roundtrip` | raster I/O | staged/atomic writer | value-equivalent to an independent replay |
| `obs_dataset_register_scaling` | dataset | catalog registration cost | exponent < 2.5 |
| `obs_dataset_find_by_path_scaling` | dataset | point lookup by path | exponent < 1.5 |
| `obs_dataset_find_by_path_hotspot` | dataset | A/B differential isolating the per-record identity branch | per-record cost measured, both cases resolve |
| `obs_governance_paging` | SQLite + UI | governance-store paging | first page ≤ page size; pages == ceil(rows/pageSize); full drain returns every row |
| `obs_governance_paging_scaling` | SQLite + UI | complexity over table size | worst doubling exponent < 2.2 (measured 1.77) |
| `obs_taskcenter_dispatch` | TaskCenter | batch dispatch, queue wait from job records | tasks completed == tasks submitted |
| `obs_taskcenter_dag` | TaskCenter | parent/child DAG ordering | ordering violations == 0 |
| `obs_temporal_tile_stream` | temporal | K-scene tile streaming | peak slots tile-bounded and unchanged when scenes double; folded means match an independent replay |
| `obs_tiled_inference` | inference | tiled synthetic inference | tiles == expected; working set ≤ 2 tile buffers |

Scale ladders per workload (small / mid / scale) are declared next to each
workload in the test file, and recorded in the `scale` block so a reader always
knows which rung produced a number.

## Reading a report

`perf_observatory_report.py report <dir>` emits Markdown in which every claim is
labelled:

- **MEASURED** — a number the harness produced, with the reproduction command
  that got it.
- **INFERRED** — a mechanism read off source lines. Not a measurement.
- **RECOMMENDED** — a candidate change, only ever offered on top of a measurement
  it is supposed to affect.

That separation is the whole point. A performance PR whose motivation is
"this looks slow" is the failure mode this is designed to make impossible.

## Adding a workload

1. Write the fixture inside a temporary directory, seeded from the workload's
   own LCG constant. No network, no `SICNU_OBS_SCRATCH` writes into the repo.
2. Run the body inside `sicnu::testing::perf::measure(fn)` and fill
   `sample.counts` / `sample.extra` with the structural numbers.
3. Call `writeRecord(...)` with an explicit `structural` block.
4. Add exactly one gate: a count ceiling, a complexity bound, a memory bound in
   structural units, or a semantic comparison. Never a millisecond budget.
5. Record the size ladder in `Ladder{small, mid, scale}`.

## Relationship to the existing harnesses

`tests/test_execution_benchmarks.cpp` (`execution-bench/1`) and
`tests/benchmark_io.cpp` remain the kernel/I-O deep-dives. This observatory adds
the cross-module surface they do not cover: a single schema across modules,
portable metrics, and structural gates that can run on any lane. The two coexist;
neither replaces the other.
