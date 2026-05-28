# Phase 2A: Python Embedding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed CPython runtime, create pybind11 bindings for QGIS API, and provide an interactive Python console dock widget.

**Architecture:** CPython is initialized in main() after QGIS init. pybind11 bindings organized by module (qgis.core, qgis.gui, qgis.analysis) expose all QGIS classes. PythonConsoleWidget provides a REPL with QgsCodeEditorPython.

**Tech Stack:** C++17, Qt6, QGIS C++ API, CPython 3.13, pybind11 (FetchContent)

---

### Task 1: pybind11 Dependency and Python Development Setup

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add pybind11 via FetchContent and Python Development package**

In `CMakeLists.txt`, after the existing `find_package` calls (after line 34), add:

```cmake
# Python Development (runtime embedding)
find_package(Python REQUIRED COMPONENTS Interpreter Development)

# pybind11 via FetchContent
include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.13.6
)
FetchContent_MakeAvailable(pybind11)
```

Also change the existing line 34 from:
```cmake
find_package(Python COMPONENTS Interpreter REQUIRED)
```
to:
```cmake
# Python (build-time code generation scripts)
find_package(Python COMPONENTS Interpreter REQUIRED)
```
(Keep it as a comment-only reference since the new find_package above handles both Interpreter and Development.)

- [ ] **Step 2: Add Python source files to build**

In the `SICNU_GEO_RS_CPP_SRCS` list (around line 140), add:

```cmake
  set(SICNU_GEO_RS_CPP_SRCS
    main.cpp
    src/processing/sicnunativealgorithms.cpp
    src/python/qgis_python.cpp
    src/python/bindings.cpp
    src/python/bindings_core.cpp
    src/python/bindings_gui.cpp
    src/python/bindings_analysis.cpp
    src/gui/python_console_widget.cpp
  )
```

- [ ] **Step 3: Add pybind11 and Python to link libraries**

In `target_link_libraries` (around line 147), add:

```cmake
  target_link_libraries(sicnu_geo_rs
    qgis_core
    qgis_gui
    qgis_native
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
    Qt6::Svg
    Qt6::Sql
    pybind11::embed
    Python::Python
  )
```

Note: `pybind11::embed` provides both pybind11 headers and Python linking. `Python::Python` ensures the correct Python library is linked.

- [ ] **Step 4: Build to verify pybind11 downloads and links**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -20`
Expected: Build may fail because source files don't exist yet, but cmake configure should succeed and pybind11 should download.

Run: `cd /home/kevin/projects/exp-rs && cmake -B build 2>&1 | grep -E "pybind11|Python" | head -10`
Expected: pybind11 found, Python Development found

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add pybind11 and Python Development dependencies"
```

---

### Task 2: Python Runtime Manager

**Files:**
- Create: `src/python/qgis_python.h`
- Create: `src/python/qgis_python.cpp`

- [ ] **Step 1: Create qgis_python.h header**

```cpp
// src/python/qgis_python.h
#pragma once

#include <QString>

class QgisPython
{
public:
    static QgisPython &instance();

    bool initialize();
    void finalize();

    bool isInitialized() const { return m_initialized; }

    bool runString(const QString &command, QString &output, QString &error);

private:
    QgisPython() = default;
    ~QgisPython() = default;
    QgisPython(const QgisPython &) = delete;
    QgisPython &operator=(const QgisPython &) = delete;

    bool m_initialized = false;
};

void init_python_bindings();
```

- [ ] **Step 2: Create qgis_python.cpp implementation**

