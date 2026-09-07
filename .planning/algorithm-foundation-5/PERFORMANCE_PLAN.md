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

(appended per milestone with date + build type)
