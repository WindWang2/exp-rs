# Georeferencer (几何校正) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a QGIS-style Georeferencer window for SICNU GEO RS with three modes (Image→Map / Image→Image / RPC), 8 transformation methods, GCP collection on twin canvases, GDAL warp output, and full TDD test coverage.

**Architecture:** Port QGIS's `src/analysis/georeferencing/` into a new `qgis_analysis` static library; port QGIS's `src/app/georeferencer/` into `src/app/georeferencer/`; reshape the main window layout (twin canvases + right dock + bottom GCP table) per `UI/design.html` `ArtboardGeoref`; add 4 new files (RPC transformer, twin-canvas sync controller, RMS scatter widget, mode toggle).

**Tech Stack:** C++17 / Qt6 (QMainWindow, QDockWidget, QPainter) / Catch2 / GDAL ≥ 3.4 (`gdal_alg.h`, `GDALCreateRPCTransformer`) / Eigen (least squares) / vendored QGIS analysis + core.

**Spec:** `docs/superpowers/specs/2026-06-02-georeferencer-design.md`

**Upstream references:** Files quoted as `qgis_ref/src/...` are vendored QGIS source; port them with the recipe in Task 1 (rename includes, drop SIP, replace `QgisApp::instance()` with injected interface).

---

## Conventions for All Tasks

- **TDD cycle:** Red → Green → Refactor. Every step has a verify command with expected output.
- **Naming:** Ported QGIS classes keep `Qgs*` prefix; new project classes use `Rs*`.
- **Build command:** `cd build && cmake .. && make -j$(nproc)` (incremental); fail = stop, do not proceed.
- **Test command:** `cd build && ctest --output-on-failure -R <CatchTestCaseName>` — note `-R` matches Catch2 TEST_CASE names (registered via `catch_discover_tests`), not binary names.
- **Commit prefix:** `feat(georef):` for new behavior, `test(georef):` for test-only, `chore(georef):` for build/CMake.
- **After every Step that says "Commit":** run `git status`; verify only intended files staged; commit with the message shown.

## Lessons from Task 1 (apply throughout)

**Plan code snippets used illustrative APIs; the real upstream APIs differ.** Adapt every test snippet in this plan to the actual signatures discovered in `qgis_ref/src/analysis/georeferencing/`:

1. **`QgsGcpTransformerInterface::createFromParameters` takes 3 args** (method, source vector, destination vector), not 1 — the factory both constructs and fits in one call.
2. **`QgsGcpTransformerInterface::transform(double &x, double &y, bool inverse)`** is in-place (3 args), not the 5-arg form some plan snippets show. To produce `fx,fy` from `x,y`: copy inputs to mutable locals, call the in-place transform.
3. **`minimumGcpCount` is a virtual instance method**, not static. Construct a temporary transformer for the method, then call. Note `ThinPlateSpline` returns `1`, not `3`.
4. **GCP vector type is `QVector<QgsPointXY>`**, not `std::vector<QgsPointXY>`.
5. **`QgsLeastSquares::linear` is 5 args** (src, dst, origin, pixelXSize, pixelYSize) — no rotation arg.
6. **Helmert requires GSL.** `qgsconfig.h` shows `HAVE_GSL` undefined; calls throw `QgsNotSupportedException`. Do NOT add Helmert assertions until GSL is wired. Linear and PolynomialOrder1 are the safe choices for early tests.
7. **`SingularException` triggers on `deltaX == 0 || deltaY == 0`**. A test input like `{{0,0},{1,1},{2,2}}` is NOT singular (x and y both vary); use `{{0,0},{0,1},{0,2}}` (all x equal) to force the throw.

**Port recipe correction.** The greedy sed in step 1.4 corrupts `#ifndef SIP_RUN`, `#ifdef SIP_RUN`, multi-line `SIP_THROW(...)`, and `#define SIP_NO_FILE`. For all subsequent file ports use targeted strips:

```bash
# Inside src/<dest>/ for each ported file:
# 1. Strip whole-line SIP_ABSTRACT / SIP_NO_FILE markers
sed -i -E '/^[[:space:]]*SIP_(ABSTRACT|NO_FILE|EXPORT)[[:space:]]*$/d' "$f"
# 2. Strip inline SIP markers (single line, no nested parens)
sed -i -E 's/[[:space:]]+SIP_(INOUT|OUT|FACTORY|SKIP|TRANSFER|TRANSFERTHIS|TRANSFERBACK|KEEPREFERENCE|RELEASEGIL|HOLDGIL|PYNAME[(][^)]*[)]|VIRTUALERRORHANDLER[(][^)]*[)])//g' "$f"
# 3. Multi-line SIP_THROW( ... ) — use perl
perl -i -0pe 's/SIP_THROW\([^)]*\)//g' "$f"
# 4. Standard renames
sed -i 's/#include "qgis_analysis.h"/#include "qgis_analysis_export.h"/g' "$f"
sed -i 's/ANALYSIS_EXPORT/QGIS_ANALYSIS_EXPORT/g' "$f"
sed -i 's/APP_EXPORT//g' "$f"   # app-side files: APP_EXPORT not exported by this project
# 5. Keep SIP_RUN inside #if* directives (they're conditional compilation guards, not markers)
```

For app-side files (Tasks 2+), `qgis_core` include path must be added to the target's `target_include_directories` — `src/core/` headers are at the *root* of that dir (e.g. `#include "qgspointxy.h"`).

---

## Task 1: Port Analysis Library (`qgis_analysis`)

**Goal:** Stand up a new `qgis_analysis` static library with the 8 georeferencing algorithm files vendored from QGIS, verified by tests for transformers and least-squares.

**Files:**
- Create: `src/analysis/CMakeLists.txt`
- Create: `src/analysis/qgis_analysis_export.h`
- Create: `src/analysis/georeferencing/qgsgcppoint.h/.cpp` (from `qgis_ref/src/analysis/georeferencing/`)
- Create: `src/analysis/georeferencing/qgsgcptransformer.h/.cpp`
- Create: `src/analysis/georeferencing/qgsleastsquares.h/.cpp`
- Create: `src/analysis/georeferencing/qgsgcpgeometrytransformer.h/.cpp`
- Create: `src/analysis/georeferencing/qgsvectorwarper.h/.cpp`
- Modify: `CMakeLists.txt` (top-level) — `add_subdirectory(src/analysis)`
- Modify: `src/core/CMakeLists.txt` — leave alone (qgis_analysis depends on qgis_core, not vice versa)
- Test: `tests/test_gcp_transformer.cpp`
- Test: `tests/test_least_squares.cpp`
- Modify: `tests/CMakeLists.txt` — register two new test targets

### Port Recipe (apply to every ported file)

For each file copied from `qgis_ref/src/analysis/georeferencing/qgs*.h` and `.cpp`:

1. Replace `#include "qgis_analysis.h"` with `#include "qgis_analysis_export.h"`.
2. Replace export macro `ANALYSIS_EXPORT` with `QGIS_ANALYSIS_EXPORT`.
3. Remove all `SIP_*` macros and adjacent `// SIP_` comments (regex: `SIP_[A-Z_]+(\([^)]*\))?`).
4. Replace bare `#include "qgsXxx.h"` (assumed adjacent) with the project's vendored path `#include "qgsXxx.h"` from `src/core/` if available, else keep the upstream path. Add include dirs in CMake.
5. Leave `Qgs*` class names untouched.

### Steps

- [ ] **Step 1.1: Create the analysis export header**

Create `src/analysis/qgis_analysis_export.h`:

```cpp
#ifndef QGIS_ANALYSIS_EXPORT_H
#define QGIS_ANALYSIS_EXPORT_H

#if defined(_WIN32) || defined(_WIN64)
  #ifdef qgis_analysis_EXPORTS
    #define QGIS_ANALYSIS_EXPORT __declspec(dllexport)
  #else
    #define QGIS_ANALYSIS_EXPORT __declspec(dllimport)
  #endif
#else
  #define QGIS_ANALYSIS_EXPORT __attribute__((visibility("default")))
#endif

#endif
```

- [ ] **Step 1.2: Create `src/analysis/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)

find_package(GDAL 3.4 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(qgis_analysis STATIC
    georeferencing/qgsgcppoint.cpp
    georeferencing/qgsgcptransformer.cpp
    georeferencing/qgsleastsquares.cpp
    georeferencing/qgsgcpgeometrytransformer.cpp
    georeferencing/qgsvectorwarper.cpp
)

target_include_directories(qgis_analysis PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/georeferencing
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/geometry
    ${CMAKE_SOURCE_DIR}/src/core/proj
)

target_link_libraries(qgis_analysis PUBLIC
    qgis_core
    GDAL::GDAL
    Qt6::Core
)

target_compile_features(qgis_analysis PUBLIC cxx_std_17)
set_target_properties(qgis_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

- [ ] **Step 1.3: Wire analysis into top-level CMake**

In top-level `CMakeLists.txt`, after `add_subdirectory(src/core)` add:

```cmake
add_subdirectory(src/analysis)
```

Verify build: `cd build && cmake .. && make qgis_analysis -j$(nproc)`. Expected: target is empty so far (no sources copied), but configure must succeed. Actually since the CPP files don't exist yet, build will fail. Defer build until Step 1.5.

- [ ] **Step 1.4: Copy the 8 files from QGIS, apply port recipe**

```bash
mkdir -p src/analysis/georeferencing
cp qgis_ref/src/analysis/georeferencing/qgsgcppoint.{h,cpp} src/analysis/georeferencing/
cp qgis_ref/src/analysis/georeferencing/qgsgcptransformer.{h,cpp} src/analysis/georeferencing/
cp qgis_ref/src/analysis/georeferencing/qgsleastsquares.{h,cpp} src/analysis/georeferencing/
cp qgis_ref/src/analysis/georeferencing/qgsgcpgeometrytransformer.{h,cpp} src/analysis/georeferencing/
cp qgis_ref/src/analysis/georeferencing/qgsvectorwarper.{h,cpp} src/analysis/georeferencing/
```

Then for each copied file run the port recipe (sed):

```bash
cd src/analysis/georeferencing
for f in *.h *.cpp; do
  sed -i 's/#include "qgis_analysis.h"/#include "qgis_analysis_export.h"/g' "$f"
  sed -i 's/ANALYSIS_EXPORT/QGIS_ANALYSIS_EXPORT/g' "$f"
  sed -i -E 's/[[:space:]]*SIP_[A-Z_]+(\([^)]*\))?//g' "$f"
done
```

- [ ] **Step 1.5: Build the static library**

```bash
cd build && cmake .. && make qgis_analysis -j$(nproc)
```

Expected: `libqgis_analysis.a` builds; if missing includes from `qgis_core`, add the path to `target_include_directories` in `src/analysis/CMakeLists.txt`. Iterate until build is green.

- [ ] **Step 1.6: Write failing test for least-squares Polynomial1 fit**

Create `tests/test_least_squares.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "qgsleastsquares.h"
#include "qgspointxy.h"
#include <vector>

using Catch::Approx;

TEST_CASE("LeastSquares Polynomial1: pure translation +100,+200", "[georef][leastsquares]") {
    std::vector<QgsPointXY> src = {{0,0},{10,0},{0,10},{10,10}};
    std::vector<QgsPointXY> dst = {{100,200},{110,200},{100,210},{110,210}};
    QgsPointXY origin;
    double pixelXSize, pixelYSize, rotation;
    QgsLeastSquares::linear(src, dst, origin, pixelXSize, pixelYSize, rotation);
    REQUIRE(origin.x() == Approx(100.0).margin(1e-9));
    REQUIRE(origin.y() == Approx(200.0).margin(1e-9));
}

TEST_CASE("LeastSquares Polynomial1: throws on collinear GCPs", "[georef][leastsquares][error]") {
    std::vector<QgsPointXY> src = {{0,0},{1,1},{2,2},{3,3}};
    std::vector<QgsPointXY> dst = {{0,0},{2,2},{4,4},{6,6}};
    QgsPointXY origin;
    double a,b,c;
    REQUIRE_THROWS_AS(QgsLeastSquares::linear(src,dst,origin,a,b,c), QgsLeastSquares::SingularException);
}
```

Note: if upstream `QgsLeastSquares` doesn't throw `SingularException`, the second test will fail. Patch the class in step 1.8.

- [ ] **Step 1.7: Register test target**

In `tests/CMakeLists.txt`, after the last existing `add_executable(test_xxx ...)`:

```cmake
add_executable(test_least_squares test_least_squares.cpp)
target_link_libraries(test_least_squares PRIVATE qgis_analysis qgis_core Catch2::Catch2WithMain)
add_test(NAME test_least_squares COMMAND test_least_squares)
```

- [ ] **Step 1.8: Run test, expect FAIL**

```bash
cd build && cmake .. && make test_least_squares -j$(nproc) && ctest -R test_least_squares --output-on-failure
```

Expected: first test passes (upstream code already handles translation), second test fails (no `SingularException` class).

- [ ] **Step 1.9: Add `SingularException` to `qgsleastsquares.h`**

In `src/analysis/georeferencing/qgsleastsquares.h`, inside the `QgsLeastSquares` class:

```cpp
public:
    class SingularException : public std::runtime_error {
      public:
        SingularException() : std::runtime_error("GCP system is singular (collinear or duplicate points)") {}
    };
```

Add `#include <stdexcept>` at top.

In `qgsleastsquares.cpp`, in each linear-system solve path (e.g. inside `linear`, `polynomial`, `projective`), wrap the Eigen / matrix solve and check the determinant or rank; if singular, `throw SingularException()`. Add near the existing solve:

```cpp
// existing: e.g. matrix.fullPivLu().solve(rhs) or normal equations
Eigen::FullPivLU<Eigen::MatrixXd> lu(A);
if (!lu.isInvertible() || lu.rank() < A.cols()) {
    throw SingularException();
}
Eigen::VectorXd x = lu.solve(rhs);
```

(Adjust to match upstream's actual solve API; the test only requires the throw happens for collinear input.)

- [ ] **Step 1.10: Run test, expect PASS**

```bash
make test_least_squares -j$(nproc) && ctest -R test_least_squares --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 1.11: Write failing test for GCP transformer (all 7 methods)**

Create `tests/test_gcp_transformer.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "qgsgcptransformer.h"
#include "qgspointxy.h"
#include <memory>

using Catch::Approx;
using TM = QgsGcpTransformerInterface::TransformMethod;

namespace {
struct GcpPair { double sx,sy,dx,dy; };

std::vector<QgsPointXY> sources(const std::vector<GcpPair>& g) {
    std::vector<QgsPointXY> r; for (auto& p:g) r.emplace_back(p.sx,p.sy); return r;
}
std::vector<QgsPointXY> destinations(const std::vector<GcpPair>& g) {
    std::vector<QgsPointXY> r; for (auto& p:g) r.emplace_back(p.dx,p.dy); return r;
}
}

TEST_CASE("minimumGcpCount per method", "[georef][transformer]") {
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(TM::Linear) == 2);
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(TM::Helmert) == 2);
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(TM::PolynomialOrder1) == 3);
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(TM::PolynomialOrder2) == 6);
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(TM::PolynomialOrder3) == 10);
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(TM::ThinPlateSpline) == 3);
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(TM::Projective) == 4);
}

TEST_CASE("Polynomial1: pure translation roundtrip", "[georef][transformer]") {
    std::vector<GcpPair> g = {{0,0,100,200},{10,0,110,200},{0,10,100,210}};
    auto t = QgsGcpTransformerInterface::createFromParameters(TM::PolynomialOrder1);
    REQUIRE(t->updateParametersFromGcps(sources(g), destinations(g), false));
    double fx=0,fy=0;
    REQUIRE(t->transform(5.0, 5.0, fx, fy, false));
    REQUIRE(fx == Approx(105.0).margin(1e-6));
    REQUIRE(fy == Approx(205.0).margin(1e-6));
}

TEST_CASE("Helmert: rotation 90deg + translation", "[georef][transformer]") {
    std::vector<GcpPair> g = {{0,0,0,0},{10,0,0,10}};
    auto t = QgsGcpTransformerInterface::createFromParameters(TM::Helmert);
    REQUIRE(t->updateParametersFromGcps(sources(g), destinations(g), false));
    double fx=0,fy=0;
    REQUIRE(t->transform(5.0, 0.0, fx, fy, false));
    REQUIRE(fx == Approx(0.0).margin(1e-6));
    REQUIRE(fy == Approx(5.0).margin(1e-6));
}
```

- [ ] **Step 1.12: Register test target**

```cmake
add_executable(test_gcp_transformer test_gcp_transformer.cpp)
target_link_libraries(test_gcp_transformer PRIVATE qgis_analysis qgis_core Catch2::Catch2WithMain)
add_test(NAME test_gcp_transformer COMMAND test_gcp_transformer)
```

- [ ] **Step 1.13: Run, fix any port errors, until PASS**

```bash
make test_gcp_transformer -j$(nproc) && ctest -R test_gcp_transformer --output-on-failure
```

Iterate: typical errors are missing include paths from `qgis_core`; add to `src/analysis/CMakeLists.txt`'s `target_include_directories`.

- [ ] **Step 1.14: Commit**

```bash
git add src/analysis/ tests/test_gcp_transformer.cpp tests/test_least_squares.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(georef): port QGIS analysis library with GCP transformer + least-squares

- Add qgis_analysis static lib (GDAL >= 3.4, links qgis_core)
- Port 5 georeferencing files from upstream (drop SIP, rename includes)
- Add QgsLeastSquares::SingularException for collinear GCPs
- Tests: 7 transformer methods + Polynomial1/Helmert fit + singular throw

Task 11.4.1 of Phase 11.4 Georeferencer (see specs/2026-06-02-georeferencer-design.md)"
```

---

## Task 2: Port QgsImageWarper with Failure + Cancel Paths

**Goal:** GDAL warp wrapper that reports progress, can be cancelled, and surfaces three distinct failure modes (disk full, input vanished, CE_Warning).

**Files:**
- Create: `src/app/georeferencer/qgsimagewarper.h/.cpp` (from `qgis_ref/src/app/georeferencer/`)
- Test: `tests/test_image_warper.cpp` (happy path + golden)
- Test: `tests/test_image_warper_cancel.cpp`
- Test: `tests/test_image_warper_errors.cpp`
- Test: `tests/test_warp_crs_passthrough.cpp`
- Test data: `tests/data/georef/synthetic_64x64.tif` (created by test fixture)
- Test data: `tests/data/georef/golden_warp_translation.tif` (created once in step 2.7)
- Modify: `tests/CMakeLists.txt`
- Modify: `src/app/CMakeLists.txt` — `app/georeferencer/` source list (created in Task 4; for now extend incrementally)

### Steps

- [ ] **Step 2.1: Create `src/app/georeferencer/` directory and port `qgsimagewarper`**

```bash
mkdir -p src/app/georeferencer
cp qgis_ref/src/app/georeferencer/qgsimagewarper.{h,cpp} src/app/georeferencer/
```

Apply port recipe (same as Task 1) plus:
- Replace `#include "qgis.h"` and unresolved app-only headers; in particular, `qgis_app/qgisapp.h` references must be removed — `QgsImageWarper` does not need `QgisApp`.
- Ensure `#include "qgsgcptransformer.h"` resolves through the analysis lib.

- [ ] **Step 2.2: Add explicit failure-mode enum to header**

Edit `src/app/georeferencer/qgsimagewarper.h`, in the `QgsImageWarper` class:

```cpp
public:
    enum class WarpStatus {
        Ok,
        DiskFull,
        InputUnavailable,
        SingularTransform,
        Cancelled,
        GdalError
    };
    struct WarpResult {
        WarpStatus status = WarpStatus::Ok;
        QString errorMessage;
        qint64 outputBytes = 0;
        int durationMs = 0;
    };
```

Change the existing `warpFile` return type from `int`/`bool` to `WarpResult` (or add a sibling that returns `WarpResult`).

- [ ] **Step 2.3: Implement failure detection in `warpFile`**

In `qgsimagewarper.cpp::warpFile`, around the existing GDAL call:

```cpp
QElapsedTimer timer; timer.start();
WarpResult result;
CPLErrorReset();

// pre-check input
GDALDatasetH src = GDALOpen(input.toUtf8().constData(), GA_ReadOnly);
if (!src) { result.status = WarpStatus::InputUnavailable;
            result.errorMessage = QStringLiteral("Cannot open source: %1").arg(input);
            return result; }

// ... existing warp setup ...

CPLErr err = GDALReprojectImage(/* ... */);

if (mFeedback && mFeedback->isCanceled()) {
    QFile::remove(output);
    result.status = WarpStatus::Cancelled;
    return result;
}

if (err == CE_Failure) {
    const char *msg = CPLGetLastErrorMsg();
    QString m = QString::fromUtf8(msg ? msg : "unknown");
    if (m.contains("No space left", Qt::CaseInsensitive) ||
        m.contains("ENOSPC", Qt::CaseInsensitive)) {
        result.status = WarpStatus::DiskFull;
    } else {
        result.status = WarpStatus::GdalError;
    }
    result.errorMessage = m;
    QFile::remove(output);
    return result;
}
// CE_Warning is non-fatal; record but continue

QFileInfo fi(output);
result.outputBytes = fi.size();
result.durationMs = static_cast<int>(timer.elapsed());
return result;
```

Add `#include <QElapsedTimer>` and `#include <QFile>` and `#include <QFileInfo>` at the top.

- [ ] **Step 2.4: Hook cancellation into the GDAL progress callback**

The existing progress callback (look for `static int CPL_STDCALL warperProgressFunc(...)` or similar) should be replaced/augmented:

```cpp
static int CPL_STDCALL warperProgressFunc(double dfComplete, const char*, void *pProgressArg) {
    auto *fb = static_cast<QgsFeedback*>(pProgressArg);
    if (fb) {
        fb->setProgress(dfComplete * 100.0);
        if (fb->isCanceled()) return FALSE;
    }
    return TRUE;
}
```

Pass `mFeedback` as `pProgressArg` when invoking `GDALReprojectImage`.

- [ ] **Step 2.5: Wire test data generator**

Create `tests/data/georef/CMakeLists.txt` (or extend `tests/CMakeLists.txt`):

```cmake
# Generated at test runtime by helper, not committed
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/data/georef)
```

In `tests/test_image_warper.cpp`, top of file:

```cpp
#include "warper_test_helpers.h"
// helper creates a 64x64 raster with 8-bit gray ramp and writes to <tmp>/synthetic_64x64.tif
```

Create `tests/warper_test_helpers.h`:

```cpp
#pragma once
#include <QString>
#include <QTemporaryDir>
#include <gdal_priv.h>

inline QString makeSynthetic64Raster(const QString& dir) {
    GDALAllRegister();
    QString path = dir + "/synthetic_64x64.tif";
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    GDALDataset* ds = drv->Create(path.toUtf8().constData(), 64, 64, 1, GDT_Byte, nullptr);
    std::vector<uint8_t> row(64);
    for (int y=0; y<64; ++y) {
        for (int x=0; x<64; ++x) row[x] = static_cast<uint8_t>((x+y) % 256);
        ds->GetRasterBand(1)->RasterIO(GF_Write, 0, y, 64, 1, row.data(), 64, 1, GDT_Byte, 0, 0);
    }
    GDALClose(ds);
    return path;
}
```

- [ ] **Step 2.6: Write the happy-path warp test**

Create `tests/test_image_warper.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "qgsimagewarper.h"
#include "qgsgcptransformer.h"
#include "qgsfeedback.h"
#include "warper_test_helpers.h"
#include <QTemporaryDir>
#include <gdal_priv.h>

using Catch::Approx;
using TM = QgsGcpTransformerInterface::TransformMethod;

TEST_CASE("ImageWarper: pure translation produces correct GeoTransform", "[georef][warper]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString src = makeSynthetic64Raster(tmp.path());
    QString out = tmp.path() + "/out.tif";

    // 4 GCPs: (px,py) -> (px+100, py+200)
    std::vector<QgsPointXY> srcPts = {{0,0},{63,0},{0,63},{63,63}};
    std::vector<QgsPointXY> dstPts = {{100,200},{163,200},{100,263},{163,263}};

    auto transform = std::make_unique<QgsGeorefTransform>(TM::PolynomialOrder1);
    REQUIRE(transform->updateParametersFromGcps(srcPts, dstPts, false));

    QgsFeedback fb;
    QgsImageWarper warper(&fb);
    auto result = warper.warpFile(
        src, out, transform.get(),
        QgsImageWarper::ResamplingMethod::NearestNeighbour,
        false /*compress*/, false /*zeroTransparent*/,
        QgsCoordinateReferenceSystem("EPSG:4326"),
        QSize() /*output size auto*/, 1.0, 1.0
    );

    REQUIRE(result.status == QgsImageWarper::WarpStatus::Ok);

    GDALAllRegister();
    GDALDataset* ds = (GDALDataset*) GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds);
    double gt[6]; ds->GetGeoTransform(gt);
    REQUIRE(gt[0] == Approx(100.0).margin(1.0));   // X origin
    REQUIRE(gt[3] == Approx(263.0).margin(1.0));   // Y origin (top)
    GDALClose(ds);
}
```

Note: `QgsGeorefTransform` is the app-side wrapper — port it as part of this task (file: `src/app/georeferencer/qgsgeoreftransform.{h,cpp}`). Apply port recipe; it composes `QgsGcpTransformerInterface`.

- [ ] **Step 2.7: Register test, build, capture golden output**

```cmake
add_executable(test_image_warper test_image_warper.cpp)
target_link_libraries(test_image_warper PRIVATE
    qgis_app_georef qgis_analysis qgis_core
    Qt6::Core Qt6::Gui GDAL::GDAL Catch2::Catch2WithMain)
add_test(NAME test_image_warper COMMAND test_image_warper)
```

`qgis_app_georef` is created in Task 4 — for now define a slim CMake static lib in `src/app/georeferencer/CMakeLists.txt`:

```cmake
add_library(qgis_app_georef STATIC
    qgsimagewarper.cpp
    qgsgeoreftransform.cpp
)
target_link_libraries(qgis_app_georef PUBLIC qgis_analysis qgis_core Qt6::Core GDAL::GDAL)
target_include_directories(qgis_app_georef PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

In top-level CMake after analysis: `add_subdirectory(src/app/georeferencer)` — but app integration happens in Task 4; for now this static lib stands alone.

Build & run:

```bash
cd build && cmake .. && make test_image_warper -j$(nproc) && ctest -R test_image_warper --output-on-failure
```

Expected: PASS. The output GeoTransform must show X origin near 100, Y origin near 263.

- [ ] **Step 2.8: Write the cancel test**

Create `tests/test_image_warper_cancel.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "qgsimagewarper.h"
#include "qgsgeoreftransform.h"
#include "qgsfeedback.h"
#include "warper_test_helpers.h"
#include <QTemporaryDir>
#include <QtConcurrent>
#include <QElapsedTimer>

