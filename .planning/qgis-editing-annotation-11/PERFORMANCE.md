# PERFORMANCE — qgis-editing-annotation-11

## Resource envelope (hard, from GOAL)

- Build: `cmake --build build-dev --target <t> -j2` (hard cap; `-j1` when RSS > 70% or load > 1.5× cores). `-j$(nproc)` forbidden.
- Tests: `-j1`, `QT_QPA_PLATFORM=offscreen`.
- Build monitoring: CPU/RSS/load logged every 60 s during long builds; if not measurable at a given step, recorded once here and cap kept at `-j2`.

## Logical scale model (no wall-clock correctness gates)

| Quantity | Bound | Enforcement |
| --- | --- | --- |
| Index bulk-build features (unit test) | 100,000 | generator in `test_edit_index`; env `RS_EDIT_INDEX_MAX_FEATURES` raises only with explicit opt-in |
| Index query correctness | brute-force oracle on sampled subset (≤2,000 features per check) | test invariant |
| Brush stroke stamps per drag | capped at 4096 stamps/stroke (coalescing move events; beyond → stamps merged/coalesced, never unbounded growth) | tool implementation + test |
| ROI stats preview pixels | early-exit above 4,000,000 px unless `allowUnbounded` | `RsRoiSemantics` option + cancel test |
| Stats chunk reads | windowed block reads (never whole-raster allocation) | `RsPixelRasterizer` windowed contract |
| Memory in tests | no RSS gate inside unit tests (host-shared); scale proven by operation counts + invariants (D10) | envelope |

## Runtime cost notes

- `RsEditIndex` keeps one `QgsSpatialIndex` per attached layer, updated incrementally on `featureAdded/featureDeleted/geometryChanged`; queries are index-backed, no full-layer scans; consumers never trigger UI rebuilds (they read facts; canvas refresh follows QGIS layer semantics).
- `editing:state` JSON is O(active layers), built on demand per tool invocation; no polling/timers.
- Persistence writes stream through `QgsVectorFileWriter`; no whole-layer materialization in memory beyond what QGIS writer already buffers.

## Measured evidence (filled per phase)

| When | Command | CPU/RSS/load | Notes |
| --- | --- | --- | --- |
| (to be filled during builds) | | | |

## Measured (2026-09-16, build + suite)

- Full fresh-build compile of qgis_core/qgis_gui/qgis_analysis + 10 test binaries at `-j2`
  (Unix Makefiles), alongside a concurrent track's build on the same host (their cc1plus count 9–15).
- Sampled load averages at poll points: 15.5–21.0 (16 cores, ≈1.0–1.3×; below the 1.5× -j1 trigger);
  host memory ≤19% used of 62 GB. The 60 s periodic sampling was not-executed for detached builds —
  recorded once here per the envelope, cap kept at `-j2`.
- Targeted suite wall time: 12.3 s for all 10 binaries (offscreen, -j1); `test_edit_index` (100k
  build + brute-force oracles) is the long pole at ~8.4 s.
- Logical-scale gates (no wall-clock assertions): index 100k features vs brute-force subset; brush
  ≤2048 stamps/stroke with coalescing; ROI preview ≤4M px fail-closed; state refresh O(1) per event
  (incremental featureCount, no recount on 100k layers).
