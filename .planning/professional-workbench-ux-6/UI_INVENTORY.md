# UI inventory @ baseline

## Shell chrome

- `main_window.{h,cpp}` + partials: menus / connections / docks / layers / misc /
  processing / project / vector / view / workbench (10 files, ~10k LOC).
- Ribbon: `shell/ribbon_controller.{h,cpp}` — pages → groups (`GroupHost`) →
  command buttons projected from the registry.
- Hidden menu host: `main_window_menus.cpp` (894 lines, literal QActions +
  QKeySequence literals — the shortcut owner QGIS Desktop apps expect).
- Command palette: `workbench/command_palette.{h,cpp}` (registry-driven).
- Workbench switcher: checkable section in 窗口 menu (manual actions).

## Panels & hosts

- InspectorHost (`workbench/inspector_host.*`) + `layer_sections.*` sections
  (general/metadata classes) — rebuildTabs lifecycle (#777/#812).
- SelectionContext (`workbench/selection_context.*`) — canvas + layer tree +
  workbench host + catalog/governance push; 150 ms debounce.
- TaskCenter surfaces: `shell/task_panel_host.*`, `shell/rs_job_panel.*`.
- Data manager panel: `panels/data_manager_panel.*` (asset selection → context).
- Workspace browser: `panels/workspace_browser_panel.*` (results selection).
- Schema forms: `shell/schema_form_builder.{h,cpp}`.
- Widgets: histogram, ROI statistics, spectral profile, cross-section,
  comparison, result summary, empty state, progress dialog…
- Design tokens: `design_tokens.h` (partial unified token system).

## Session windows (external workbenches)

- Classification lab: `classification/qgsclassificationmainwindow.*` — exposes
  `isSessionDirty()`, `hasInFlightCompute()`, `cancelInFlightCompute()`;
  `ClassifySessionAdapter` (workbench/session_adapters.*) wraps them but is
  only used by tests, not by the live shell.
- Georeferencer shells: `georeferencer/qgsgeoref_{shell,image_to_map}_window.*`;
  `rs_georeferencing_session.h` exposes `isDirty()`.
- OBIA: `obia/rs_obia_main_window.*`.
- Layout designer: `layout/qgslayoutdesignerdialog.*`.

## Display layer

- `display/qgis_display_manager.*`: per-view `{QgsMapCanvas, QgsLayerTree,
  QgsMapLayerStore, QgsLayerTreeMapCanvasBridge}` records; thread-affine
  mutations; `canvasLayerSyncCount` test hook; `activeViewId` authority (ADR 0019).
- `active_view_host.*`: shell façade over the active view (open/display/remove/
  refresh/zoom) + main-window layer tree view integration.

## Tests (in-scope executables)

test_workbench_host · test_selection_context · test_command_registry ·
test_command_palette · test_inspector_host · test_interactive_session_contract ·
test_layer_sync_contract · test_shortcut_conflicts ·
test_active_view_host_data_context · test_theme_selector_parity ·
test_job_engine · test_task_center · test_data_manager_reap ·
test_schema_form_builder (see TEST_MATRIX.md for the full matrix).
