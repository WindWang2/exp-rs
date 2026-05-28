# Phase 2A: Python Embedding + pybind11 Bridge + Console UI

## Overview

Embed a CPython runtime into the SICNU GEO RS application, create pybind11 bindings for the full QGIS C++ API, and provide an interactive Python console dock widget.

This is Phase 2A of the Python plugin support initiative. Phase 2B (plugin loader, Python processing provider) will be designed separately.

## Architecture

### Python Runtime

- Embed CPython 3.x via `Py_Initialize()` in `main()` after `QgsApplication::initQgis()`
- `Py_Finalize()` before application exit
- Python home configured to use system Python installation
- GIL management: single-threaded initially (main thread only)

### pybind11 Bridge

- **Dependency**: pybind11 via CMake FetchContent
- **Module structure**: Matches QGIS's Python module layout:
  - `qgis.core` — Core classes (QgsProject, QgsMapLayer, QgsVectorLayer, QgsRasterLayer, QgsGeometry, QgsFeature, QgsFields, QgsCoordinateReferenceSystem, QgsRectangle, QgsPointXY, QgsApplication, etc.)
  - `qgis.gui` — GUI classes (QgsMapCanvas, QgsLayerTreeView, QgsCodeEditorPython, etc.)
  - `qgis.analysis` — Analysis classes (QgsRasterAnalyzer, etc.)
- **Binding files**: One `.cpp` per module (`src/python/bindings_core.cpp`, `bindings_gui.cpp`, `bindings_analysis.cpp`)
- **Entry point**: `src/python/qgis_python.cpp` — defines the top-level `qgis` Python module that re-exports sub-modules
- **Coverage goal**: All 500+ public QGIS classes. Implemented incrementally — each class added as needed, but architecture supports full coverage.

### Console UI

- **Widget**: `PythonConsoleWidget` (inherits QWidget)
  - Top: `QgsCodeEditorPython` for code input (syntax highlighting, auto-indent)
  - Bottom: `QTextEdit` (read-only) for output/stderr
  - Toolbar: Run, Clear, Load Script, Save Script
- **Dock**: Added as `QDockWidget` titled "Python Console" in bottom dock area
- **Menu**: Processing menu → "Python Console" item to show/raise the dock
- **Execution**: `PyRun_String()` in the embedded interpreter, capture stdout/stderr via `sys.stdout` redirection
- **API**: Inherits `QgsPythonRunner` interface so C++ code can call Python

### Integration

```
main()
├── QgsApplication init
├── Py_Initialize()                    ← NEW
├── init_python_bindings()             ← NEW: register pybind11 modules
├── QgsApplication::processingRegistry()->addProvider(...)
├── QgisDesktopWindow constructor
│   ├── setupMapCanvas()
│   ├── setupDockWidgets()
│   │   ├── Layers dock
│   │   ├── Browser dock
│   │   ├── Processing Toolbox dock
│   │   ├── Python Console dock       ← NEW
│   │   └── Overview dock
│   ├── setupMenus()
│   │   └── Processing menu
│   │       ├── Toolbox
│   │       ├── Python Console        ← NEW
│   │       └── History
│   └── ...
├── app.exec()
├── Py_Finalize()                      ← NEW
└── exit
```

## Changes

### New Files

1. **`src/python/qgis_python.h`** — Python runtime manager (init, finalize, execute)
2. **`src/python/qgis_python.cpp`** — Implementation
3. **`src/python/bindings_core.cpp`** — pybind11 bindings for qgis.core
4. **`src/python/bindings_gui.cpp`** — pybind11 bindings for qgis.gui
5. **`src/python/bindings_analysis.cpp`** — pybind11 bindings for qgis.analysis
6. **`src/python/bindings.cpp`** — Top-level module definition, registers sub-modules
7. **`src/gui/python_console_widget.h`** — PythonConsoleWidget header
8. **`src/gui/python_console_widget.cpp`** — PythonConsoleWidget implementation

### Modified Files

1. **`CMakeLists.txt`**:
   - Add `find_package(Python REQUIRED COMPONENTS Interpreter Development)`
   - Add `FetchContent` for pybind11
   - Add new source files to build target
   - Link Python libraries

2. **`main.cpp`**:
   - Add `#include "src/python/qgis_python.h"`
   - Add `Py_Initialize()` / `Py_Finalize()`
   - Add `init_python_bindings()` call
   - Add Python Console dock widget
   - Add Python Console menu item

