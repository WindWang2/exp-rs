# ADR 0088: Applying a QA Mask to a Product (`rs:apply_mask`)

## Context

The QA masking slice (B5, ADR-0065 groundwork) could **derive** binary masks
(`rs:qa_mask` — Landsat QA_PIXEL / Sentinel-2 SCL / generic bitmask) but the
optical workflow stopped there: nothing turned a mask into an analysis-ready
product. The DoD lineage requires "QA / Cloud / Shadow / Snow Mask → Analysis
Ready Raster", and the reusable preprocessing DAG
(`lab.preprocess.optical`, ADR 0083) explicitly noted applying the mask as
"follow-up once an apply-mask operator exists".

## Decision

Add `rs:apply_mask`, a block-streaming RSOperator (`Streaming` memory policy)
that sets masked pixels to NoData in every band of a product raster:

- **Inputs**: `input` (multi-band product), `mask` (band 1: value > 0 =
  masked), `output`, optional `no_data`, optional `align_mask` (default true).
- **Grid discipline** (shared service, ADR-0065 / A4): the input and mask
  grids are compared with `compareGrids()`. Same-CRS grid differences (pixel
  size / origin / extent) are auto-aligned by nearest-neighbor sampling of the
  mask onto the input grid (safe for integer classification masks); pixels
  outside the mask extent are treated as clear. CRS mismatches and missing CRS
  are always an error — no auto-warp. Ungeoreferenced rasters fall back to
  identical-dimension checks.
- **Semantics preserved**: output keeps the input's band count, data type,
  geotransform/CRS, per-band NoData (input band NoData reused; `no_data` is
  required for bands that define none), and band metadata (`SICNU_BAND_ROLE`,
  `WAVELENGTH`, `FWHM`), so downstream operators stay product-aware. The
  dataset records `SICNU_MASKED_BY` = mask path.
- **Outputs**: `output`, `maskedPixels`, `totalPixels`, `maskedPercent`,
  `aligned`.
- **Execution seam**: registered in the RSOperator registry and the atomic
  processing registry like every other operator — one code path for GUI,
  TaskCenter, CLI, and Agent.
- **Workflow integration**: the `lab.preprocess.optical` DAG now chains
  `atmospheric → apply_mask → ndvi`, feeding the QA-mask artifact from the
  side branch into the product so NDVI computes on cloud-free pixels.

`GdalDatasetWrapper` gains a read-only `dataset()` accessor (raw handle for
metadata operations the wrapper does not wrap; ownership stays with the
wrapper).

## Consequences

- The preprocessing lineage is complete end to end: import → calibrate →
  derive mask → correct → apply mask → analysis-ready → index, with the mask a
  reusable artifact rather than a throwaway file.
- Mask application is block-streaming (`O(width * blockRows * 2 floats)`),
  cancellable, and covered by tests at the operator seam (same-grid, auto-align
  at 60 m→30 m, CRS rejection, `align_mask=false` rejection, missing-NoData
  error, metadata pass-through) and the dialog seam (`buildParams()` JSON
  assembly, output-path suggestion, `align_mask`/`no_data` surfacing).
- The 60 m SCL vs 10 m product case is handled automatically, so users no
  longer need to resample masks by hand; CRS problems still surface as
  actionable errors.
