# PERFORMANCE — host, commands, and resource discipline (8.0)

## Evidence host

- Linux 6.18 LTS x64, 16 cores, 62 GB RAM
- Clang 22.1.8 (primary lane), GCC 16.2.1 (documented; flaky ICEs — see D1
  in PLAN.md), CMake 4.4.3, Ninja, ccache 4.14 (warm, 5.2 GB)
- GDAL 3.13.3 (system)

## Commands used (reproducible)

```bash
# configure (Clang primary)
cmake -B build-clang -S . -G Ninja -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_LTO=OFF -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++

# targeted build of all lane targets (bounded parallelism)
cmake --build build-clang --target <lane targets...> --parallel 12

# verification ladder (L0..L7; L6/L8 optional)
python3 scripts/verification_ladder.py --build-dir build-clang --json ladder.json

# benchmarks
./build-clang/tests/benchmark_quality7 --out build-clang/benchmarks/quality7.json
./build-clang/tests/benchmark_scale8   --out build-clang/benchmarks/scale8.json

# readiness report
python3 scripts/collect_readiness.py --build-dir build-clang \
  --ladder-results ladder.json
```

## Resource bounds actually applied

- ONE heavy build at a time; `-j12` compile, tests strictly sequential
  (ladder runs one process at a time, ctest lane `-j1`)
- fuzzers: fixed seeds, ≤ 512-byte inputs, ≤ 400 iterations/seed
- benchmarks: 100k-class operations are in-memory/SQLite or 1 MiB rasters —
  no 100GB-class data anywhere; wall-clock capped per item
- stress suite bounds inherited from 7.0 (RUN_SERIAL, explicit budgets)
- CONTENDED-HOST CAVEAT: two other 8.0 track agents build concurrently on
  this box (exp-rs-execution-plane-8, exp-rs-geospatial-data-fabric-8); all
  wall-clock numbers below carry that noise. Evidence, never gates.
- benchmark_scale8 run with default env scales on this host:
  schedule 1k+10k+100k submit/await (single repeat per scale — repeat-ID
  reuse bug found by review B fixed) + 100k DatasetStore inserts + 256
  window reads + 100k trace emits ≈ 45-50 min wall under contention.

## Baseline artifacts

- `benchmarks/quality7.json` (7.0 micro baselines; Windows debug evidence in
  repo; Linux Release evidence recorded on this host in build tree)
- `benchmarks/scale8.json` (this track; Linux Release evidence)
- Both carry environment headers; both are evidence, never gates.

## Scale8 findings (bounded rerun, contended host)

| Measurement | ops/s | Note |
|---|---|---|
| schedule_1000 | 26 215 | instant JobEngine jobs, submit+await |
| schedule_10000 | 389 | **cliff**: per-job wait cost grows with in-flight records |
| schedule_20000 | 105 | cliff deepens — follow-up for the execution-plane track |
| dataset_metadata_insert_20000 | 15 007 | single-threaded createDataset bulk |
| dataset_metadata_page_read | 44.6 | 1000-row pages over the bulk store |
| raster_window_reads | 152 618 | 64² windows over 1024² Float32 |
| trace_file_sink_overhead | 220 437 | FileTraceSink, 4 MiB rotation / 4096 queue |

The 1k → 20k schedule degradation (~250×) is the headline WP-G finding:
either waitForJob's per-id bookkeeping or the retained-record set is
super-linear at scale. Bounded rerun env: SICNU_BENCH_SCHEDULE_MAX=20000,
SICNU_BENCH_DATASET_ROWS=20000 (the default 100k-scale run exceeds the
60-min ladder budget on this contended host — teardown, not the measured
loops, dominates; follow-up for owners).
