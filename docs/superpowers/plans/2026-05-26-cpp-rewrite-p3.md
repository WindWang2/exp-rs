# C++ Rewrite — Phase P3 (GUI Canvas + Map Tools) Implementation Plan

**Goal:** Build `libqgis_gui.so` and expose `QgsMapCanvas`, `QgsLayerTreeView`,
`QgsMapToolPan`, `QgsMapToolZoom` through `_antigravity_core`. Gate with headless
(offscreen) pytest: canvas renders a layer, map tool objects instantiate correctly.

**Starting state:** P2 complete (commit `80a8f15`). Branch: `feat/p2-symbology`.
`libqgis_core.so` built and working. `_antigravity_core` has 22+ classes.

**Architecture:**
```
libqgis_native.so  ←  src/native/   (tiny, Qt::DBus on Linux)
libqgis_gui.so     ←  src/gui/      (depends on core + native + Qt Widgets)
_antigravity_core  ←  src/python/   (links gui + core)
```

**Headless testing:** set `QT_QPA_PLATFORM=offscreen` — Qt creates an offscreen
surface, all widget operations succeed without a real display.

**Optional features to disable in src/gui/CMakeLists.txt:**
- `WITH_QWTPOLAR` / QWT: not installed on this system
- `WITH_QSCINTILLA`: not installed
- `WITH_QTWEBENGINE`: not needed for canvas
- `WITH_QTGAMEPAD`: not needed

---

## Task 0: Create P3 branch

- [ ] `git checkout -b feat/p3-gui feat/p2-symbology`

---

## Task 1: Vendor native + GUI source

**Files created:** `src/native/`, `src/gui/`

- [ ] **Step 1: Copy subtrees**
  ```bash
  cp -r qgis_ref/src/native src/native
  cp -r qgis_ref/src/gui   src/gui
  ```

- [ ] **Step 2: Verify key files**
  ```bash
  test -f src/native/qgsnative.cpp && \
  test -f src/gui/qgsmapcanvas.cpp && \
  test -f src/gui/maptools/qgsmaptoolpan.cpp && \
  test -f src/gui/layertree/qgslayertreeview.cpp && echo OK
  ```
  Expected: `OK`.

- [ ] **Step 3: Commit vendor**
  ```bash
  git add src/native src/gui
  git commit -m "feat(p3): vendor QGIS native + GUI source subtrees"
  ```

---

## Task 2: Build libqgis_native

**Files modified:** `CMakeLists.txt`, new `src/native/CMakeLists.txt`

- [ ] **Step 1: Write `src/native/CMakeLists.txt`**
  ```cmake
  find_package(Qt6 REQUIRED COMPONENTS DBus)
  
  set(QGIS_NATIVE_SRCS qgsnative.cpp)
  set(QGIS_NATIVE_HDRS qgsnative.h)
  
  add_library(qgis_native SHARED ${QGIS_NATIVE_SRCS})
  target_include_directories(qgis_native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(qgis_native PRIVATE Qt6::Core Qt6::DBus)
  generate_export_header(qgis_native EXPORT_FILE_NAME qgis_native.h
    BASE_NAME QGIS_NATIVE)
  ```

- [ ] **Step 2: Add to top-level CMakeLists.txt**
  Add `add_subdirectory(src/native)` before `add_subdirectory(src/gui)`.

- [ ] **Step 3: Build and verify**
  ```bash
  cmake -S . -B cmake-build -G Ninja 2>&1 | tail -3
  cmake --build cmake-build --target qgis_native 2>&1 | tail -5
  find cmake-build -name 'libqgis_native*.so' | head -3
  ```
  Expected: `libqgis_native.so` exists.

---

## Task 3: Configure src/gui (cmake configure only)

The upstream `src/gui/CMakeLists.txt` needs trimming for our build.

**Files modified:** `CMakeLists.txt`, `src/gui/CMakeLists.txt`

- [ ] **Step 1: Add options to top-level CMakeLists.txt**
  ```cmake
  option(WITH_QWTPOLAR      "" OFF)
  option(WITH_QSCINTILLA    "" OFF)
  option(WITH_QTWEBENGINE   "" OFF)
  option(WITH_QTGAMEPAD     "" OFF)
  option(WITH_QUICK         "" OFF)
  find_package(Qt6 REQUIRED COMPONENTS UiTools SvgWidgets MultimediaWidgets)
  add_subdirectory(src/native)
  add_subdirectory(src/gui)
  ```

