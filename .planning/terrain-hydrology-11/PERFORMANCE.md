# PERFORMANCE — terrain-hydrology-11

## Memory model (frames of float32; N = width×height)

| Kernel | Frames | Bound |
|---|---|---|
| resolveFlats (epsilon priority-flood) | dem + filled + heap(uint32 indices + float keys) | ~ N·(4+4) B + heap ≤ N entries (12–16 B/entry) ≈ 24·N B |
| flowDirectionInf | filled + angles + receivers | 3 frames ≈ 12·N B |
| dInfinityAccumulation | directions + acc + topological stack | 3 frames ≈ 12·N B |
| streamNetwork | dir/acc + mask + order + segments index | ≤ 5 frames ≈ 20·N B |
| detectOutlets | filled + dir | 2 frames ≈ 8·N B |
| viewshedR3 (single) | dem + visibility + horizon sectors (S=72: 72·N float ≈ 288·N B worst case; S configurable) | S·N·4 B dominating; single-observer classification mode uses rolling per-azimuth buffers ≈ 3 frames |
| cumulativeViewshed | dem + counts(uint16) + per-observer scratch | ~ 10·N B + 3 frames |
| shadowDuration | dem + horizon sectors + track | same as HorizonGrid |
| multiscaleTPI | dem + out per scale (emitted per scale) + integral-image rings | 2 frames + scale loop |
| geomorphon | dem + out + 2 line buffers (search radius) | 2 frames + 2·R B |

Operators state `estimatedRamBytes` from these frame counts (checkedMulN with
overflow guards). **Hard cap:** new full-frame kernels fail closed with
`InvalidInputData` above `SICNU_TERRAIN_MAX_CELLS` (default 268,435,456 cells =
16384² ≈ 6.4 GB/frame; documented env override). Rationale: a fail-closed cap
turns silent OOM into a diagnosable error; the override documents the escape
hatch for hosts that knowingly have the RAM.

## Wall-time / scale evidence policy

No wall-clock numbers are used as correctness gates. Scale evidence:
- bounded logical-scale invariants (mass conservation, connectivity, counts)
  run in the normal gate at N ≤ 4096²-equivalent;
- one env-opt-in hermetic scale test (`SICNU_TERRAIN_SCALE_TESTS=ON`) runs the
  hydrology chain on a large logical grid (e.g. 8192² synthetic) asserting
  completion + invariant, labelled `[.scale]` so default ctest excludes it.

## Host observations (recorded during builds)

- (to be appended per build: command, -j level, peak RSS %, load, duration)
