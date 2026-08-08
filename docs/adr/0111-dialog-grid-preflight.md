# ADR 0111: Dialog-Level Grid Preflight for Multi-Raster Operators

## Context

The shared grid-compatibility service (ADR 0066) is enforced inside the
operators — change detection, post-classification comparison, apply-mask and
image fusion fail or warn at runtime. But the DoD UI/UX contract asks for
"validate before execution": the user should learn about a pixel-grid
incompatibility when clicking Run, not after the task has already been
submitted and failed asynchronously. None of the multi-raster dialogs checked
the grids up front.

## Decision

- New shared helper `rasterGridCompatibilityMessage(a, b, allowPixelSizeMismatch)`
  in `dialog_utils`: opens both files, runs the shared `compareGrids` service
  via `gridFromDataset`, and returns an actionable message for the primary
  blocking issue (plus first warning, e.g. NoData mismatch) or an empty string
  when the pair can proceed. Unopenable files yield a generic message.
  `allowPixelSizeMismatch` drops `PixelSizeMismatch` verdicts — the fusion
  case, where the MS raster is intentionally resampled onto the finer pan
  grid (matching the operator's existing exemption at `image_fusion.cpp:647`).
- Wired into the run path before submission:
  - `ChangeDetectionDialog::validateInputs` — before/after must share a grid.
  - `PostClassificationDialog::validateInputs` — same for the two classified
    rasters.
  - `FusionDialog::onRun` — pan/ms co-registration with the pixel-size
    exemption (CRS/origin/extent still enforced).
  Each shows a `QMessageBox::warning` with the actionable message and aborts.

## Consequences

- Multi-raster analysis dialogs now give the DoD-mandated actionable
  validation errors up front (e.g. "10 m vs 20 m pixel grids") instead of a
  failed background task.
- The verdicts are pinned headlessly in `test_grid_compat_dialog_utils`
  (identical passes; resolution/CRS/misalignment blocked; fusion exemption
  drops only pixel-size issues; ungeoreferenced pair falls back compatible;
  missing file → generic message).
- The helper stays the single dialog-side seam; future multi-raster dialogs
  (mosaicking comparisons, multi-date analysis) pick it up for free.
