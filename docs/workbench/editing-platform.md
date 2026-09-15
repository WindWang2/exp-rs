# Editing Platform — Workbench Contract (F11)

This document is the user/agent-facing contract of the editing foundation
(`src/app/editing/`). Behavior here is pinned by the `test_edit_*` suites;
if this document and the tests disagree, that is a bug.

## 1. Edit session (`RsEditSession`)

* **Attach**: `attachLayer(layer, startEditing=true)` starts editing and
  fails closed with an error string when the layer cannot edit (invalid
  layer, read-only provider, already tracked). An *already-editing* layer is
  adopted, not rejected.
* **Facts**: `state(layerId)` returns `{editing, modified, locked,
  undoDepth, redoDepth, featureCount, selectedCount}`; `isDirty()` is the
  aggregate over attached layers. `featureCount` is maintained incrementally
  (counted once at attach), so state refreshes are O(1) even on 100k-feature
  layers.
* **Undo/redo**: `undo(id)/redo(id)` delegate to the layer's own undo stack
  and refuse (no side effect) when the layer is unknown, locked, or has
  nothing on that side. `undoAll()/redoAll()` move every movable layer.
* **Commit/rollback**: `commit(id)` collects provider errors on failure — a
  rejected commit is never silent (fallback message when the layer names no
  reason, e.g. `setAllowCommit(false)`). `commitAll(errors)` commits every
  dirty layer and reports per-layer failures. `rollback(id)` discards
  changes *and* stops editing; `detachLayer(id)` rolls dirty layers back by
  default.
* **Lifecycle**: layers removed from the project are dropped from tracking
  *inside* `layerWillBeRemoved`, so no callback can observe a dangling
  layer. A dirty session under `QgsProject::clear()` shrinks to empty and
  stays usable.
* **Locks**: a locked layer refuses edit command guards and undo/redo.
  Commit/rollback are deliberately never lockable (save paths stay
  available).

## 2. Map tools

* **Brush** (`RsSampleBrushTool`): left-drag paints disc stamps; release
  adds ONE multipart feature under ONE edit command (one undo step). Target
  layers MUST be MultiPolygon-typed — a single-part layer is refused
  explicitly (a multipart stroke would otherwise only fail later at
  commit). Stamp radius is in **layer CRS units**; stamp tessellation is
  `kDiscSegments` per quadrant. Strokes coalesce beyond `kMaxStampsPerStroke` stamps to
  bound memory. Without a target layer / without edit mode / while locked /
  without a positive radius, the tool emits `strokeRefused(reason)` and
  touches nothing.
* **Erase** (`RsSampleEraseTool`): same stroke contract; removes whole
  features intersecting any stamp of the stroke (never splits geometry).
  A stroke hitting nothing commits zero removals (not an error).
* **Split / reshape / simplify / …**: QGIS-native tools under
  `MapToolManager` (pre-existing; this platform does not duplicate them).
* **Annotations** (`RsAnnotationController`): owns the project's single
  `QgsAnnotationLayer` (reused on project reopen, never duplicated);
  programmatic `addPointText/addMarker/removeItem`; optional interactive
  tools from the vendored gui registry. Annotation undo is not supported in
  this slice (QGIS annotation layers have no edit buffer).

## 3. Snapping & validity

* `RsSnappingController` is the single writer of the canvas snapping
  config; read-back getters mirror exactly what was set. Default profile:
  vertex+segment on the active layer, intersection snapping configurable.
* `RsGeometryValidity::validate` returns structured issues (never throws,
  null geometry is an issue). `repairPreview` returns a `makeValid`
  candidate; **nothing in the editing platform applies a repair implicitly**
  — callers decide, under an edit command.

## 4. ROI ↔ raster semantics (`RsRoiSemantics`)

* CRS: ROI geometries are transformed to the raster CRS first; unusable
  transforms fail closed with an error (never raw untransformed coordinates).
* Footprint: center-of-pixel membership via `RsPixelRasterizer`, computed on
  a **window** (ROI bbox ∩ raster extent), never the full raster.
* NoData/edge: `pixelCount` counts covered pixels inside the extent;
  `validPixelCount` excludes NoData pixels (band-1 mask); statistics are
  computed over valid pixels only.
* Stats preview: per-band min/max/mean/population-stddev; cancelable;
  ROIs above the pixel budget are refused with no partial results.
* `writeClassLabel` writes the class attribute under one edit command;
  missing fields and locked layers fail closed.

## 5. Large editing (`RsEditIndex`)

* One `QgsSpatialIndex` per layer, bulk-built at attach (bounded, cancelable)
  and maintained incrementally from `featureAdded/featureDeleted/
  geometryChanged` — including undo/redo-driven geometry changes.
* Queries (`intersects`, `nearest`, `bounds`, `selectionBounds`) never scan
  the layer and never trigger UI rebuilds.
* Attach refuses layers above the cap (default 100 000 features;
  `RS_EDIT_INDEX_MAX_FEATURES` opt-in override), fail-closed.

## 6. Agent surface

* `editing:state` (read-only `SpatialTool`, catalog group `editing`):
  `{editing: {dirty, layers: [{id, name, editing, modified, locked,
  undoDepth, redoDepth, featureCount, selectedCount}], snapping:
  {enabled, tolerance, intersectionSnapping}}}`.
* **No agent write path exists**: executing the tool cannot change feature
  counts, buffers, stacks, or locks. Layer writes stay explicit app
  commands with undo/permission semantics.

## 7. Persistence (`RsEditPersistence`)

* `exportLayer(layer, targetPath, layerName)` — suffix decides the driver
  (`.gpkg` → GPKG, `.geojson` → GeoJSON); anything else is refused.
* Writes go to `<target>.tmp-<pid>` and atomically rename over the target;
  every failure path removes the temp file and leaves any previous target
  byte-identical. Paths are QString (Unicode-safe).
* `commitReported(layers, errors)` commits dirty layers with per-layer
  error strings (never discards commit results).

## 8. Known limits / follow-ups

* Legacy `main_window_vector.cpp` dialogs are not yet routed through the
  session (migration follow-up).
* No OGR provider plugins in the current build profile: file-based
  re-open tests degrade to format-level oracles; GPKG round-trip via
  `QgsVectorLayer` re-open requires a provider build.
* Annotation undo, live snapping *indicator* UI, and a snap/validity panel
  wiring are follow-ups (`qgssnappingwidget.cpp` exists compiled-but-
  unwired upstream in the app and is intentionally untouched here).
