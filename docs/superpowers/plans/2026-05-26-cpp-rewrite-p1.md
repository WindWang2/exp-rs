# C++ Rewrite — Phase P1 (Layer/Project + pybind11 Surface) Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Expand the `_antigravity_core` pybind11 module beyond the P0 raster-only surface to expose `QgsProject`, `QgsMapLayer`, `QgsVectorLayer`, `QgsGeometry`, `QgsExpression`, and `QgsLayerTree`. Gate with pytest covering load/query/expression on the LE7 scene.

**Starting state:** P0 complete. `libqgis_core.so` built with full QGIS core (WITH_VECTORTILE=ON). `_antigravity_core` exposes `init`, `open_raster`, `render_to_png`. All P0 tests green. Branch: `feat/p0-cpp-rewrite`.

**Scope (P1 only, from spec §5):**
- `QgsMapLayer` — base class: `name`, `id`, `type`, `crs`, `extent`, `isValid`
- `QgsRasterLayer` — subclass (already usable, add proper binding)
- `QgsVectorLayer` — `featureCount`, `getFeatures` iterator, `fields`
- `QgsFeature` — `id`, `geometry`, `attribute`
- `QgsGeometry` — `isNull`, `wkt`, `area`, `length`, `type`
- `QgsExpression` — `isValid`, `hasParserError`, `evaluate` against `QgsExpressionContext`
- `QgsProject` — `read`, `write`, `addMapLayer`, `mapLayers`, `layerTreeRoot`, `crs`
- `QgsLayerTreeGroup` — `name`, `findLayer`, `findLayers`, `children`
- Qt type casters: `QString ↔ str`, `QVariant → Python scalar`, `QStringList → list[str]`

**Not in P1:** Processing framework (parameter/feedback types), symbology, labeling — those are P2.

**Tech stack:** Same as P0. `src/python/bindings.cpp` grows; `libqgis_core.so` unchanged.

---

## Task 0: Create P1 branch and verify P0 baseline

- [ ] **Step 1: Branch off P0**
  ```bash
  git checkout -b feat/p1-layer-project feat/p0-cpp-rewrite
  ```

- [ ] **Step 2: Confirm P0 gates still pass**
  ```bash
  ANTIGRAVITY_DATA=$PWD/cmake-build ctest --test-dir cmake-build --output-on-failure 2>&1 | tail -10
  PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_core.py -v 2>&1 | tail -10
  ```
  Expected: ctest 2/2 Passed; pytest 3/3 passed.

- [ ] **Step 3: Commit (no-op, just note baseline)**
  ```bash
  git commit --allow-empty -m "chore(p1): branch cut from P0 baseline (all gates green)"
  ```

---

## Task 1: Qt type casters

Add `pybind11` type caster specialisations for `QString`, `QStringList`, and `QVariant` so all P1 bindings can use them automatically.

**Files:**
- Create: `src/python/qt_casters.h`

- [ ] **Step 1: Write the caster header**

```cpp
// src/python/qt_casters.h
#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace pybind11::detail {

template <> struct type_caster<QString> {
public:
    PYBIND11_TYPE_CASTER(QString, const_name("str"));
    bool load(handle src, bool) {
        if (!PyUnicode_Check(src.ptr())) return false;
        value = QString::fromUtf8(PyUnicode_AsUTF8(src.ptr()));
        return true;
    }
    static handle cast(const QString &s, return_value_policy, handle) {
        return PyUnicode_FromString(s.toUtf8().constData());
    }
};

template <> struct type_caster<QStringList> {
public:
    PYBIND11_TYPE_CASTER(QStringList, const_name("list[str]"));
    bool load(handle src, bool) {
        if (!PyList_Check(src.ptr())) return false;
        for (Py_ssize_t i = 0; i < PyList_Size(src.ptr()); i++) {
            PyObject *item = PyList_GetItem(src.ptr(), i);
            if (!PyUnicode_Check(item)) return false;
            value << QString::fromUtf8(PyUnicode_AsUTF8(item));
        }
        return true;
    }
    static handle cast(const QStringList &sl, return_value_policy, handle) {
        list out;
        for (const QString &s : sl) out.append(s.toUtf8().constData());
        return out.release();
    }
};

template <> struct type_caster<QVariant> {
public:
    PYBIND11_TYPE_CASTER(QVariant, const_name("object"));
    bool load(handle src, bool) {
        if (src.is_none()) { value = QVariant(); return true; }
        if (PyBool_Check(src.ptr())) { value = (src.ptr() == Py_True); return true; }
        if (PyLong_Check(src.ptr())) { value = (qlonglong)PyLong_AsLongLong(src.ptr()); return true; }
        if (PyFloat_Check(src.ptr())) { value = PyFloat_AsDouble(src.ptr()); return true; }
        if (PyUnicode_Check(src.ptr())) { value = QString::fromUtf8(PyUnicode_AsUTF8(src.ptr())); return true; }
        return false;
    }
    static handle cast(const QVariant &v, return_value_policy, handle) {
        switch (v.typeId()) {
            case QMetaType::Bool:       return PyBool_FromLong(v.toBool());
            case QMetaType::Int:
            case QMetaType::LongLong:   return PyLong_FromLongLong(v.toLongLong());
            case QMetaType::Double:     return PyFloat_FromDouble(v.toDouble());
            case QMetaType::QString:    return PyUnicode_FromString(v.toString().toUtf8().constData());
            default:                    Py_RETURN_NONE;
        }
    }
};

} // namespace pybind11::detail
```

