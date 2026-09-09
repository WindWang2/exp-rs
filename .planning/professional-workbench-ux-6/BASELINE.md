# Baseline — audit at track start (master @ 74fd0c7c)

## Architecture as found

```
QgisDesktopWindow (src/app/main_window*.cpp, ~10 partial class files)
 ├─ WorkbenchHost (workbench/workbench_host.{h,cpp})        — registry + activate/deactivate
 │   ├─ MapWorkbench            (workbench/adapters.cpp)    — embedded canvas stack
 │   └─ ExternalWindowWorkbench ×5 (classify, georef-i2i, georef-i2m, obia, layout)
 │       — opener-only wiring in main_window_workbench.cpp:74-92; NO windowGetter /
 │         dirtyFn / closeFn hooks installed (#813)
 ├─ SelectionContext (workbench/selection_context.*)        — debounced projection (150 ms)
 ├─ CommandRegistry (workbench/command_registry.*)          — 1 definition → n projections
 │   └─ command_defs.cpp registers ~30 shell commands
 ├─ CommandPalette (workbench/command_palette.*)
 ├─ InspectorHost (workbench/inspector_host.*)              — registerSection + rebuildTabs
 │   └─ layer_sections.cpp — General/Metadata/etc. raster/vector sections
 ├─ shell/schema_form_builder.*  — schema-driven parameter forms
 ├─ shell/task_panel_host.*, rs_job_panel.* — task/result surfaces
 ├─ ActiveViewHost (active_view_host.*)                     — active display view façade
 └─ display/qgis_display_manager.*                          — per-view QgsMapCanvas +
     QgsLayerTree + QgsMapLayerStore + QgsLayerTreeMapCanvasBridge (ADR 0019)
Data authority: data/DataManager (thread-affinity contract #703, no internal locking)
Execution: processing/framework/TaskCenter → jobs/JobEngine → RSOperatorRegistry
```

## Defects confirmed (all reproduced by reading the code; see ISSUE_MAP.md)

| # | Class | Root cause (file:line @ baseline) |
|---|-------|-----------------------------------|
| 777 | P0 UAF | `InspectorHost::rebuildTabs` `delete oldTabs` deletes QTabWidget → destroys child InspectorSection pages while `m_sections` keeps the pointers (inspector_host.cpp:95) |
| 778 | P0 UAF | `SelectionContext` holds raw `m_canvas/m_layerTree/m_workbenchHost`; cached snapshot keeps `activeLayer/selectedLayers` raw pointers; nothing invalidates the cache when a layer dies (selection_context.cpp:235-320) |
| 779 | P0 race | `QgisDisplayManager::removeLayer` removes the layer from the view's store (destroying it) without stopping/settling an in-flight canvas render (qgis_display_manager.cpp:855-896); same in replaceLayer path (:841) |
| 780 | P0 test | `test_inspector_host` never exercises supported→unsupported→supported re-selection (tests/test_inspector_host.cpp) |
| 792 | P1 abort | `Q_ASSERT_X(m_shortcutOwner.isEmpty())` fires on the *second* different command's `action(id, installShortcut=true)` — single QString owner (command_registry.cpp:86-90) |
| 793 | P1 | `ActiveViewHost::refreshCanvasLayers` reads `QgsProject::checkedLayers()` (global) instead of the active view's own tree (active_view_host.cpp:542-557) |
| 794 | P1 test | no test ever calls `action(..., installShortcut=true)` |
| 795 | P1 test | `test_shortcut_conflicts` scans only `main_window_menus.cpp` |
| 796 | P1 test | `test_layer_sync_contract` is fully synchronous; no in-flight render removal |
| 797 | P1 | ROI statistics + histogram widgets push unbounded GDAL scans onto `QThreadPool::globalInstance()` (roi_statistics_widget.cpp:152, histogram_widget.cpp:253) |
| 798 | P1 deadlock | JobEngine worker running a body that submits a sub-job and blocks on it: pool saturated (`tryPickJobLocked` `m_running >= m_maxWorkers`) → sub-job never picked; no worker-originated capacity expansion (job_engine.cpp:563) |
| 799 | P1 race | TaskCenter flush loop submits the job, THEN registers `m_taskByJobId[jobId]` — a fast-completing job's record is dropped as "unknown", task stranded Dispatching (task_center.cpp:1374-1407) |
| 800 | P1 | DataManager const accessors (`asset()`, `assets()`, `findByPath()`, `provenance()`, `derivedFrom/OutputsOf()`, `leaseCount/leases()`, `hasActiveEditLease()`, `planUnload()`, `catalogGeneration()`) enforce nothing — the documented affinity contract (#703) is prose-only for readers |
| 812 | P2 | `QTabWidget::currentChanged` never connected — non-initial tabs never populate (blank secondary tabs) |
| 813 | P2 | ExternalWindowWorkbench has setWindowGetter/setDirtyFn/setCloseFn but main_window_workbench.cpp installs none → dirty/in-flight state never reaches close/lifecycle decisions |

## Build & test infrastructure as found

- Vendored QGIS in-tree (`src/core`, `src/gui`, …); app in `src/app`; tests in `tests/` (Catch2, one executable per contract, workbench sources compiled directly into each test target).
- Machine: 16 cores, MSVC 14.38, Ninja at `C:/Qt/Tools/Ninja/ninja.exe`, Qt 6.8.0 at `C:/deps/Qt`, vcpkg at `C:/deps/vcpkg` with prebuilt deps **shared** from `exp-rs-win/build-win/vcpkg_installed` (the recipe sibling tracks use).
- Resource discipline: `-j2` build, `CTEST_PARALLEL_LEVEL=1`.
