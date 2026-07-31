# Production App Interface Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `SicnuAppInterface` into the production shell (`QgisDesktopWindow`) so all Python plugin IPC channels are live in the shipped app, with an injectable plugin menu that avoids the forbidden `QMainWindow::menuBar()` path.

**Architecture:** `SicnuAppInterface` gains `setPluginMenu(QMenu*)`; `pluginMenu()` returns the injected menu when set and only then falls back to its legacy lazy `menuBar()` creation. `QgisDesktopWindow`'s constructor creates the interface (with its existing `m_activeViewHost` / `m_projectContext`), injects a 插件 menu from the detached `appMenuBar()`, and hands the interface to `PluginManager::setAppInterface` before `loadPlugins`.

**Tech Stack:** C++17, Qt 6, vendored QGIS, Catch2, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-08-01-production-app-interface-wiring-spec.md` (commit `0d3fa682d9`)

## Global Constraints

- NEVER call `QMainWindow::menuBar()` in `QgisDesktopWindow` code paths — the shell installs menu-widget chrome and `menuBar()` deletes it (`src/app/main_window.h:136-141`). Menu hosting goes through the detached `appMenuBar()` only.
- Do NOT modify `src/gui/main_window.{h,cpp}` (`SicnuMainWindow` — confirmed dead code), `worker_daemon.py`, `PluginManager`, or `PythonPluginAdapter`.
- The lazy `menuBar()` fallback in `SicnuAppInterface::pluginMenu()` must be preserved for non-injected (test/legacy) usage.
- All new app-side code touching `SicnuAppInterface` must be guarded by `#ifdef SICNU_EMBED_PYTHON` (the build defines it, but the sources must still compile with it off — `sicnu_app_interface.cpp` is only compiled then). Follow the existing guard style in `src/app/main_window.cpp` (`#ifdef SICNU_EMBED_PYTHON`).
- Match surrounding style: 4-space indent in `src/app/main_window.cpp` / `sicnu_app_interface.{h,cpp}`; 2-space indent in `tests/test_python_plugin_manager.cpp`; `QStringLiteral` for literals.
- Test binary: `./build/tests/test_python_plugin_manager` (Catch2, tag-filtered). Build: `cmake --build build --target <target> -j"$(nproc)"`.
- Sample fixture: `samples/dem_sample.tif` under `TEST_DATA_DIR` (= `<repo>/data`).

---

### Task 1: Injectable plugin menu on `SicnuAppInterface` + test additions

**Files:**
- Modify: `src/app/python/sicnu_app_interface.h` (add setter after the `projectContext()` accessor at line 48)
- Modify: `src/app/python/sicnu_app_interface.cpp:68-78` (`pluginMenu()` injection priority)
- Test: `tests/test_python_plugin_manager.cpp` (new `[python][iface]` TEST_CASE + one CRS assertion in the `[python][bridge][headless]` case)

**Interfaces:**
- Consumes: existing `SicnuAppInterface(QWidget*, ActiveViewHost*, sicnu::app::ProjectContext*, QObject*)` ctor; existing member `QMenu *m_pluginMenu = nullptr;` (`sicnu_app_interface.h:338`).
- Produces (Task 2 relies on this): `void setPluginMenu( QMenu *menu )` — injected menu takes priority in `QMenu *pluginMenu() override`.

- [ ] **Step 1: Write the failing tests**

Add this TEST_CASE after the existing `"SicnuAppInterface implements QgisInterface facade"` case (`tests/test_python_plugin_manager.cpp:71`):

```cpp
TEST_CASE( "SicnuAppInterface injected plugin menu takes priority over lazy menuBar creation", "[python][iface]" )
{
  REQUIRE( QgisPython::instance().initialize() );

  QMainWindow mainWindow;
  SicnuAppInterface iface( &mainWindow, nullptr, nullptr );

  QMenu injectedMenu( QStringLiteral( "插件" ) );
  iface.setPluginMenu( &injectedMenu );

  CHECK( iface.pluginMenu() == &injectedMenu );
  // The lazy QMainWindow::menuBar() fallback must not run when a menu is
  // injected: the real menu bar stays empty.
  CHECK( mainWindow.menuBar()->actions().isEmpty() );
}
```

In the existing `"AppInterfaceBridge headless asset seam via DataManager"` case, section `"openPath registers the asset headlessly and auto-sets the active asset"`, add one assertion after the `summary.type` check:

```cpp
    CHECK( !summary.crs.isEmpty() );
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][iface]"`
Expected: BUILD FAILURE — `SicnuAppInterface` has no `setPluginMenu` member.

Then (after the build failure is confirmed, comment nothing out — proceed to Step 3, which fixes the build; the CRS assertion may pass or fail at runtime):
**If the CRS assertion fails at runtime after Step 4** (fixture may lack CRS), STOP and report DONE_WITH_CONCERNS with the observed `summary.crs` value — do not weaken the assertion on your own.

- [ ] **Step 3: Add the setter to the header**

In `src/app/python/sicnu_app_interface.h`, after line 48 (`sicnu::app::ProjectContext *projectContext() const { return m_projectContext; }`):

```cpp

    /// Injected plugin menu takes priority over the lazy QMainWindow::menuBar()
    /// fallback — required in shells where menuBar() destroys installed chrome.
    void setPluginMenu( QMenu *menu ) { m_pluginMenu = menu; }
```

- [ ] **Step 4: Implement injection priority in `pluginMenu()`**

In `src/app/python/sicnu_app_interface.cpp`, replace the `pluginMenu()` body (lines 68-78) with:

```cpp
QMenu *SicnuAppInterface::pluginMenu()
{
    // An injected menu (e.g. hosted on a detached QMenuBar) takes priority;
    // the lazy menuBar() fallback must never run in shells whose menu-widget
    // chrome forbids QMainWindow::menuBar().
    if ( m_pluginMenu )
    {
        return m_pluginMenu;
    }
    if ( m_mainWindow )
    {
        if ( auto *mainWin = qobject_cast<QMainWindow *>( m_mainWindow ) )
        {
            m_pluginMenu = mainWin->menuBar()->addMenu( tr( "插件" ) );
        }
    }
    return m_pluginMenu;
}
```

- [ ] **Step 5: Run the iface + bridge tests**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][iface],[python][bridge]"`
Expected: PASS — new injection case, the pre-existing iface cases (which exercise the lazy fallback: `SicnuAppInterface implements QgisInterface facade` calls `pluginMenu()` without injection), and both bridge cases including the new CRS assertion.

- [ ] **Step 6: Commit**

```bash
git add src/app/python/sicnu_app_interface.h src/app/python/sicnu_app_interface.cpp tests/test_python_plugin_manager.cpp
git commit -m "feat(python): injectable plugin menu on SicnuAppInterface

setPluginMenu() lets the host supply a menu (e.g. from a detached
QMenuBar); pluginMenu() prefers it and only falls back to the legacy
lazy QMainWindow::menuBar() creation when nothing was injected. Also
locks the WKT CRS summary behavior with a non-empty assertion."
```

---

### Task 2: Wire `SicnuAppInterface` in `QgisDesktopWindow` + build & smoke verification

**Files:**
- Modify: `src/app/main_window.h` (forward declaration + guarded member after `m_pluginManager` at line 470)
- Modify: `src/app/main_window.cpp` (include in the existing `#ifdef SICNU_EMBED_PYTHON` block at lines 72-76; wiring at lines 140-143)

**Interfaces:**
- Consumes: Task 1's `void setPluginMenu( QMenu *menu )`; `QgisDesktopWindow::appMenuBar()` (`src/app/main_window.h:141`); existing members `m_activeViewHost` (`std::unique_ptr<ActiveViewHost>`), `m_projectContext` (`std::unique_ptr<sicnu::app::ProjectContext>`), `m_pluginManager` (`std::unique_ptr<class PluginManager>`); `PluginManager::setAppInterface(SicnuAppInterface*)` (`src/core/plugin_manager.h:30`).
- Produces: production wiring only — no new public API.

- [ ] **Step 1: Add the member to the header**

In `src/app/main_window.h`, add the forward declaration near the other forward declarations (after line 90, `namespace Sicnu { class PythonScriptEditor; }`):

```cpp
class SicnuAppInterface;
```

Add the guarded member after line 470 (`std::unique_ptr<class PluginManager> m_pluginManager;`):

```cpp
#ifdef SICNU_EMBED_PYTHON
    std::unique_ptr<SicnuAppInterface> m_appInterface;
#endif
```

- [ ] **Step 2: Add the include**

In `src/app/main_window.cpp`, inside the existing `#ifdef SICNU_EMBED_PYTHON` include block (lines 72-76), after `#include "python/sicnu_python_api.h"`:

