# Spec: Tailor-Made QGIS-Modeled Python Plugin System

**Parent:** ADR 0009, ADR 0010, Wayfinder Map [#75](https://github.com/WindWang2/exp-rs/issues/75)  
**Status:** Approved — Ready for implementation tickets (`/to-tickets` & `/implement`).

## Problem & Goal

SICNU GEO RS needs a clean, tailor-made plugin system allowing internal developers and external users to extend the platform with Python and C++ plugins modeled after QGIS (`iface`, `metadata.txt`, `classFactory`, `initGui`, `unload`).

The plugin system must **not** be a legacy port that bypasses our architecture. Instead, it must seamlessly integrate with:
- **Data & Display Seam (ADR 0009 / ADR 0010):** All layer loading from Python routes through `ActiveViewHost::openPath()`, registering a `DataAsset` in `DataManager` and displaying it on the active `DisplayView`.
- **Task Center & Algorithm Engine:** Python-authored processing algorithms register with `AlgorithmEngine` and execute asynchronously via `TaskCenter::submitJob`.
- **Unified Plugin Host:** Single `PluginManager` hosting both C++ (`QPluginLoader`) and Python (`classFactory(iface)`) plugins.

---

## Architectural Design

```text
                               ┌────────────────────────────────┐
                               │     Python / C++ Plugin        │
                               └──────────────┬─────────────────┘
                                              │ classFactory(iface)
                                              ▼
┌───────────────────────────────────────────────────────────────────────────────┐
│                    Application Facade: SicnuAppInterface                       │
│                    (Inherits QGIS QInterface / QgisInterface)                 │
├───────────────────────────────────────────────────────────────────────────────┤
│  UI Facade                     Data & Display Seam       Processing           │
│  ├─ mainWindow()               ├─ activeViewHost()       └─ algorithmEngine() │
│  ├─ mapCanvas()                ├─ projectContext()          (submits Task)    │
│  ├─ layerTreeView()            ├─ addRasterLayer()                            │
│  ├─ messageBar()               └─ addVectorLayer()                            │
│  └─ addPluginToMenu()             (routes to DataManager                      │
│                                    + DisplayManager)                          │
└───────────────────────────────────────┬───────────────────────────────────────┘
                                        │
                                        ▼
┌───────────────────────────────────────────────────────────────────────────────┐
│                      Unified PluginManager Host                               │
│  - Scans plugins/ directory (metadata.txt + __init__.py OR .so)              │
│  - Dependency DAG resolution & ordered load/unload                            │
│  - Python exception sandboxing (QgsMessageBar + log)                          │
└───────────────────────────────────────────────────────────────────────────────┘
```

---

## Component Specifications

### 1. `SicnuAppInterface` Facade Adapter
- **File:** `src/app/python/sicnu_app_interface.h` & `.cpp`
- **Inheritance:** `class SicnuAppInterface : public QgisInterface`
- **Responsibilities:**
  - Wraps `QgisDesktopWindow`, `ActiveViewHost`, and `ProjectContext`.
  - Implements core `QgisInterface` methods:
    - `mapCanvas()` $\rightarrow$ active view canvas
    - `layerTreeView()` $\rightarrow$ active view tree
    - `mainWindow()` $\rightarrow$ `QgisDesktopWindow`
    - `messageBar()` $\rightarrow$ main window message bar
    - `activeLayer()` $\rightarrow$ `ActiveViewHost::activeLayer()`
    - `addRasterLayer(path, name)` / `addVectorLayer(path, name)` $\rightarrow$ `ActiveViewHost::openPath(path)`
    - `addPluginToMenu(name, action)` / `removePluginMenu(name, action)` $\rightarrow$ `appMenuBar()` / Plugins menu
    - `addToolBarIcon(action)` / `removeToolBarIcon(action)` $\rightarrow$ Plugins toolbar
    - `addDockWidget(area, widget)` / `removeDockWidget(widget)` $\rightarrow$ QMainWindow dock system

### 2. Embedded Python Runner & Global Hook (`SicnuPythonRunner`)
- **File:** `src/python/sicnu_python_runner.h` & `.cpp`
- **Inheritance:** `class SicnuPythonRunner : public QgsPythonRunner`
- **Responsibilities:**
  - Wraps `QgisPython::instance().runString()` and `evalString()`.
  - Registered globally via `QgsPythonRunner::setInstance(new SicnuPythonRunner())` on `QgisPython::initialize()`.
  - Unblocks C++ expression functions, custom attribute forms, and Scintilla Python editor execution.

### 3. Data & Display Seam Refactoring (`SicnuPythonApi`)
- **File:** `src/python/sicnu_python_api.h` & `.cpp`
- **Refactoring:**
  - `addRasterLayer(path, name)` and `addVectorLayer(path, name)` updated from raw `QgsProject::addMapLayer` to call `ActiveViewHost::openPath(path)`.
  - Guarantees `DataAsset` creation in `DataManager` and `DisplayLayer` creation in active `DisplayView`.

### 4. Unified `PluginManager` & Python Adapter
- **Files:** `src/core/plugin_manager.h` & `.cpp`, `src/app/python/python_plugin_adapter.h` & `.cpp`
- **Responsibilities:**
  - Scans `plugins/` directory for folders containing `metadata.txt` + `__init__.py`.
  - Parses `metadata.txt` for `name`, `version`, `description`, `plugin_dependencies`.
  - Instantiates Python plugins via `plugins[pkg] = package.classFactory(iface)`.
  - Invokes `initGui()` on load and `unload()` on teardown/disable.
  - Catches exceptions in Python plugin code, displays error in `QgsMessageBar`, and safely unloads partial `sys.modules`.

### 5. Python Processing Provider Integration
- **Files:** `src/processing/framework/algorithm_engine.h` & `.cpp`
- **Responsibilities:**
  - Listens to `QgsApplication::processingRegistry()->providerAdded`.
  - Wraps Python-authored `QgsProcessingAlgorithm` instances in `QgsProcessingAlgorithmAdapter`.
  - Submits executions to `TaskCenter::submitJob` for background thread pool execution.

### 6. CMake & SIP 6.x Build Integration
- **Files:** `CMakeLists.txt`, `cmake/SIPMacros.cmake`, `src/python/CMakeLists.txt`
- **Responsibilities:**
  - Controlled by `option(SICNU_EMBED_PYTHON "Enable Python embedding and SIP bindings" ON)`.
  - Invokes `sip-build` with dynamically generated `pyproject.toml` to parse `refs/qgis/python/PyQt6/` `.sip.in` files.
  - Compiles `_core.so`, `_gui.so`, `_analysis.so` C-extensions linked to `qgis_core`, `qgis_gui`, `qgis_analysis`.

---

## Implementation Waves

### Wave 1 — Embedded Engine & Seam Refactor (Tasks #79 & #81)
1. Implement `SicnuPythonRunner` and register with `QgsPythonRunner::setInstance()`.
2. Refactor `SicnuPythonApi` to delegate to `ActiveViewHost::openPath()`.
3. Enable CMake `SICNU_EMBED_PYTHON=ON` by default and verify `import qgis.core` in unit tests.

### Wave 2 — Application Facade & Plugin Host (Tasks #77 & #78)
1. Implement `SicnuAppInterface` inheriting `QgisInterface`.
2. Extend `PluginManager` to scan Python plugin folders, parse `metadata.txt`, and invoke `classFactory(iface)`.
3. Wire UI menu (`Plugins` / 插件), toolbar, dock, and error sandboxing (`QgsMessageBar`).

### Wave 3 — Processing Integration & End-to-End Test (Task #80)
1. Wire `QgsProcessingRegistry::providerAdded` signal to `AlgorithmEngine::populateFromProcessingRegistry()`.
2. Add end-to-end unit test with sample Python plugin loading, UI action triggering, and processing algorithm execution via Task Center.

---

## Verification Plan

### Automated Tests
```bash
# 1. Build with Python embedding enabled
cmake -B build -DSICNU_EMBED_PYTHON=ON && cmake --build build -j$(nproc)

# 2. Run Python engine & seam tests
./build/tests/test_python_engine
./build/tests/test_python_plugin_manager
./build/tests/test_python_processing_provider
```

### Manual Verification
1. Open application shell `./build/sicnu_geo_rs`.
2. Verify Python Console dock opens and executes `iface.activeLayer()`.
3. Load a sample Python plugin from `plugins/sample_plugin`.
4. Verify plugin menu action creates a `DataAsset` in Data Manager and displays a layer in 视图图层 dock.