```cpp
// src/python/qgis_python.cpp
#include "qgis_python.h"

#include <Python.h>
#include <pybind11/embed.h>
#include <QString>
#include <QDebug>

namespace py = pybind11;

QgisPython &QgisPython::instance()
{
    static QgisPython sInstance;
    return sInstance;
}

bool QgisPython::initialize()
{
    if (m_initialized)
        return true;

    if (!Py_IsInitialized())
    {
        py::initialize_interpreter();
        qDebug() << "Python interpreter initialized";
    }

    // Redirect stdout/stderr to capture output
    try
    {
        py::exec(R"(
import sys
from io import StringIO

class _ConsoleOutput:
    def __init__(self):
        self.buffer = StringIO()
    def write(self, text):
        self.buffer.write(text)
    def flush(self):
        pass
    def getvalue(self):
        val = self.buffer.getvalue()
        self.buffer = StringIO()
        return val

sys.stdout = _ConsoleOutput()
sys.stderr = _ConsoleOutput()
)");
        m_initialized = true;
        qDebug() << "Python stdout/stderr redirected";
    }
    catch (py::error_already_set &e)
    {
        qWarning() << "Failed to initialize Python console output:" << e.what();
        m_initialized = true; // Still mark as initialized, console output just won't capture
    }

    return true;
}

void QgisPython::finalize()
{
    if (!m_initialized)
        return;

    if (Py_IsInitialized())
    {
        py::finalize_interpreter();
        qDebug() << "Python interpreter finalized";
    }

    m_initialized = false;
}

bool QgisPython::runString(const QString &command, QString &output, QString &error)
{
    if (!m_initialized)
    {
        error = QStringLiteral("Python not initialized");
        return false;
    }

    try
    {
        py::object scope = py::globals();
        py::exec(command.toUtf8().constData(), scope);

        // Capture stdout
        py::object stdout_obj = py::module_::import("sys").attr("stdout");
        output = QString::fromStdString(stdout_obj.attr("getvalue")().cast<std::string>());

        // Capture stderr
        py::object stderr_obj = py::module_::import("sys").attr("stderr");
        error = QString::fromStdString(stderr_obj.attr("getvalue")().cast<std::string>());

        return true;
    }
    catch (py::error_already_set &e)
    {
        error = QString::fromUtf8(e.what());
        return false;
    }
}

void init_python_bindings()
{
    // Sub-modules are auto-registered via pybind11 PYBIND11_MODULE macros
    // This function triggers their initialization by importing them
    try
    {
        py::module_::import("qgis");
        qDebug() << "Python qgis module loaded";
    }
    catch (py::error_already_set &e)
    {
        qWarning() << "Failed to load Python qgis module:" << e.what();
    }
}
```

- [ ] **Step 3: Create stub source files for bindings**

Create `src/python/bindings.cpp`:
```cpp
// src/python/bindings.cpp
#include <pybind11/pybind11.h>

namespace py = pybind11;

// Forward declarations of sub-module init functions
void init_qgis_core(py::module_ &);
void init_qgis_gui(py::module_ &);
void init_qgis_analysis(py::module_ &);

PYBIND11_MODULE(qgis, m)
{
    m.doc() = "SICNU GEO RS Python bindings";

    auto core = m.def_submodule("core", "QGIS Core classes");
    init_qgis_core(core);

    auto gui = m.def_submodule("gui", "QGIS GUI classes");
    init_qgis_gui(gui);

    auto analysis = m.def_submodule("analysis", "QGIS Analysis classes");
    init_qgis_analysis(analysis);
}
```

Create `src/python/bindings_core.cpp`:
```cpp
// src/python/bindings_core.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

void init_qgis_core(py::module_ &m)
{
    m.doc() = "QGIS Core Python bindings";
    // Classes will be added in Tasks 4-6
}
```

Create `src/python/bindings_gui.cpp`:
```cpp
// src/python/bindings_gui.cpp
#include <pybind11/pybind11.h>

namespace py = pybind11;

void init_qgis_gui(py::module_ &m)
{
    m.doc() = "QGIS GUI Python bindings";
    // Classes will be added in Task 7
}
```

Create `src/python/bindings_analysis.cpp`:
```cpp
// src/python/bindings_analysis.cpp
#include <pybind11/pybind11.h>

namespace py = pybind11;

void init_qgis_analysis(py::module_ &m)
{
    m.doc() = "QGIS Analysis Python bindings";
    // Classes will be added in Task 8
}
```

- [ ] **Step 4: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/python/
git commit -m "feat(python): add Python runtime manager and binding stubs"
```

---

### Task 3: Python Console Widget

**Files:**
- Create: `src/gui/python_console_widget.h`
- Create: `src/gui/python_console_widget.cpp`

- [ ] **Step 1: Create python_console_widget.h**

```cpp
// src/gui/python_console_widget.h
#pragma once

