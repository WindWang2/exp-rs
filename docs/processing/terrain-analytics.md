# Terrain Analytics — Hydrology, Visibility, Solar & Landform

Contracts for the full-frame terrain family introduced by track
`terrain-hydrology-11` (F16). The local 3×3 kernels (slope, aspect, hillshade,
roughness, TRI, TPI, curvatures, local relief) keep their contracts in
`foundation-5.md`; this document covers the hydrology/visibility/solar/landform
surface. All kernels: deterministic, NoData barriers, fail-closed cell budget
(`SICNU_TERRAIN_MAX_CELLS`, default 2²⁸ cells).

## Hydrology — `rs:terrain_flow`

| Product | Contract |
|---|---|
| `fill` | Priority-flood depression filling; NoData cells (and cells adjacent to them) seed the drain boundary at their own elevation (#848). |
| `flow_direction` | D8 steepest descent, ESRI codes (0 = sink). Flats on a merely-filled surface stay sinks. |
| `flow_accumulation` | Self-inclusive drainage counts via topological peel; NoData cells carry the sentinel (#783). |
| `watershed` | Pour-point reverse-BFS basin labels (first point wins ties). |
| `flat_resolve` | Epsilon-gradient flat resolution: every valid cell gains a strictly descending path (steps ≥ `flatEpsilon` = max(relief·2⁻²⁰, 1e-6)); monotone non-decreasing vs the input; byte-equal to `fill` when no flats/depressions exist. Result JSON: `flatEpsilon`, `raisedCells`. |
| `flow_direction_inf` | D∞ (Tarboton 1997): steepest descent on the 8 triangular facets, restricted to each facet wedge; degrees clockwise from north; −1 = undecided (pit/unresolved flat); NoData passthrough. On planar surfaces the exact gradient azimuth is reproduced. Result JSON adds `undecidedCells` (count of −1 cells, NoData excluded). |
| `stream_network` | Threshold extraction (`threshold` ≥ 1, self-inclusive counts) + Strahler orders over the D8 graph. Output raster: order per stream cell, 0 elsewhere, NoData cells carry the sentinel. Result JSON: `streamCells`, `maxStrahlerOrder`, optional `segments` (Strahler links from heads/junctions to the next junction, GDAL pixel-centre map coordinates, capped at 200 with `segmentsTruncated`). |
| `outlets` | Valid cells with D8 direction 0 (interior sinks, rim outflows). Output: 0/1 mask; NoData cells carry the sentinel. Result JSON: `outletCount`, `outlets` (capped at 1000, `outletsTruncated`). Outlets are computed from the fill of this run; interior flats report as outlets unless resolved (run `flat_resolve` first for true basin outlets). |

## Visibility — `rs:terrain_viewshed`

| Product | Contract |
|---|---|
| `viewshed` | Ring-sweep R3-family viewshed (deterministic permissive merge): byte raster 1/0, 255 = NoData. Exactly one observer (`observer`); use `cumulative` for several. Observer/target heights in metres, `radius` in map units (0 = full frame). Result JSON: `visibleCells`, `analysedCells` (non-NoData), `visibleFraction` (denominator = analysed cells). |
| `cumulative` | Per-observer viewshed runs accumulated: uint16 counts, 65535 = NoData, max 64 observers. |

Curvature/refraction: elevations are lowered by `(1−k)·d²/(2·R_earth)` (standard
k = 0.13) before the line-of-sight math — exact per LOS pair. Refused with
`InvalidParameter` on geographic (degree) CRS inputs. NoData cells are opaque:
rays stop at them; terrain behind a NoData region is invisible and blocks
nothing beyond. Observers outside the grid or on NoData are refused.

## Solar — `rs:terrain_solar`

| Product | Contract |
|---|---|
| `shadow_duration` | Weighted fraction of daylight track samples during which a cell is terrain-shadowed (parallel-ray model, exact per sample; azimuths quantized to 1° sectors, ≤ 1024 samples). Samples with elevation ≤ 0 or weight ≤ 0 are excluded from numerator and denominator. No curvature/refraction on shadows (v1). |
| `hillshade_series` | One band per track sample via the existing `TerrainAnalysis::hillshade` kernel (no second hillshade implementation). |

Sun track: explicit `sun_track` (`az,elev[,weight];…`, degrees) or generated
from `day_of_year` / `latitude` between `start_hour`–`end_hour` (local SOLAR
time, step `step_hours`; weight = step). Generated positions use the
low-precision solar model (Spencer declination series + hour angle; declared
±1°): supply your own track when you need ephemeris-grade positions.

## Landform — `rs:terrain_landform`

| Product | Contract |
|---|---|
| `tpi_multiscale` | TPI per radius (`radii`, max 16 scales): `z − mean(2r+1)² square window, centre excluded`; one band per radius. Square windows are a documented v1 choice. |
| `landform_class` | Weiss (2001) classes from standardized TPI (inner/outer radius) and Horn slope (reused from `rs:terrain_analysis`'s kernel): 0 plains, 1 valley, 2 lower slope, 3 middle slope, 4 upper slope, 5 peak, 255 NoData. |
| `geomorphon` | Jasiewicz–Stepinski (2013) ternary pattern per cell (8 compass directions, nearest-cell line-of-sight with `flat_radius` skip and `flat_thresh_deg`), classified into the unambiguous v1 subset: 0 flat, 1 peak, 2 pit, 3 ridge, 4 valley, 5 slope, 6 other (shoulder/spur/hollow/footslope patterns), 255 NoData. Form histogram in the result JSON. |

## Failure semantics (family-wide)

- Errors: `InvalidParameter` (bad params/observer/CRS), `InvalidInputData`
  (empty DEM, degenerate geotransform, cell budget exceeded), `GdalError`
  (I/O), `ComputationError`, `Cancelled` (cooperative; kernels poll via the
  operator's cancel flag and cancellation always surfaces as `Cancelled`,
  never as a generic computation failure).
- Outputs: written through `GdalStreamingOutput` with `abandon()` on any
  failure — a cancelled or failed run leaves no partial file.
- Determinism: fixed neighbour/facet orders, strict-improvement comparisons;
  all operators pinned `bit_exact` in their capability sidecars.

## Scale & memory

Full-frame kernels state their working set in `estimatedRamBytes` (dynamic per
input plus a 4096² anchor): flow ≈ 6 float frames, viewshed ≈ 22 bytes/cell,
solar ≈ 16 bytes/cell, landform ≈ 48 bytes/cell. The cell budget caps at 2²⁸
cells by default and is raised only explicitly via `SICNU_TERRAIN_MAX_CELLS`.
Tiled/external-memory hydrology routing remains a documented follow-up — the
budget fails closed instead of overcommitting.
