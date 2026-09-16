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

- 2026-09-15 configure: `cmake --preset dev-default -G Ninja -DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=<main-repo cache>` (first configure failed on a Catch2
  git-clone TLS error; reused the main repo's populated _deps source). Exit 0.
- 2026-09-15/16 cold build (`ninja -j2`, targets: 7 terrain suites + sicnu_geo_rs_cli +
  capability_knowledge_tool): ~1900 ninja steps, wall ≈ 2.5 h shared with concurrent tracks
  (host load 12–14 of 16 cores throughout; memory peak ≈ 20/62 GiB). -j2 cap held; no -j1
  degradation was needed.
- 2026-09-16 scale run: `SICNU_TERRAIN_SCALE_TESTS=ON ./tests/test_terrain_hydrology
  "*hermetic scale*"` → 2048² (4.19 M cells) fill+D∞+accumulation+monotonicity in 6.99 s,
  ≈ 10 float frames ≈ 170 MB RSS budget by the frame model (not wall-clock gated; the
  gate is the mass/monotonicity invariant).
