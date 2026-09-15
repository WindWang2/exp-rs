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
