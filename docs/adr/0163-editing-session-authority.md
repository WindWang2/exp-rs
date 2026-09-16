# ADR 0163 — Editing Session Authority

Status: Proposed (F11 · qgis-editing-annotation-11)
Date: 2026-09-15

## Context

Before this track the app had rich but *headless-of-authority* editing:

* `MapToolManager` owns the full QGIS-native vector toolset and the vertex tool port, but edit *state* is ad hoc: `QgisDesktopWindow::checkUnsavedChanges()` scans all project layers with a modal dialog and **discards `commitChanges()`'s return value** (`main_window_vector.cpp:66`); `undo()/redo()` act on the active layer only.
* Nothing in `src/app` configured canvas snapping (`QgsSnappingUtils` zero references), geometry validity was never reported, and annotations — fully compiled in the vendored gui library — had zero consumers.
* Agents could not perceive editing state: the `SpatialToolProvider` catalog had no editing namespace.

## Decision

Introduce `src/app/editing/` as the editing *state/lifecycle/reporting* authority, with these boundaries:

1. **One session object.** `RsEditSession` is the single authority for *edit state* — which layers are attached, their editing/modified/locked facts, undo/redo depth, feature and selection counts — and for *lifecycle* — commit/rollback with collected errors, locks, and project-removal safety (`layerWillBeRemoved` drops tracking before the layer dies). It never owns geometry: the `QgsVectorLayer` edit buffer stays the only geometry truth.
2. **No second engine anywhere.** Snapping configures the canvas's own `QgsSnappingUtils` (`RsSnappingController`); pixel membership stays with `RsPixelRasterizer` (center-of-pixel); agent visibility is a plain `SpatialTool` in the existing `SpatialToolRegistry`. Every capability is a thin authority-respecting layer, never a duplicate.
3. **Validity never mutates silently.** `RsGeometryValidity` reports structured issues and produces a `makeValid` *preview*; applying a repair is an explicit caller decision under an edit command.
4. **Agents read, never write.** The `editing:state` tool publishes stable facts only (dirty, per-layer state, snapping). There is no agent-callable editing mutation, so nothing can bypass undo/permission semantics.
5. **Persistence is atomic.** `RsEditPersistence` exports via `QgsVectorFileWriter` into a temp file renamed over the target, cleans every failure path, and refuses unsupported formats instead of guessing a driver.
6. **Vendored QGIS stays untouched.** All new code subclasses or composes existing gui/core/analysis classes.

## Consequences

* Legacy `main_window_vector.cpp` flows keep working; migrating them onto the session is a follow-up. During the overlap, the session is additive: it does not change existing dialog behavior.
* Locks are conservative: they refuse new edit commands and undo/redo, but never commit/rollback (save paths must stay available).
* Brush/erase semantics are documented unit contracts: radius in layer CRS units, one stroke = one undo step, erase is whole-feature.
* Annotation layers have no undo buffer in QGIS core; undo for annotations is explicitly not-supported in this slice.

## References

* `CAPABILITY_MATRIX.md` (before/after), `CURRENT_ARCHITECTURE.md` (authority map), `DECISIONS.md` D1–D14 in the track planning directory.
