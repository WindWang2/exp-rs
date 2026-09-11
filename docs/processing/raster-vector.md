# Processing: Raster-Vector Analytics (Scientific Processing 8.0)

> Authority for the raster↔vector operator family added by 8.0 (package D).
> Companion: [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md).
> Implementation seam: `operators/rs/rs_raster_vector.{h,cpp}` — ONE shared
> windowed rasterization path so `rs:rasterize` and `rs:zonal_stats` can
> never disagree about what a geometry covers.

## 1. `rs:rasterize` — vector burn onto a reference grid

1. The reference raster donates the output grid, CRS and geotransform
   (north-up enforced; rotated grids are typed refusals). Unburned pixels
   are NaN and carry the declared NoData — never zero.
2. Burn values: a constant `value` or a numeric `field`. Non-numeric field
   values are typed refusals, never silently coerced.
3. Membership: GDAL pixel-center selection, or ALL_TOUCHED with
   `allTouched=true`. Overlapping geometries: LAST feature wins (input
   order) — the same rule zonal statistics applies, so a burn and a
   subsequent zonal summary of the same vector agree cell-for-cell.
4. Geometryless features and geometries outside the grid are counted in the
   result (`geometrylessFeatures`, `outsideGridFeatures`), not fatal.

## 2. `rs:zonal_stats` — per-zone raster statistics

1. Zones stream through the geospatial `VectorReader` contract with its
   declared CRS transform into the value raster's CRS (foundation axis-order
   policy). A raster without CRS, or a zone feature missing the declared
   `zoneField`, is a typed refusal.
2. Statistics per (zone, band): count, nodata count, min, max, mean,
   population stddev (÷N, Welford's online moments — stable single pass),
   and an exact median. Valid pixel = finite AND not the band's declared
   sentinel.
3. Zones accumulate across output windows (bounded 256² windows; a zone
   spanning windows is still one row). Overlapping zones: last feature
   wins, matching `rs:rasterize`.
4. Median is budgeted: a shared 16M-value collection; zones cut off by the
   budget are flagged (`median_truncated`, `medianTruncatedZones`) while
   their streaming statistics stay exact.
5. Zones with no valid pixel still report (count 0, NaN statistics) — an
   empty row is information, not noise. An EMPTY vector is a refusal
   (almost always a caller mistake).
6. CSV columns: `zone_key,band,valid_pixels,nodata_pixels,min,max,mean,stddev,median,median_truncated`.

## 3. Memory contracts

* Feature cache: geometry handles + keys are byte-budgeted
  (256 MiB); oversized vectors are a typed refusal telling the caller to
  subset — no unbounded materialization path exists.
* Rasterization happens in ≤ 256×256 MEM windows; value rasters are read
  window-by-window; no full-frame buffers.

## 4. Evidence

`tests/test_raster_vector.cpp` — analytic rectangle zones on known grids,
constant and attribute burns, last-wins overlap, ALL_TOUCHED, window-
spanning zones (4-window grid), sentinel exclusion, empty/geometryless
features, CRS84→UTM zone transform through the foundation policy, refusal
matrix.
