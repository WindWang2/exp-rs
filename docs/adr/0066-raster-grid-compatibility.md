# ADR 0066: Shared Raster-Grid Compatibility Service

## Context

Raster operations that combine pixels from two or more rasters (change
detection, band math, image fusion, classification application, mosaicking,
multi-date analysis) each performed their own partial grid checks — or none:

- `rs:change_detection` checked only `width/height` equality, with no CRS,
  pixel-size, origin, or extent validation;
- `rs:band_math` and `rs:image_fusion` assumed alignment silently;
- `rs:mosaic` compared projection strings only;
- the only centralized multi-raster check, `preflightVirtualRaster`, was
  virtual-raster-specific and origin-blind (two same-size grids offset by half
  a pixel passed as Compatible) and NoData-blind.

The goal's "Grid Compatibility and Alignment Service" requires a shared
validation before any multi-raster operation, with actionable messages and an
alignment recommendation, shared by every consumer instead of per-dialog
partial checks.

## Decision

1. **`RasterGrid`** (`src/data/raster_grid_compat.{h,cpp}`, namespace
   `sicnu::data`): a minimal, pure grid description — CRS WKT, geotransform,
   width/height, per-band NoData — buildable from `RasterStructure`
   (catalog side) or directly from a GDAL dataset (operator side). The service
   opens nothing and mutates nothing.

2. **`compareGrids(a, b)`** returns a `GridCompatReport` of ordered issues
   (verdict, stable code, actionable message, blocking flag). Priority order,
   first blocking issue wins:
   - `MissingCrs` — exactly one raster has no CRS;
   - `CrsMismatch` — CRSs differ (message: reproject the second raster);
   - `PixelSizeMismatch` — pixel sizes or rotation differ (message names both
     grids, e.g. "30 x 30 vs 60 x 60", and says resample);
   - `OriginMisalignment` — same pixel size but a sub-pixel origin offset;
   - `ExtentMismatch` — same lattice but differing extents (message says clip
     both rasters to a common extent; distinguishes disjoint extents);
   - `NoDataMismatch` — per-band NoData values differ; **warning-grade**
     (`blocking = false`).
   Two rasters that both lack georeferencing are not spatially comparable and
   report Compatible — callers fall back to dimension checks.

3. **`compareStructures(a, b)`** convenience over `RasterStructure` values for
   catalog-side checks.

4. **First consumer**: `rs:change_detection` replaces its dimension-only check
   with `compareGrids` on grids built from the two datasets; blocking issues
   throw `RSOperatorError(InvalidInputData)` with the actionable message before
   any pixel work, `NoDataMismatch` is logged as a warning. `GdalDatasetWrapper`
   gains `hasGeoTransform()` for grid construction.

## Consequences

- Change detection now rejects CRS/pixel/origin/extent-incompatible inputs with
  actionable errors instead of silently comparing mismatched pixels.
- The service is the single place grid compatibility is defined; band math,
  fusion, classification application, and mosaicking can adopt it incrementally
  (same verdict vocabulary, no per-dialog partial checks).
- Unreferenced rasters keep the legacy dimension-only behavior (no regression
  for synthetic fixtures).
- NoData differences are surfaced as warnings rather than failures — combining
  pixels still proceeds, but the inconsistency is visible.
