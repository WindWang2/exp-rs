# CURRENT_ARCHITECTURE — editing/ROI authority map (as of baseline a5b11b7f10)

```
QgisDesktopWindow (src/app/main_window*.cpp)
├── MapToolManager (src/app/map_tools/map_tool_manager.{h,cpp})
│     owns QGIS-native tools: pan/zoom/identify/measure/select,
│     addFeature, move/rotate/scale, offsetCurve, reshape, splitFeatures,
│     splitParts, simplify, reverseLine, addRing/addPart/fillRing,
│     deletePart/deleteRing, trimExtend, chamferFillet, featureArray,
│     QgsVertexTool (src/app/vertextool/)
├── legacy edit actions (src/app/main_window_vector.cpp)
│     checkUnsavedChanges(): scans ALL project layers, QMessageBox,
│       commitChanges() return value DISCARDED (:66)  ← silent-commit-failure bug class
│     undo()/redo(): active layer only, no aggregate state (:110-126)
├── classification workbench (src/app/classification/)
│     RsRoiTool{Point,Rectangle,Polygon,Freehand,MagicWand} → roiDrawn(geom,classId)
│     → RsPixelRasterizer → RsRoiCollection (RsRoi IO: rs_roi_io.h)
├── workbench infra (src/app/workbench/, main_window_workbench.cpp)
│     MissionContext (+store, sidecar/project-XML persistence)   [D18, merged]
│     IWorkbench registry (isDirty()/augmentSelectionContext())
│     WorkbenchContextTool → SpatialToolRegistry ("workbench:context",
│       injected PayloadProvider + QPointer<QgisDesktopWindow> guard)
├── agent surface (src/agent/)
│     SpatialTool (name ns:tool, input/output JSON schema, SpatialToolResult)
│     SpatialToolRegistry (singleton) → SpatialToolProvider::provideTools()
│       filters id prefixes {spatial:, temporal:, cartography:, symbology:,
│       workflow:, workspace:, project:/asset:/lineage:/result:/run:/
│       collection:, harness:} → AgentTool catalog (tools/list, MCP)
│     InteractionToolRegistry (second surface), mcp_server.{h,cpp}
├── analysis authorities (src/analysis/)
│     RsPixelRasterizer (center-of-pixel, windowed) — canonical membership
│     RsRoiLabeler (ROI-majority labeling; fail-closed + cancel)
│     RsTrainingDataExtraction (class field fallback chain classField→"class"→"id")
└── canvas: QgsMapCanvas::snappingUtils() exists but NOTHING in the app
      configures snapping (grep: zero hits in src/app)

QGIS gui (vendored, curated subset in src/gui/CMakeLists.txt)
├── maptools/: full upstream impls (qgsmaptoolcapture 2204 lines etc.) — no stubs
└── annotations/: 10 classes compiled (create/modify/select annotation map
    tools, item widgets) — ZERO app-level consumers today
```

## Gaps (numbered; each maps to a work package)

| # | Gap | Package |
|---|-----|---------|
| G1 | No editing-session authority (aggregated edit state, undo/redo, commit-error reporting, lock, layer-removal lifecycle) | A |
| G2 | No brush/erase sample painting; annotations unreachable from app | B |
| G3 | Snapping never configured; no validity report/preview path | C |
| G4 | ROI↔raster semantics (footprint, raster-CRS fail-closed transform, NoData-aware counts, cancelable stats) not attached to any edit surface | D |
| G5 | No maintained spatial index over an editing layer; no bounded/cancelable large-layer helpers | E |
| G6 | No `editing:` namespace in agent catalog; edit facts invisible to agents | F |
| G7 | No atomic export/interchange (temp+rename) or commit-with-error-report helper | G |

## Target architecture (this track adds)

```
src/app/editing/                          [NEW — the authority layer]
├── rs_edit_session.{h,cpp}               A  aggregated edit authority (attach,
│                                            state, undo/redo, commit/rollback
│                                            with errors, locks, lifecycle)
├── rs_edit_command_guard.{h,cpp}         A  RAII beginEditCommand/endEditCommand
├── rs_snapping_controller.{h,cpp}        C  configures canvas QgsSnappingConfig
├── rs_geometry_validity.{h,cpp}          C  validate + repairPreview (no silent apply)
├── rs_sample_brush_tool.{h,cpp}          B  brush stamps → one edit command / stroke
├── rs_sample_erase_tool.{h,cpp}          B  erase intersecting sample features
├── rs_annotation_controller.{h,cpp}      B  wires vendored annotation tools to a
│                                            project QgsAnnotationLayer
├── rs_roi_semantics.{h,cpp}              D  footprint/CRS/NoData/stats/cancel
├── rs_edit_index.{h,cpp}                 E  QgsSpatialIndex maintenance + queries
├── rs_edit_agent_tool.{h,cpp}            F  "editing:state" read-only SpatialTool
└── rs_edit_persistence.{h,cpp}           G  atomic GeoJSON/GPKG export + commit report

Wiring (append-only): main_window_workbench.cpp (session + tool registration),
src/app/CMakeLists.txt (new sources), tests/CMakeLists.txt (new test blocks),
tests/test_edit_*.cpp + test_editing_e2e.cpp, docs/adr/0163-*, docs/workbench/editing-platform.md
```

Invariant: `RsEditSession` holds **no geometry truth of its own** — QGIS `QgsVectorLayer` edit buffers remain the geometry authority; the session is the *state/lifecycle/reporting* authority over them. `RsPixelRasterizer` stays the only pixel-membership oracle. One snapping engine (canvas `QgsSnappingUtils`). One agent catalog (`SpatialToolRegistry`).