#include <QWidget>

class QTextEdit;
class QPushButton;

namespace QgsCodeEditorPython { class QgsCodeEditorPython; }

class PythonConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PythonConsoleWidget(QWidget *parent = nullptr);

public slots:
    void executeCommand();
    void clearOutput();

private:
    QgsCodeEditorPython *m_codeEditor = nullptr;
    QTextEdit *m_outputArea = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    void appendOutput(const QString &text, bool isError = false);
};
```

Wait — `QgsCodeEditorPython` is a class, not a namespace. Let me fix the header.

- [ ] **Step 2: Create python_console_widget.h (corrected)**

```cpp
// src/gui/python_console_widget.h
#pragma once

#include <QWidget>

class QTextEdit;
class QPushButton;
class QgsCodeEditorPython;

class PythonConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PythonConsoleWidget(QWidget *parent = nullptr);

public slots:
    void executeCommand();
    void clearOutput();

private:
    QgsCodeEditorPython *m_codeEditor = nullptr;
    QTextEdit *m_outputArea = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    void appendOutput(const QString &text, bool isError = false);
};
```

- [ ] **Step 3: Create python_console_widget.cpp**

```cpp
// src/gui/python_console_widget.cpp
#include "python_console_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QScrollBar>

#include <codeeditors/qgscodeeditorpython.h>

#include "src/python/qgis_python.h"

PythonConsoleWidget::PythonConsoleWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Toolbar
    auto *toolbarLayout = new QHBoxLayout();
    m_runButton = new QPushButton(tr("Run"), this);
    m_clearButton = new QPushButton(tr("Clear"), this);
    toolbarLayout->addWidget(m_runButton);
    toolbarLayout->addWidget(m_clearButton);
    toolbarLayout->addStretch();
    mainLayout->addLayout(toolbarLayout);

    // Code editor (input)
    m_codeEditor = new QgsCodeEditorPython(this);
    m_codeEditor->setMinimumHeight(100);
    mainLayout->addWidget(m_codeEditor, 2);

    // Output area
    m_outputArea = new QTextEdit(this);
    m_outputArea->setReadOnly(true);
    m_outputArea->setMinimumHeight(80);
    mainLayout->addWidget(m_outputArea, 1);

    // Connections
    connect(m_runButton, &QPushButton::clicked, this, &PythonConsoleWidget::executeCommand);
    connect(m_clearButton, &QPushButton::clicked, this, &PythonConsoleWidget::clearOutput);

    // Ctrl+Enter to run
    // QgsCodeEditorPython handles its own key events; we use the button
}

void PythonConsoleWidget::executeCommand()
{
    QString code = m_codeEditor->text();
    if (code.trimmed().isEmpty())
        return;

    appendOutput(QStringLiteral(">>> %1").arg(code));

    // Initialize Python if needed
    if (!QgisPython::instance().isInitialized())
        QgisPython::instance().initialize();

    QString output, error;
    bool success = QgisPython::instance().runString(code, output, error);

    if (!output.isEmpty())
        appendOutput(output);
    if (!error.isEmpty())
        appendOutput(error, true);

    if (!success && error.isEmpty())
        appendOutput(tr("Execution failed"), true);
}

void PythonConsoleWidget::clearOutput()
{
    m_outputArea->clear();
}

void PythonConsoleWidget::appendOutput(const QString &text, bool isError)
{
    if (isError)
        m_outputArea->setTextColor(Qt::red);
    else
        m_outputArea->setTextColor(Qt::white);

    m_outputArea->append(text);

    // Auto-scroll to bottom
    QScrollBar *sb = m_outputArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}
```

- [ ] **Step 4: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/gui/python_console_widget.h src/gui/python_console_widget.cpp
git commit -m "feat(python): add PythonConsoleWidget"
```

---

### Task 4: Core Bindings — QgsApplication, QgsProject, QgsMapLayer

**Files:**
- Modify: `src/python/bindings_core.cpp`

- [ ] **Step 1: Add core class bindings**

Replace the contents of `src/python/bindings_core.cpp`:

