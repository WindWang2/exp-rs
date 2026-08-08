# ADR 0101: Post-Classification Comparison Dialog

## Context

`rs:post_classification_change` (ADR 0089) was operator-only: the change
detection entry points (the legacy dialog and the `tool.rs.change_detection`
workflow tool) covered continuous methods but nothing exposed the per-class
transition matrix in the UI. That left a backend/UI alignment gap the DoD
explicitly guards against ("backend and UI capability are aligned").

## Decision

- New `PostClassificationDialog` ("后分类比较"), a `RasterProcessingDialogBase`
  presentation adapter: before/after thematic raster pickers, per-side class
  band selectors, an optional class-count spin (0 = auto from the observed
  maximum, capped at 255 for the UInt16 change codes), the shared output row,
  and the standard Run/Cancel bar. `buildParams()` assembles the
  `rs:post_classification_change` JSON; `onRun()` submits it through
  `runOperatorTask` — the same Task Center seam as every other dialog.
- Menu wiring: "遥感 → 分析 → 后分类比较..." opens the dialog; a main-window
  slot `openPostClassificationDialog()` is added next to the change-detection
  one.

## Consequences

- The post-classification comparison workflow is now reachable from the
  task-centric 遥感 menu, completing the change-detection UI surface
  (continuous methods + thematic transition matrix), with the operator
  remaining the single execution path (no Registry bypass).
- The dialog seam is pinned by a headless test: layer/band selection flows
  through `buildParams()`, `class_count` is omitted by default (auto) and
  surfaced when set, and the operator JSON matches the registry contract.
