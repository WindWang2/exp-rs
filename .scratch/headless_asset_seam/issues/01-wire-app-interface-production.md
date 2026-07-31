# Ticket 01: Wire `SicnuAppInterface` in production

**Status:** completed
**Priority:** high
**Discovered:** 2026-08-01 headless asset seam exploration

## Problem

`src/gui/main_window.cpp` (~line 273) constructs `PluginManager(m_mapCanvas, m_layerTree, this)` but never calls `PluginManager::setAppInterface()`. Only tests wire it. Consequences in the shipped app:

- `PythonPluginAdapter::initialize` builds the proxy with a null menu and null `ActiveViewHost`, so `ui.add_plugin_menu` always degrades to `ui_unavailable` and canvas/message-bar IPC methods are dead.
- Without a `ProjectContext` on the interface, the new headless asset seam also receives a null `DataManager`, so `data.add_layer` / `catalog.*` fail too.

## Scope

- Instantiate `SicnuAppInterface` with the real main window, `ActiveViewHost`, and `ProjectContext` in `SicnuMainWindow::loadPlugins()` (or its caller) and pass it to `PluginManager::setAppInterface()`.
- Verify a sample Python plugin's menu action appears and `catalog.get_active_layer` works in the running GUI app.

## Out of scope

- `processing.execute_algorithm` daemon stub (ticket 02).