```cpp
// src/python/bindings_core.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmaplayer.h>
#include <qgsvectorlayer.h>
#include <qgsrasterlayer.h>
#include <qgsfeature.h>
#include <qgsfields.h>
#include <qgsfield.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgsfeaturerequest.h>
#include <qgsfeatureiterator.h>
#include <qgswkbtypes.h>
#include <qgslogger.h>

namespace py = pybind11;

void init_qgis_core(py::module_ &m)
{
    m.doc() = "QGIS Core Python bindings";

    // ── QgsPointXY ──
    py::class_<QgsPointXY>(m, "QgsPointXY")
        .def(py::init<>())
        .def(py::init<double, double>(), py::arg("x"), py::arg("y"))
        .def(py::init<const QgsPointXY &>())
        .def("x", &QgsPointXY::x)
        .def("y", &QgsPointXY::y)
        .def("setX", &QgsPointXY::setX)
        .def("setY", &QgsPointXY::setY)
        .def("isEmpty", &QgsPointXY::isEmpty)
        .def("__repr__", [](const QgsPointXY &p) {
            return "QgsPointXY(" + std::to_string(p.x()) + ", " + std::to_string(p.y()) + ")";
        });

    // ── QgsRectangle ──
    py::class_<QgsRectangle>(m, "QgsRectangle")
        .def(py::init<>())
        .def(py::init<double, double, double, double>(),
             py::arg("xmin"), py::arg("ymin"), py::arg("xmax"), py::arg("ymax"))
        .def(py::init<const QgsPointXY &, const QgsPointXY &>())
        .def(py::init<const QgsRectangle &>())
        .def("xMinimum", &QgsRectangle::xMinimum)
        .def("yMinimum", &QgsRectangle::yMinimum)
        .def("xMaximum", &QgsRectangle::xMaximum)
        .def("yMaximum", &QgsRectangle::yMaximum)
        .def("width", &QgsRectangle::width)
        .def("height", &QgsRectangle::height)
        .def("center", &QgsRectangle::center)
        .def("isEmpty", &QgsRectangle::isEmpty)
        .def("isNull", &QgsRectangle::isNull)
        .def("contains", &QgsRectangle::contains, py::arg("p"))
        .def("__repr__", [](const QgsRectangle &r) {
            return "QgsRectangle(" + std::to_string(r.xMinimum()) + ", " +
                   std::to_string(r.yMinimum()) + ", " + std::to_string(r.xMaximum()) +
                   ", " + std::to_string(r.yMaximum()) + ")";
        });

    // ── QgsCoordinateReferenceSystem ──
    py::class_<QgsCoordinateReferenceSystem>(m, "QgsCoordinateReferenceSystem")
        .def(py::init<>())
        .def(py::init<const QString &>(), py::arg("srs"))
        .def(py::init<const QgsCoordinateReferenceSystem &>())
        .def("isValid", &QgsCoordinateReferenceSystem::isValid)
        .def("authid", &QgsCoordinateReferenceSystem::authid)
        .def("description", &QgsCoordinateReferenceSystem::description)
        .def("toWkt", &QgsCoordinateReferenceSystem::toWkt)
        .def("createFromString", &QgsCoordinateReferenceSystem::createFromString, py::arg("definition"))
        .def_static("fromEpsgId", &QgsCoordinateReferenceSystem::fromEpsgId, py::arg("epsg"));

    // ── QgsField ──
    py::class_<QgsField>(m, "QgsField")
        .def(py::init<>())
        .def(py::init<const QString &, const QString &, int, int, int>(),
             py::arg("name"), py::arg("type") = QStringLiteral("QString"),
             py::arg("len") = 0, py::arg("prec") = 0, py::arg("subType") = 0)
        .def("name", &QgsField::name)
        .def("typeName", &QgsField::typeName)
        .def("type", &QgsField::type)
        .def("length", &QgsField::length)
        .def("precision", &QgsField::precision)
        .def("isNull", &QgsField::isNull);

    // ── QgsFields ──
    py::class_<QgsFields>(m, "QgsFields")
        .def(py::init<>())
        .def(py::init<const QgsFields &>())
        .def("count", &QgsFields::count)
        .def("isEmpty", &QgsFields::isEmpty)
        .def("at", &QgsFields::at, py::arg("i"))
        .def("field", py::overload_cast<int>(&QgsFields::field, py::const_), py::arg("i"))
        .def("field", py::overload_cast<const QString &>(&QgsFields::field, py::const_), py::arg("name"))
        .def("indexOf", &QgsFields::indexOf, py::arg("name"))
        .def("names", &QgsFields::names);

    // ── QgsGeometry ──
    py::class_<QgsGeometry>(m, "QgsGeometry")
        .def(py::init<>())
        .def(py::init<const QgsGeometry &>())
        .def_static("fromPointXY", &QgsGeometry::fromPointXY, py::arg("point"))
        .def_static("fromPolygonXY", [](const QVector<QVector<QgsPointXY>> &polygon) {
            return QgsGeometry::fromPolygonXY(polygon);
        })
        .def("isNull", &QgsGeometry::isNull)
        .def("isEmpty", &QgsGeometry::isEmpty)
        .def("isMultipart", &QgsGeometry::isMultipart)
        .def("type", &QgsGeometry::type)
        .def("wkbType", &QgsGeometry::wkbType)
        .def("asWkt", &QgsGeometry::asWkt)
        .def("asJson", &QgsGeometry::asJson)
        .def("area", &QgsGeometry::area)
        .def("length", &QgsGeometry::length)
        .def("buffer", &QgsGeometry::buffer, py::arg("distance"), py::arg("segments"))
        .def("centroid", &QgsGeometry::centroid)
        .def("convexHull", &QgsGeometry::convexHull)
        .def("boundingBox", &QgsGeometry::boundingBox)
        .def("intersection", &QgsGeometry::intersection, py::arg("other"))
        .def("combine", &QgsGeometry::combine, py::arg("geometry"))
        .def("difference", &QgsGeometry::difference, py::arg("geometry"))
        .def("simplify", &QgsGeometry::simplify, py::arg("tolerance"))
        .def("transform", py::overload_cast<const QgsCoordinateTransform &>(&QgsGeometry::transform))
        .def("vertexCount", &QgsGeometry::vertexCount);

    // ── QgsFeature ──
    py::class_<QgsFeature>(m, "QgsFeature")
        .def(py::init<>())
        .def(py::init<QgsFeatureId>(), py::arg("id"))
        .def(py::init<const QgsFeature &>())
        .def("id", &QgsFeature::id)
        .def("isValid", &QgsFeature::isValid)
        .def("fields", &QgsFeature::fields)
        .def("geometry", &QgsFeature::geometry)
        .def("setGeometry", &QgsFeature::setGeometry, py::arg("geometry"))
        .def("hasGeometry", &QgsFeature::hasGeometry)
        .def("attribute", py::overload_cast<int>(&QgsFeature::attribute, py::const_), py::arg("fieldIdx"))
        .def("attribute", py::overload_cast<const QString &>(&QgsFeature::attribute, py::const_), py::arg("name"))
        .def("setAttribute", py::overload_cast<int, const QVariant &>(&QgsFeature::setAttribute), py::arg("fieldIdx"), py::arg("value"))
        .def("setAttribute", py::overload_cast<const QString &, const QVariant &>(&QgsFeature::setAttribute), py::arg("name"), py::arg("value"))
        .def("setFields", &QgsFeature::setFields, py::arg("fields"), py::arg("initAttributes") = true);

    // ── QgsMapLayer ──
    py::class_<QgsMapLayer>(m, "QgsMapLayer")
        .def("id", &QgsMapLayer::id)
        .def("name", &QgsMapLayer::name)
        .def("setName", &QgsMapLayer::setName)
        .def("isValid", &QgsMapLayer::isValid)
        .def("type", &QgsMapLayer::type)
        .def("crs", &QgsMapLayer::crs)
        .def("setCrs", &QgsMapLayer::setCrs)
        .def("extent", &QgsMapLayer::extent)
        .def("dataProvider", &QgsMapLayer::dataProvider, py::return_value_policy::reference)
        .def("opacity", &QgsMapLayer::opacity)
        .def("setOpacity", &QgsMapLayer::setOpacity);

    // ── QgsVectorLayer ──
    py::class_<QgsVectorLayer, QgsMapLayer>(m, "QgsVectorLayer")
        .def(py::init<const QString &, const QString &, const QString &>(),
             py::arg("path"), py::arg("baseName"), py::arg("providerKey") = QStringLiteral("ogr"))
        .def("isValid", &QgsVectorLayer::isValid)
        .def("featureCount", &QgsVectorLayer::featureCount)
        .def("fields", &QgsVectorLayer::fields)
        .def("getFeatures", [](QgsVectorLayer &layer) {
            return layer.getFeatures();
        })
        .def("selectedFeatures", &QgsVectorLayer::selectedFeatures)
        .def("geometryType", &QgsVectorLayer::geometryType)
        .def("wkbType", &QgsVectorLayer::wkbType);

    // ── QgsRasterLayer ──
    py::class_<QgsRasterLayer, QgsMapLayer>(m, "QgsRasterLayer")
        .def(py::init<const QString &, const QString &, const QString &>(),
             py::arg("path"), py::arg("baseName"), py::arg("providerKey") = QStringLiteral("gdal"))
        .def("isValid", &QgsRasterLayer::isValid)
        .def("width", &QgsRasterLayer::width)
        .def("height", &QgsRasterLayer::height)
        .def("bandCount", &QgsRasterLayer::bandCount)
        .def("rasterUnitsPerPixelX", &QgsRasterLayer::rasterUnitsPerPixelX)
        .def("rasterUnitsPerPixelY", &QgsRasterLayer::rasterUnitsPerPixelY);

    // ── QgsProject ──
    py::class_<QgsProject, QObject>(m, "QgsProject")
        .def_static("instance", &QgsProject::instance, py::return_value_policy::reference)
        .def("fileName", &QgsProject::fileName)
        .def("setFileName", &QgsProject::setFileName)
        .def("title", &QgsProject::title)
        .def("setTitle", &QgsProject::setTitle)
        .def("addMapLayer", &QgsProject::addMapLayer, py::arg("layer"), py::arg("addToLegend") = true)
        .def("removeMapLayer", py::overload_cast<QgsMapLayer *>(&QgsProject::removeMapLayer))
        .def("mapLayers", &QgsProject::mapLayers)
        .def("mapLayersByName", &QgsProject::mapLayersByName)
        .def("layerTreeRoot", &QgsProject::layerTreeRoot, py::return_value_policy::reference)
        .def("crs", &QgsProject::crs)
        .def("setCrs", &QgsProject::setCrs)
        .def("read", py::overload_cast<const QString &>(&QgsProject::read))
        .def("write", py::overload_cast<const QString &>(&QgsProject::write));

    // ── QgsApplication ──
    py::class_<QgsApplication, QApplication>(m, "QgsApplication")
        .def_static("instance", &QgsApplication::instance, py::return_value_policy::reference)
        .def_static("processingRegistry", &QgsApplication::processingRegistry, py::return_value_policy::reference)
        .def_static("setPrefixPath", &QgsApplication::setPrefixPath)
        .def_static("initQgis", &QgsApplication::initQgis)
        .def_static("exitQgis", &QgsApplication::exitQgis);

    // ── QgsCoordinateTransform ──
    py::class_<QgsCoordinateTransform>(m, "QgsCoordinateTransform")
        .def(py::init<const QgsCoordinateReferenceSystem &, const QgsCoordinateReferenceSystem &, const QgsCoordinateTransformContext &>())
        .def("transform", py::overload_cast<const QgsPointXY &>(&QgsCoordinateTransform::transform, py::const_))
        .def("transformBoundingBox", &QgsCoordinateTransform::transformBoundingBox, py::arg("rect"), py::arg("handle180Crossover") = false);

    // ── QgsFeatureRequest ──
    py::class_<QgsFeatureRequest>(m, "QgsFeatureRequest")
        .def(py::init<>())
        .def(py::init<QgsFeatureId>())
        .def("setFilterRect", &QgsFeatureRequest::setFilterRect)
        .def("setFlags", &QgsFeatureRequest::setFlags);

    // ── QgsWkbTypes ──
    py::class_<QgsWkbTypes>(m, "QgsWkbTypes")
        .def_static("geometryType", &QgsWkbTypes::geometryType)
        .def_static("isMultiType", &QgsWkbTypes::isMultiType)
        .def_static("displayString", &QgsWkbTypes::displayString);
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -20`
Expected: Build succeeds (or shows specific API errors to fix)

