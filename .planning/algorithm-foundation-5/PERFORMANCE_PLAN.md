# Performance Plan — Foundation 5.0

Baseline convention: `benchmarks/*.json` (programmatic scenarios, JSON
results; see `benchmarks/spectral_index_streaming.json` for shape).

## Scale scenarios (programmatic synthetic data, no big fixtures)

| Scenario | Size | Metric |
|---|---|---|
| Terrain family (new products) | 512², 4096² | wall, peak RSS |
| SAR neighborhood (dual-pol, speckle audit) | 2048², 2 bands | wall, peak RSS, tile throughput |
| Focal/zonal stats | 4096² + 64 zones | wall, RSS |
| Classification training (new backends) | 250k samples × 12 bands | wall |
| Long temporal series (CUSUM/EWMA/seasonal MK) | 4096² × 240 dates | wall, RSS (guards on T²) |
| Large tiled composite | VRT-backed synthetic, 8192² | wall, temp disk, RSS |
| Cancellation latency | mid-run cancel each family | time-to-stop, no partial file |

## Rules

- Peak RSS via `/usr/bin/time -v` (or getrusage harness) captured per run.
- No O(raster) allocations on streaming paths; verify by RSS ≪ raster bytes
  for the tiled scenarios; violations are P1 findings.
- Every new operator family adds/extends a `benchmarks/` JSON with: scenario
  id, sizes, wall ms, RSS bytes, generator seed, date, build type.
- Numbers recorded in this file's "Results" section per milestone; no
  cross-machine comparisons claimed.

## Results

2026-09-08, Release build, Ninja -j2, AMD Ryzen 9 5900HX (16 cores, 62 GB).
Collected via the execution-bench harness (SICNU_EXEC_BENCH_OUT; 512²
default sizes, single in-process TaskCenter run each; benchmarks report,
never gate):

| Workload | wall ms | cpu ms | peak RSS delta MB |
|---|---|---|---|
| focal_stats_streaming (3x3 mean) | 126 | 130 | 15 |
| proximity_edt | 50 | 52 | 16 |
| spectral_derivative (order 1, 4 bands) | 209 | 215 | 24 |
| topographic_correction (2 bands, C) | 281 | 288 | 12 |
| terrain_curvature (profile) | 109 | 112 | 86 |

Larger sizes (4096², SICNU_BENCH_LARGE=1) are wired through the same
harness; the recorded default sizes keep ctest-friendly runtimes.
peak-RSS deltas for the full-frame families (curvature 86 MB at 512² = the
documented 3-frame full-frame contract; per-op JSONs in benchmarks/) match
their declared estimates.