- [ ] **Step 2: Iterate configure until clean**
  ```bash
  cmake -S . -B cmake-build -G Ninja 2>&1 | grep -E "^CMake Error|^-- Could NOT" | head -20
  ```
  For each `Could NOT find` error: add `option(WITH_xxx "" OFF)` or stub the
  missing `find_package`. Mark each change `# ANTIGRAVITY:`.

- [ ] **Step 3: Confirm configure ends without error**
  ```bash
  cmake -S . -B cmake-build -G Ninja 2>&1 | tail -5
  ```
  Expected: `-- Generating done` / `-- Build files have been written`.

---

## Task 4: Compile libqgis_gui (closure loop)

> Iterative, similar to P0 Task 5. Time-box: **5 working days.**
> Exit criterion: `libqgis_gui.so` links without errors.

**Files modified:** `src/gui/CMakeLists.txt`

- [ ] **Step 1: First build attempt**
  ```bash
  cmake --build cmake-build --target qgis_gui 2>&1 | \
    grep -E "error:|undefined reference" | sort | uniq -c | sort -rn | head -30
  ```

- [ ] **Step 2: Resolve errors in priority order**
  1. Missing header → add include path or copy from `qgis_ref`
  2. Optional-dep compile error (qwt, qscintilla, etc.) → confirm `WITH_xxx=OFF`
     and remove the corresponding `.cpp` from `QGIS_GUI_SRCS` (`# ANTIGRAVITY:`)
  3. Mesh/3D/pointcloud GUI → remove those `.cpp` from SRCS
  4. `qgis_native` symbol missing → check `target_link_libraries`

  Re-build after each batch.

- [ ] **Step 3: Verify link**
  ```bash
  cmake --build cmake-build --target qgis_gui 2>&1 | tail -5
  find cmake-build -name 'libqgis_gui*.so' | head -3
  ```
  Expected: `libqgis_gui.so` exists.

---

## Task 5: pybind11 bindings for GUI classes

**Files modified:** `src/python/bindings.cpp`, `src/python/CMakeLists.txt`

- [ ] **Step 1: Link `_antigravity_core` against `qgis_gui`**
  In `src/python/CMakeLists.txt`:
  ```cmake
  target_link_libraries(_antigravity_core PRIVATE qgis_core qgis_gui Qt6::Core Qt6::Gui Qt6::Widgets)
  ```

- [ ] **Step 2: Add GUI includes to bindings.cpp**
  ```cpp
  // P3: GUI
  #include <qgsmapcanvas.h>
  #include <qgsmaptool.h>
  #include <maptools/qgsmaptoolpan.h>
  #include <maptools/qgsmaptoolzoom.h>
  #include <layertree/qgslayertreeview.h>
  ```

- [ ] **Step 3: Add bindings**
  ```cpp
  // ── P3: QgsMapCanvas ──────────────────────────────────────────────────────
  py::class_<QgsMapCanvas>(m, "QgsMapCanvas")
      .def(py::init([]() { return new QgsMapCanvas(); }),
           py::return_value_policy::take_ownership)
      .def("setLayers", [](QgsMapCanvas &c, const std::vector<QgsMapLayer*> &layers) {
          QList<QgsMapLayer*> lst;
          for (auto *l : layers) lst.append(l);
          c.setLayers(lst);
      })
      .def("setExtent",    [](QgsMapCanvas &c, const QgsRectangle &r) { c.setExtent(r); })
      .def("refresh",      &QgsMapCanvas::refresh)
      .def("scale",        &QgsMapCanvas::scale)
      .def("magnificationFactor", &QgsMapCanvas::magnificationFactor)
      .def("saveAsImage",  [](QgsMapCanvas &c, const QString &path) {
          return c.saveAsImage(path);
      });

  // ── P3: QgsMapTool ────────────────────────────────────────────────────────
  py::class_<QgsMapTool>(m, "QgsMapTool");

  py::class_<QgsMapToolPan, QgsMapTool>(m, "QgsMapToolPan")
      .def(py::init([](QgsMapCanvas *c) { return new QgsMapToolPan(c); }),
           py::arg("canvas"),
           py::return_value_policy::take_ownership);

  py::class_<QgsMapToolZoom, QgsMapTool>(m, "QgsMapToolZoom")
      .def(py::init([](QgsMapCanvas *c, bool zoomOut) {
               return new QgsMapToolZoom(c, zoomOut);
           }),
           py::arg("canvas"), py::arg("zoomOut") = false,
           py::return_value_policy::take_ownership);

  // ── P3: QgsLayerTreeView ──────────────────────────────────────────────────
  py::class_<QgsLayerTreeView>(m, "QgsLayerTreeView")
      .def(py::init([]() { return new QgsLayerTreeView(); }),
           py::return_value_policy::take_ownership)
      .def("setModel", [](QgsLayerTreeView &v, QgsLayerTreeModel *m) {
          v.setModel(m);
      });
  ```

