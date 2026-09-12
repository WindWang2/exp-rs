# PERFORMANCE — model-runtime-multimodal-9

Environment (fixed for all numbers below unless stated):
- Host: linux 6.18, RTX 3080 Laptop 16 GB (SM 8.6), driver 610.57.04/CUDA 13.3
- Build: Release, Ninja, -j4, gcc, ccache warm
- ORT: 1.30.0 GPU SDK (pip) — CPU EP lane AND CUDA EP lane
- Data: synthetic/known-answer fixtures; no giant rasters

## M9 results (Release, quiet machine, 2026-09-12)

Files: `benchmarks/model-runtime-4.json` (CPU + opencv_dnn + ORT-CPU),
`benchmarks/model-runtime-9-cuda.json` (NEW, ORT-CUDA).

| Metric | opencv_dnn / CPU ORT | ORT CUDA EP (RTX 3080) |
|---|---|---|
| Session cold load | 398 ms (opencv identity incl. cv init) / 35.3 ms (ORT) | 27.9 ms (CUDA EP init incl.) |
| Session warm load | 15.8 / 20.5 ms | 7.8 ms |
| Named 1x2x64x64 forwards | 21,020 /s (0.048 ms avg) | 986 /s (1.013 ms avg, verification-inclusive) |
| Tiled 512² 4-band (opencv_dnn, 16px tiles) | 5.58 M px/s | n/a (opencv lane) |
| In-forward cancel latency | 1.53 ms (ORT CPU) | — |
| Multi-input/temporal 64 tiles | 46.1 ms (1,387 tiles/s) | — |
| VRAM free before/after (NVML) | — | 13,727 → 13,707 MiB (as committed) |

Honest notes:
- The CUDA per-forward number is dominated by launch + H2D/D2H copy
  overhead for TINY tensors (1x2x64x64); it is NOT a throughput claim for
  production tile sizes — it is a real-execution + correctness anchor
  (bit-exact vs CPU on the sum fixture). Larger-tile CUDA benchmarks need
  production-weight models and belong with the model zoo work.
- CPU numbers were re-measured on the quiet machine; the first bench run
  (three parallel track builds active) was discarded as load-contaminated.
- Debug-vs-Release comparisons: none (Release only).
- 100k-extent evidence: tile planning is arithmetic over extents (the
  engine never materializes the raster); the batch-run full-suite sweep
  (114,661 assertions) plus the bounded-window accumulator analysis in
  ARCHITECTURE.md D6 stand in for a dedicated giant-raster bench, which
  would require a multi-GB fixture (out of the data-bound policy).
