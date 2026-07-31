# Headless Asset Seam for Python App Interface Proxy Specification

**Status:** Ready for Implementation
**Date:** 2026-08-01
**Subsystem:** `src/python/isolated/`, `src/app/python/`
**ADR Ref:** ADR 0015 (Single GIS Shell Facade / ActiveViewHost), ADR 0009/0010 (DataManager as asset authority)
**Origin:** Architecture review Candidate 3 — "Replace Shallow `QgisInterface` IPC Proxying with Headless Asset Seam"

---

## Problem Statement

Out-of-process Python plugins call `iface.*` methods that become JSON-RPC requests handled by `PythonAppInterfaceProxy` (`src/python/isolated/python_app_interface_proxy.cpp`). After the ADR 0015 refactor, every proxy method funnels through `AppInterfaceBridge`, which is bound exclusively to `ActiveViewHost*` — a GUI shell facade over `QgsMapCanvas` / `QgsMessageBar` / `QgsLayerTreeView`. Of the six IPC methods, only `processing.register_algorithm` is headless-safe; `catalog.get_active_layer`, `data.add_layer`, `canvas.get_state`, `ui.push_message_bar`, and `ui.add_plugin_menu` all require Qt Widgets to function.

This makes the Python plugin seam unusable in the headless Agent Executor mode (CLI `--pipeline`, MCP `--mcp`; see `.planning/architecture.md`), where no QWidget may exist. The asset operations the plugins actually need — registering data sources, querying the catalog — are core `DataManager` capabilities that have no intrinsic GUI dependency. The proxy is therefore shallow: it proxies GUI widgets where it should proxy assets.

Two related defects were discovered during exploration and are explicitly **out of scope** (recorded as follow-up tickets):

1. In production, `SicnuAppInterface` is never instantiated (`src/gui/main_window.cpp` creates `PluginManager` but never calls `setAppInterface()`), so the GUI plugin UI channel is currently dead outside tests.
2. `worker_daemon.py` answers `processing.execute_algorithm` with `-32601`, so registered Python algorithms currently succeed without executing.

---

## Solution

Invert the dependency of `AppInterfaceBridge`: bind it to `sicnu::data::DataManager*` (required, the headless asset authority) with `ActiveViewHost*` demoted to an optional enhancement (display, canvas state, message bar). The bridge gains an explicit `m_activeAssetId` that replaces "canvas current layer" as the meaning of "active layer" for plugins. `PythonAppInterfaceProxy` and `PythonPluginAdapter` are adapted so the proxy can be constructed and fully serve asset/catalog/processing IPC methods without any QWidget. GUI-mode behavior is preserved whenever a view host and menu are bound.

The Python-side daemon protocol is unchanged except for one additive method, `catalog.set_active_layer`; `worker_daemon.py` itself is not modified.

---

## User Stories

1. As a headless agent executor, I want Python plugin `iface.addRasterLayer()` / catalog calls to work through `DataManager` without any `QgsMapCanvas`, so that plugins can run in CLI/MCP mode in the future.
2. As a plugin author, I want `iface.activeLayer()` to return the layer I most recently added (or explicitly selected), so that my plugin logic behaves identically in GUI and headless modes.
3. As a developer, I want GUI-only IPC methods (`ui.add_plugin_menu`, `canvas.get_state`, `ui.push_message_bar`) to degrade gracefully when no view host is bound, so that headless operation never crashes the proxy.
4. As a test engineer, I want to drive the proxy's full asset IPC chain (`data.add_layer` → `catalog.get_active_layer` → `catalog.set_active_layer`) in a unit test without constructing any QWidget, so that the headless seam is continuously verified.

---

## Implementation Decisions

### 1. `AppInterfaceBridge` rebind (`src/python/isolated/app_interface_bridge.h/.cpp`)

- Constructor changes from `(ActiveViewHost*)` to `(sicnu::data::DataManager *dataManager, ActiveViewHost *viewHost = nullptr)`.
- New member `QString m_activeAssetId` and method `setActiveAsset(const QString &assetId)` (validates the asset exists in `DataManager`; returns structured error otherwise).
- `getActiveLayerSummary()`: resolves `m_activeAssetId` via `DataManager::asset(id)` and serializes; when unset or the asset was removed, returns the existing `no_active_layer` fallback (Python contract unchanged).
- `openPath(path)`: registers via `DataManager::registerSource(...)`; on success auto-sets `m_activeAssetId` to the new asset. When `m_viewHost` is non-null, additionally routes through the existing view host display path so layers still appear on the map in GUI mode.
- `getCanvasViewportSummary()` / `pushMessageBarAlert(...)`: logic unchanged; with `m_viewHost == nullptr` return the existing `no_canvas` empty structure / silently succeed.

