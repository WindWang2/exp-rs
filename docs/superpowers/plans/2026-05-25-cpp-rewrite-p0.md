# C++ Rewrite — Phase P0 (Rendering Core) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a forked QGIS C++ rendering core inside this repo that opens the LE7 scene, renders it off the main thread, and proves the PROJ-SIGSEGV and performance problems from the spec are gone — gated by automated tests.

**Architecture:** Vendor `qgis_ref/src/core` (+ its `external/`, `cmake/`, `cmake_templates/`) into `src/`. Build it whole as `libantigravity_core.so` with optional features compiled out via a generated `qgsconfig.h` (no `HAVE_POSTGRESQL`, no spatialite, etc.). Expose a deliberately narrow pybind11 module `_antigravity_core` (open raster, read CRS, render to PNG). The existing Python app/agent/analysis stay untouched in P0 — nothing is deleted until the gates pass.

**Tech Stack:** CMake 4.3, Qt 6.11 (Core/Gui/Concurrent), GDAL 3.13, PROJ 9.8, bison 3.8 / flex 2.6, pybind11, Python 3.13 (PySide6 for the existing app).

---

## Scope & Refinements to the Spec

This plan covers **only P0** from `docs/superpowers/specs/2026-05-25-cpp-rewrite-design.md`. P1–P4 get their own plans, written after Task 13 produces the real dependency-closure size and re-calibrates the §16 effort estimates.

Two deliberate refinements of the spec, decided during planning:

1. **§3 "subset by not compiling" → "build whole, trim later."** Core is one monolithic library (1023 `.cpp` in a single `QGIS_CORE_SRCS`, plus a bison/flex-generated expression parser). Hand-subsetting it is a size optimization, not a prerequisite for first pixels. P0 builds core whole with optional *features* compiled out via `qgsconfig.h`; removing mesh/pointcloud/vectortile *source files* is deferred to a later cleanup plan.
2. **§17 parity baseline → GDAL reference, not the old Python path.** The Python render path is what we're replacing and its internals are mock-heavy; a deterministic `gdalwarp` reference is a better golden master. We compare the C++ render against GDAL within an RMSE tolerance.

**Time-box:** Task 5 (get core to link) is the project's largest unknown. It is time-boxed to **10 working days**. If core does not link by then, stop and fall back to PyQGIS (spec §2 option B, §16 rollback) — do not keep grinding.

---

## File Structure

Files created in P0 (everything new lives under `src/` and `tests/cpp/` so the existing Python tree is untouched):