- [ ] **Step 4: Build and verify**
  ```bash
  cmake --build cmake-build --target _antigravity_core 2>&1 | tail -5
  ```
  Expected: links successfully.

---

## Task 6: Headless pytest suite

**File:** `tests/test_cpp_p3.py`

- [ ] **Step 1: Write the test**
  ```python
  """P3 验收测试：QgsMapCanvas / MapTool / LayerTreeView (无头模式)。"""
  import os, sys
  os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
  import pytest

  BUILD    = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cmake-build"))
  BUILD_PY = os.path.join(BUILD, "src", "python")
  DATA_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
  LE7_TIF  = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")

  @pytest.fixture(scope="session")
  def core():
      sys.path.insert(0, BUILD_PY); sys.path.insert(0, BUILD)
      os.environ.setdefault("ANTIGRAVITY_DATA", BUILD)
      import _antigravity_core as c; c.init(BUILD); return c

  def test_canvas_create(core):
      canvas = core.QgsMapCanvas()
      assert canvas is not None

  def test_canvas_scale(core):
      canvas = core.QgsMapCanvas()
      assert canvas.scale() >= 0

  def test_canvas_render_layer(core, tmp_path):
      canvas = core.QgsMapCanvas()
      layer = core.QgsRasterLayer(LE7_TIF, "le7")
      assert layer.isValid()
      canvas.setLayers([layer])
      canvas.setExtent(layer.extent())
      canvas.refresh()
      out = str(tmp_path / "canvas.png")
      result = canvas.saveAsImage(out)
      assert result and os.path.exists(out)

  def test_maptool_pan(core):
      canvas = core.QgsMapCanvas()
      tool = core.QgsMapToolPan(canvas)
      assert tool is not None

  def test_maptool_zoom(core):
      canvas = core.QgsMapCanvas()
      tool = core.QgsMapToolZoom(canvas, False)
      assert tool is not None

  def test_layertreeview_create(core):
      view = core.QgsLayerTreeView()
      assert view is not None

  def test_p3_exports(core):
      for name in ["QgsMapCanvas", "QgsMapTool", "QgsMapToolPan",
                   "QgsMapToolZoom", "QgsLayerTreeView"]:
          assert hasattr(core, name), f"缺少 {name}"
  ```

- [ ] **Step 2: Run**
  ```bash
  QT_QPA_PLATFORM=offscreen PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build \
    pytest tests/test_cpp_p3.py -v
  ```
  Expected: 7/7 PASS.

---

## Task 7: Full gate run + commit

- [ ] **Step 1: Full gate**
  ```bash
  ANTIGRAVITY_DATA=$PWD/cmake-build ctest --test-dir cmake-build --output-on-failure
  QT_QPA_PLATFORM=offscreen PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build \
    pytest tests/test_cpp_core.py tests/test_cpp_p1.py tests/test_cpp_p2.py \
           tests/test_cpp_p3.py -v
  ```
  Expected: ctest 2/2; pytest 35/35.

- [ ] **Step 2: Commit**
  ```bash
  git add src/native src/gui src/python/bindings.cpp src/python/CMakeLists.txt \
          tests/test_cpp_p3.py docs/superpowers/plans/2026-05-26-cpp-rewrite-p3.md \
          CMakeLists.txt
  git commit -m "feat(p3): libqgis_gui + QgsMapCanvas/MapTool/LayerTreeView pybind11 + tests"
  ```

---

## Rollback

If `libqgis_gui.so` does not link within 5 days, stop and note the blocker.
`python-final` tag remains; existing P0–P2 work is unaffected.

## Self-Review

- `libqgis_native.so` is a pre-requisite for `libqgis_gui.so` — Task 2 before Task 3.
- Headless test uses `QT_QPA_PLATFORM=offscreen` to avoid display requirement.
- `_antigravity_core` links `qgis_gui` in Task 5, so GUI classes are available in Python.
- No Python core/gui files deleted in P3 — that's a separate cleanup task.
