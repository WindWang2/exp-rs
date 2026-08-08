# ADR 0107: Shared Raster Layer Picker Widget (C5)

## Context

The C5 task-center UI list calls for shared widgets for "raster selection",
alongside the band-role selector (ADR 0102). Every multi-raster dialog
(change detection, post-classification comparison, comparison dialog, ...)
still populated its own before/after layer combos with a copy of the
`QgsProject::mapLayers()` raster-filter loop.

## Decision

- New `RasterLayerCombo` (`app/widgets/raster_layer_combo.{h,cpp}`): a
  QComboBox that lists the project's valid raster layers (name → layer id)
  and resolves the selection — `currentLayerId()`,
  `currentRasterLayer()`, `selectLayer(id)` (no-op for unknown ids).
- `PostClassificationDialog` and `ChangeDetectionDialog` adopt it for their
  before/after pickers: the duplicated `mapLayers()` populate loops are
  replaced by `populate()`, and band selectors resolve through
  `currentRasterLayer()`.

## Consequences

- One canonical raster picker for all multi-raster dialogs: the comparison
  dialog adoption completes the C5 consolidation (post-classification,
  change-detection, and comparison dialogs all use `RasterLayerCombo`), and
  three dialogs lose their duplicated populate loops while keeping identical
  behavior (their dialog tests stay green: 42/1, 22/8, and the widget test
  13/1 pin listing, id resolution, and unknown-id no-op).