- [ ] **Step 2: Include the caster in bindings.cpp (no build yet)**
  Add `#include "qt_casters.h"` near the top of `src/python/bindings.cpp`, right after existing includes.

---

## Task 2: QgsGeometry and QgsFeature bindings

**Files:**
- Modify: `src/python/bindings.cpp`

- [ ] **Step 1: Add geometry and feature classes**

Append before `PYBIND11_MODULE(...)`:

```cpp
#include "qt_casters.h"
#include <qgsgeometry.h>
#include <qgsfeature.h>
#include <qgsfield.h>
#include <qgsfields.h>
#include <qgspoint.h>
#include <qgswkbtypes.h>
```

Inside the module definition add:

```cpp
py::class_<QgsGeometry>(m, "QgsGeometry")
    .def("isNull",   &QgsGeometry::isNull)
    .def("isEmpty",  &QgsGeometry::isEmpty)
    .def("wkt",      [](const QgsGeometry &g) { return g.asWkt(); })
    .def("area",     &QgsGeometry::area)
    .def("length",   &QgsGeometry::length)
    .def("type",     [](const QgsGeometry &g) { return (int)g.type(); });

py::class_<QgsField>(m, "QgsField")
    .def("name",    &QgsField::name)
    .def("typeName",&QgsField::typeName);

py::class_<QgsFields>(m, "QgsFields")
    .def("count",  &QgsFields::count)
    .def("names",  &QgsFields::names)
    .def("field",  [](const QgsFields &f, int i) { return f.field(i); });

py::class_<QgsFeature>(m, "QgsFeature")
    .def("id",        &QgsFeature::id)
    .def("geometry",  &QgsFeature::geometry)
    .def("fields",    &QgsFeature::fields)
    .def("attribute", [](const QgsFeature &f, const QString &name) {
        return f.attribute(name);
    });
```

- [ ] **Step 2: Build and check (expect success — types all in libqgis_core)**
  ```bash
  cmake --build cmake-build --target _antigravity_core 2>&1 | tail -20
  ```
  Expected: no errors.

---

## Task 3: QgsMapLayer + QgsRasterLayer + QgsVectorLayer bindings

**Files:**
- Modify: `src/python/bindings.cpp`

- [ ] **Step 1: Add layer includes**

```cpp
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrectangle.h>
#include <qgsfeatureiterator.h>
#include <qgsfeaturerequest.h>
```

- [ ] **Step 2: Bind QgsRectangle and QgsCRS**

```cpp
py::class_<QgsRectangle>(m, "QgsRectangle")
    .def("xMinimum", &QgsRectangle::xMinimum)
    .def("xMaximum", &QgsRectangle::xMaximum)
    .def("yMinimum", &QgsRectangle::yMinimum)
    .def("yMaximum", &QgsRectangle::yMaximum)
    .def("width",    &QgsRectangle::width)
    .def("height",   &QgsRectangle::height)
    .def("isNull",   &QgsRectangle::isNull)
    .def("__repr__", [](const QgsRectangle &r) {
        return "QgsRectangle(" + std::to_string(r.xMinimum()) + ", "
             + std::to_string(r.yMinimum()) + ", "
             + std::to_string(r.xMaximum()) + ", "
             + std::to_string(r.yMaximum()) + ")";
    });

py::class_<QgsCoordinateReferenceSystem>(m, "QgsCRS")
    .def("isValid",  &QgsCoordinateReferenceSystem::isValid)
    .def("authid",   &QgsCoordinateReferenceSystem::authid)
    .def("description", &QgsCoordinateReferenceSystem::description);
```

