# CAPABILITY_MATRIX — qgis-editing-annotation-11

Status legend: **implemented** (local evidence), **pre-existing** (already on master, cited), **degraded** (works with stated limits), **not-supported** (honest absence).

| Capability | Before (baseline a5b11b7f10) | After (this track) |
| --- | --- | --- |
| Aggregated edit-session state (dirty/undo/redo/selection facts) | not-supported (ad-hoc active-layer only) | **implemented** — `RsEditSession` (T1,T2) |
| Commit/rollback with error reporting | degraded — `commitChanges()` result discarded in legacy path (`main_window_vector.cpp:66`) | **implemented** — session commit collects errors (T3,T4) |
| Layer edit locks | not-supported | **implemented** — session lock, tools/guards refuse (T5) |
| Layer-removal / project-close lifecycle during edit | degraded — no guard; QMessageBox ad-hoc | **implemented** — session handles removal/close without dangling (T6,T7) |
| Point/box/polygon/freehand ROI drawing | pre-existing (`rs_roi_tool_*`) | pre-existing (untouched; no duplication) |
| Split/reshape/simplify/… vector tools | pre-existing (`MapToolManager`, QGIS-native) | pre-existing (untouched) |
| Brush sample painting | not-supported | **implemented** — `RsSampleBrushTool`, one undo per stroke (T13) |
| Erase sample painting | not-supported | **implemented** — `RsSampleEraseTool` (T14) |
| Annotation create/modify/select from app | not-supported (gui classes compiled, zero consumers) | **implemented** — `RsAnnotationController` over vendored tools (T16) |
| Snapping configuration (vertex/segment/endpoint) | not-supported (canvas utils never configured) | **implemented** — `RsSnappingController` (T8,T9) |
| Geometry validity reporting | not-supported | **implemented** — `RsGeometryValidity::validate` (T10,T12) |
| Validity repair | not-supported | **implemented** — preview + explicit apply, never silent (T11) |
| ROI pixel footprint (center-of-pixel) | pre-existing via `RsPixelRasterizer` (classification path) | **implemented** — editing-side contract reusing the same oracle (T17) |
| ROI↔raster CRS transform fail-closed | not-supported on editing surfaces | **implemented** (T18) |
| NoData/edge-aware ROI counts & stats | degraded (stats widget exists w/o NoData mask contract) | **implemented** — masked valid counts + cancelable bounded preview (T19,T20) |
| ROI class-label/attribute write-back | degraded (classification workbench internal) | **implemented** — session-wrapped write-back (T21) |
| 100k-feature queryable index over edit layer | not-supported | **implemented** — `RsEditIndex` bulk+incremental, brute-force-verified (T22,T23) |
| Selection bounds helper | not-supported | **implemented** (T24) |
| Incremental render under large edits | pre-existing (QGIS layer changed-signals scope) | pre-existing; index adds no full rebuilds (asserted by construction, T22/T23) |
| Background stats cancel | pre-existing pattern (RoiStatisticsWidget epoch) | **implemented** in ROI semantics (T20) |
| Agent-visible edit state | not-supported | **implemented** — `editing:state` read-only SpatialTool (T25,T26) |
| Agent write access to editing | not-supported | **not-supported by design** (envelope F; T26 asserts absence) |
| Atomic export GeoJSON/GPKG | not-supported | **implemented** — temp+rename, failure cleanup (T27,T28) |
| Temporary memory sample layers | pre-existing (QGIS memory provider) | pre-existing; used by tools/tests |
| Project reopen after edit | pre-existing (edits committed or lost by design) | unchanged semantics; documented |

Honest limits: brush radius units = layer CRS units (D7); erase is whole-feature; annotation controller covers item create/select/modify via vendored tools without custom item types; index capped by env for tests (D10).