TEST_CASE("ImageWarper: cancellation exits within 500ms and removes output", "[georef][warper][cancel]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    // Larger raster so warp doesn't finish instantly
    QString src = makeSyntheticRaster(tmp.path(), 2048, 2048);
    QString out = tmp.path() + "/out.tif";

    auto transform = std::make_unique<QgsGeorefTransform>(QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1);
    std::vector<QgsPointXY> s = {{0,0},{2047,0},{0,2047},{2047,2047}};
    std::vector<QgsPointXY> d = {{100,200},{2147,200},{100,2247},{2147,2247}};
    transform->updateParametersFromGcps(s, d, false);

    QgsFeedback fb;
    QgsImageWarper warper(&fb);

    auto fut = QtConcurrent::run([&]{
        return warper.warpFile(src, out, transform.get(),
            QgsImageWarper::ResamplingMethod::Cubic, false, false,
            QgsCoordinateReferenceSystem("EPSG:4326"), QSize(), 1.0, 1.0);
    });

    QThread::msleep(200);
    QElapsedTimer t; t.start();
    fb.cancel();
    auto result = fut.result();
    qint64 exitMs = t.elapsed();

    REQUIRE(result.status == QgsImageWarper::WarpStatus::Cancelled);
    REQUIRE(exitMs <= 500);
    REQUIRE_FALSE(QFile::exists(out));
}
```

Add to `warper_test_helpers.h` an overload `makeSyntheticRaster(dir, w, h)`.

- [ ] **Step 2.9: Register, build, run cancel test**

```cmake
add_executable(test_image_warper_cancel test_image_warper_cancel.cpp)
target_link_libraries(test_image_warper_cancel PRIVATE qgis_app_georef qgis_analysis qgis_core Qt6::Core Qt6::Concurrent GDAL::GDAL Catch2::Catch2WithMain)
add_test(NAME test_image_warper_cancel COMMAND test_image_warper_cancel)
```

Run: `make test_image_warper_cancel && ctest -R test_image_warper_cancel --output-on-failure`. Expected: PASS.

- [ ] **Step 2.10: Write failure-path test (read-only output + singular GCPs)**

Create `tests/test_image_warper_errors.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "qgsimagewarper.h"
#include "qgsgeoreftransform.h"
#include "qgsfeedback.h"
#include "warper_test_helpers.h"
#include <QTemporaryDir>

TEST_CASE("ImageWarper: read-only output path returns GdalError", "[georef][warper][error]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString src = makeSynthetic64Raster(tmp.path());
    QString out = "/dev/null/nope/out.tif"; // unwritable

    auto transform = std::make_unique<QgsGeorefTransform>(QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1);
    std::vector<QgsPointXY> s = {{0,0},{63,0},{0,63}};
    std::vector<QgsPointXY> d = {{100,200},{163,200},{100,263}};
    transform->updateParametersFromGcps(s, d, false);

    QgsFeedback fb;
    QgsImageWarper warper(&fb);
    auto result = warper.warpFile(src, out, transform.get(),
        QgsImageWarper::ResamplingMethod::NearestNeighbour, false, false,
        QgsCoordinateReferenceSystem("EPSG:4326"), QSize(), 1.0, 1.0);

    REQUIRE((result.status == QgsImageWarper::WarpStatus::GdalError ||
             result.status == QgsImageWarper::WarpStatus::DiskFull));
    REQUIRE_FALSE(result.errorMessage.isEmpty());
}

TEST_CASE("Transform fit: 4 collinear GCPs throws SingularException", "[georef][transformer][error]") {
    auto t = QgsGcpTransformerInterface::createFromParameters(
        QgsGcpTransformerInterface::TransformMethod::PolynomialOrder2);
    std::vector<QgsPointXY> s = {{0,0},{1,1},{2,2},{3,3},{4,4},{5,5}};
    std::vector<QgsPointXY> d = {{0,0},{2,2},{4,4},{6,6},{8,8},{10,10}};
    REQUIRE_THROWS_AS(t->updateParametersFromGcps(s, d, false),
                      QgsLeastSquares::SingularException);
}
```

Register, build, run: expect PASS.

- [ ] **Step 2.11: Write CRS pass-through test**

Create `tests/test_warp_crs_passthrough.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "qgsimagewarper.h"
#include "qgsgeoreftransform.h"
#include "qgsfeedback.h"
#include "warper_test_helpers.h"
#include <QTemporaryDir>
#include <gdal_priv.h>

using Catch::Approx;

TEST_CASE("ImageWarper: source==target CRS does not reproject", "[georef][warper][crs]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString src = makeSynthetic64RasterWithCrs(tmp.path(), "EPSG:32650");
    QString out = tmp.path() + "/out.tif";

    auto transform = std::make_unique<QgsGeorefTransform>(QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1);
    std::vector<QgsPointXY> s = {{0,0},{63,0},{0,63}};
    std::vector<QgsPointXY> d = {{500,1000},{563,1000},{500,1063}};
    transform->updateParametersFromGcps(s, d, false);

    QgsFeedback fb;
    QgsImageWarper warper(&fb);
    auto result = warper.warpFile(src, out, transform.get(),
        QgsImageWarper::ResamplingMethod::NearestNeighbour, false, false,
        QgsCoordinateReferenceSystem("EPSG:32650"), QSize(), 1.0, 1.0);

    REQUIRE(result.status == QgsImageWarper::WarpStatus::Ok);

    GDALAllRegister();
    GDALDataset* ds = (GDALDataset*) GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds);
    double gt[6]; ds->GetGeoTransform(gt);
    REQUIRE(gt[0] == Approx(500.0).margin(1.0));
    REQUIRE(gt[3] == Approx(1063.0).margin(1.0));
    GDALClose(ds);
}
```

Add `makeSynthetic64RasterWithCrs` to helpers (sets GeoTransform + ProjectionRef on a 64x64 synthetic).

Register, build, run: expect PASS.

- [ ] **Step 2.12: Commit**

```bash
git add src/app/georeferencer/qgsimagewarper.{h,cpp} src/app/georeferencer/qgsgeoreftransform.{h,cpp} \
        src/app/georeferencer/CMakeLists.txt \
        tests/test_image_warper.cpp tests/test_image_warper_cancel.cpp \
        tests/test_image_warper_errors.cpp tests/test_warp_crs_passthrough.cpp \
        tests/warper_test_helpers.h tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(georef): GDAL warp with cancel + 3 failure modes + CRS passthrough

- Port QgsImageWarper and QgsGeorefTransform from upstream
- Add WarpResult struct (Ok/DiskFull/InputUnavailable/SingularTransform/Cancelled/GdalError)
- Wire QgsFeedback into GDAL progress callback for cooperative cancel
- 4 tests: happy path, cancel<=500ms, error paths, CRS passthrough

Task 11.4.2"
```

---

## Task 3: GCP List + `.points` v2 Persistence

**Goal:** GCP data model with type field, signal emission on edits, residual recomputation, and forward-compatible `.points` file (v1 read, v2 write).

**Files:**
- Create: `src/app/georeferencer/qgsgcplist.h/.cpp` (port + extend)
- Create: `src/app/georeferencer/qgsgcplistmodel.h/.cpp` (port)
- Modify: `src/analysis/georeferencing/qgsgcppoint.h/.cpp` — add `mPointType` field
- Test: `tests/test_gcp_list.cpp`
- Test: `tests/test_gcp_points_file.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/app/georeferencer/CMakeLists.txt`

### Steps

- [ ] **Step 3.1: Add `pointType` to `QgsGcpPoint`**

In `src/analysis/georeferencing/qgsgcppoint.h`, inside `QgsGcpPoint`:

```cpp
public:
    QString pointType() const { return mPointType; }
    void setPointType(const QString &type) { mPointType = type; }

private:
    QString mPointType;
```

Add `#include <QString>` if missing. Update both constructors (default + parametrized) to leave `mPointType` empty by default.

- [ ] **Step 3.2: Write failing test for `.points` file v2 round-trip**

Create `tests/test_gcp_points_file.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "qgsgcplist.h"
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

TEST_CASE("GCP .points v2: write+read round-trip preserves type", "[georef][points]") {
    QgsGCPList list;
    {
        QgsGcpPoint p(QgsPointXY(100,200), QgsPointXY(400000,4280000),
                      QgsCoordinateReferenceSystem("EPSG:32650"), true);
        p.setPointType("road");
        list.appendPoint(p);
    }
    {
        QgsGcpPoint p(QgsPointXY(300,400), QgsPointXY(401000,4281000),
                      QgsCoordinateReferenceSystem("EPSG:32650"), false);
        p.setPointType("bridge");
        list.appendPoint(p);
    }

    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString path = tmp.path() + "/round.points";
    REQUIRE(list.saveGcps(path));

    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    QString head = QTextStream(&f).readLine();
    REQUIRE(head == "# QGEOS .points v2");
    f.close();

    QgsGCPList loaded;
    QgsCoordinateReferenceSystem destCrs;
    REQUIRE(loaded.loadGcps(path, destCrs));
    REQUIRE(loaded.size() == 2);
    REQUIRE(loaded.at(0)->pointType() == "road");
    REQUIRE(loaded.at(1)->pointType() == "bridge");
    REQUIRE(loaded.at(0)->isEnabled());
    REQUIRE_FALSE(loaded.at(1)->isEnabled());
}

TEST_CASE("GCP .points v1: legacy file without header reads with empty type", "[georef][points]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString path = tmp.path() + "/legacy.points";
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    QTextStream out(&f);
    out << "mapX,mapY,pixelX,pixelY,enable,dX,dY,residual\n";
    out << "400000,4280000,100,-200,1,0,0,0\n";
    f.close();

    QgsGCPList loaded;
    QgsCoordinateReferenceSystem destCrs;
    REQUIRE(loaded.loadGcps(path, destCrs));
    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded.at(0)->pointType().isEmpty());
}
```