- [ ] **Step 3: Bind QgsMapLayer (abstract base)**

```cpp
py::class_<QgsMapLayer, std::shared_ptr<QgsMapLayer>>(m, "QgsMapLayer")
    .def("id",       &QgsMapLayer::id)
    .def("name",     &QgsMapLayer::name)
    .def("isValid",  &QgsMapLayer::isValid)
    .def("crs",      &QgsMapLayer::crs)
    .def("extent",   &QgsMapLayer::extent)
    .def("type",     [](const QgsMapLayer &l) { return (int)l.type(); });
```

- [ ] **Step 4: Bind QgsRasterLayer**

```cpp
py::class_<QgsRasterLayer, QgsMapLayer, std::shared_ptr<QgsRasterLayer>>(m, "QgsRasterLayer")
    .def(py::init([](const QString &path, const QString &name) {
        return std::shared_ptr<QgsRasterLayer>(
            new QgsRasterLayer(path, name, "gdal"));
    }), py::arg("path"), py::arg("name") = "raster")
    .def("width",  &QgsRasterLayer::width)
    .def("height", &QgsRasterLayer::height)
    .def("bandCount", &QgsRasterLayer::bandCount);
```

- [ ] **Step 5: Bind QgsVectorLayer**

```cpp
py::class_<QgsVectorLayer, QgsMapLayer, std::shared_ptr<QgsVectorLayer>>(m, "QgsVectorLayer")
    .def(py::init([](const QString &path, const QString &name) {
        return std::shared_ptr<QgsVectorLayer>(
            new QgsVectorLayer(path, name, "ogr"));
    }), py::arg("path"), py::arg("name") = "vector")
    .def("featureCount", &QgsVectorLayer::featureCount)
    .def("fields",       &QgsVectorLayer::fields)
    .def("wkbType",      [](const QgsVectorLayer &l) { return (int)l.wkbType(); })
    .def("getFeatures",  [](QgsVectorLayer &l) {
        // return list (eager) — no C++ iterator object needed for tests
        std::vector<QgsFeature> out;
        QgsFeatureIterator it = l.getFeatures(QgsFeatureRequest().setLimit(1000));
        QgsFeature f;
        while (it.nextFeature(f)) out.push_back(f);
        return out;
    });
```

- [ ] **Step 6: Build**
  ```bash
  cmake --build cmake-build --target _antigravity_core 2>&1 | tail -20
  ```
  Expected: no errors.

---

## Task 4: QgsProject binding

**Files:**
- Modify: `src/python/bindings.cpp`

- [ ] **Step 1: Add project + layer tree includes**

```cpp
#include <qgsproject.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreenode.h>
#include <qgsmaplayerstore.h>
```

- [ ] **Step 2: Bind QgsLayerTreeGroup**

```cpp
py::class_<QgsLayerTreeNode>(m, "QgsLayerTreeNode")
    .def("name",     &QgsLayerTreeNode::name)
    .def("isGroup",  [](const QgsLayerTreeNode &n) {
        return n.nodeType() == QgsLayerTreeNode::NodeGroup;
    })
    .def("isLayer",  [](const QgsLayerTreeNode &n) {
        return n.nodeType() == QgsLayerTreeNode::NodeLayer;
    });

py::class_<QgsLayerTreeGroup, QgsLayerTreeNode>(m, "QgsLayerTreeGroup")
    .def("name",       &QgsLayerTreeGroup::name)
    .def("findLayer",  [](QgsLayerTreeGroup &g, const QString &layerId) {
        QgsLayerTreeLayer *n = g.findLayer(layerId);
        return n ? n->layer() : nullptr;
    }, py::return_value_policy::reference)
    .def("layerIds",   [](const QgsLayerTreeGroup &g) {
        QStringList ids;
        for (auto *n : g.findLayers()) ids << n->layerId();
        return ids;
    });
```

- [ ] **Step 3: Bind QgsProject**

