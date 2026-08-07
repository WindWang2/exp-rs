# ADR 0098: Shared `processing::gridFromDataset` (de-duplicate grid builders)

## Context

The F-phase architecture review (GOAL review loop: "duplicated raster
compatibility checks") found the same dataset→`RasterGrid` builder copy-pasted
in four places: `rs:change_detection`, `rs:apply_mask`,
`rs:post_classification_change`, and a lambda in the fusion kernel
(`image_fusion.cpp`). Each had already drifted slightly (naming, comments),
and any future operator needing a grid preflight would add a fifth copy.

## Decision

Add `sicnu::processing::gridFromDataset(const GdalDatasetWrapper &ds)` in
`processing/gdal/gdal_grid_compat.{h,cpp}` — the operator-side grid
description consumed by the shared compatibility service (ADR-0065 / A4) —
and replace all four local builders with calls to it. The helper lives in the
processing layer (which legitimately depends on `data`), takes the dataset
read-only, and is a pure projection: CRS, geotransform flag/value, dimensions,
and per-band NoData.

## Consequences

- One canonical grid-builder for operator inputs: new operators get the same
  preflight semantics by calling it, and the compatibility service remains the
  single source of truth for grid comparison.
- The refactor is behavior-preserving: the change-detection, apply-mask,
  post-classification, and fusion suites stay green (operator 1160/38, GDAL
  116/12, fusion 1133/18) — the duplicated checks were already identical, so
  no verdict changes.