- [ ] **Step 3: Commit**

```bash
git add src/python/bindings_core.cpp
git commit -m "feat(python): add core bindings (QgsProject, QgsVectorLayer, QgsGeometry, etc.)"
```

---

### Task 5: GUI Bindings

**Files:**
- Modify: `src/python/bindings_gui.cpp`

- [ ] **Step 1: Add GUI class bindings**

Replace the contents of `src/python/bindings_gui.cpp`:

```cpp
// src/python/bindings_gui.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <qgsmapcanvas.h>
#include <qgsmapsettings.h>
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreegroup.h>
#include <layertree/qgslayertreelayer.h>
#include <layertree/qgslayertreemodel.h>
#include <codeeditors/qgscodeeditorpython.h>

namespace py = pybind11;

void init_qgis_gui(py::module_ &m)
{
    m.doc() = "QGIS GUI Python bindings";

    // ── QgsMapCanvas ──
    py::class_<QgsMapCanvas, QWidget>(m, "QgsMapCanvas")
        .def(py::init<>())
        .def("setLayers", &QgsMapCanvas::setLayers)
        .def("layers", &QgsMapCanvas::layers)
        .def("setExtent", &QgsMapCanvas::setExtent)
        .def("extent", &QgsMapCanvas::extent)
        .def("refresh", &QgsMapCanvas::refresh)
        .def("setDestinationCrs", &QgsMapCanvas::setDestinationCrs)
        .def("mapSettings", &QgsMapCanvas::mapSettings, py::return_value_policy::reference)
        .def("zoomIn", &QgsMapCanvas::zoomIn)
        .def("zoomOut", &QgsMapCanvas::zoomOut)
        .def("zoomToFullExtent", &QgsMapCanvas::zoomToFullExtent);

    // ── QgsMapSettings ──
    py::class_<QgsMapSettings>(m, "QgsMapSettings")
        .def(py::init<>())
        .def("setDestinationCrs", &QgsMapSettings::setDestinationCrs)
        .def("destinationCrs", &QgsMapSettings::destinationCrs)
        .def("setExtent", &QgsMapSettings::setExtent)
        .def("extent", &QgsMapSettings::extent)
        .def("setLayers", &QgsMapSettings::setLayers)
        .def("layers", &QgsMapSettings::layers)
        .def("setOutputSize", &QgsMapSettings::setOutputSize)
        .def("outputSize", &QgsMapSettings::outputSize);

    // ── QgsLayerTreeGroup ──
    py::class_<QgsLayerTreeGroup, QgsLayerTreeNode>(m, "QgsLayerTreeGroup")
        .def("name", &QgsLayerTreeGroup::name)
        .def("setName", &QgsLayerTreeGroup::setName)
        .def("addLayer", &QgsLayerTreeGroup::addLayer)
        .def("addGroup", &QgsLayerTreeGroup::addGroup)
        .def("findGroup", &QgsLayerTreeGroup::findGroup, py::return_value_policy::reference)
        .def("findLayers", &QgsLayerTreeGroup::findLayers)
        .def("layerOrder", &QgsLayerTreeGroup::layerOrder)
        .def("removeChildNode", &QgsLayerTreeGroup::removeChildNode);

    // ── QgsLayerTreeLayer ──
    py::class_<QgsLayerTreeLayer, QgsLayerTreeNode>(m, "QgsLayerTreeLayer")
        .def("name", &QgsLayerTreeLayer::name)
        .def("layer", &QgsLayerTreeLayer::layer, py::return_value_policy::reference);

    // ── QgsCodeEditorPython ──
    py::class_<QgsCodeEditorPython, QWidget>(m, "QgsCodeEditorPython")
        .def(py::init<QWidget *, const QString &, bool>(),
             py::arg("parent") = nullptr, py::arg("title") = QString(), py::arg("foldMarginVisible") = true)
        .def("text", &QgsCodeEditorPython::text)
        .def("setText", &QgsCodeEditorPython::setText);
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -20`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/python/bindings_gui.cpp
git commit -m "feat(python): add GUI bindings (QgsMapCanvas, QgsLayerTreeGroup, etc.)"
```

---

### Task 6: Analysis Bindings

**Files:**
- Modify: `src/python/bindings_analysis.cpp`

- [ ] **Step 1: Add analysis class bindings**

Replace the contents of `src/python/bindings_analysis.cpp`:

```cpp
// src/python/bindings_analysis.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

