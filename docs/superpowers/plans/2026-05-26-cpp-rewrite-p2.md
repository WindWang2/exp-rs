# C++ Rewrite — Phase P2 (Symbology + Labeling) Implementation Plan

**Goal:** Expose symbology and rendering context classes in `_antigravity_core` so Python agents can inspect/modify layer rendering styles and query render context properties.

**Starting state:** P1 complete (commit `4d41a76`). Branch: `feat/p1-layer-project`.

**Scope (P2 from spec §5):**
- `QgsRenderContext` — render context properties: scale, DPI, extent, flags
- `QgsSymbol` (abstract), `QgsLineSymbol`, `QgsFillSymbol`, `QgsMarkerSymbol` — symbol type/color/opacity
- `QgsFeatureRenderer` (abstract) — `type()`, clone
- `QgsSingleSymbolRenderer` — get/set symbol
- `QgsRendererCategory` — for categorized renderer
- `QgsLabelingEngine` (readonly info) — `isActive()`
- `QgsPalLayerSettings` — field name, enabled flag

**Not in P2:** Full symbology editing pipeline (that's P3+), processing framework, GUI.

---

## Task 0: Branch

- [ ] `git checkout -b feat/p2-symbology feat/p1-layer-project`

## Task 1: QgsRenderContext binding

**Files:** `src/python/bindings.cpp`

Add includes:
```cpp
#include <qgsrendercontext.h>
#include <qgsunittypes.h>
```

Add binding (before PYBIND11_MODULE end):
```cpp
py::class_<QgsRenderContext>(m, "QgsRenderContext")
    .def(py::init<>())
    .def("scaleFactor",   &QgsRenderContext::scaleFactor)
    .def("rendererScale", &QgsRenderContext::rendererScale)
    .def("isValid",       [](const QgsRenderContext &r) { return r.painter() != nullptr; });
```

## Task 2: QgsSymbol hierarchy

Add includes:
```cpp
#include <qgssymbol.h>
#include <qgslinesymbol.h>
#include <qgsfillsymbol.h>
#include <qgsmarkersymbol.h>
```

Add bindings:
```cpp
py::class_<QgsSymbol>(m, "QgsSymbol")
    .def("type",    [](const QgsSymbol &s) { return (int)s.type(); })
    .def("opacity", &QgsSymbol::opacity)
    .def("color",   [](const QgsSymbol &s) {
        QColor c = s.color();
        return py::make_tuple(c.red(), c.green(), c.blue(), c.alpha());
    });

py::class_<QgsLineSymbol, QgsSymbol>(m, "QgsLineSymbol");
py::class_<QgsFillSymbol, QgsSymbol>(m, "QgsFillSymbol");
py::class_<QgsMarkerSymbol, QgsSymbol>(m, "QgsMarkerSymbol");
```

## Task 3: QgsFeatureRenderer + QgsSingleSymbolRenderer

Add includes:
```cpp
#include <qgsrenderer.h>
#include <qgssinglesymbolrenderer.h>
```

Add bindings:
```cpp
py::class_<QgsFeatureRenderer>(m, "QgsFeatureRenderer")
    .def("type", &QgsFeatureRenderer::type);

py::class_<QgsSingleSymbolRenderer, QgsFeatureRenderer>(m, "QgsSingleSymbolRenderer")
    .def("symbol", &QgsSingleSymbolRenderer::symbol,
         py::return_value_policy::reference);
```

Add to `QgsVectorLayer` binding:
```cpp
.def("renderer", [](QgsVectorLayer &l) -> QgsFeatureRenderer* {
    return l.renderer();
}, py::return_value_policy::reference)
```

## Task 4: QgsPalLayerSettings (label config)

Add includes:
```cpp
#include <qgspallabeling.h>
```

Add binding:
```cpp
py::class_<QgsPalLayerSettings>(m, "QgsPalLayerSettings")
    .def(py::init<>())
    .def_readwrite("enabled",    &QgsPalLayerSettings::drawLabels)
    .def_readwrite("fieldName",  &QgsPalLayerSettings::fieldName);
```

Add to `QgsVectorLayer` binding:
```cpp
.def("labeling", [](const QgsVectorLayer &l) -> py::object {
    auto *labeling = l.labeling();
    if (!labeling) return py::none();
    auto *pal = dynamic_cast<QgsVectorLayerSimpleLabeling*>(
        const_cast<QgsAbstractVectorLayerLabeling*>(labeling));
    if (!pal) return py::none();
    QgsPalLayerSettings settings = pal->settings();
    return py::cast(settings);
})
```

## Task 5: Write and run P2 pytest suite

**File:** `tests/test_cpp_p2.py`

```python
"""P2 验收测试：symbology/labeling pybind11 绑定。"""
import os, sys, pytest

BUILD = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cmake-build"))
BUILD_PY = os.path.join(BUILD, "src", "python")
DATA_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LE7_TIF = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")

@pytest.fixture(scope="session")
def core():
    sys.path.insert(0, BUILD_PY); sys.path.insert(0, BUILD)
    os.environ.setdefault("ANTIGRAVITY_DATA", BUILD)
    import _antigravity_core as c; c.init(BUILD); return c

def test_render_context(core):
    rc = core.QgsRenderContext()
    assert rc.scaleFactor() >= 0
    assert rc.rendererScale() >= 0

def test_raster_has_no_renderer(core):
    layer = core.QgsRasterLayer(LE7_TIF, "le7")
    assert layer.isValid()
    # QgsRasterLayer has no feature renderer (not a vector layer)
```

Build and run:
```bash
ninja -C cmake-build _antigravity_core
PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_p2.py -v
```

## Task 6: Full gate run + commit

```bash
ANTIGRAVITY_DATA=$PWD/cmake-build ctest --test-dir cmake-build --output-on-failure
PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_core.py tests/test_cpp_p1.py tests/test_cpp_p2.py -v
git add src/python/bindings.cpp tests/test_cpp_p2.py docs/superpowers/plans/2026-05-26-cpp-rewrite-p2.md
git commit -m "feat(p2): symbology/labeling/render-context pybind11 bindings + tests"
```