- `cmake-build/` — out-of-source build dir (coexists with the old `build/`; not committed)
- `CMakeLists.txt` — top-level: finds deps, generates `qgsconfig.h`, adds subdirs
- `cmake/` — vendored from `qgis_ref/cmake/` (CMake modules QGIS's core build needs)
- `cmake_templates/qgsconfig.h.in` — vendored from `qgis_ref/cmake_templates/`
- `src/core/` — vendored from `qgis_ref/src/core/` (the fork)
- `src/external/` — vendored from `qgis_ref/external/` (kdbush, nanoarrow, nmea — referenced by core SRCS)
- `src/runtime/antigravity_init.cpp` / `.h` — `QgsApplication` bring-up + data-path wiring (our code)
- `src/python/bindings.cpp` — pybind11 module `_antigravity_core` (our code)
- `tests/cpp/CMakeLists.txt` — ctest registration
- `tests/cpp/test_thread_safety.cpp` — concurrent transform + raster read (the §12 acceptance)
- `tests/cpp/test_render_smoke.cpp` — render LE7 to QImage, assert non-empty
- `tests/golden/make_reference.py` — produces the GDAL golden-master PNG + stats (our code)
- `tests/test_cpp_core.py` — pytest driving `_antigravity_core`: open, CRS, render, parity, perf
- `src/QGIS_BASELINE.md` — records the upstream tag/commit each forked file came from (spec §15)
- `docs/superpowers/plans/p0-closure-log.md` — the iterative closure log from Task 5

---

## Task 0: Verify toolchain & record versions

**Files:**
- Create: `src/QGIS_BASELINE.md`

- [ ] **Step 1: Verify every build dependency resolves**

Run:
```bash
cmake --version && bison --version | head -1 && flex --version && \
gdalinfo --version && qmake6 -query QT_VERSION && \
python3 -c "import pybind11, sys; print('pybind11', pybind11.__version__)"
```
Expected: cmake ≥ 4.3, bison ≥ 3.8, flex ≥ 2.6, GDAL ≥ 3.8, Qt ≥ 6.5, pybind11 prints a version. If pybind11 is missing: `pip install pybind11`.

- [ ] **Step 2: Record the QGIS baseline**

Find the upstream version the fork is based on:
```bash
grep -E 'CPACK_PACKAGE_VERSION' qgis_ref/CMakeLists.txt | head -3
git -C qgis_ref rev-parse HEAD 2>/dev/null || echo "no git in qgis_ref"
```
Write `src/QGIS_BASELINE.md` with the version string and commit (or "snapshot, no SHA"), and this line: "All files under `src/core`, `src/external`, `cmake`, `cmake_templates` are vendored from this baseline. Local edits are marked `// ANTIGRAVITY:`."

- [ ] **Step 3: Commit**

```bash
git add src/QGIS_BASELINE.md
git commit -m "chore(p0): record QGIS fork baseline and toolchain"
```

---

## Task 1: Capture the GDAL golden-master render (parity baseline)

Do this first so the parity gate (Task 11) has a fixed target captured before any C++ exists.

**Files:**
- Create: `tests/golden/make_reference.py`
- Create (generated, committed): `tests/golden/le7_b4_ref.png`, `tests/golden/le7_b4_ref.json`

- [ ] **Step 1: Write the reference generator**

```python
# tests/golden/make_reference.py
"""Deterministic GDAL golden master for the P0 parity gate.
Renders LE7 band 4 (NIR) to a fixed 512x512 PNG in the source CRS with a
fixed 2-98 percentile stretch. The C++ path (Task 11) must match within RMSE.
"""
import json, os, subprocess, sys
import numpy as np
from osgeo import gdal

SRC = "data/LE7/LE71300411999327EDC00_B4.TIF"
OUT_PNG = "tests/golden/le7_b4_ref.png"
OUT_JSON = "tests/golden/le7_b4_ref.json"
SIZE = 512

def main():
    gdal.UseExceptions()
    ds = gdal.Open(SRC)
    band = ds.GetRasterBand(1)
    arr = band.ReadAsArray().astype(np.float64)
    lo, hi = np.percentile(arr[arr > 0], [2, 98])
    stretched = np.clip((arr - lo) / (hi - lo) * 255.0, 0, 255).astype(np.uint8)
    # resample to SIZE x SIZE with GDAL (bilinear) for a fixed reference
    warped = gdal.Warp("", ds, format="MEM", width=SIZE, height=SIZE,
                       resampleAlg="bilinear")
    w = warped.GetRasterBand(1).ReadAsArray().astype(np.float64)
    wb = np.clip((w - lo) / (hi - lo) * 255.0, 0, 255).astype(np.uint8)
    gdal.GetDriverByName("PNG").CreateCopy(
        OUT_PNG,
        gdal.GetDriverByName("MEM").Create("", SIZE, SIZE, 1, gdal.GDT_Byte))
    out = gdal.Open(OUT_PNG, gdal.GA_Update)
    out.GetRasterBand(1).WriteArray(wb)
    out.FlushCache()
    meta = {"src": SRC, "size": SIZE, "stretch_lo": lo, "stretch_hi": hi,
            "crs": ds.GetProjection()[:40], "mean": float(wb.mean())}
    with open(OUT_JSON, "w") as f:
        json.dump(meta, f, indent=2)
    print("wrote", OUT_PNG, meta)

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it and verify the golden master exists**

Run: `python tests/golden/make_reference.py`
Expected: prints `wrote tests/golden/le7_b4_ref.png {...}` and both files exist with non-zero size.

- [ ] **Step 3: Commit**

```bash
git add tests/golden/make_reference.py tests/golden/le7_b4_ref.png tests/golden/le7_b4_ref.json
git commit -m "test(p0): add GDAL golden-master render for parity gate"
```

---

## Task 2: Vendor the QGIS core source into the fork

**Files:**
- Create: `src/core/` (copied), `src/external/` (copied), `cmake/` (copied), `cmake_templates/qgsconfig.h.in` (copied)

- [ ] **Step 1: Copy the source subtrees**

Run:
```bash
cp -r qgis_ref/src/core src/core
cp -r qgis_ref/external src/external
cp -r qgis_ref/cmake cmake
mkdir -p cmake_templates && cp qgis_ref/cmake_templates/qgsconfig.h.in cmake_templates/
```

- [ ] **Step 2: Verify the copy landed and core SRCS references resolve locally**

Run:
```bash
test -f src/core/qgsapplication.cpp && test -f src/core/CMakeLists.txt && \
test -d src/external/nmea && test -f cmake_templates/qgsconfig.h.in && echo OK
```
Expected: `OK`.

- [ ] **Step 3: Commit the vendored fork**

```bash
git add src/core src/external cmake cmake_templates
git commit -m "feat(p0): vendor QGIS core source subtree into fork"
```

---

## Task 3: Generate qgsconfig.h and a top-level CMakeLists (configure only)

**Files:**
- Create: `CMakeLists.txt`

- [ ] **Step 1: Write the top-level CMakeLists (core only for now)**

```cmake
cmake_minimum_required(VERSION 3.20)
project(antigravity VERSION 1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg Xml Sql)
find_package(GDAL REQUIRED)
find_package(PROJ REQUIRED)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# --- qgsconfig.h: all optional features OFF for P0 ---
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(RELEASE_NAME "Antigravity")
set(QGIS_VERSION_INT 10000)
set(QGIS_PLUGIN_SUBDIR "plugins")
set(QGIS_DATA_SUBDIR "share")
set(QGIS_LIBEXEC_SUBDIR "libexec")
set(QGIS_LIB_SUBDIR "lib")
set(QGIS_QML_SUBDIR "qml")
set(QGIS_SERVER_MODULE_SUBDIR "server")
# HAVE_* left undefined ⇒ #cmakedefine compiles those features out
configure_file(cmake_templates/qgsconfig.h.in ${CMAKE_BINARY_DIR}/qgsconfig.h)
include_directories(${CMAKE_BINARY_DIR})

add_subdirectory(src/core)
```

- [ ] **Step 2: Configure (do not build yet)**

Run:
```bash
cmake -S . -B cmake-build -G Ninja 2>&1 | tee /tmp/p0-configure.log
```
Expected: configure completes OR fails only with messages about missing `cmake/` modules or undefined CMake variables — those are Task 4. A generated `cmake-build/qgsconfig.h` must exist:
```bash
test -f cmake-build/qgsconfig.h && grep -c '#define VERSION' cmake-build/qgsconfig.h
```
Expected: file exists, prints `1` or more.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(p0): top-level CMake + qgsconfig.h generation (optional features off)"
```

---

## Task 4: Make `cmake -B cmake-build` configure cleanly

The core `CMakeLists.txt` was written for QGIS's full build (it references `WITH_*` options and helper functions from `cmake/`). Reconcile it for our trimmed configure.

**Files:**
- Modify: `src/core/CMakeLists.txt` (mark every change `// ANTIGRAVITY:` in a comment / `# ANTIGRAVITY:`)
- Modify: `CMakeLists.txt` (add any `WITH_*`/`set()` defaults the core file expects)

- [ ] **Step 1: Iterate configure → fix → re-configure**

Loop:
```bash
cmake -S . -B cmake-build -G Ninja 2>&1 | tee /tmp/p0-configure.log
```
For each error: if it's an undefined `WITH_xxx`, add `option(WITH_xxx "" OFF)` to top-level `CMakeLists.txt`. If it's a missing helper macro, copy the defining `.cmake` from `qgis_ref/cmake/` into `cmake/`. If it's an optional subdir (mesh test data, pdf4qt, etc.), guard it off. Mark guarded blocks `# ANTIGRAVITY: disabled for P0`.

- [ ] **Step 2: Confirm clean configure**

Run: `cmake -S . -B cmake-build -G Ninja 2>&1 | tail -5`
Expected: ends with `-- Generating done` / `-- Build files have been written to: .../cmake-build` and no errors.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt cmake src/core/CMakeLists.txt
git commit -m "build(p0): configure trimmed core (optional WITH_ features off)"
```

---

## Task 5: Compile and link `libantigravity_core` (the closure loop)

> **This is the time-boxed unknown (10 working days).** It is inherently iterative, not a 5-minute step. The exit criterion is precise; the fallback is explicit.

**Files:**
- Modify: `src/core/CMakeLists.txt` (trim `QGIS_CORE_SRCS` entries that pull unavailable optional deps; mark `# ANTIGRAVITY:`)
- Create: `docs/superpowers/plans/p0-closure-log.md` (running log)

- [ ] **Step 1: First build attempt; capture the error surface**

Run:
```bash
cmake --build cmake-build --target qgis_core 2>&1 | tee /tmp/p0-build.log
grep -E 'error:|undefined reference|No such file' /tmp/p0-build.log | sort | uniq -c | sort -rn | head -40
```
Record the top error clusters in `p0-closure-log.md`.

- [ ] **Step 2: Resolve, in this priority order, re-building after each batch**

1. **Missing header / external** → ensure the file exists under `src/external/` or `src/core/`; copy from `qgis_ref` if the vendor step missed it.
2. **Optional-dep compile errors** (spatialite, postgres, hana, oracle, pdal, etc.) → confirm the matching `HAVE_*` is undefined in `cmake-build/qgsconfig.h`; if the code isn't guarded, remove that `.cpp` from `QGIS_CORE_SRCS` (`# ANTIGRAVITY:`) since the feature is excluded (spec §6).
3. **`QgsExpression` / bison-flex outputs** → confirm `BISON_TARGET`/`FLEX_TARGET` ran (expression engine is REQUIRED, spec §6); never remove it.
4. **Mesh / pointcloud / vectortile / 3d errors** → remove those source entries from `QGIS_CORE_SRCS` (excluded, spec §5). Log every removed file.

Re-run the build after each batch; append the new error count to the log.

- [ ] **Step 3: Verify the link**

Run:
```bash
cmake --build cmake-build --target qgis_core 2>&1 | tail -5
find cmake-build -name 'libqgis_core*.so' -o -name 'libantigravity_core*.so' | head
```
Expected: build prints no `error:`; a `libqgis_core*.so` (the core target name) exists.
**Exit criterion: the core shared library links.** If day 10 arrives without a link, stop and invoke the PyQGIS fallback (spec §16).

- [ ] **Step 4: Record the realized closure and commit**

In `p0-closure-log.md`, write the final count of compiled `.cpp` files (`grep -c '\.cpp' src/core/CMakeLists.txt` minus removed) — this is the real number that re-calibrates §16 for P1–P4.
```bash
git add src/core/CMakeLists.txt docs/superpowers/plans/p0-closure-log.md
git commit -m "build(p0): link libantigravity_core; record dependency closure"
```

---

## Task 6: Runtime init harness + CRS smoke test

**Files:**
- Create: `src/runtime/antigravity_init.h`, `src/runtime/antigravity_init.cpp`
- Create: `tests/cpp/test_render_smoke.cpp` (CRS portion first)
- Modify: `CMakeLists.txt` (add `src/runtime`, `tests/cpp`)

- [ ] **Step 1: Write the init harness**

```cpp
// src/runtime/antigravity_init.h
#pragma once
#include <QString>
// Initializes QgsApplication with bundled data paths. Idempotent.
// dataRoot points at the dir holding srs.db/qgis.db/resources (spec §9).
void antigravity_init(const QString &dataRoot);
```
```cpp
// src/runtime/antigravity_init.cpp
#include "antigravity_init.h"
#include <qgsapplication.h>
#include <mutex>
static std::once_flag g_once;
void antigravity_init(const QString &dataRoot) {
    std::call_once(g_once, [&]() {
        static int argc = 0;
        static QgsApplication app(argc, nullptr, /*GUIenabled=*/false);
        QgsApplication::setPrefixPath(dataRoot, true);
        QgsApplication::initQgis();
    });
}
```

- [ ] **Step 2: Write the CRS smoke test (failing — no data wired yet)**

```cpp
// tests/cpp/test_render_smoke.cpp
#include "antigravity_init.h"
#include <qgscoordinatereferencesystem.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
int main() {
    antigravity_init(qgetenv("ANTIGRAVITY_DATA"));
    QgsCoordinateReferenceSystem crs("EPSG:32650"); // LE7 is UTM-ish
    if (!crs.isValid()) { fprintf(stderr, "CRS invalid: srs.db not found?\n"); return 1; }
    printf("CRS ok: %s\n", crs.authid().toUtf8().constData());
    return 0;
}
```

- [ ] **Step 3: Register the test target and build**

Add to `CMakeLists.txt`: `enable_testing()` and `add_subdirectory(tests/cpp)`. Create `tests/cpp/CMakeLists.txt`:
```cmake
add_executable(test_render_smoke test_render_smoke.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/antigravity_init.cpp)
target_include_directories(test_render_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/runtime ${CMAKE_BINARY_DIR})
target_link_libraries(test_render_smoke PRIVATE qgis_core Qt6::Core)
add_test(NAME render_smoke COMMAND test_render_smoke)
```
Run: `cmake -S . -B cmake-build -G Ninja && cmake --build cmake-build --target test_render_smoke`
Expected: builds.

- [ ] **Step 4: Wire data paths until the CRS test passes**

Stage the QGIS resource bundle and point the env var at it:
```bash
cp -r qgis_ref/resources cmake-build/share_resources 2>/dev/null || true
export ANTIGRAVITY_DATA=$PWD/cmake-build
ctest --test-dir cmake-build -R render_smoke --output-on-failure
```
If `CRS invalid`, locate `srs.db`/`proj.db` (`find / -name srs.db 2>/dev/null`, `gdal-config --datadir`) and set `QgsApplication::setPkgDataPath()` accordingly in `antigravity_init.cpp`. Iterate until:
Expected: `CRS ok: EPSG:32650` and ctest reports `Passed`.

- [ ] **Step 5: Commit**

```bash
git add src/runtime tests/cpp/CMakeLists.txt tests/cpp/test_render_smoke.cpp CMakeLists.txt
git commit -m "feat(p0): QgsApplication runtime init + passing CRS smoke test"
```

---

## Task 7: Render LE7 off-thread to QImage (C++ smoke test)

**Files:**
- Modify: `tests/cpp/test_render_smoke.cpp` (add render path)
- Modify: `tests/cpp/CMakeLists.txt`

- [ ] **Step 1: Add a failing render assertion**

Append to `test_render_smoke.cpp`'s `main()` before `return 0;`:
```cpp
{
    QgsRasterLayer *layer = new QgsRasterLayer(
        "data/LE7/LE71300411999327EDC00_B4.TIF", "b4", "gdal");
    if (!layer->isValid()) { fprintf(stderr, "layer invalid\n"); return 2; }
    QgsMapSettings settings;
    settings.setLayers({layer});
    settings.setExtent(layer->extent());
    settings.setOutputSize(QSize(512, 512));
    settings.setDestinationCrs(layer->crs());
    QgsMapRendererSequentialJob job(settings);   // parallel job swapped in Task 9
    job.start();
    job.waitForFinished();
    QImage img = job.renderedImage();
    if (img.isNull() || img.size() != QSize(512,512)) {
        fprintf(stderr, "render empty\n"); return 3;
    }
    img.save("cmake-build/le7_cpp_render.png");
    printf("render ok\n");
}
```
Add the includes: `#include <qgsrasterlayer.h> #include <qgsmapsettings.h> #include <qgsmaprenderersequentialjob.h> #include <QImage>`.

- [ ] **Step 2: Build and run; expect FAIL first if paths/providers off**

Run: `cmake --build cmake-build --target test_render_smoke && ANTIGRAVITY_DATA=$PWD/cmake-build ctest --test-dir cmake-build -R render_smoke --output-on-failure`
Expected first run may FAIL with `layer invalid` (GDAL provider not registered). Fix: ensure `QgsApplication::initQgis()` registered providers; if the GDAL provider is a plugin, register it statically in `antigravity_init.cpp`.

- [ ] **Step 3: Iterate to green**

Expected final: `render ok`, ctest `Passed`, and `cmake-build/le7_cpp_render.png` is a non-empty 512×512 image (`gdalinfo cmake-build/le7_cpp_render.png | grep 'Size is'`).

- [ ] **Step 4: Commit**

```bash
git add tests/cpp/test_render_smoke.cpp tests/cpp/CMakeLists.txt
git commit -m "feat(p0): render LE7 to QImage via QgsMapRenderer (C++ smoke test)"
```

---

## Task 8: pybind11 module `_antigravity_core`

**Files:**
- Create: `src/python/bindings.cpp`, `src/python/CMakeLists.txt`
- Create: `tests/test_cpp_core.py`
- Modify: `CMakeLists.txt` (add `src/python`)

- [ ] **Step 1: Write the narrow binding**

```cpp
// src/python/bindings.cpp
#include <pybind11/pybind11.h>
#include "antigravity_init.h"
#include <qgsrasterlayer.h>
#include <qgsmapsettings.h>
#include <qgsmaprenderersequentialjob.h>
#include <QImage>
namespace py = pybind11;

static py::dict open_raster(const std::string &path) {
    QgsRasterLayer layer(QString::fromStdString(path), "r", "gdal");
    py::dict d;
    d["valid"] = layer.isValid();
    if (layer.isValid()) {
        d["width"] = layer.width();
        d["height"] = layer.height();
        d["crs"] = layer.crs().authid().toStdString();
    }
    return d;
}

static bool render_to_png(const std::string &path, const std::string &out, int size) {
    QgsRasterLayer *layer = new QgsRasterLayer(QString::fromStdString(path), "r", "gdal");
    if (!layer->isValid()) return false;
    QgsMapSettings s;
    s.setLayers({layer}); s.setExtent(layer->extent());
    s.setOutputSize(QSize(size, size)); s.setDestinationCrs(layer->crs());
    QgsMapRendererSequentialJob job(s);
    job.start(); job.waitForFinished();
    QImage img = job.renderedImage();
    return !img.isNull() && img.save(QString::fromStdString(out));
}

PYBIND11_MODULE(_antigravity_core, m) {
    m.def("init", [](const std::string &root){ antigravity_init(QString::fromStdString(root)); });
    m.def("open_raster", &open_raster);
    m.def("render_to_png", &render_to_png);
}
```

- [ ] **Step 2: Write the build file**

```cmake
# src/python/CMakeLists.txt
find_package(pybind11 REQUIRED)
pybind11_add_module(_antigravity_core ${CMAKE_SOURCE_DIR}/src/python/bindings.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/antigravity_init.cpp)
target_include_directories(_antigravity_core PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/runtime ${CMAKE_BINARY_DIR})
target_link_libraries(_antigravity_core PRIVATE qgis_core Qt6::Core Qt6::Gui)
```
Add `add_subdirectory(src/python)` to top-level `CMakeLists.txt`.

- [ ] **Step 3: Write the failing pytest**

```python
# tests/test_cpp_core.py
import os, sys, subprocess
import pytest

BUILD = os.path.join(os.path.dirname(__file__), "..", "cmake-build")

@pytest.fixture(scope="session")
def core():
    sys.path.insert(0, BUILD)
    import _antigravity_core as c
    c.init(BUILD)
    return c

def test_open_le7_b4(core):
    info = core.open_raster("data/LE7/LE71300411999327EDC00_B4.TIF")
    assert info["valid"] is True
    assert info["width"] > 7000 and info["height"] > 7000
    assert info["crs"].startswith("EPSG:")
```

- [ ] **Step 4: Build and run**

Run: `cmake --build cmake-build --target _antigravity_core && PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_core.py::test_open_le7_b4 -v`
Expected: PASS — module imports, LE7 opens, dims > 7000, CRS is `EPSG:...`.

- [ ] **Step 5: Commit**

```bash
git add src/python tests/test_cpp_core.py CMakeLists.txt
git commit -m "feat(p0): _antigravity_core pybind11 module + open-raster test"
```

---

## Task 9: Thread-safety regression — the PROJ-SIGSEGV repro (§12 acceptance)

**Files:**
- Create: `tests/cpp/test_thread_safety.cpp`
- Modify: `tests/cpp/CMakeLists.txt`
- Modify: `tests/cpp/test_render_smoke.cpp` (swap sequential → parallel render job)

- [ ] **Step 1: Write the concurrent-transform test (the exact thing that crashed in Python)**

```cpp
// tests/cpp/test_thread_safety.cpp
#include "antigravity_init.h"
#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrasterlayer.h>
#include <QtConcurrent>
#include <QVector>
#include <cstdio>
int main() {
    antigravity_init(qgetenv("ANTIGRAVITY_DATA"));
    QgsCoordinateReferenceSystem src("EPSG:4326"), dst("EPSG:3857");
    QVector<int> work(64);
    // 64 parallel tasks each build a transform + open the CRS-bearing raster.
    // In Python this SIGSEGV'd in PROJ DatabaseContext. Must run clean here.
    std::atomic<int> ok{0};
    QtConcurrent::blockingMap(work, [&](int &){
        QgsCoordinateTransform ct(src, dst, QgsCoordinateTransformContext());
        QgsPointXY p = ct.transform(QgsPointXY(116.4, 39.9)); // Beijing
        QgsRasterLayer layer("data/LE7/LE71300411999327EDC00_B4.TIF", "r", "gdal");
        if (layer.isValid() && p.x() != 0.0) ok++;
    });
    printf("thread-safe transforms ok: %d/64\n", ok.load());
    return ok.load() == 64 ? 0 : 1;
}
```

- [ ] **Step 2: Register, build, run**

Add to `tests/cpp/CMakeLists.txt`:
```cmake
add_executable(test_thread_safety test_thread_safety.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/antigravity_init.cpp)
target_include_directories(test_thread_safety PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/runtime ${CMAKE_BINARY_DIR})
target_link_libraries(test_thread_safety PRIVATE qgis_core Qt6::Core Qt6::Concurrent)
add_test(NAME thread_safety COMMAND test_thread_safety)
```
Run: `cmake --build cmake-build --target test_thread_safety && ANTIGRAVITY_DATA=$PWD/cmake-build ctest --test-dir cmake-build -R thread_safety --output-on-failure`
Expected: `thread-safe transforms ok: 64/64`, ctest `Passed`, **no SIGSEGV** (exit 0). This is the headline §1/§12 acceptance.

- [ ] **Step 3: Swap the render smoke test to the parallel job**

In `test_render_smoke.cpp` replace `QgsMapRendererSequentialJob` with `QgsMapRendererParallelJob` (include `<qgsmaprendererparalleljob.h>`), rebuild, re-run `render_smoke`.
Expected: still `render ok` — proves off-main-thread parallel rendering works.

- [ ] **Step 4: Commit**

```bash
git add tests/cpp/test_thread_safety.cpp tests/cpp/test_render_smoke.cpp tests/cpp/CMakeLists.txt
git commit -m "test(p0): concurrent PROJ/GDAL regression passes; parallel render job"
```

---

## Task 10: Parity gate — C++ render vs GDAL golden master

**Files:**
- Modify: `tests/test_cpp_core.py`

- [ ] **Step 1: Add the failing parity test**

```python
# append to tests/test_cpp_core.py
import numpy as np
from osgeo import gdal

def _rmse(a, b):
    a = a.astype(np.float64); b = b.astype(np.float64)
    return float(np.sqrt(np.mean((a - b) ** 2)))

def test_render_parity(core, tmp_path):
    out = str(tmp_path / "cpp.png")
    assert core.render_to_png("data/LE7/LE71300411999327EDC00_B4.TIF", out, 512)
    cpp = gdal.Open(out).GetRasterBand(1).ReadAsArray()
    ref = gdal.Open("tests/golden/le7_b4_ref.png").GetRasterBand(1).ReadAsArray()
    assert cpp.shape == ref.shape == (512, 512)
    rmse = _rmse(cpp, ref)
    # 8-bit stretched single band; allow resampler/stretch differences
    assert rmse < 25.0, f"render RMSE {rmse:.2f} exceeds tolerance vs golden master"
```

- [ ] **Step 2: Run; tune only the documented knobs if it fails**

Run: `cmake --build cmake-build --target _antigravity_core && PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_core.py::test_render_parity -v`
Expected: PASS. If RMSE is high, the difference is resampling/stretch, not correctness — align the C++ render's resampling to bilinear (match `make_reference.py`) before loosening the threshold. Do not raise the threshold to mask a real mismatch.

- [ ] **Step 3: Commit**

```bash
git add tests/test_cpp_core.py
git commit -m "test(p0): render parity gate vs GDAL golden master (RMSE < 25)"
```

---

## Task 11: Performance gate — beat the ~1s Python baseline

**Files:**
- Modify: `tests/test_cpp_core.py`

- [ ] **Step 1: Add the failing perf test**

```python
# append to tests/test_cpp_core.py
import time

def test_render_perf(core, tmp_path):
    out = str(tmp_path / "perf.png")
    # warm caches once
    core.render_to_png("data/LE7/LE71300411999327EDC00_B4.TIF", out, 1024)
    t0 = time.perf_counter()
    assert core.render_to_png("data/LE7/LE71300411999327EDC00_B4.TIF", out, 1024)
    dt = time.perf_counter() - t0
    # spec §1 baseline ~1s for LE7 in Python; C++ must beat it clearly
    assert dt < 0.6, f"render took {dt:.3f}s, not faster than Python baseline"
```

- [ ] **Step 2: Run**

Run: `PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_core.py::test_render_perf -v`
Expected: PASS with the measured time printed under 0.6s.

- [ ] **Step 3: Commit**

```bash
git add tests/test_cpp_core.py
git commit -m "test(p0): performance gate — LE7 render under 0.6s"
```

---

## Task 12: Full gate run + tag the rollback point

**Files:**
- Modify: `docs/superpowers/plans/p0-closure-log.md` (final summary)

- [ ] **Step 1: Run every P0 gate together**

Run:
```bash
ANTIGRAVITY_DATA=$PWD/cmake-build ctest --test-dir cmake-build --output-on-failure
PYTHONPATH=. ANTIGRAVITY_DATA=$PWD/cmake-build pytest tests/test_cpp_core.py -v
```
Expected: ctest — `render_smoke`, `thread_safety` Passed. pytest — `test_open_le7_b4`, `test_render_parity`, `test_render_perf` all PASS.

- [ ] **Step 2: Tag the last Python-only tree as the rollback point (spec §7/§16)**

> The old Python core/gui is NOT deleted in P0 — only tagged so a later cleanup plan can delete it safely and PyQGIS fallback stays one checkout away.
```bash
git tag python-final -m "Last fully-working Python tree before C++ cutover"
```

- [ ] **Step 3: Write the P0 summary into the closure log**

In `p0-closure-log.md` record: realized core `.cpp` count, P0 wall-clock spent vs the 10-day box, measured render time, and a one-line re-calibration note for §16 (e.g. "closure was N files; P1 estimate revised to X").

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/p0-closure-log.md
git commit -m "docs(p0): P0 gates green; record closure + re-calibrated estimates"
```

---

## Subsequent Plans (written after P0)

Each becomes its own plan file once Task 13's closure data makes the estimates real:

- `…-cpp-rewrite-p1.md` — Layer/project + **expression engine** (REQUIRED, spec §6) + the broader pybind11 surface (spec §10): `QgsProject`, `QgsMapLayer`, parameter/feedback types.
- `…-cpp-rewrite-p2.md` — Symbology + labeling.
- `…-cpp-rewrite-p3.md` — GUI canvas (`QgsMapCanvas`) + map tools; begin replacing the PySide6 app.
- `…-cpp-rewrite-p4.md` — OGC server + plugin system + **hybrid analysis migration** (spec §11, trampoline registry) + the batch delete of old Python core/gui behind the `python-final` tag.

---

## Self-Review

- **Spec coverage (P0 rows only):** §1 motivation → Tasks 9 (SIGSEGV) + 11 (perf). §2 fallback → Task 5 time-box. §3 closure loop → Task 5. §5 P0 module list → Tasks 2/5. §6 expression required / optionals excluded → Task 5 Step 2. §8 build deps/order → Tasks 3–8. §9 runtime init → Task 6. §10 pybind11 → Task 8. §12 thread safety → Task 9. §15 baseline → Task 0. §16 risk/rollback → Task 5 box + Task 12 tag. §17 parity/perf/thread gates → Tasks 10/11/9. P1–P4 → explicitly deferred above.
- **Placeholder scan:** Task 5 is iterative by nature (a compile closure), not a placeholder — it has an exact exit criterion (core links) and fallback (PyQGIS at day 10). All code steps contain real code.
- **Type consistency:** `antigravity_init(QString)` is defined in Task 6 and reused verbatim in Tasks 8/9. The pybind11 funcs `init`/`open_raster`/`render_to_png` defined in Task 8 match their pytest callers in Tasks 8/10/11. Render job type is `QgsMapRendererSequentialJob` in Tasks 6–8, deliberately swapped to `QgsMapRendererParallelJob` in Task 9 Step 3 (noted at both sites).