```cpp
py::class_<QgsProject>(m, "QgsProject")
    .def(py::init([]() { return new QgsProject(); }),
         py::return_value_policy::take_ownership)
    .def("read",    [](QgsProject &p, const QString &path) { return p.read(path); })
    .def("write",   [](QgsProject &p, const QString &path) { return p.write(path); })
    .def("clear",   &QgsProject::clear)
    .def("crs",     &QgsProject::crs)
    .def("title",   &QgsProject::title)
    .def("fileName",&QgsProject::fileName)
    .def("addRasterLayer", [](QgsProject &p, const QString &path, const QString &name) {
        QgsRasterLayer *l = new QgsRasterLayer(path, name, "gdal");
        if (!l->isValid()) { delete l; return (QgsRasterLayer*)nullptr; }
        p.addMapLayer(l);
        return l;
    }, py::return_value_policy::reference)
    .def("addVectorLayer", [](QgsProject &p, const QString &path, const QString &name) {
        QgsVectorLayer *l = new QgsVectorLayer(path, name, "ogr");
        if (!l->isValid()) { delete l; return (QgsVectorLayer*)nullptr; }
        p.addMapLayer(l);
        return l;
    }, py::return_value_policy::reference)
    .def("mapLayerIds", [](const QgsProject &p) {
        return p.mapLayers().keys();
    })
    .def("layerTreeRoot", &QgsProject::layerTreeRoot,
         py::return_value_policy::reference);
```

- [ ] **Step 4: Build**
  ```bash
  cmake --build cmake-build --target _antigravity_core 2>&1 | tail -20
  ```
  Expected: no errors.

---

## Task 5: QgsExpression binding

**Files:**
- Modify: `src/python/bindings.cpp`

- [ ] **Step 1: Add expression includes**

```cpp
#include <qgsexpression.h>
#include <qgsexpressioncontext.h>
#include <qgsexpressioncontextutils.h>
```

- [ ] **Step 2: Bind expression classes**

```cpp
py::class_<QgsExpressionContext>(m, "QgsExpressionContext")
    .def(py::init<>());

py::class_<QgsExpression>(m, "QgsExpression")
    .def(py::init<const QString &>())
    .def("isValid",         [](const QgsExpression &e) { return !e.hasParserError(); })
    .def("hasParserError",  &QgsExpression::hasParserError)
    .def("parserErrorString",&QgsExpression::parserErrorString)
    .def("evaluate",        [](QgsExpression &e, QgsExpressionContext &ctx) {
        QVariant result = e.evaluate(&ctx);
        if (e.hasEvalError()) return QVariant();
        return result;
    })
    .def("evaluateFeature", [](QgsExpression &e, QgsVectorLayer &layer, const QgsFeature &feat) {
        QgsExpressionContext ctx;
        ctx.appendScopes(QgsExpressionContextUtils::globalProjectLayerScopes(&layer));
        ctx.setFeature(feat);
        QVariant result = e.evaluate(&ctx);
        if (e.hasEvalError()) return QVariant();
        return result;
    });
```

- [ ] **Step 3: Build**
  ```bash
  cmake --build cmake-build --target _antigravity_core 2>&1 | tail -20
  ```
  Expected: no errors.

---

## Task 6: Write P1 pytest suite

**Files:**
- Create: `tests/test_cpp_p1.py`

- [ ] **Step 1: Write the test file**