Build & run: FAIL (loadGcps/saveGcps don't yet handle type column or v2 header).

- [ ] **Step 3.3: Port `qgsgcplist.h/.cpp` and add v2 read/write**

Port from `qgis_ref/src/app/georeferencer/qgsgcplist.{h,cpp}` using the recipe.

In `saveGcps(const QString &path)`:

```cpp
QFile f(path);
if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
QTextStream out(&f);
out << "# QGEOS .points v2\n";
out << "mapX,mapY,pixelX,pixelY,enable,dX,dY,residual,pointType\n";
for (const auto *p : *this) {
    out << QString::number(p->destinationPoint().x(),'f',8) << ","
        << QString::number(p->destinationPoint().y(),'f',8) << ","
        << QString::number(p->sourcePoint().x(),'f',6) << ","
        << QString::number(-p->sourcePoint().y(),'f',6) << ","
        << (p->isEnabled()?1:0) << ","
        << "0,0,0,"
        << p->pointType() << "\n";
}
return true;
```

In `loadGcps(const QString &path, QgsCoordinateReferenceSystem &destCrs)`:

```cpp
QFile f(path);
if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
QTextStream in(&f);
bool v2 = false;
QString first = in.readLine();
if (first.startsWith("# QGEOS .points v2")) {
    v2 = true;
    in.readLine(); // header row
} else {
    // first line was the header; rewind processing
    // do nothing — fall through to consume as data lines below
    // but we just consumed the header; treat it as v1 header
}

while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.isEmpty()) continue;
    QStringList cols = line.split(',');
    if (cols.size() < 5) continue;
    double mx = cols[0].toDouble();
    double my = cols[1].toDouble();
    double px = cols[2].toDouble();
    double py = -cols[3].toDouble();
    bool enabled = cols[4].toInt() != 0;
    QString type = (v2 && cols.size() >= 9) ? cols[8] : QString();
    QgsGcpPoint p(QgsPointXY(px, py), QgsPointXY(mx, my), destCrs, enabled);
    p.setPointType(type);
    appendPoint(p);
}
return true;
```

- [ ] **Step 3.4: Run tests, iterate, expect PASS**

```bash
make test_gcp_points_file -j$(nproc) && ctest -R test_gcp_points_file --output-on-failure
```

- [ ] **Step 3.5: Write failing test for GCP list signals + residuals**

Create `tests/test_gcp_list.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QSignalSpy>
#include "qgsgcplist.h"
#include "qgsgeoreftransform.h"

TEST_CASE("GCPList: appendPoint emits changed", "[georef][gcplist]") {
    QgsGCPList list;
    QSignalSpy spy(&list, &QgsGCPList::changed);
    QgsGcpPoint p(QgsPointXY(10,20), QgsPointXY(100,200),
                  QgsCoordinateReferenceSystem("EPSG:32650"), true);
    list.appendPoint(p);
    REQUIRE(spy.count() == 1);
}

TEST_CASE("GCPList: updateResiduals skips disabled points", "[georef][gcplist]") {
    QgsGCPList list;
    QgsCoordinateReferenceSystem crs("EPSG:32650");
    list.appendPoint(QgsGcpPoint(QgsPointXY(0,0), QgsPointXY(0,0), crs, true));
    list.appendPoint(QgsGcpPoint(QgsPointXY(10,0), QgsPointXY(10,0), crs, true));
    list.appendPoint(QgsGcpPoint(QgsPointXY(0,10), QgsPointXY(0,10), crs, true));
    list.appendPoint(QgsGcpPoint(QgsPointXY(99,99), QgsPointXY(0,0), crs, false)); // outlier disabled

    auto transform = std::make_unique<QgsGeorefTransform>(
        QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1);
    list.updateResiduals(transform.get(), crs, crs);

    // first 3 should have ~zero residual; 4th (disabled) skipped
    REQUIRE(std::abs(list.at(0)->residual().x()) < 1e-6);
    REQUIRE(std::abs(list.at(3)->residual().x()) < 1e-6); // not updated; init is zero
}
```

Register and run: PASS (signal + skip-disabled already in ported code).

- [ ] **Step 3.6: Commit**

```bash
git add src/analysis/georeferencing/qgsgcppoint.{h,cpp} \
        src/app/georeferencer/qgsgcplist.{h,cpp} src/app/georeferencer/qgsgcplistmodel.{h,cpp} \
        tests/test_gcp_list.cpp tests/test_gcp_points_file.cpp \
        tests/CMakeLists.txt src/app/georeferencer/CMakeLists.txt
git commit -m "feat(georef): GCP list with type field and .points v2 format

- Add pointType field to QgsGcpPoint (road/bridge/etc.)
- saveGcps writes '# QGEOS .points v2' header + pointType column
- loadGcps detects v1 vs v2; v1 reads type as empty
- Tests: round-trip with type, legacy v1 fallback, signal emission, residual skip

Task 11.4.3"
```

---

## Task 4: Main Window Shell + Raster Menu Wiring

**Goal:** Empty `QgsGeoreferencerMainWindow` with menu bar, three toolbars (mode toggle, GCP ops, navigation), status bar, and a "Georeferencer..." action under the main app's Raster menu. No canvases yet (Task 5) and no GCP table (Task 6) — just the skeleton.

**Files:**
- Create: `src/app/georeferencer/qgsgeoreferencermainwindow.h/.cpp`
- Create: `src/app/georeferencer/rs_georef_mode_toggle.h/.cpp`
- Modify: `src/app/main_window.h/.cpp` — add `openGeoreferencer()` slot and Raster menu entry
- Modify: `src/app/CMakeLists.txt` — link `qgis_app_georef`
- Modify: `resources/icons.qrc` — register `georeferencer/*.svg`
- Modify: `src/app/georeferencer/CMakeLists.txt`
- Test: `tests/test_georef_window.cpp` (skeleton smoke)

### Steps

- [ ] **Step 4.1: Create `rs_georef_mode_toggle.h`**

```cpp
#pragma once
#include <QWidget>
#include <QButtonGroup>

class RsGeorefModeToggle : public QWidget {
    Q_OBJECT
  public:
    enum Mode { ImageToMap, ImageToImage, RpcPhysical };
    Q_ENUM(Mode)

    explicit RsGeorefModeToggle(QWidget *parent = nullptr);
    Mode currentMode() const { return mMode; }
    void setMode(Mode m);

  signals:
    void modeChanged(Mode newMode);

  private:
    Mode mMode = ImageToMap;
    QButtonGroup *mGroup = nullptr;
};
```

- [ ] **Step 4.2: Create `rs_georef_mode_toggle.cpp`**

```cpp
#include "rs_georef_mode_toggle.h"
#include <QHBoxLayout>
#include <QPushButton>

RsGeorefModeToggle::RsGeorefModeToggle(QWidget *parent)
    : QWidget(parent), mGroup(new QButtonGroup(this)) {
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(2,2,2,2);
    lay->setSpacing(1);

    const QStringList labels = { tr("Image → Map"), tr("Image → Image"), tr("RPC 物理模型") };
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QPushButton(labels[i], this);
        btn->setCheckable(true);
        btn->setObjectName(QStringLiteral("rsModeBtn"));
        btn->setProperty("modeIndex", i);
        if (i == 0) btn->setChecked(true);
        mGroup->addButton(btn, i);
        lay->addWidget(btn);
    }
    setObjectName(QStringLiteral("rsModeToggle"));
    setStyleSheet(R"(
        #rsModeToggle { background: var(--bg-1); border: 1px solid var(--line-2); border-radius: 4px; padding: 2px; }
        QPushButton#rsModeBtn { padding: 2px 12px; font-size: 11px; border: 1px solid transparent; border-radius: 3px; background: transparent; color: #5f6b7a; }
        QPushButton#rsModeBtn:checked { background: #ffffff; color: #208830; border: 1px solid #99c2a2; font-weight: 600; }
    )");

    connect(mGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id){
                mMode = static_cast<Mode>(id);
                emit modeChanged(mMode);
            });
}

void RsGeorefModeToggle::setMode(Mode m) {
    if (m == mMode) return;
    mMode = m;
    auto *btn = mGroup->button(static_cast<int>(m));
    if (btn) btn->setChecked(true);
    emit modeChanged(mMode);
}
```

- [ ] **Step 4.3: Create `qgsgeoreferencermainwindow.h` (skeleton)**

```cpp
#pragma once
#include <QMainWindow>
#include "rs_georef_mode_toggle.h"

class QToolBar;
class QStatusBar;
class QLabel;
class QgisInterface;

class QgsGeoreferencerMainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit QgsGeoreferencerMainWindow(QgisInterface *iface, QWidget *parent = nullptr);

  protected:
    void closeEvent(QCloseEvent *e) override;

  private:
    void setupMenus();
    void setupToolbars();
    void setupStatusBar();

    QgisInterface *mIface = nullptr;
    RsGeorefModeToggle *mModeToggle = nullptr;
    QToolBar *mModeBar = nullptr;
    QLabel *mRmsLabel = nullptr;
    QLabel *mCoordLabel = nullptr;
    QLabel *mCrsLabel = nullptr;
};
```

- [ ] **Step 4.4: Create `qgsgeoreferencermainwindow.cpp` (skeleton)**

```cpp
#include "qgsgeoreferencermainwindow.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QCloseEvent>
#include <QAction>

QgsGeoreferencerMainWindow::QgsGeoreferencerMainWindow(QgisInterface *iface, QWidget *parent)
    : QMainWindow(parent), mIface(iface) {
    setWindowTitle(tr("Georeferencer · 几何校正"));
    resize(1200, 800);
    setupMenus();
    setupToolbars();
    setupStatusBar();

    auto *placeholder = new QLabel(tr("[Canvas area placeholder — Task 5]"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}

void QgsGeoreferencerMainWindow::setupMenus() {
    auto *fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(tr("Open Raster..."), this, [](){});
    fileMenu->addAction(tr("Load .points..."), this, [](){});
    fileMenu->addAction(tr("Save .points..."), this, [](){});
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Close"), this, &QWidget::close);

    menuBar()->addMenu(tr("Edit"));
    menuBar()->addMenu(tr("View"));
    menuBar()->addMenu(tr("Settings"));
    menuBar()->addMenu(tr("Help"));
}

void QgsGeoreferencerMainWindow::setupToolbars() {
    mModeBar = addToolBar(tr("Mode"));
    mModeBar->setMovable(false);
    mModeToggle = new RsGeorefModeToggle(this);
    mModeBar->addWidget(mModeToggle);
    mModeBar->addSeparator();
    mModeBar->addAction(QIcon(":/icons/georef/add_point"), tr("Add GCP"), this, [](){});
    mModeBar->addAction(QIcon(":/icons/georef/del_point"), tr("Delete GCP"), this, [](){});
    mModeBar->addAction(QIcon(":/icons/georef/load"), tr("Load .gcp"), this, [](){});
    mModeBar->addAction(QIcon(":/icons/georef/export"), tr("Export .gcp"), this, [](){});
    mModeBar->addSeparator();
    mModeBar->addAction(QIcon(":/icons/georef/sync"), tr("Sync zoom"), this, [](){});
    mModeBar->addAction(QIcon(":/icons/georef/fit"), tr("Zoom to all"), this, [](){});
    auto *sift = mModeBar->addAction(QIcon(":/icons/georef/sift"), tr("Auto match (SIFT)"), this, [this](){
        // Phase 11.5 placeholder
        statusBar()->showMessage(tr("SIFT auto-match coming in Phase 11.5"), 3000);
    });
    sift->setObjectName(QStringLiteral("rsGeorefSiftAction"));
    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mModeBar->addWidget(spacer);
    mModeBar->addAction(QIcon(":/icons/georef/preview"), tr("Preview"), this, [](){});
    auto *apply = mModeBar->addAction(QIcon(":/icons/georef/apply"), tr("Apply"), this, [](){});
    apply->setObjectName(QStringLiteral("rsGeorefApplyAction"));
}

void QgsGeoreferencerMainWindow::setupStatusBar() {
    mCoordLabel = new QLabel(tr("—"), this);
    mCrsLabel = new QLabel(tr("CRS: —"), this);
    mRmsLabel = new QLabel(tr("RMS: —"), this);
    mRmsLabel->setObjectName(QStringLiteral("rsGeorefRmsLabel"));
    statusBar()->addWidget(mCoordLabel, 1);
    statusBar()->addPermanentWidget(mCrsLabel);
    statusBar()->addPermanentWidget(mRmsLabel);
}

void QgsGeoreferencerMainWindow::closeEvent(QCloseEvent *e) {
    // TODO Task 7: persist QgsSettings, ask about unsaved GCPs
    e->accept();
}
```

- [ ] **Step 4.5: Extend `src/app/georeferencer/CMakeLists.txt`**

```cmake
qt_add_library(qgis_app_georef STATIC
    qgsimagewarper.cpp
    qgsgeoreftransform.cpp
    qgsgcplist.cpp
    qgsgcplistmodel.cpp
    qgsgeoreferencermainwindow.cpp
    rs_georef_mode_toggle.cpp
)
target_link_libraries(qgis_app_georef PUBLIC
    qgis_analysis qgis_core
    Qt6::Core Qt6::Gui Qt6::Widgets
    GDAL::GDAL)
target_include_directories(qgis_app_georef PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(qgis_app_georef PROPERTIES AUTOMOC ON)
```

- [ ] **Step 4.6: Wire into main app**

In `src/app/main_window.h`, add:

```cpp
class QgsGeoreferencerMainWindow;
// ...
public slots:
    void openGeoreferencer();
private:
    QgsGeoreferencerMainWindow *m_georefWindow = nullptr;
```

In `src/app/main_window.cpp`:

```cpp
#include "georeferencer/qgsgeoreferencermainwindow.h"

void QgisDesktopWindow::openGeoreferencer() {
    if (!m_georefWindow) {
        m_georefWindow = new QgsGeoreferencerMainWindow(/*iface*/ nullptr, this);
        m_georefWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    m_georefWindow->show();
    m_georefWindow->raise();
    m_georefWindow->activateWindow();
}
```

In `setupMenu()` after `Mosaic...` line (around line 286):

```cpp
rasterMenu->addSeparator();
rasterMenu->addAction(QIcon(":/icons/r_ster_calc"), tr("Georeferencer..."),
                      this, &QgisDesktopWindow::openGeoreferencer);
```

(Use a placeholder icon for now; replace in Step 4.8.)

In `src/app/CMakeLists.txt`, link `qgis_app_georef`:

```cmake
target_link_libraries(sicnu_geo_rs PRIVATE qgis_app_georef ...)
```

And `add_subdirectory(georeferencer)` if not present.

- [ ] **Step 4.7: Write smoke test**

Create `tests/test_georef_window.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QToolBar>
#include "qgsgeoreferencermainwindow.h"
#include "rs_georef_mode_toggle.h"

namespace {
int fake_argc = 1;
char fake_argv0[] = "test";
char* fake_argv[] = {fake_argv0, nullptr};
}

TEST_CASE("GeorefMainWindow: constructs with mode toggle and Apply action", "[georef][window]") {
    QApplication app(fake_argc, fake_argv);
    QgsGeoreferencerMainWindow w(nullptr);
    REQUIRE(w.findChild<RsGeorefModeToggle*>() != nullptr);
    REQUIRE(w.findChild<QAction*>("rsGeorefApplyAction") != nullptr);
    REQUIRE(w.findChild<QAction*>("rsGeorefSiftAction") != nullptr);
    REQUIRE(w.findChild<QLabel*>("rsGeorefRmsLabel") != nullptr);
}

TEST_CASE("ModeToggle: switching emits modeChanged", "[georef][window][mode]") {
    QApplication app(fake_argc, fake_argv);
    RsGeorefModeToggle t;
    QSignalSpy spy(&t, &RsGeorefModeToggle::modeChanged);
    t.setMode(RsGeorefModeToggle::RpcPhysical);
    REQUIRE(spy.count() == 1);
    REQUIRE(t.currentMode() == RsGeorefModeToggle::RpcPhysical);
}
```

Register test target:

```cmake
add_executable(test_georef_window test_georef_window.cpp)
target_link_libraries(test_georef_window PRIVATE qgis_app_georef qgis_analysis qgis_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Catch2::Catch2WithMain)
set_target_properties(test_georef_window PROPERTIES AUTOMOC ON)
add_test(NAME test_georef_window COMMAND test_georef_window)
```

- [ ] **Step 4.8: Add georef icon resources**

Copy SVGs:

```bash
mkdir -p resources/svg-icons/georef
cp qgis_ref/images/themes/default/georeferencer/*.svg resources/svg-icons/georef/ 2>/dev/null || true
# If subset only, create flat add_point.svg / del_point.svg / load.svg / export.svg / sync.svg / fit.svg / sift.svg / preview.svg / apply.svg
```

In `resources/icons.qrc` add a new prefix or extend the existing one:

```xml
<qresource prefix="/icons/georef">
    <file alias="add_point">svg-icons/georef/add_point.svg</file>
    <file alias="del_point">svg-icons/georef/del_point.svg</file>
    <file alias="load">svg-icons/georef/load.svg</file>
    <file alias="export">svg-icons/georef/export.svg</file>
    <file alias="sync">svg-icons/georef/sync.svg</file>
    <file alias="fit">svg-icons/georef/fit.svg</file>
    <file alias="sift">svg-icons/georef/sift.svg</file>
    <file alias="preview">svg-icons/georef/preview.svg</file>
    <file alias="apply">svg-icons/georef/apply.svg</file>
</qresource>
```

- [ ] **Step 4.9: Build, run smoke test, hand-launch app**

```bash
cd build && make -j$(nproc) && ctest -R test_georef_window --output-on-failure
./sicnu_geo_rs &
# Menu: Raster -> Georeferencer... -> window opens with toolbar + status bar
```

Expected: tests pass; app's Raster menu shows "Georeferencer..."; clicking opens an empty-canvas Georeferencer window.

- [ ] **Step 4.10: Commit**

```bash
git add src/app/georeferencer/qgsgeoreferencermainwindow.{h,cpp} \
        src/app/georeferencer/rs_georef_mode_toggle.{h,cpp} \
        src/app/georeferencer/CMakeLists.txt \
        src/app/main_window.{h,cpp} src/app/CMakeLists.txt \
        resources/icons.qrc resources/svg-icons/georef/ \
        tests/test_georef_window.cpp tests/CMakeLists.txt
git commit -m "feat(georef): main window shell + Raster menu wiring

- QgsGeoreferencerMainWindow with mode toggle / toolbar / status bar
- RsGeorefModeToggle (Image→Map / Image→Image / RPC)
- Wire openGeoreferencer() into main Raster menu after Mosaic
- 9 georef icons registered in QRC
- Smoke tests: window constructs, mode toggle emits signal

Task 11.4.4"
```

---

## Task 5: Twin Canvas + `RsTwinCanvasSyncController`

**Goal:** Replace placeholder central widget with side-by-side `QgsMapCanvas` (SRC + REF) and a controller that keeps their `extent` in sync, with signal-storm throttling.

**Files:**
- Create: `src/app/georeferencer/rs_twincanvas_sync_controller.h/.cpp`
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.h/.cpp`
- Test: `tests/test_twincanvas_sync.cpp`

### Steps

- [ ] **Step 5.1: Create the sync controller header**

```cpp
#pragma once
#include <QObject>
#include <QTimer>

class QgsMapCanvas;
class QgsRectangle;

class RsTwinCanvasSyncController : public QObject {
    Q_OBJECT
  public:
    RsTwinCanvasSyncController(QgsMapCanvas *src, QgsMapCanvas *ref, QObject *parent = nullptr);
    bool isEnabled() const { return mEnabled; }
  public slots:
    void setEnabled(bool on);
  private slots:
    void onSrcExtentChanged();
    void onRefExtentChanged();
  private:
    QgsMapCanvas *mSrc = nullptr;
    QgsMapCanvas *mRef = nullptr;
    bool mEnabled = true;
    bool mApplying = false;          // reentrancy guard
    QTimer mThrottle;                // 16ms coalesce
    enum Pending { None, FromSrc, FromRef } mPending = None;
};
```

- [ ] **Step 5.2: Implement the sync controller**

```cpp
#include "rs_twincanvas_sync_controller.h"
#include <qgsmapcanvas.h>
#include <qgsrectangle.h>

RsTwinCanvasSyncController::RsTwinCanvasSyncController(QgsMapCanvas *src, QgsMapCanvas *ref, QObject *parent)
    : QObject(parent), mSrc(src), mRef(ref) {
    mThrottle.setSingleShot(true);
    mThrottle.setInterval(16);  // ~60 FPS coalesce
    connect(&mThrottle, &QTimer::timeout, this, [this](){
        if (!mEnabled || mPending == None) { mPending = None; return; }
        mApplying = true;
        if (mPending == FromSrc) mRef->setExtent(mSrc->extent());
        else                     mSrc->setExtent(mRef->extent());
        mRef->refresh();
        mSrc->refresh();
        mApplying = false;
        mPending = None;
    });
    connect(mSrc, &QgsMapCanvas::extentsChanged, this, &RsTwinCanvasSyncController::onSrcExtentChanged);
    connect(mRef, &QgsMapCanvas::extentsChanged, this, &RsTwinCanvasSyncController::onRefExtentChanged);
}

void RsTwinCanvasSyncController::setEnabled(bool on) {
    mEnabled = on;
    if (!on) { mThrottle.stop(); mPending = None; }
}

void RsTwinCanvasSyncController::onSrcExtentChanged() {
    if (!mEnabled || mApplying) return;
    mPending = FromSrc;
    mThrottle.start();
}

void RsTwinCanvasSyncController::onRefExtentChanged() {
    if (!mEnabled || mApplying) return;
    mPending = FromRef;
    mThrottle.start();
}
```

- [ ] **Step 5.3: Write failing test for sync controller**

Create `tests/test_twincanvas_sync.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <QApplication>
#include <QSignalSpy>
#include <QtTest>
#include <qgsmapcanvas.h>
#include "rs_twincanvas_sync_controller.h"

using Catch::Approx;
namespace { int fake_argc=1; char *fake_argv[]={(char*)"t",nullptr}; }

TEST_CASE("Twin canvas: src extent change propagates to ref", "[georef][sync]") {
    QApplication app(fake_argc, fake_argv);
    QgsMapCanvas src, ref;
    src.resize(400, 300); ref.resize(400, 300);
    RsTwinCanvasSyncController ctl(&src, &ref);

    src.setExtent(QgsRectangle(100, 100, 200, 200));
    QTest::qWait(50);  // allow throttle to fire

    REQUIRE(ref.extent().xMinimum() == Approx(100.0).margin(1.0));
    REQUIRE(ref.extent().xMaximum() == Approx(200.0).margin(1.0));
}

TEST_CASE("Twin canvas: disabled → no propagation", "[georef][sync]") {
    QApplication app(fake_argc, fake_argv);
    QgsMapCanvas src, ref;
    src.resize(400, 300); ref.resize(400, 300);
    RsTwinCanvasSyncController ctl(&src, &ref);
    ctl.setEnabled(false);
    auto originalRef = ref.extent();

    src.setExtent(QgsRectangle(500, 500, 600, 600));
    QTest::qWait(50);

    REQUIRE(ref.extent().xMinimum() == Approx(originalRef.xMinimum()).margin(1e-6));
}
```

Register test (`add_executable(test_twincanvas_sync ...)` linking `qgis_app_georef`, `Qt6::Test`).

Build & run: expect PASS.

- [ ] **Step 5.4: Replace placeholder central widget in main window**

In `qgsgeoreferencermainwindow.h`:

```cpp
#include "rs_twincanvas_sync_controller.h"
class QgsMapCanvas;
class QSplitter;
// ...
private:
    QgsMapCanvas *mSrcCanvas = nullptr;
    QgsMapCanvas *mRefCanvas = nullptr;
    RsTwinCanvasSyncController *mSyncCtl = nullptr;
```

In `qgsgeoreferencermainwindow.cpp`, replace the placeholder block:

```cpp
#include <QSplitter>
#include <qgsmapcanvas.h>

// in constructor, replace setCentralWidget(placeholder) with:
auto *split = new QSplitter(Qt::Horizontal, this);
mSrcCanvas = new QgsMapCanvas(this);
mSrcCanvas->setObjectName(QStringLiteral("rsSrcCanvas"));
mSrcCanvas->setCanvasColor(Qt::white);
mRefCanvas = new QgsMapCanvas(this);
mRefCanvas->setObjectName(QStringLiteral("rsRefCanvas"));
mRefCanvas->setCanvasColor(Qt::white);
split->addWidget(mSrcCanvas);
split->addWidget(mRefCanvas);
split->setStretchFactor(0, 1);
split->setStretchFactor(1, 1);
setCentralWidget(split);

mSyncCtl = new RsTwinCanvasSyncController(mSrcCanvas, mRefCanvas, this);
```

Wire the toolbar's "Sync zoom" action to toggle `mSyncCtl->setEnabled(...)`.

- [ ] **Step 5.5: Add `mSrcCanvas`/`mRefCanvas` smoke check to main-window test**

Extend `tests/test_georef_window.cpp`:

```cpp
TEST_CASE("GeorefMainWindow: has two QgsMapCanvas children with sync controller", "[georef][window]") {
    QApplication app(fake_argc, fake_argv);
    QgsGeoreferencerMainWindow w(nullptr);
    REQUIRE(w.findChild<QgsMapCanvas*>("rsSrcCanvas") != nullptr);
    REQUIRE(w.findChild<QgsMapCanvas*>("rsRefCanvas") != nullptr);
    REQUIRE(w.findChild<RsTwinCanvasSyncController*>() != nullptr);
}
```

Build & run: expect PASS.

- [ ] **Step 5.6: Hand-test in real app**

```bash
make -j$(nproc) && ./sicnu_geo_rs
# Open Raster -> Georeferencer; drag the source canvas — ref should follow within ~16ms
# Toggle "Sync zoom" off — drags should be independent
```

- [ ] **Step 5.7: Commit**

```bash
git add src/app/georeferencer/rs_twincanvas_sync_controller.{h,cpp} \
        src/app/georeferencer/qgsgeoreferencermainwindow.{h,cpp} \
        src/app/georeferencer/CMakeLists.txt \
        tests/test_twincanvas_sync.cpp tests/test_georef_window.cpp tests/CMakeLists.txt
git commit -m "feat(georef): twin canvas with 16ms-throttled sync controller

- RsTwinCanvasSyncController coalesces extentsChanged via single-shot timer
- Reentrancy guard prevents signal loop between SRC and REF canvases
- Wire Sync zoom toolbar action to enable/disable
- Tests: extent propagation, disabled state isolation

Task 11.4.5"
```

---

## Task 6: GCP Table (`QgsGCPListWidget` Rewrite + Type Delegate)

**Goal:** Bottom-docked GCP table that matches `design.html`: 10 columns including 类型, 26px row height, checkbox column, selected-row blue left-bar, residual ≥ 1px highlighted with ⚠.

**Files:**
- Create: `src/app/georeferencer/qgsgcplistwidget.h/.cpp` (rewrite from scratch — upstream code is too tied to QGIS layout)
- Create: `src/app/georeferencer/qgsgeorefdelegates.h/.cpp` (port + add type combobox delegate)
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.h/.cpp` — add bottom dock with GCP widget
- Modify: `resources/styles.qss` — add georef table styles
- Test: `tests/test_gcp_list_widget.cpp`

### Steps

- [ ] **Step 6.1: Port delegates and add `RsGcpTypeDelegate`**

Port `qgsgeorefdelegates.{h,cpp}` (the existing X/Y double-spinbox delegate and enable-checkbox delegate are reused as-is).

Append to `qgsgeorefdelegates.h`:

```cpp
class RsGcpTypeDelegate : public QStyledItemDelegate {
    Q_OBJECT
  public:
    explicit RsGcpTypeDelegate(QObject *parent = nullptr);
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &idx) const override;
  private:
    QStringList mOptions = {"road","river","bridge","crossroad","building","other"};
};
```

In `.cpp`:

```cpp
#include <QComboBox>
RsGcpTypeDelegate::RsGcpTypeDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QWidget *RsGcpTypeDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const {
    auto *cb = new QComboBox(parent);
    cb->setEditable(true);
    cb->addItems(mOptions);
    return cb;
}

void RsGcpTypeDelegate::setEditorData(QWidget *editor, const QModelIndex &idx) const {
    auto *cb = qobject_cast<QComboBox*>(editor);
    if (cb) cb->setCurrentText(idx.data(Qt::EditRole).toString());
}

void RsGcpTypeDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &idx) const {
    auto *cb = qobject_cast<QComboBox*>(editor);
    if (cb) model->setData(idx, cb->currentText(), Qt::EditRole);
}
```

- [ ] **Step 6.2: Create `QgsGCPListWidget` header**

```cpp
#pragma once
#include <QTableView>
class QgsGCPList;
class QgsGCPListModel;

class QgsGCPListWidget : public QTableView {
    Q_OBJECT
  public:
    explicit QgsGCPListWidget(QWidget *parent = nullptr);
    void setGCPList(QgsGCPList *list);

  signals:
    void pointEnabled(int row, bool enabled);
    void pointTypeChanged(int row, const QString &type);
    void deleteRowsRequested(const QList<int> &rows);

  private:
    QgsGCPListModel *mModel = nullptr;
    QgsGCPList *mList = nullptr;
};
```

- [ ] **Step 6.3: Implement `QgsGCPListWidget`**

```cpp
#include "qgsgcplistwidget.h"
#include "qgsgcplistmodel.h"
#include "qgsgcplist.h"
#include "qgsgeorefdelegates.h"
#include <QHeaderView>
#include <QFont>

QgsGCPListWidget::QgsGCPListWidget(QWidget *parent) : QTableView(parent) {
    setObjectName(QStringLiteral("rsGcpTable"));
    mModel = new QgsGCPListModel(this);
    setModel(mModel);
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(26);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setStretchLastSection(true);
    setSelectionBehavior(SelectRows);
    setSelectionMode(SingleSelection);
    setAlternatingRowColors(true);
    QFont mono("IBM Plex Mono", 10);
    setFont(mono);
    horizontalHeader()->setFont(QFont("IBM Plex Sans", 9, QFont::DemiBold));
    setShowGrid(false);

    // Default column widths from design.html: 40,40,120,120,140,140,80,80,90,120
    const int widths[10] = {40,40,120,120,140,140,80,80,90,120};
    for (int c = 0; c < 10; ++c) setColumnWidth(c, widths[c]);

    setItemDelegateForColumn(9, new RsGcpTypeDelegate(this));
}

void QgsGCPListWidget::setGCPList(QgsGCPList *list) {
    mList = list;
    mModel->setGCPList(list);
}
```

- [ ] **Step 6.4: Extend `QgsGCPListModel` to 10 columns**

In `qgsgcplistmodel.cpp`'s `columnCount` return 10. In `headerData` return:

```cpp
static const char* HDR[10] = {"启用","#","X 源 (px)","Y 源 (px)","X 参 (m)","Y 参 (m)","ΔX","ΔY","RMS (px)","类型"};
return tr(HDR[section]);
```

In `data` for `Qt::DisplayRole`:
- col 0: empty (checkbox handled by checkstate)
- col 1: row id
- col 2/3: sourcePoint().x()/-y() formatted `%.1f`
- col 4/5: destinationPoint().x()/y() formatted `%.2f`
- col 6/7: residual().x()/y() formatted `%+.2f` if enabled, "—" if disabled
- col 8: residual magnitude formatted `%.2f`, or "—"
- col 9: pointType()

For col 8/6/7 add `Qt::ForegroundRole`: if residual magnitude ≥ 1.0 return `QBrush(QColor("#bf8700"))` (warn color); add `Qt::DecorationRole` "⚠" at col 8 when warn.

For col 0 implement `Qt::CheckStateRole` get/set; `setData` for col 0 emits `pointEnabled`; `setData` for col 9 calls `point->setPointType` and emits `pointTypeChanged`.

- [ ] **Step 6.5: Mount widget in main window bottom dock**

In `qgsgeoreferencermainwindow.h`:

```cpp
class QgsGCPListWidget;
class QDockWidget;
// ...
private:
    QgsGCPListWidget *mGcpTable = nullptr;
    QDockWidget *mGcpDock = nullptr;
    QgsGCPList *mGcps = nullptr;
```

In `.cpp` constructor, after central widget:

```cpp
mGcps = new QgsGCPList(this);  // owns points
mGcpDock = new QDockWidget(tr("GCP 表"), this);
mGcpDock->setObjectName(QStringLiteral("rsGcpDock"));
mGcpTable = new QgsGCPListWidget(mGcpDock);
mGcpTable->setGCPList(mGcps);
mGcpDock->setWidget(mGcpTable);
mGcpDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
addDockWidget(Qt::BottomDockWidgetArea, mGcpDock);
mGcpDock->resize(mGcpDock->width(), 280);
```

- [ ] **Step 6.6: Add styles**

Append to `resources/styles.qss`:

```css
QTableView#rsGcpTable {
    gridline-color: #e6eaef;
    background: #ffffff;
    alternate-background-color: rgba(20,23,28,0.02);
    selection-background-color: rgba(32,136,48,0.08);
    selection-color: #208830;
}
QTableView#rsGcpTable::item:selected {
    border-left: 2px solid #208830;
}
QHeaderView::section {
    background: qlineargradient(x1:0 y1:0 x2:0 y2:1, stop:0 #fafbfc, stop:1 #f1f3f6);
    color: #5f6b7a;
    border: none;
    border-bottom: 1px solid #e6eaef;
    padding: 4px 6px;
    text-transform: uppercase;
    font-size: 9px;
    font-weight: 600;
}
```

- [ ] **Step 6.7: Write GCP table tests**

Create `tests/test_gcp_list_widget.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QSignalSpy>
#include "qgsgcplistwidget.h"
#include "qgsgcplist.h"

namespace { int fake_argc=1; char *fake_argv[]={(char*)"t",nullptr}; }

TEST_CASE("GCP table: shows 10 columns", "[georef][table]") {
    QApplication app(fake_argc, fake_argv);
    QgsGCPListWidget w;
    QgsGCPList list;
    w.setGCPList(&list);
    REQUIRE(w.model()->columnCount() == 10);
}

TEST_CASE("GCP table: displays pointType in column 9", "[georef][table]") {
    QApplication app(fake_argc, fake_argv);
    QgsGCPListWidget w;
    QgsGCPList list;
    QgsGcpPoint p(QgsPointXY(0,0), QgsPointXY(0,0),
                  QgsCoordinateReferenceSystem("EPSG:4326"), true);
    p.setPointType("river");
    list.appendPoint(p);
    w.setGCPList(&list);
    REQUIRE(w.model()->data(w.model()->index(0, 9)).toString() == "river");
}
```

Build, run: expect PASS.

- [ ] **Step 6.8: Hand-test**

```bash
./sicnu_geo_rs
# Raster -> Georeferencer; bottom dock shows GCP table with 10 columns and 26px rows
```

- [ ] **Step 6.9: Commit**

```bash
git add src/app/georeferencer/qgsgcplistwidget.{h,cpp} \
        src/app/georeferencer/qgsgeorefdelegates.{h,cpp} \
        src/app/georeferencer/qgsgcplistmodel.{h,cpp} \
        src/app/georeferencer/qgsgeoreferencermainwindow.{h,cpp} \
        resources/styles.qss tests/test_gcp_list_widget.cpp \
        src/app/georeferencer/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(georef): GCP table widget with type delegate + design.html styles

- 10-column QgsGCPListWidget: 启用 / # / X源 / Y源 / X参 / Y参 / ΔX / ΔY / RMS / 类型
- RsGcpTypeDelegate combobox (road/river/bridge/crossroad/building/other)
- Residual >= 1px highlighted with warn color + warn glyph
- IBM Plex Mono rows, IBM Plex Sans headers
- Bottom QDockWidget, 280px default height
- Tests: column count, type round-trip

Task 11.4.6"
```

---

## Task 7: Right Dock Parameter Panel + RMS Scatter + Apply

**Goal:** 340px right `QDockWidget` containing 5 sections (变换 / 重采样 / RMS / CRS / 输出), `RsRmsScatterWidget`, and the wired-up Apply action that runs `QgsImageWarper` on a `QThreadPool` task with progress + cancel + edit-lock + structured log.

**Files:**
- Create: `src/app/georeferencer/rs_rms_scatter_widget.h/.cpp`
- Create: `src/app/georeferencer/rs_georef_params_panel.h/.cpp`
- Create: `src/app/georeferencer/rs_warp_task.h/.cpp` (QgsTask subclass)
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.h/.cpp`
- Test: `tests/test_rms_scatter.cpp`
- Test: `tests/test_georef_window_warp_lock.cpp`

### Steps

- [ ] **Step 7.1: Create `RsRmsScatterWidget`**

`rs_rms_scatter_widget.h`:

```cpp
#pragma once
#include <QWidget>

class RsRmsScatterWidget : public QWidget {
    Q_OBJECT
  public:
    explicit RsRmsScatterWidget(QWidget *parent = nullptr);
    void setResiduals(const QVector<QPointF> &dx_dy);
    void setWarnThreshold(double pixels) { mWarnPx = pixels; update(); }
    QSize sizeHint() const override { return {150, 150}; }
  protected:
    void paintEvent(QPaintEvent *) override;
  private:
    QVector<QPointF> mPts;
    double mWarnPx = 1.0;
};
```

`rs_rms_scatter_widget.cpp`:

```cpp
#include "rs_rms_scatter_widget.h"
#include <QPainter>
#include <cmath>

RsRmsScatterWidget::RsRmsScatterWidget(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("rsRmsScatter"));
    setMinimumSize(150, 150);
}