## Python Module Structure

```python
# After embedding, users can do:
import qgis
from qgis.core import (
    QgsApplication,
    QgsProject,
    QgsVectorLayer,
    QgsRasterLayer,
    QgsGeometry,
    QgsFeature,
    QgsFields,
    QgsField,
    QgsPointXY,
    QgsRectangle,
    QgsCoordinateReferenceSystem,
    QgsMapLayer,
    QgsProcessingRegistry,
    # ... all core classes
)
from qgis.gui import (
    QgsMapCanvas,
    QgsLayerTreeView,
    QgsCodeEditorPython,
    # ... all gui classes
)
from qgis.analysis import (
    # ... analysis classes
)

# Access the running application
project = QgsProject.instance()
layers = project.mapLayers()

# Note: iface object (application API) is Phase 2B
```

## Console Widget Design

```
┌─────────────────────────────────────────────┐
│ [Run] [Clear] [Load] [Save]                 │
├─────────────────────────────────────────────┤
│ QgsCodeEditorPython (input area)            │
│                                             │
│ > project = QgsProject.instance()           │
│ > print(project.fileName())                 │
│                                             │
├─────────────────────────────────────────────┤
│ Output (read-only QTextEdit)                │
│                                             │
│ /home/user/project.qgs                      │
│ >>>                                         │
│                                             │
└─────────────────────────────────────────────┘
```

## Binding Strategy

Each QGIS class gets a pybind11 binding declaration. Example pattern:

```cpp
// In bindings_core.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <qgsvectorlayer.h>
#include <qgsproject.h>
// ...

namespace py = pybind11;

PYBIND11_MODULE(qgis_core, m) {
    m.doc() = "QGIS Core Python bindings";

    py::class_<QgsVectorLayer, QgsMapLayer>(m, "QgsVectorLayer")
        .def(py::init<const QString &, const QString &, const QString &>())
        .def("isValid", &QgsVectorLayer::isValid)
        .def("featureCount", &QgsVectorLayer::featureCount)
        .def("fields", &QgsVectorLayer::fields)
        // ... all public methods
        ;

    py::class_<QgsProject>(m, "QgsProject")
        .def_static("instance", &QgsProject::instance, py::return_value_policy::reference)
        .def("addMapLayer", &QgsProject::addMapLayer)
        .def("mapLayers", &QgsProject::mapLayers)
        // ...
        ;
}
```

## Memory Management

- **Singleton objects** (QgsProject::instance(), QgsApplication::instance()): Use `py::return_value_policy::reference` — Python does NOT own these objects
- **Objects created in Python** (e.g., `QgsVectorLayer(path, name, provider)`): Use `py::return_value_policy::take_ownership` — Python owns and will delete
- **Objects returned by value**: Use `py::return_value_policy::automatic` (default) — pybind11 copies
- **Qt parent-child**: pybind11 respects Qt ownership; child objects are not double-freed when parent is deleted
- **QgsMapLayer ownership**: Layers added to QgsProject are owned by the project; Python references become weak

## Stdout/Stderr Capture

Python console redirects sys.stdout and sys.stderr to capture output:

```python
class ConsoleOutput:
    def __init__(self, callback):
        self.callback = callback
        self.buffer = ""
    def write(self, text):
        self.buffer += text
        self.callback(text)
    def flush(self):
        pass
```

C++ side creates this object and sets `sys.stdout = ConsoleOutput(cpp_callback)`.

## Verification

1. Build succeeds with Python and pybind11 linked
2. Launch app → "Python Console" dock visible in bottom panel
3. Type `print("hello")` in console → output appears
4. Type `from qgis.core import QgsProject; print(QgsProject.instance().fileName())` → works
5. Type `from qgis.core import QgsVectorLayer; print(QgsVectorLayer)` → class accessible
6. Processing menu → "Python Console" shows/focuses the dock
7. No crash on exit (Py_Finalize called correctly)

## Phase 2B (Future — Separate Spec)

Not included in this spec:
- Python plugin loader (scan `~/.sicnu_geo_rs/plugins/`)
- Plugin registry and lifecycle management
- Python processing algorithm provider (`QgsProcessingPythonProvider`)
- Plugin UI (install, enable/disable, configure)
- `iface` object exposing application API to Python