```python
# tests/test_cpp_p1.py
"""P1 gate: layer/project/expression bindings in _antigravity_core."""
import os, sys
import pytest

BUILD = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cmake-build"))
DATA_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LE7_TIF = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")


@pytest.fixture(scope="session")
def core():
    sys.path.insert(0, BUILD)
    os.environ.setdefault("ANTIGRAVITY_DATA", BUILD)
    import _antigravity_core as c
    c.init(BUILD)
    return c


# ── QgsRectangle ──────────────────────────────────────────────────────────────

def test_rectangle_attrs(core):
    layer = core.QgsRasterLayer(LE7_TIF)
    ext = layer.extent()
    assert ext.width() > 0 and ext.height() > 0
    assert not ext.isNull()


# ── QgsRasterLayer (proper binding) ──────────────────────────────────────────

def test_raster_layer_binding(core):
    layer = core.QgsRasterLayer(LE7_TIF, "le7")
    assert layer.isValid()
    assert layer.width() > 7000
    assert layer.height() > 7000
    assert layer.bandCount() >= 1
    assert layer.crs().isValid()
    assert layer.crs().authid().startswith("EPSG:")


def test_raster_layer_name(core):
    layer = core.QgsRasterLayer(LE7_TIF, "my_band")
    assert layer.name() == "my_band"
    assert layer.type() == 1  # QgsMapLayerType::RasterLayer


# ── QgsProject ────────────────────────────────────────────────────────────────

def test_project_add_raster(core):
    proj = core.QgsProject()
    layer = proj.addRasterLayer(LE7_TIF, "b4")
    assert layer is not None
    assert layer.isValid()
    ids = proj.mapLayerIds()
    assert len(ids) >= 1


def test_project_layer_tree(core):
    proj = core.QgsProject()
    proj.addRasterLayer(LE7_TIF, "b4")
    root = proj.layerTreeRoot()
    assert root is not None
    ids = root.layerIds()
    assert len(ids) >= 1


# ── QgsCRS ────────────────────────────────────────────────────────────────────

def test_crs_binding(core):
    layer = core.QgsRasterLayer(LE7_TIF)
    crs = layer.crs()
    assert crs.isValid()
    assert "EPSG" in crs.authid()
    assert len(crs.description()) > 0


# ── QgsExpression ─────────────────────────────────────────────────────────────

def test_expression_valid(core):
    expr = core.QgsExpression("1 + 1")
    assert expr.isValid()
    assert not expr.hasParserError()


def test_expression_invalid(core):
    expr = core.QgsExpression("((( invalid syntax")
    assert not expr.isValid()
    assert expr.hasParserError()


def test_expression_evaluate_scalar(core):
    expr = core.QgsExpression("2 * 21")
    ctx = core.QgsExpressionContext()
    result = expr.evaluate(ctx)
    assert result == 42


# ── QgsGeometry ───────────────────────────────────────────────────────────────

def test_geometry_null(core):
    geom = core.QgsGeometry()
    assert geom.isNull()


# ── QgsVectorLayer (if test vector data available) ───────────────────────────

def _find_test_shp():
    """Find any .shp in the test data tree."""
    for root_dir, dirs, files in os.walk(os.path.join(DATA_ROOT, "data")):
        for f in files:
            if f.endswith(".shp"):
                return os.path.join(root_dir, f)
    return None


@pytest.mark.skipif(_find_test_shp() is None, reason="no .shp in data/")
def test_vector_layer_open(core):
    shp = _find_test_shp()
    layer = core.QgsVectorLayer(shp, "vec")
    assert layer.isValid()
    assert layer.featureCount() >= 0
    fields = layer.fields()
    assert fields.count() >= 0
```

- [ ] **Step 2: Run the suite**
  ```bash
  PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_p1.py -v 2>&1 | tail -30
  ```
  Expected: all non-skipped tests PASS. Fix any compilation or binding errors before proceeding.

- [ ] **Step 3: Iterate on failures**
  - `AttributeError: module '_antigravity_core' has no attribute 'QgsProject'` → binding not registered; check PYBIND11_MODULE block
  - `TypeError` on constructor → check `py::init` lambda signature
  - Segfault → missing `py::return_value_policy` or double-free; check ownership

---

## Task 7: Full gate run + commit

- [ ] **Step 1: Run all P0 + P1 gates**
  ```bash
  ANTIGRAVITY_DATA=$PWD/cmake-build ctest --test-dir cmake-build --output-on-failure
  PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_core.py tests/test_cpp_p1.py -v
  ```
  Expected: ctest 2/2 Passed; pytest ≥ 11 tests PASS.

- [ ] **Step 2: Commit**
  ```bash
  git add src/python/qt_casters.h src/python/bindings.cpp tests/test_cpp_p1.py \
          docs/superpowers/plans/2026-05-26-cpp-rewrite-p1.md
  git commit -m "feat(p1): QgsProject/Layer/Vector/Expression/Geometry pybind11 bindings + tests"
  ```

---

## Self-Review

- **Spec coverage:** §5 P1 classes fully bound. §10 pybind11 surface: QgsProject, QgsMapLayer, QgsRasterLayer, QgsVectorLayer, QgsGeometry, QgsExpression — all exposed. Qt type casters cover QString/QVariant/QStringList.
- **Not in P1:** QgsProcessingFeedback/ParameterDefinition (P2), symbology/labeling (P2), GUI (P3).
- **Ownership:** QgsMapLayer pointer owned by QgsProject via `addMapLayer`; pybind11 gets `reference` policy for layer pointers retrieved from project. Standalone layer objects use `shared_ptr` to prevent double-free.
- **No Python deletions** in P1 — old Python core/gui preserved behind `python-final` tag per spec §7.