```cpp
#include "app/python/sicnu_app_interface.h"
```

- [ ] **Step 3: Wire the interface in the constructor**

In `src/app/main_window.cpp`, replace lines 140-143:

```cpp
    // Load plugins
    qDebug() << "Loading plugins...";
    m_pluginManager = std::make_unique<PluginManager>(m_mapCanvas, m_layerTreeView);
    m_pluginManager->loadPlugins(QCoreApplication::applicationDirPath() + "/../plugins");
```

with:

```cpp
    // Load plugins
    qDebug() << "Loading plugins...";
    m_pluginManager = std::make_unique<PluginManager>(m_mapCanvas, m_layerTreeView);
#ifdef SICNU_EMBED_PYTHON
    // Wire the application interface facade so Python plugins get a live
    // plugin menu, the DataManager asset seam, and the view host. The menu
    // is hosted on the detached appMenuBar(): QMainWindow::menuBar() is
    // forbidden here (it deletes the installed top chrome).
    m_appInterface = std::make_unique<SicnuAppInterface>( this, m_activeViewHost.get(), m_projectContext.get() );
    m_appInterface->setPluginMenu( appMenuBar()->addMenu( tr( "插件" ) ) );
    m_pluginManager->setAppInterface( m_appInterface.get() );
#endif
    m_pluginManager->loadPlugins(QCoreApplication::applicationDirPath() + "/../plugins");
```

- [ ] **Step 4: Build the application target**

Run: `cmake --build build --target sicnu_geo_rs -j"$(nproc)"`
Expected: BUILD SUCCESS (this is the first time this change set compiles the app target; the test target alone does not cover `main_window.cpp`).

- [ ] **Step 5: Offscreen smoke run**

Run: `QT_QPA_PLATFORM=offscreen timeout --signal=TERM 25 ./build/sicnu_geo_rs 2>&1 | tail -40; echo "exit=$?"`
Expected:
- Log shows the initialization sequence reaching `Loading plugins...` (and `Window initialized`-equivalent output or later lines).
- No `Segmentation fault`, no `ASSERT`, no `menuBar`-related warnings.
- Exit code `124` (timeout kill of the event loop) or `0` is acceptable; a signal-kill crash (`139`/`134`) is a FAILURE — STOP and report BLOCKED with the log tail.

- [ ] **Step 6: Re-run the python test suite (regression)**

Run: `./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS — all cases (11 test cases, 87+ assertions; the Task-1 additions are included).

- [ ] **Step 7: Commit**

```bash
git add src/app/main_window.h src/app/main_window.cpp
git commit -m "feat(app): wire SicnuAppInterface into QgisDesktopWindow plugin loading

PluginManager now receives a fully-wired SicnuAppInterface (main window,
ActiveViewHost, ProjectContext) with the plugin menu hosted on the
detached appMenuBar, activating all Python plugin IPC channels in the
shipped app. Closes follow-up ticket 01 from the headless asset seam work."
```

---

## Self-Review Notes (completed by plan author)

- **Spec coverage:** §1 injectable menu + preserved fallback (Task 1 Steps 3-4), §2 header member + constructor wiring + null-context tolerance (Task 2 Steps 1-3; `m_projectContext` may be null and `SicnuAppInterface` accepts that), §3 test additions (Task 1 Steps 1, 5), Testing Decisions build + offscreen smoke (Task 2 Steps 4-5), Out of Scope honored (no `SicnuMainWindow`, no `displayed` flag, no daemon changes).
- **Ticket status note:** the follow-up ticket `.scratch/headless_asset_seam/issues/01-wire-app-interface-production.md` should be marked completed after Task 2 lands (fold into Task 2's commit if convenient; not a plan requirement).
- **Type consistency:** `setPluginMenu( QMenu * )` identical in Task 1 producer and Task 2 consumer; `SicnuAppInterface( this, m_activeViewHost.get(), m_projectContext.get() )` matches the ctor `( QWidget *, ActiveViewHost *, sicnu::app::ProjectContext *, QObject * = nullptr )`.
- **Include resolution:** `"app/python/sicnu_app_interface.h"` resolves through the `src/` include dir already used by `main_window.cpp` (`<core/plugin_manager.h>`), matching the pattern in `src/core/plugin_manager.cpp:4`.
- **Known risk:** the CRS assertion depends on `samples/dem_sample.tif` carrying a CRS; Task 1 Step 2 instructs the implementer to escalate rather than weaken it.
