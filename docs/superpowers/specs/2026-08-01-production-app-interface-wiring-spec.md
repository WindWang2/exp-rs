# Production App Interface Wiring Specification

**Status:** Ready for Implementation
**Date:** 2026-08-01
**Subsystem:** `src/app/python/`, `src/app/` (shell)
**ADR Ref:** ADR 0015 (Single GIS Shell Facade), ADR 0009/0010 (Data/Display seam)
**Origin:** Follow-up ticket 01 from the headless asset seam work (`.scratch/headless_asset_seam/issues/01-wire-app-interface-production.md`)

---

## Problem Statement

The Python plugin seam (headless asset seam, commits `a98ef06ceb`..`bde5d8cbdf`) is exercised only by tests: in the shipped application, `PluginManager` never receives a `SicnuAppInterface`, so `PythonPluginAdapter::initialize` builds its proxy with a null menu, null `ActiveViewHost`, and null `DataManager` — every IPC channel except `processing.register_algorithm` is dead.

The ticket's original file reference was wrong: `src/gui/main_window.cpp` (`SicnuMainWindow`) is dead code — nothing in the repository instantiates it. The real production window is `QgisDesktopWindow` (`src/app/main_window.cpp`, used by `src/app/main.cpp:48`), which already owns both prerequisites (`m_projectContext`, `m_activeViewHost`, created at `main_window.cpp:105-118`) but creates `PluginManager` without `setAppInterface()` (`main_window.cpp:142`).

One hazard blocks naive wiring: `SicnuAppInterface::pluginMenu()` lazily calls `QMainWindow::menuBar()->addMenu()` (`sicnu_app_interface.cpp:74`), and `QgisDesktopWindow` forbids calling `menuBar()` after its product chrome is installed — doing so deletes the top chrome (`src/app/main_window.h:136-141`). The moment a Python plugin initializes, the adapter calls `pluginMenu()` and would destroy the shell's top bar.

---

## Solution

Two small changes:

1. **Injectable plugin menu on `SicnuAppInterface`:** add `setPluginMenu(QMenu*)`. `pluginMenu()` returns the injected menu when set, and only falls back to the legacy lazy `menuBar()` creation when no menu was injected. The dangerous path is thereby never taken in the production shell.
2. **Wire the interface in `QgisDesktopWindow`'s constructor:** between `m_activeViewHost` creation (`main_window.cpp:112`) and `PluginManager` creation (`main_window.cpp:142`), create `SicnuAppInterface(this, m_activeViewHost.get(), m_projectContext.get())`, inject a 插件 menu built from the detached `appMenuBar()` (safe: `appMenuBar()` exists since `setupMenu()` ran at line 93, and the detached bar is the shell's designated action host), and pass the interface to `m_pluginManager->setAppInterface()`.

After wiring, `PythonPluginAdapter::initialize` obtains a working plugin menu, the `DataManager` asset seam (via `projectContext()->dataManager()`), and the view host — all six proxy IPC channels plus the in-process iface channels become live in the shipped app. Python plugin menu actions land on the detached menu bar as action/shortcut hosts, consistent with how C++ plugin `menuActions()` are handled (`main_window.cpp:156-158`).

---

## User Stories

1. As a Python plugin author, I want my plugin's `iface.addPluginToMenu()` call to register a real action in the running app, so that menu callbacks route back to my plugin over IPC.
2. As a Python plugin author, I want `iface.activeLayer()` / `addRasterLayer()` to work in the shipped app, so that my plugin is not limited to the test harness.
3. As a shell maintainer, I want the plugin menu hosted on the detached `appMenuBar()`, so that plugin loading never destroys the product top chrome.
4. As a test engineer, I want the injection-priority semantics of `pluginMenu()` locked by a unit test, so that a future refactor cannot silently re-enable the `menuBar()` path in the shell.

---

## Implementation Decisions

### 1. `SicnuAppInterface` (`src/app/python/sicnu_app_interface.h/.cpp`)

- Add `void setPluginMenu( QMenu *menu ) { m_pluginMenu = menu; }` (member `m_pluginMenu` already exists at `sicnu_app_interface.h:338`).
- `pluginMenu()`: return `m_pluginMenu` if non-null; otherwise keep the existing lazy-creation fallback unchanged.
- No other changes (stubs, `pluginToolBar()`, data/layer methods all untouched).

### 2. `QgisDesktopWindow` constructor (`src/app/main_window.cpp`, `src/app/main_window.h`)

- New member `std::unique_ptr<SicnuAppInterface> m_appInterface;` in `main_window.h`.
- In the constructor, after `m_activeViewHost` creation and before `m_pluginManager->loadPlugins(...)`:
  1. `m_appInterface = std::make_unique<SicnuAppInterface>( this, m_activeViewHost.get(), m_projectContext.get() );`
  2. `m_appInterface->setPluginMenu( appMenuBar()->addMenu( tr( "插件" ) ) );`
  3. `m_pluginManager->setAppInterface( m_appInterface.get() );`
- `m_projectContext` may be null on its failure branch (`main_window.cpp:109`): the interface and the whole adapter path are null-safe by design (headless asset seam, Task 1-3), so no extra handling is needed.
- Ordering constraint: `setPluginMenu` uses `appMenuBar()`, which is valid at this point because `setupMenu()` ran earlier in the constructor; `menuBar()` is never called.

### 3. Test additions (`tests/test_python_plugin_manager.cpp`)

- `[python][iface]`: injected-menu semantics — with a real `QMainWindow` as `mainWindow`, after `setPluginMenu(externalMenu)`, `pluginMenu()` returns `externalMenu` and the main window's `menuBar()` gained no 插件 menu.
- `[python][bridge][headless]`: add `CHECK( !summary.crs.isEmpty() )` to the openPath section (locks the WKT CRS behavior, per final-review recommendation).

---

## Testing Decisions

- Testing seam: `tests/test_python_plugin_manager.cpp` (existing `[python][iface]` and `[python][bridge][headless]` blocks).
- Test cases:
  1. Injected menu takes priority and no lazy `menuBar()` menu is created.
  2. CRS summary is non-empty after headless `openPath` on the raster fixture.
  3. Regression: all existing `[python]` cases unchanged and passing.
- Build verification: the `sicnu_geo_rs` application target must compile (this change touches app-side code; the previous round only built the test target).
- Runtime smoke: `QT_QPA_PLATFORM=offscreen` launch with a short timeout — the window completes initialization and reaches plugin loading without chrome-related crashes. (The smoke verifies "wiring does not break the shell"; plugin discovery depends on the install layout and is not asserted.)

---

## Out of Scope

- `SicnuMainWindow` (`src/gui/main_window.cpp`) — confirmed dead code (no instantiation anywhere); left untouched, removal is a separate cleanup decision.
- The `displayed` flag on the `data.add_layer` IPC response (YAGNI until a plugin needs it).
- Heavyweight GUI unit tests for the `QgisDesktopWindow` constructor (the wiring is branch-free; covered by compile + smoke).
- `worker_daemon.py` / `processing.execute_algorithm` (ticket 02).

---

## Further Notes

- `SicnuAppInterface` receives `this` as `mainWindow`: `pluginToolBar()` (`addToolBar`) and dock channels become live; `addToolBar` does not touch `menuBar()` and is safe for the product chrome. `pluginMenu()` can never reach the lazy `menuBar()` path in the shell because a menu is always injected before any plugin loads.
- Legacy dead-window cleanup candidate: `src/gui/main_window.{h,cpp}` (`SicnuMainWindow`) plus its references in `src/plugins/layer_tree/layer_tree_plugin.h` — flag for a future removal ticket.
