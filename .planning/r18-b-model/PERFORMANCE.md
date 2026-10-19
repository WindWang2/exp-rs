# R18-B Performance

Machine: same host as TEST_RESULTS.md (5-track shared lock, -j2, Release).
Method: identical input/config, ≥3 runs, wall time via `date +%s%N`; the
bench's own per-phase JSON baseline recorded (benchmarks/model-runtime-4.json).

## test_model_runtime_bench (SICNU_MODEL_BENCH=1), with fixes

| run | wall ms | result |
|-----|---------|--------|
| 1 (cold-ish) | 1223 | CPU lane pass, baseline JSON written; CUDA lane gated (no ORT) |
| 2 | 1027 | same |
| 3 | 1046 | same |

Baseline phase numbers (run 1): cold_load 400.5 ms; multi-input 64 tiles in
88.7 ms (721 tiles/s); cancel latency 19.6 ms; 3.41 Mpx/s. Peak RSS sampling is
Windows-pinned in the bench (recorded 0 on Linux); no RSS regression claim.

## Fix-attributable overhead analysis (no unmeasured claims)

- W4b slide fix (the only hot-loop change): adds one upward `memcpy` of
  (span − cleared) rows per accumulator Mat per flush. For a 512-tile / 64-halo
  / 4096-wide / 3-class raster: 5 mats × 128 rows × 4096 px × 4 B ≈ 10 MB per
  tile-row flush ≈ 2-3 ms — under ~3% of one 512² forward pass, and only on
  the opt-in feather path. The existing bench does not exercise feather
  blending, so no measured before/after exists for it; the bound above is
  arithmetic, not a measured ratio.
- W1: the clone merely moved under the lock (same single copy) — zero delta by
  construction.
- W6 fsync: adds one fsync of the staged raster + sidecar and a
  best-effort directory sync per publish — the same durability cost the
  detection lane (#1174) and the chunk family already pay; correctness-
  mandated, on the publish path (once per run), not per tile.
- All other fixes are branch/guard changes off hot loops.

## No speedups claimed

This track claims zero performance improvements; all changes are correctness,
fail-closed, or durability. Priorities per Oracle-C: correctness > speed.
