# ADR 0072: Change Detection 2.0 — Methods, Thresholds, Cleanup, Area

## Context

`rs:change_detection` only supported difference / normalized-difference /
threshold change-mask on a single band — the mission's "foundation, not a
workflow". The DoD requires "change detection is a real workflow rather than
only pixel differencing", with preflight (grid compatibility — ADR 0066),
multiple methods, threshold strategies, post-processing, and change
statistics/area.

## Decision

1. **Kernels** (`change_detection.{h,cpp}`):
   - `ratio` — after/before with NaN guard for zero denominators.
   - `cvaMagnitude` — Change Vector Analysis magnitude across all bands
     (`sqrt(Σ (after_b − before_b)²)`), NaN-propagating.
   - `otsuThreshold` — Otsu between-class variance on a histogram (16..1024
     bins); single-level scenes return their own level.
   - `percentileThreshold` — nearest-rank percentile over finite values
     (clamped 0..100).
   - `morphologicalCleanup(mask, w, h, iterations, MorphOp)` — 3x3 erode /
     dilate / open / close on 0/1 masks; 255 (NoData) cells never change.

2. **Operator** (`rs:change_detection`): methods gain `ratio` and `cva`
   (CVA requires equal band counts; uses all bands). New `makeMask` path
   writes a proper UInt8 0/1 mask with `thresholdMethod` (manual / otsu /
   percentile), optional `cleanup` + `cleanupIterations`, and result
   statistics: `thresholdUsed`, `changedPixels`, `totalPixels`,
   `changedPercent`, and `changedArea` (map-units² from the geotransform).
   The legacy `change_mask` method and the raster-output path are unchanged.

## Consequences

- Change detection becomes a genuine workflow: multi-method, automatic or
  statistical thresholds, morphological cleanup, and quantitative area /
  percentage reporting — all over the shared grid-compat preflight and the
  operator seam (UI/CLI/MCP).
- No algorithm duplication; kernels are unit-tested, the operator is
  execution-tested (ratio, CVA magnitude, Otsu mask + cleanup + area).
- Memory stays O(pixels × bands) in-process (change detection remains a
  full-raster-in-memory operator — documented for the large-raster audit).