### 2. `PythonAppInterfaceProxy` adaptation (`src/python/isolated/python_app_interface_proxy.h/.cpp`)

- Constructor changes from `(PythonIpcServer*, QMenu*, ActiveViewHost*, QObject*)` to `(PythonIpcServer*, sicnu::data::DataManager*, QMenu* = nullptr, ActiveViewHost* = nullptr, QObject* = nullptr)`.
- `handleIpcMessage` gains a `catalog.set_active_layer` branch → `bridge.setActiveAsset(id)`.
- `ui.add_plugin_menu`: with `m_parentMenu == nullptr`, returns a structured `ui_unavailable` response instead of dereferencing null.
- `setParentMenu` / `setActiveViewHost` retained for GUI-mode late binding.

### 3. `PythonPluginAdapter` initialization (`src/app/python/python_plugin_adapter.cpp`)

- `initialize` obtains `DataManager*` from `SicnuAppInterface::projectContext()->dataManager()` when the interface is present; menu and view host are still late-bound in GUI mode as today. When no interface is wired (current production state), existing degradation behavior is preserved unchanged.

### 4. `SicnuAppInterface` (`src/app/python/sicnu_app_interface.h/.cpp`)

- No behavioral change. Its existing `ProjectContext*` (constructor-injected, see `sicnu_app_interface.h:34-37`) is the adapter's access path to `DataManager`; add a `projectContext()` accessor only if one does not already exist.

### 5. Behavior change (acknowledged)

In GUI mode, `catalog.get_active_layer` changes from "canvas current layer" to "the plugin-driven `m_activeAssetId`". Rationale: identical semantics across modes; `data.add_layer` auto-setting active covers the dominant plugin usage. Acceptable because the production GUI channel is currently dead (follow-up ticket 1), so no real user workflow depends on canvas-selection reporting.

---

## Testing Decisions

- Testing seam: `tests/test_python_plugin_manager.cpp`, extending the existing `[python][bridge]` and `[python][isolated][api]` blocks.
- Test cases:
  1. Bridge headless asset flow: construct `DataManager` + `AppInterfaceBridge(dataManager, nullptr)`; assert `openPath` registers the asset, auto-sets active, and `getActiveLayerSummary` returns it.
  2. Bridge active-asset semantics: unset → `no_active_layer`; `setActiveAsset` with unknown id → structured error; valid id → query hits.
  3. Bridge headless degradation: no view host → `getCanvasViewportSummary` returns `no_canvas`; `pushMessageBarAlert` does not crash.
  4. Proxy headless IPC slice: construct proxy with `DataManager*` + null menu + null view host; drive `data.add_layer` → `catalog.get_active_layer` → `catalog.set_active_layer` via `handleIpcMessage`; assert no `QgsMapCanvas` is involved.
  5. Proxy UI degradation: `ui.add_plugin_menu` with null menu returns `ui_unavailable`, no crash.
  6. GUI regression: existing `[iface]`, `[adapter]`, `[bridge]`, `[api]` cases updated to new constructor signatures with assertions unchanged.

---

## Out of Scope

- Fixing production wiring of `SicnuAppInterface` in `src/gui/main_window.cpp` (follow-up ticket).
- Implementing `processing.execute_algorithm` in `worker_daemon.py` (follow-up ticket).
- Loading Python plugins in CLI `--pipeline` / MCP `--mcp` modes.
- Modifying `worker_daemon.py` or the Python-side `SicnuPythonIface` API surface (`catalog.set_active_layer` is added at the IPC layer only; exposing it to the Python API is future work).
- Changes to `DataManager`, `ActiveViewHost`, `PythonIpcServer`, or the IPC transport.

---

## Further Notes

- Aligns with ADR 0015 (view host as the single GUI facade — now optional) and ADR 0009/0010 (`DataManager` as asset authority).
- Precedent for "assets headless, map view optional": `WorkspaceSnapshot::capture(dataManager, viewHost = nullptr)` (`src/agent/workspace_snapshot.h`).
- Follow-up tickets to be recorded under `.scratch/`:
  1. Wire `PluginManager::setAppInterface` in production (`main_window.cpp`).
  2. Implement `processing.execute_algorithm` in `worker_daemon.py` (currently `-32601`).
- `CONTEXT.md` glossary entries for the proxy/bridge to be updated upon implementation.
