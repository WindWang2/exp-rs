# PERFORMANCE
Memory/streaming contracts and measured evidence for new hot paths are appended below.

## Streaming/memory contracts of new hot paths

- rs:sar_geocode: O(tile + bounded window). Tile 256²; per-tile source
  window capped at 4M floats (16 MiB); over-budget tiles fall back to
  per-pixel 2×2 reads (bounded, slow, counted). Forward RD per cell: 1
  sign-scan (32 orbit interpolations) + ≤60 bisection halvings + 1
  interpolation — bounded regardless of input.
- rs:sar_temporal_stats: O(tile × N scenes); per-pixel median over ≤N
  samples (nth_element on a reused buffer).
- rs:rasterize / rs:zonal_stats: O(window) rasterization (256² MEM) +
  byte-budgeted feature cache (256 MiB geometric bytes) + 16M-float median
  budget; value rasters read window-by-window.
- Determinism grades: all new kernels are bit-exact (no parallel floating
  reductions; fixed iteration bounds; ordered feature/window iteration).

## Local environment evidence
- Host: 16 CPU / 62 GiB; kernel 6.18; ccache 74% cacheable / 67% hit.
- Compiler: clang 22.1.8 (worktree build). GCC 16.2.1 snapshot produced
  nondeterministic ICEs on qgis_core under heavy concurrent load (six 8.0
  track worktrees building simultaneously, load average > 60); clang has
  none. Switching compilers is local build tooling only — the branch is
  compiler-agnostic.
- Build commands: `cmake --preset dev-default -B build -G Ninja
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`;
  `cmake --build build --target <test> -j 2` (bounded parallelism policy).
