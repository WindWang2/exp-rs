# ADR 0104: Real Raster Previews in the Comparison Dialog + Change-Detection Hook

## Context

The dual-view interpretation aid existed (`ComparisonDialog` — SplitScreen /
Flicker over the shared `ComparisonWidget`), but its layer loader drew only a
text **placeholder** pixmap ("In a real implementation, this would render the
actual raster data"): the before/after panes showed layer metadata, never the
imagery. The DoD change-detection workbench explicitly calls for "dual
synchronized viewports and Swipe where they improve interpretation", and the
preview guidance says previews must not execute the full raster.

## Decision

- `ComparisonDialog::loadLayerToWidget` now renders the actual raster via
  `QgsMapRendererParallelJob` at a fixed preview size (400×400): QGIS renders
  at output resolution, reading only the needed pixels — a lightweight,
  full-raster-free preview (no statistics scan, no full-resolution decode).
  The text placeholder remains only as a fallback when the provider cannot
  render.
- `ChangeDetectionDialog` gains a "双视图对比..." button in the input section
  that opens `ComparisonDialog` prefilled with the selected before/after
  rasters (`setLeftLayer`/`setRightLayer`), so the change-detection workflow
  offers split-screen/Swipe + flicker interpretation without leaving the task.
- `ComparisonWidget` exposes `hasLeftImage()` / `hasRightImage()` for tests
  and UI enablement.

## Consequences

- The change-detection workflow now links comparison to interpretation: pick
  before/after → 双视图对比 → split-screen swipe / flicker on real imagery,
  then run the operator. The preview stays cheap by construction.
- Tests pin the seam: a real two-raster fixture renders non-null left/right
  previews through the dialog (previously the placeholder would have passed
  the same test — the rendering path is now genuinely exercised).
