# CURRENT_ARCHITECTURE — linked-visual-analytics-11 (at baseline a5b11b7f)

## Authority map (linked-visual relevant)

| Concern | Authority | Notes |
|---|---|---|
| View registry | `display::QgisDisplayManager` (owned by `ProjectContext`, by value) | views = DisplayViewId (QUuid); signals: activeViewChanged, viewAdded, viewAboutToBeRemoved, viewRemoved, autoDisplayFailed |
| Layer id (per view) | `DisplayLayerId` (custom property "sicnu/displayLayerId" on QgsMapLayer) | relocate/adopt keep id across views |
| Cross-view layer identity | `DisplayLayerSnapshot::assetId()` (data::AssetId) | empty → not linkable (D2) |
| Visibility mutation | `QgisDisplayManager::setLayerVisible` | **emits nothing at baseline** (D3 adds the observation seam) |
| Canvas geometry | QgsMapCanvas (extentsChanged, xyCoordinates, scale(), rotation()) | QGIS vendored at src/gui |
| Extent link (dual split) | `shell::RsDualViewportSyncController` | separate surface; untouched |
| Extent link (N-view) | `app::ViewLinkController` (shell/) | exists, unmounted, no groups/history/cursor |
| VA payloads | `va::VaData` (bounded value types) | histogram/series/scatter/box/matrix/areas |
| VA async | `va::VaDataSource` + `RsScanPool` (2 workers, generation tokens) + `marshal_ui` | stale-drop pattern |
| VA charts | `va::VaChartWidget` | rangeSelected/pointSelected/categorySelected; no hover |
| VA panel | `va::VaWorkbenchPanel` (dock; mounted in main_window_workbench.cpp:595) | histogram→scatter filter is private |
| Selection projection | `SelectionContext` / `SelectionContextSnapshot` / `ContextRules` | workbench-level "what is selected", NOT a linkage bus |
| Object identity | `object_identity.h` (WorkbenchObjectRef, ObjectKind) | typed refs for agent/workbench |
| Commands | `CommandRegistry` + `command_defs.cpp` | availability from ContextFacts; help↔registry 1:1 enforced by test_command_contract_9 |

## Target architecture (this track)

```
QgisDesktopWindow
 ├─ ProjectContext::displayManager ──► ViewLinkController (groups, history,
 │                                      extent+cursor sync, crosshair markers)
 │                                 ──► VaLayerLinkController (visibility/opacity
 │                                      by AssetId; observes layerStateChanged)
 ├─ VaSelectionHub (process-level; shell-owned instance)
 │     ▲ publish: charts (range/category/point), views (cursor/region)
 │     ▼ subscribe: VaWorkbenchPanel (brushing), probes
 ├─ VaCursorProbe (RsScanPool hover sampling, stale-generation drop)
 └─ CommandRegistry: view.* (linkCenter/linkScale/linkCursor/linkVisibility/
                          linkUndo/linkGroupStatus)
```

Loop suppression is two-layer: hub generation echo-drop (D5) + controller
mApplying guards for Qt signal echo. All cross-CRS transforms fail closed
(transform error → skip that view, count it; never apply untransformed
geometry — lesson from open issue #1005).