void init_qgis_analysis(py::module_ &m)
{
    m.doc() = "QGIS Analysis Python bindings";
    // Analysis classes will be added as needed.
    // Potential classes: QgsRasterCalculator, QgsZonalStatistics, etc.
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/python/bindings_analysis.cpp
git commit -m "feat(python): add analysis bindings stub"
```

---

### Task 7: Integration in main.cpp

**Files:**
- Modify: `main.cpp`

- [ ] **Step 1: Add Python includes**

In `main.cpp`, after the processing framework includes (around line 85), add:

```cpp
// Python embedding
#include "src/python/qgis_python.h"
#include "src/gui/python_console_widget.h"
```

- [ ] **Step 2: Initialize Python in main()**

In `main()`, after `QgsApplication::initQgis()` (around line 988), add:

```cpp
    // Initialize Python embedding
    QgisPython::instance().initialize();
    init_python_bindings();
```

- [ ] **Step 3: Add Python Console dock widget**

In `setupDockWidgets()`, after the Processing Toolbox dock (around line 316), add:

```cpp
        // Python Console (Bottom)
        QgsDockWidget *pythonDock = new QgsDockWidget("Python Console", this);
        pythonDock->setObjectName("pythonDock");
        auto *pythonConsole = new PythonConsoleWidget(pythonDock);
        pythonDock->setWidget(pythonConsole);
        addDockWidget(Qt::BottomDockWidgetArea, pythonDock);
```

- [ ] **Step 4: Add Python Console menu item**

In `setupMenu()`, in the Processing menu (around line 228), add:

```cpp
        processingMenu->addAction("Python Console", this, [this]() {
            if (auto *dock = findChild<QgsDockWidget *>("pythonDock"))
                dock->setVisible(true);
        });
```

- [ ] **Step 5: Finalize Python on exit**

In `main()`, before `return app->exec()` or after the event loop, add Python finalization. Find the end of main() and add before the return:

```cpp
    // Finalize Python before exit
    QgisPython::instance().finalize();
```

Note: This should be placed after `app->exec()` returns but before the final return.

- [ ] **Step 6: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 7: Run to verify**

Run: `cd /home/kevin/projects/exp-rs && timeout 5 ./build/sicnu_geo_rs 2>&1 || true`
Expected: No crash, Python Console dock visible

- [ ] **Step 8: Commit**

```bash
git add main.cpp
git commit -m "feat(python): integrate Python embedding and console in main app"
```

---

### Task 8: Final Verification

**Files:**
- None (verification only)

- [ ] **Step 1: Clean build**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | grep -E "error:" | head -10`
Expected: No errors

- [ ] **Step 2: Run the application**

Run: `cd /home/kevin/projects/exp-rs && timeout 5 ./build/sicnu_geo_rs 2>&1 || true`
Expected: No crash

- [ ] **Step 3: Verify features**

Launch the app and verify:
1. "Python Console" dock visible in bottom panel
2. Processing menu has "Python Console" item
3. Type `print("hello")` in console, click Run → "hello" appears in output
4. Type `from qgis.core import QgsProject; print(QgsProject.instance().fileName())` → works
5. Type `from qgis.core import QgsVectorLayer; print(QgsVectorLayer)` → class accessible
6. No crash on exit

---

### Future Tasks (Phase 2B — Separate Spec)

Not included in this plan:
- Python plugin loader (scan `~/.sicnu_geo_rs/plugins/`)
- Plugin registry and lifecycle management
- Python processing algorithm provider
- `iface` object exposing application API
- Plugin UI (install, enable/disable)