void RsRmsScatterWidget::setResiduals(const QVector<QPointF> &v) {
    mPts = v;
    update();
}

void RsRmsScatterWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int W = width(), H = height();
    int cx = W / 2, cy = H / 2;
    int R = std::min(W, H) / 2 - 4;

    p.fillRect(rect(), QColor("#ffffff"));
    p.setPen(QPen(QColor("#e6eaef"), 1));
    p.drawLine(0, cy, W, cy);
    p.drawLine(cx, 0, cx, H);

    double scale = R / std::max(1.5, mWarnPx * 1.4);

    p.setPen(QPen(QColor("#bf8700"), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    int warnR = std::min(R, int(mWarnPx * scale));
    p.drawEllipse(QPoint(cx, cy), warnR, warnR);

    for (const auto &pt : mPts) {
        double r = std::hypot(pt.x(), pt.y());
        QColor c = (r >= mWarnPx) ? QColor("#bf8700") : QColor("#208830");
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        int x = cx + int(pt.x() * scale);
        int y = cy + int(pt.y() * scale);
        p.drawEllipse(QPoint(x, y), 3, 3);
    }
}
```

- [ ] **Step 7.2: Write scatter test**

Create `tests/test_rms_scatter.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QImage>
#include "rs_rms_scatter_widget.h"

namespace { int fake_argc=1; char *fake_argv[]={(char*)"t",nullptr}; }

TEST_CASE("RmsScatter: paints without crashing on empty + 7 points", "[georef][scatter]") {
    QApplication app(fake_argc, fake_argv);
    RsRmsScatterWidget w;
    w.resize(150,150);
    QImage img(w.size(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    w.render(&img);

    w.setResiduals({{0.1,0.1},{0.4,-0.3},{1.2,0.5},{-0.2,0.6},{0.8,-0.8},{0.0,0.0},{1.5,-1.4}});
    QImage img2(w.size(), QImage::Format_ARGB32);
    w.render(&img2);
    REQUIRE(img != img2);  // something was drawn
}
```

Build, run: PASS.

- [ ] **Step 7.3: Create parameter panel header**

`rs_georef_params_panel.h`:

```cpp
#pragma once
#include <QWidget>
#include "qgsgcptransformer.h"
#include "qgsimagewarper.h"

class QComboBox;
class QLineEdit;
class QLabel;
class QDoubleSpinBox;
class RsRmsScatterWidget;
class QgsCrsSelectionWidget;

class RsGeorefParamsPanel : public QWidget {
    Q_OBJECT
  public:
    explicit RsGeorefParamsPanel(QWidget *parent = nullptr);

    QgsGcpTransformerInterface::TransformMethod transformMethod() const;
    QgsImageWarper::ResamplingMethod resamplingMethod() const;
    QString outputPath() const;
    QgsCoordinateReferenceSystem destCrs() const;
    double outputPixelSize() const;
    QString demPath() const;             // RPC only
    void setRpcMode(bool on);            // toggles DEM section visibility

  public slots:
    void setRmsValues(int total, int enabled, double rmsPx, double xRms, double yRms, double maxRms, int maxRmsRowId);
    void setResidualScatter(const QVector<QPointF> &dxdy);
    void setMinimumGcpCount(int n);
    void setActualGcpCount(int n);

  signals:
    void transformMethodChanged();
    void resamplingMethodChanged();
    void outputPathChanged(const QString &);

  private:
    QComboBox *mTransformCombo = nullptr;
    QComboBox *mResamplingCombo = nullptr;
    QDoubleSpinBox *mPixelSize = nullptr;
    QLineEdit *mOutputPath = nullptr;
    QLineEdit *mDemPath = nullptr;
    QWidget *mDemSection = nullptr;
    QLabel *mMinPtsLabel = nullptr;
    QLabel *mActualPtsLabel = nullptr;
    QLabel *mXRms = nullptr;
    QLabel *mYRms = nullptr;
    QLabel *mTotalRms = nullptr;
    QLabel *mMaxRms = nullptr;
    RsRmsScatterWidget *mScatter = nullptr;
    // ... CRS picker ...
};
```

- [ ] **Step 7.4: Implement parameter panel**

`rs_georef_params_panel.cpp`: build the panel with `QVBoxLayout` of 5 grouped sections (use `QGroupBox` or styled `QFrame`). Wire each control's `currentIndexChanged` / `textChanged` to the corresponding signal.

Key code for RPC visibility:

```cpp
void RsGeorefParamsPanel::setRpcMode(bool on) {
    mDemSection->setVisible(on);
    // disable other transform options
    for (int i = 0; i < mTransformCombo->count(); ++i) {
        if (i == int(QgsGcpTransformerInterface::TransformMethod::InvalidTransform)) continue;
        auto m = mTransformCombo->itemData(i).value<QgsGcpTransformerInterface::TransformMethod>();
        bool isRpc = false; // placeholder until Task 8 adds RpcModel value
        mTransformCombo->view()->setRowHidden(i, on && !isRpc);
    }
}
```

(Will be finalized in Task 8 when RPC enum value is added.)

- [ ] **Step 7.5: Mount panel as right dock**

In main window constructor:

```cpp
#include "rs_georef_params_panel.h"
// ...
auto *paramDock = new QDockWidget(tr("校正参数"), this);
paramDock->setObjectName(QStringLiteral("rsParamDock"));
mParamsPanel = new RsGeorefParamsPanel(paramDock);
paramDock->setWidget(mParamsPanel);
paramDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
addDockWidget(Qt::RightDockWidgetArea, paramDock);
paramDock->resize(340, paramDock->height());
```

Add `RsGeorefParamsPanel *mParamsPanel = nullptr;` to header.

- [ ] **Step 7.6: Create `RsWarpTask` (`QgsTask` subclass)**

`rs_warp_task.h`:

```cpp
#pragma once
#include <qgstaskmanager.h>
#include "qgsimagewarper.h"
#include "qgsgeoreftransform.h"

class RsWarpTask : public QgsTask {
    Q_OBJECT
  public:
    RsWarpTask(const QString &in, const QString &out,
               QgsGeorefTransform *transform,
               QgsImageWarper::ResamplingMethod r,
               const QgsCoordinateReferenceSystem &destCrs,
               double pixelSize);
    bool run() override;
    void cancel() override;
    const QgsImageWarper::WarpResult &result() const { return mResult; }

  private:
    QString mIn, mOut;
    QgsGeorefTransform *mTransform = nullptr;
    QgsImageWarper::ResamplingMethod mResamp;
    QgsCoordinateReferenceSystem mDestCrs;
    double mPixelSize;
    QgsFeedback mFb;
    QgsImageWarper::WarpResult mResult;
};
```

`rs_warp_task.cpp`:

```cpp
#include "rs_warp_task.h"

RsWarpTask::RsWarpTask(const QString &in, const QString &out,
                       QgsGeorefTransform *t,
                       QgsImageWarper::ResamplingMethod r,
                       const QgsCoordinateReferenceSystem &c, double px)
    : QgsTask(tr("Warping %1").arg(QFileInfo(in).fileName()),
              QgsTask::CanCancel),
      mIn(in), mOut(out), mTransform(t), mResamp(r), mDestCrs(c), mPixelSize(px) {
    connect(&mFb, &QgsFeedback::progressChanged, this, [this](double p){ setProgress(p); });
}

bool RsWarpTask::run() {
    QgsImageWarper w(&mFb);
    mResult = w.warpFile(mIn, mOut, mTransform, mResamp, true, false,
                         mDestCrs, QSize(), mPixelSize, mPixelSize);
    return mResult.status == QgsImageWarper::WarpStatus::Ok;
}

void RsWarpTask::cancel() {
    mFb.cancel();
    QgsTask::cancel();
}
```

- [ ] **Step 7.7: Wire Apply action**

In `qgsgeoreferencermainwindow.cpp`, replace the Apply lambda (Step 4.4) with:

```cpp
auto *apply = mModeBar->addAction(QIcon(":/icons/georef/apply"), tr("Apply"), this, [this](){
    applyTransform();
});
apply->setObjectName(QStringLiteral("rsGeorefApplyAction"));
```

Add the slot:

```cpp
void QgsGeoreferencerMainWindow::applyTransform() {
    if (!mTransform) {
        statusBar()->showMessage(tr("请先添加 GCP"), 3000);
        return;
    }
    auto method = mParamsPanel->transformMethod();
    if (mGcps->countEnabled() < QgsGcpTransformerInterface::minimumGcpCount(method)) {
        statusBar()->showMessage(tr("GCP 数量不足"), 3000);
        return;
    }
    if (mParamsPanel->outputPath().isEmpty()) {
        statusBar()->showMessage(tr("请填写输出路径"), 3000);
        return;
    }

    // Lock GCP table during warp
    mGcpTable->setEnabled(false);
    mModeBar->findChild<QAction*>("rsGeorefApplyAction")->setEnabled(false);

    auto *task = new RsWarpTask(mSourceRasterPath, mParamsPanel->outputPath(),
                                mTransform.get(), mParamsPanel->resamplingMethod(),
                                mParamsPanel->destCrs(), mParamsPanel->outputPixelSize());
    connect(task, &QgsTask::taskCompleted, this, [this, task](){
        emitStructuredLog(task->result());
        mGcpTable->setEnabled(true);
        mModeBar->findChild<QAction*>("rsGeorefApplyAction")->setEnabled(true);
        statusBar()->showMessage(tr("已输出: %1 (%2 bytes, %3 ms)")
            .arg(mParamsPanel->outputPath())
            .arg(task->result().outputBytes)
            .arg(task->result().durationMs), 6000);
    });
    connect(task, &QgsTask::taskTerminated, this, [this, task](){
        emitStructuredLog(task->result());
        mGcpTable->setEnabled(true);
        mModeBar->findChild<QAction*>("rsGeorefApplyAction")->setEnabled(true);
    });
    QgsApplication::taskManager()->addTask(task);
}

void QgsGeoreferencerMainWindow::emitStructuredLog(const QgsImageWarper::WarpResult &r) {
    QJsonObject o;
    o["event"] = "warp_finished";
    o["method"] = QVariant::fromValue(mParamsPanel->transformMethod()).toString();
    o["gcp_total"] = int(mGcps->size());
    o["gcp_enabled"] = int(mGcps->countEnabled());
    o["rms_px"] = mLastRms;
    o["resampling"] = QVariant::fromValue(mParamsPanel->resamplingMethod()).toString();
    o["output"] = mParamsPanel->outputPath();
    o["output_bytes"] = qint64(r.outputBytes);
    o["duration_ms"] = r.durationMs;
    o["status"] = r.status == QgsImageWarper::WarpStatus::Ok ? "ok"
                : r.status == QgsImageWarper::WarpStatus::Cancelled ? "cancelled"
                : "failed";
    if (r.status != QgsImageWarper::WarpStatus::Ok) {
        o["error_code"] = int(r.status);
        o["error_msg"] = r.errorMessage;
    }
    QgsMessageLog::logMessage(QJsonDocument(o).toJson(QJsonDocument::Compact),
                              QStringLiteral("Georeferencer"), Qgis::MessageLevel::Info);
}
```

Add headers, slot declarations, and `mTransform` / `mLastRms` member to `.h`.

- [ ] **Step 7.8: Write UI-edit-lock test**

Create `tests/test_georef_window_warp_lock.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QToolBar>
#include "qgsgeoreferencermainwindow.h"
#include "qgsgcplistwidget.h"

namespace { int fake_argc=1; char *fake_argv[]={(char*)"t",nullptr}; }

TEST_CASE("Warp lock: while warp pending GCP table is disabled", "[georef][window][lock]") {
    QApplication app(fake_argc, fake_argv);
    QgsGeoreferencerMainWindow w(nullptr);

    auto *table = w.findChild<QgsGCPListWidget*>("rsGcpTable");
    REQUIRE(table);
    REQUIRE(table->isEnabled());

    // simulate "warp started" — call public setter we add
    w.setWarpInProgressForTest(true);
    REQUIRE_FALSE(table->isEnabled());

    w.setWarpInProgressForTest(false);
    REQUIRE(table->isEnabled());
}
```

Add the test hook:

```cpp
// In header, public:
void setWarpInProgressForTest(bool on) {
    mGcpTable->setEnabled(!on);
    auto *apply = mModeBar->findChild<QAction*>("rsGeorefApplyAction");
    if (apply) apply->setEnabled(!on);
}
```

Build, run: PASS.

- [ ] **Step 7.9: Hand-test full apply flow with synthetic data**

```bash
./sicnu_geo_rs
# Open Raster -> Georeferencer; load synthetic 64x64 raster;
# Inject 4 GCPs programmatically via test backdoor or hand-click on canvas;
# Choose Polynomial1; set output path; click Apply;
# Verify output GeoTIFF exists, table is locked during warp, message log shows JSON line
```

- [ ] **Step 7.10: Commit**

```bash
git add src/app/georeferencer/rs_rms_scatter_widget.{h,cpp} \
        src/app/georeferencer/rs_georef_params_panel.{h,cpp} \
        src/app/georeferencer/rs_warp_task.{h,cpp} \
        src/app/georeferencer/qgsgeoreferencermainwindow.{h,cpp} \
        src/app/georeferencer/CMakeLists.txt \
        tests/test_rms_scatter.cpp tests/test_georef_window_warp_lock.cpp \
        tests/CMakeLists.txt
git commit -m "feat(georef): right param dock + RMS scatter + Apply with task + edit lock

- RsRmsScatterWidget: 150x150 QPainter scatter, warn ring at 1px
- RsGeorefParamsPanel: 5 sections (transform/resampling/RMS/CRS/output)
- RsWarpTask: QgsTask wrapping QgsImageWarper with QgsFeedback cancel
- Apply locks GCP table + button until task completes
- Structured log JSON written to QgsMessageLog tag 'Georeferencer'
- Tests: scatter render, edit lock toggling

Task 11.4.7"
```

---

## Task 8: RPC Physical Model + DEM Field + Mode Switch

**Goal:** Add `QgsRpcGcpTransformer` (wrapping `GDALCreateRPCTransformer`), wire RPC mode to surface DEM field, validate DEM CRS, and prove RPC fit + warp with synthetic RPC test data.

**Files:**
- Create: `src/analysis/georeferencing/qgsrpcgcptransformer.h/.cpp`
- Modify: `src/analysis/georeferencing/qgsgcptransformer.h` — add `RpcPhysical` to `TransformMethod` enum
- Modify: `src/analysis/georeferencing/qgsgcptransformer.cpp::createFromParameters` — return `QgsRpcGcpTransformer` for `RpcPhysical`
- Modify: `src/app/georeferencer/rs_georef_params_panel.cpp` — show DEM section + lock combo
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.cpp` — handle mode change → setRpcMode
- Create test data: `tests/data/georef/synthetic_rpc.tif` (generated by test fixture)
- Test: `tests/test_rpc_transformer.cpp`
- Test: `tests/test_georef_window_rpc_mode.cpp`

### Steps

- [ ] **Step 8.1: Add `RpcPhysical` to the `TransformMethod` enum**

In `qgsgcptransformer.h`:

```cpp
enum class TransformMethod : int {
    Linear, Helmert, PolynomialOrder1, PolynomialOrder2, PolynomialOrder3,
    ThinPlateSpline, Projective,
    RpcPhysical = 7,
    InvalidTransform = 65535
};
```

Update `minimumGcpCount`:

```cpp
case TransformMethod::RpcPhysical: return 0; // RPC reads coefficients from raster metadata; GCPs are refinement
```

- [ ] **Step 8.2: Create `QgsRpcGcpTransformer`**

`qgsrpcgcptransformer.h`:

```cpp
#pragma once
#include "qgsgcptransformer.h"
#include "qgsgeometry.h"
#include <gdal_alg.h>
#include "qgis_analysis_export.h"

class QGIS_ANALYSIS_EXPORT QgsRpcGcpTransformer : public QgsGcpTransformerInterface {
  public:
    explicit QgsRpcGcpTransformer(const QString &sourceRasterPath = QString(),
                                  const QString &demPath = QString());
    ~QgsRpcGcpTransformer() override;

    void setSourceRasterPath(const QString &p) { mSrc = p; }
    void setDemPath(const QString &p) { mDem = p; }

    QgsGcpTransformerInterface *clone() const override;
    TransformMethod method() const override { return TransformMethod::RpcPhysical; }

    bool updateParametersFromGcps(const QVector<QgsPointXY> &source,
                                  const QVector<QgsPointXY> &destination,
                                  bool invertYAxis = false) override;

    int minimumGcpCount() const override { return 0; }

    bool transform(double x, double y, double &fx, double &fy, bool inverse) const override;

    bool isValid() const { return mTransformArg != nullptr; }

  signals_class:
    // Plain virtual emit not needed; mainwindow watches via QgsMessageLog
  private:
    void freeTransformer();

    QString mSrc;
    QString mDem;
    void *mTransformArg = nullptr;
};
```

Actually `QgsGcpTransformerInterface` may not be a `QObject`; if it is, use Q_OBJECT. For now keep it free of signals — emit DEM-CRS warnings via the warper layer.

`qgsrpcgcptransformer.cpp`:

```cpp
#include "qgsrpcgcptransformer.h"
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <QFileInfo>

QgsRpcGcpTransformer::QgsRpcGcpTransformer(const QString &src, const QString &dem)
    : mSrc(src), mDem(dem) {}

QgsRpcGcpTransformer::~QgsRpcGcpTransformer() { freeTransformer(); }

void QgsRpcGcpTransformer::freeTransformer() {
    if (mTransformArg) { GDALDestroyRPCTransformer(mTransformArg); mTransformArg = nullptr; }
}

QgsGcpTransformerInterface *QgsRpcGcpTransformer::clone() const {
    auto *c = new QgsRpcGcpTransformer(mSrc, mDem);
    return c;
}

bool QgsRpcGcpTransformer::updateParametersFromGcps(const QVector<QgsPointXY> &,
                                                     const QVector<QgsPointXY> &,
                                                     bool) {
    freeTransformer();
    GDALAllRegister();
    GDALDataset *ds = (GDALDataset*) GDALOpen(mSrc.toUtf8().constData(), GA_ReadOnly);
    if (!ds) return false;
    char **md = ds->GetMetadata("RPC");
    if (!md) { GDALClose(ds); return false; }
    GDALRPCInfoV2 rpc;
    if (!GDALExtractRPCInfoV2(md, &rpc)) { GDALClose(ds); return false; }

    char **opts = nullptr;
    if (!mDem.isEmpty() && QFileInfo::exists(mDem)) {
        opts = CSLSetNameValue(opts, "RPC_DEM", mDem.toUtf8().constData());
        opts = CSLSetNameValue(opts, "RPC_DEMINTERPOLATION", "bilinear");
    }
    mTransformArg = GDALCreateRPCTransformerV2(&rpc, FALSE, 0.1, opts);
    CSLDestroy(opts);
    GDALClose(ds);
    return mTransformArg != nullptr;
}

bool QgsRpcGcpTransformer::transform(double x, double y, double &fx, double &fy, bool inverse) const {
    if (!mTransformArg) return false;
    double X = x, Y = y, Z = 0.0;
    int success = 0;
    if (!GDALRPCTransform(mTransformArg, inverse ? TRUE : FALSE, 1, &X, &Y, &Z, &success)) return false;
    if (!success) return false;
    fx = X; fy = Y;
    return true;
}
```

Add to `src/analysis/CMakeLists.txt`:

```cmake
add_library(qgis_analysis STATIC
    georeferencing/qgsgcppoint.cpp
    georeferencing/qgsgcptransformer.cpp
    georeferencing/qgsleastsquares.cpp
    georeferencing/qgsgcpgeometrytransformer.cpp
    georeferencing/qgsvectorwarper.cpp
    georeferencing/qgsrpcgcptransformer.cpp
)
```

- [ ] **Step 8.3: Update factory + DEM CRS validation in warper**

In `qgsgcptransformer.cpp::createFromParameters`:

```cpp
case TransformMethod::RpcPhysical:
    return new QgsRpcGcpTransformer();
```

In `qgsimagewarper.cpp::warpFile`, before warp when transformer is RPC, check DEM CRS:

```cpp
auto *rpc = dynamic_cast<QgsRpcGcpTransformer*>(transform);
if (rpc && !rpc->demPath().isEmpty()) {
    GDALDataset *dem = (GDALDataset*) GDALOpen(rpc->demPath().toUtf8().constData(), GA_ReadOnly);
    if (dem) {
        const char *proj = dem->GetProjectionRef();
        QgsCoordinateReferenceSystem demCrs;
        if (proj) demCrs = QgsCoordinateReferenceSystem::fromWkt(QString::fromUtf8(proj));
        GDALClose(dem);
        if (demCrs.isValid() && demCrs != destCrs) {
            // emit warning via QgsMessageLog
            QgsMessageLog::logMessage(
                tr("DEM CRS (%1) differs from target CRS (%2); RPC results may shift")
                    .arg(demCrs.authid(), destCrs.authid()),
                QStringLiteral("Georeferencer"), Qgis::MessageLevel::Warning);
        }
    }
}
```

Add public `demPath()` to `QgsRpcGcpTransformer`.

- [ ] **Step 8.4: Add synthetic RPC test data helper**

Append to `tests/warper_test_helpers.h`:

```cpp
// Create a 64x64 raster with synthetic RPC metadata roughly representing
// a tilt-and-translate model so transform() output is predictable.
inline QString makeSyntheticRpcRaster(const QString &dir) {
    GDALAllRegister();
    QString path = dir + "/synthetic_rpc.tif";
    auto *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    GDALDataset *ds = drv->Create(path.toUtf8().constData(), 64, 64, 1, GDT_Byte, nullptr);
    char **md = nullptr;
    // Minimal RPC00B fields — these constants encode an identity-ish mapping
    md = CSLSetNameValue(md, "LINE_OFF",   "32");
    md = CSLSetNameValue(md, "SAMP_OFF",   "32");
    md = CSLSetNameValue(md, "LAT_OFF",    "39.0");
    md = CSLSetNameValue(md, "LONG_OFF",   "116.0");
    md = CSLSetNameValue(md, "HEIGHT_OFF", "0");
    md = CSLSetNameValue(md, "LINE_SCALE", "32");
    md = CSLSetNameValue(md, "SAMP_SCALE", "32");
    md = CSLSetNameValue(md, "LAT_SCALE",  "0.001");
    md = CSLSetNameValue(md, "LONG_SCALE", "0.001");
    md = CSLSetNameValue(md, "HEIGHT_SCALE","1000");
    // Identity polynomials: a single non-zero linear term per coefficient
    md = CSLSetNameValue(md, "LINE_NUM_COEFF",
        "0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0");
    md = CSLSetNameValue(md, "LINE_DEN_COEFF",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0");
    md = CSLSetNameValue(md, "SAMP_NUM_COEFF",
        "0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0");
    md = CSLSetNameValue(md, "SAMP_DEN_COEFF",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0");
    ds->SetMetadata(md, "RPC");
    CSLDestroy(md);
    GDALClose(ds);
    return path;
}
```

- [ ] **Step 8.5: Write RPC transformer test**

Create `tests/test_rpc_transformer.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "qgsrpcgcptransformer.h"
#include "warper_test_helpers.h"
#include <QTemporaryDir>

using Catch::Approx;

TEST_CASE("RpcTransformer: identity RPC at center maps to LAT/LONG_OFF", "[georef][rpc]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString src = makeSyntheticRpcRaster(tmp.path());
    QgsRpcGcpTransformer t(src);
    REQUIRE(t.updateParametersFromGcps({}, {}, false));
    REQUIRE(t.isValid());

    double fx=0, fy=0;
    REQUIRE(t.transform(32.0, 32.0, fx, fy, false));
    REQUIRE(fx == Approx(116.0).margin(0.001));
    REQUIRE(fy == Approx(39.0).margin(0.001));
}

TEST_CASE("RpcTransformer: minimumGcpCount is 0", "[georef][rpc]") {
    REQUIRE(QgsGcpTransformerInterface::minimumGcpCount(
        QgsGcpTransformerInterface::TransformMethod::RpcPhysical) == 0);
}

TEST_CASE("RpcTransformer: invalid path returns false on fit", "[georef][rpc]") {
    QgsRpcGcpTransformer t("/does/not/exist.tif");
    REQUIRE_FALSE(t.updateParametersFromGcps({}, {}, false));
    REQUIRE_FALSE(t.isValid());
}
```

Register, build, run: expect PASS. If the synthetic RPC fails (GDAL rejects identity polynomials as too thin), substitute the helper with a real tile from open data (e.g. a small LC09 RPC sample) — record the actual source in the helper comment.

- [ ] **Step 8.6: Wire RPC mode in params panel + main window**

In `rs_georef_params_panel.cpp::RsGeorefParamsPanel`'s constructor, populate the transform combo with both regular methods and the RPC entry; tag each item's `userData` with the enum value. In `setRpcMode`:

```cpp
void RsGeorefParamsPanel::setRpcMode(bool on) {
    mDemSection->setVisible(on);
    for (int i = 0; i < mTransformCombo->count(); ++i) {
        auto m = mTransformCombo->itemData(i).value<QgsGcpTransformerInterface::TransformMethod>();
        bool isRpc = (m == QgsGcpTransformerInterface::TransformMethod::RpcPhysical);
        mTransformCombo->view()->setRowHidden(i, on != isRpc);
    }
    if (on) {
        int rpcIdx = mTransformCombo->findData(
            QVariant::fromValue(QgsGcpTransformerInterface::TransformMethod::RpcPhysical));
        if (rpcIdx >= 0) mTransformCombo->setCurrentIndex(rpcIdx);
    }
}
```

In `qgsgeoreferencermainwindow.cpp` constructor:

```cpp
connect(mModeToggle, &RsGeorefModeToggle::modeChanged,
        mParamsPanel, [this](RsGeorefModeToggle::Mode m) {
    mParamsPanel->setRpcMode(m == RsGeorefModeToggle::RpcPhysical);
});
```

- [ ] **Step 8.7: Write RPC-mode UI test**

Create `tests/test_georef_window_rpc_mode.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include "qgsgeoreferencermainwindow.h"
#include "rs_georef_mode_toggle.h"
#include "rs_georef_params_panel.h"

namespace { int fake_argc=1; char *fake_argv[]={(char*)"t",nullptr}; }

TEST_CASE("RPC mode: DEM section visible only when RPC selected", "[georef][window][rpc]") {
    QApplication app(fake_argc, fake_argv);
    QgsGeoreferencerMainWindow w(nullptr);

    auto *panel = w.findChild<RsGeorefParamsPanel*>();
    auto *toggle = w.findChild<RsGeorefModeToggle*>();
    REQUIRE(panel); REQUIRE(toggle);

    // Default: ImageToMap, DEM hidden
    REQUIRE_FALSE(panel->isDemSectionVisible());

    toggle->setMode(RsGeorefModeToggle::RpcPhysical);
    REQUIRE(panel->isDemSectionVisible());
    REQUIRE(panel->transformMethod() == QgsGcpTransformerInterface::TransformMethod::RpcPhysical);

    toggle->setMode(RsGeorefModeToggle::ImageToMap);
    REQUIRE_FALSE(panel->isDemSectionVisible());
}
```

Add `bool isDemSectionVisible() const { return mDemSection->isVisible(); }` to the params panel.

Build & run: PASS.

- [ ] **Step 8.8: Hand-test full RPC flow**

```bash
./sicnu_geo_rs
# Raster -> Georeferencer; open synthetic RPC tif; toggle to RPC mode;
# DEM field appears; select a DEM (or leave empty); add 1 GCP for refinement;
# Click Apply -> output GeoTIFF created with RPC warp;
# QgsMessageLog shows DEM-CRS warning if DEM CRS doesn't match target
```

- [ ] **Step 8.9: Commit**

```bash
git add src/analysis/georeferencing/qgsrpcgcptransformer.{h,cpp} \
        src/analysis/georeferencing/qgsgcptransformer.{h,cpp} \
        src/analysis/CMakeLists.txt \
        src/app/georeferencer/qgsimagewarper.{h,cpp} \
        src/app/georeferencer/rs_georef_params_panel.{h,cpp} \
        src/app/georeferencer/qgsgeoreferencermainwindow.{h,cpp} \
        tests/test_rpc_transformer.cpp tests/test_georef_window_rpc_mode.cpp \
        tests/warper_test_helpers.h tests/CMakeLists.txt
git commit -m "feat(georef): RPC physical model transformer + DEM field + mode switch

- QgsRpcGcpTransformer wraps GDALCreateRPCTransformerV2 with optional DEM
- New TransformMethod::RpcPhysical (minimumGcpCount = 0; coeffs from raster metadata)
- QgsImageWarper warns when DEM CRS != target CRS (Qgis::MessageLevel::Warning)
- RsGeorefModeToggle::RpcPhysical -> params panel reveals DEM section and locks combo
- Tests: identity RPC roundtrip, invalid path, UI DEM visibility toggling

Task 11.4.8 — Phase 11.4 Georeferencer COMPLETE"
```

- [ ] **Step 8.10: Update planning files**

Update `progress.md`: append a section under Phase 11:

```markdown
## Phase 11.4 — Georeferencer ✅ COMPLETE (2026-XX-XX)
- 12/12 Catch2 tests green
- Manual smoke: Polynomial2 warp on GF-2 256x256 produces GeoTIFF matching golden
- Manual smoke: cancel exits ≤ 1s, table re-enables
- Manual smoke: RPC mode + synthetic raster produces output
- Structured log JSON line written to QgsMessageLog "Georeferencer" tag
- design.html visual review pending (run ui_diff_check separately)
```

Update `findings.md` if non-trivial discoveries surface (e.g. GDAL version pin issues).

Update `task_plan.md`: mark all 8 sub-tasks `[x]`.

Commit:

```bash
git add progress.md findings.md task_plan.md
git commit -m "docs(georef): mark Phase 11.4 Georeferencer complete in planning files"
```

---

## Self-Review

I checked the spec sections against this plan:

| Spec section | Covered by | Notes |
|---|---|---|
| §1 Goals | All 8 tasks | ✓ |
| §3.1 qgis_analysis lib + GDAL 3.4 | Task 1 (CMake) | ✓ |
| §3.2 app/georeferencer files | Tasks 2,3,4,5,6,7,8 | ✓ all 20 ported/new files mapped |
| §3.3 ui resources | Task 4 step 4.8 + Task 6 step 6.6 | ✓ (mapcoordsdialog .ui deferred; see gap below) |
| §3.4 port recipe | Task 1 step 1.4 recipe | ✓ |
| §4 UI layout | Tasks 4,5,6,7 | ✓ |
| §4.2 QGIS differences table | Tasks 5 (sync), 8 (RPC), 7 (param panel as dock), 7 (RMS scatter), 6 (type col), 4 (SIFT placeholder) | ✓ |
| §4.3 main app entry | Task 4 step 4.6 | ✓ |
| §5.1 GCP collection | Task 5+6 — wired but **add-point flow not fully implemented in plan** | ⚠ See gap below |
| §5.2 transform fit | Task 1 + Task 7 step 7.7 (residual recompute hook missing — covered by QgsGCPList) | ✓ |
| §5.3 GDAL warp + 6 failure modes | Task 2 step 2.3 | ✓ |
| §5.4 .points v2 | Task 3 | ✓ |
| §5.5 structured log | Task 7 step 7.7 | ✓ |
| §6 12 tests | Tasks 1,2,3,5,6,7,8 | ✓ all 12 enumerated |
| §6 test data sourcing | Task 8 step 8.4 | ✓ helper synthetic + fallback note |
| §7 8 sub-tasks | Tasks 1–8 | ✓ |
| §8 risks | Task 1 (GDAL 3.4), Task 5 (sync throttle), Task 8 (DEM CRS) | ✓ |

**Gaps found and fixed inline above** (added to the plan during self-review):

1. **GCP add-point mouse-tool flow** is referenced (`qgsgeoreftooladdpoint`) but not given its own step. Add to Task 5: a Step 5.8 adds the port of `qgsgeoreftooladdpoint.{h,cpp}` and wires the toolbar's "Add GCP" action to install the tool on both canvases.
2. **MapCoords dialog** for "input target coord" in RPC mode and manual mode — port `qgsmapcoordsdialog` as part of Task 5 (Step 5.9). The .ui file already exists in `src/ui/georeferencer/`.

Append to **Task 5** (after step 5.7):

- [ ] **Step 5.8: Port `QgsGeorefToolAddPoint` and wire to "Add GCP" toolbar action**

Port `qgsgeoreftooladdpoint.{h,cpp}` and `qgsgeoreftooldeletepoint.{h,cpp}` and `qgsgeoreftoolmovepoint.{h,cpp}` from `qgis_ref/src/app/georeferencer/`. In main window, wire toolbar actions:

```cpp
mAddPointTool = new QgsGeorefToolAddPoint(mSrcCanvas, this);
connect(mAddPointTool, &QgsGeorefToolAddPoint::showCoordDialog, this, &QgsGeoreferencerMainWindow::showCoordDialog);
// toolbar "Add GCP" action:
addPointAction->setCheckable(true);
connect(addPointAction, &QAction::toggled, this, [this](bool on){
    mSrcCanvas->setMapTool(on ? mAddPointTool : nullptr);
});
```

- [ ] **Step 5.9: Port `QgsMapCoordsDialog`**

Port `qgsmapcoordsdialog.{h,cpp}` and ensure `src/ui/georeferencer/qgsmapcoordsdialogbase.ui` is added to the AUTOUIC list in `src/app/georeferencer/CMakeLists.txt`:

```cmake
set_target_properties(qgis_app_georef PROPERTIES AUTOMOC ON AUTOUIC ON)
target_include_directories(qgis_app_georef PRIVATE ${CMAKE_SOURCE_DIR}/src/ui/georeferencer)
```

Wire `showCoordDialog` slot in main window to open it; on accept, call `mGcps->appendPoint(...)`.

(End of plan.)

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-02-georeferencer-implementation.md`. Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Best for an 8-task plan of this scope: each task is self-contained, hands off cleanly, and the per-task git commit gives a natural review checkpoint.

2. **Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints for review.

Which approach?
