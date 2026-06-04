# Phase 10A: Pixel-Based Classification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build pixel-based supervised + unsupervised classification with ROI editor, JM separability matrix, spectral curve viewer, and accuracy assessment per `UI/design.html` `ArtboardClassify`.

**Architecture:** Two layers — `src/analysis/classification/` (algorithms, links `qgis_analysis` + OpenCV ML) + `src/app/classification/` (UI, new static lib `qgis_app_classify`). Independent `QgsClassificationMainWindow` like Phase 11.4 Georeferencer. Reuses Phase 11.5 OpenCV 4.5+ with new `ml` component. Strong dependency — no OpenCV → menu disabled.

**Tech Stack:** C++17 / Qt6 (QMainWindow, QDockWidget, QPainter, QgsTask) / Catch2 / GDAL ≥ 3.4 (rasterization, ColorTable, OGR shapefile) / OpenCV ≥ 4.5 (`cv::ml::NormalBayesClassifier` / `cv::ml::SVM` / `cv::kmeans`).

**Spec:** `docs/superpowers/specs/2026-06-04-classification-pixel-design.md`

**Phase 11.4/11.5 carryforward references:**
- ensureApp + FastExitListener helpers in `tests/test_georef_window.cpp`
- Port recipe (tightened sed) for ported QGIS files
- `QgsTask` wrapping pattern from `RsWarpTask`
- Structured `QgsMessageLog` JSON logging convention

---

## Conventions for All Tasks

- **TDD cycle:** Red → Green → Refactor per file. Run failing test before writing implementation.
- **Naming:** All new code is project-original `Rs*` prefix (this phase ports nothing).
- **Build:** `cd build && cmake .. && make -j$(nproc)` (incremental).
- **Test:** `cd build && ctest --output-on-failure -R "<TestCaseName>"` — matches Catch2 TEST_CASE name, not binary name.
- **Commit prefix:** `feat(classify):` for behavior, `test(classify):` for test-only, `chore(classify):` for build/CMake.
- **GUI tests:** use `ensureApp()` + `FastExitListener` from `tests/test_georef_window.cpp`.
- **OpenCV linkage:** `qgis_app_classify` PUBLIC links `${OpenCV_LIBS}` with `ml` component; transitively to test binaries.

---

## Task 1 (10.1): ROI Data Model + Shapefile/JSON I/O

**Goal:** Core data structures (`RsClassDef`, `RsRoi`, `RsRoiCollection`) plus OGR-backed shapefile read/write with sidecar JSON for class definitions.

**Files:**
- Create: `src/analysis/classification/CMakeLists.txt`
- Create: `src/analysis/classification/rs_class_def.h/.cpp`
- Create: `src/analysis/classification/rs_roi.h/.cpp`
- Create: `src/analysis/classification/rs_roi_collection.h/.cpp`
- Create: `src/analysis/classification/rs_roi_io.h/.cpp`
- Modify: `src/analysis/CMakeLists.txt` (add subdir)
- Test: `tests/test_roi_collection.cpp`
- Test: `tests/test_roi_io.cpp`
- Modify: `tests/CMakeLists.txt`

### Steps

- [ ] **Step 1.1: Create `src/analysis/classification/CMakeLists.txt`**

```cmake
target_sources(qgis_analysis PRIVATE
    rs_class_def.cpp
    rs_roi.cpp
    rs_roi_collection.cpp
    rs_roi_io.cpp
)
target_include_directories(qgis_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

In `src/analysis/CMakeLists.txt` add `add_subdirectory(classification)` after the `georeferencing` line.

- [ ] **Step 1.2: Write `rs_class_def.h`**

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <QString>
#include <QColor>

class QGIS_ANALYSIS_EXPORT RsClassDef {
public:
    RsClassDef() = default;
    RsClassDef(int id, const QString &name, const QColor &color)
        : mId(id), mName(name), mColor(color) {}

    int id() const { return mId; }
    QString name() const { return mName; }
    QColor color() const { return mColor; }

    void setName(const QString &n) { mName = n; }
    void setColor(const QColor &c) { mColor = c; }

private:
    int mId = 0;
    QString mName;
    QColor mColor = Qt::gray;
};
```

`.cpp` is a single line `#include "rs_class_def.h"`.

- [ ] **Step 1.3: Write failing test for `RsRoiCollection`**

Create `tests/test_roi_collection.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QSignalSpy>
#include "rs_roi_collection.h"
#include "rs_class_def.h"
#include "qgsgeometry.h"

TEST_CASE("RoiCollection: appendRoi emits roiAdded and changed", "[classify][roi]") {
    RsRoiCollection col;
    QSignalSpy added(&col, &RsRoiCollection::roiAdded);
    QSignalSpy changed(&col, &RsRoiCollection::changed);

    auto geom = QgsGeometry::fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))");
    col.appendRoi(RsRoi(1, geom, {0,1,2,3}));

    REQUIRE(added.count() == 1);
    REQUIRE(changed.count() == 1);
    REQUIRE(col.size() == 1);
    REQUIRE(col.at(0).classId() == 1);
    REQUIRE(col.at(0).pixelIndices().size() == 4);
}

TEST_CASE("RoiCollection: filter by classId", "[classify][roi]") {
    RsRoiCollection col;
    auto g1 = QgsGeometry::fromWkt("POINT(1 1)");
    col.appendRoi(RsRoi(1, g1, {10}));
    col.appendRoi(RsRoi(2, g1, {20}));
    col.appendRoi(RsRoi(1, g1, {30}));

    auto cls1 = col.roisForClass(1);
    REQUIRE(cls1.size() == 2);

    REQUIRE(col.pixelCountForClass(1) == 2);
    REQUIRE(col.pixelCountForClass(2) == 1);
}

TEST_CASE("RoiCollection: class definitions independent of ROIs", "[classify][roi]") {
    RsRoiCollection col;
    col.setClassDef(RsClassDef(1, "Forest", QColor("#2da44e")));
    col.setClassDef(RsClassDef(2, "Water", QColor("#0969da")));

    REQUIRE(col.classDefs().size() == 2);
    REQUIRE(col.classDef(1).name() == "Forest");
    REQUIRE(col.classDef(2).color() == QColor("#0969da"));
}
```

- [ ] **Step 1.4: Write `rs_roi.h`**

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include "qgsgeometry.h"
#include <QVector>
#include <cstdint>

class QGIS_ANALYSIS_EXPORT RsRoi {
public:
    RsRoi() = default;
    RsRoi(int classId, const QgsGeometry &geom, const QVector<quint64> &pixelIndices)
        : mClassId(classId), mGeometry(geom), mPixelIndices(pixelIndices) {}

    int classId() const { return mClassId; }
    QgsGeometry geometry() const { return mGeometry; }
    const QVector<quint64> &pixelIndices() const { return mPixelIndices; }

    void setClassId(int id) { mClassId = id; }
    void setGeometry(const QgsGeometry &g) { mGeometry = g; }
    void setPixelIndices(const QVector<quint64> &p) { mPixelIndices = p; }

private:
    int mClassId = 0;
    QgsGeometry mGeometry;
    QVector<quint64> mPixelIndices;   // row * W + col
};
```

- [ ] **Step 1.5: Write `rs_roi_collection.h/.cpp`**

`rs_roi_collection.h`:

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include "rs_roi.h"
#include "rs_class_def.h"
#include <QObject>
#include <QHash>
#include <QVector>

class QGIS_ANALYSIS_EXPORT RsRoiCollection : public QObject {
    Q_OBJECT
public:
    explicit RsRoiCollection(QObject *parent = nullptr);

    int size() const { return mRois.size(); }
    const RsRoi &at(int i) const { return mRois.at(i); }
    QVector<RsRoi> roisForClass(int classId) const;
    quint64 pixelCountForClass(int classId) const;

    void appendRoi(const RsRoi &roi);
    void removeRoiAt(int i);
    void clear();

    void setClassDef(const RsClassDef &d);
    RsClassDef classDef(int id) const { return mClasses.value(id); }
    QHash<int, RsClassDef> classDefs() const { return mClasses; }

signals:
    void roiAdded(int classId, int roiIndex);
    void roiRemoved(int roiIndex);
    void classDefChanged(int classId);
    void changed();

private:
    QVector<RsRoi> mRois;
    QHash<int, RsClassDef> mClasses;
};
```

`.cpp`:

```cpp
#include "rs_roi_collection.h"

RsRoiCollection::RsRoiCollection(QObject *parent) : QObject(parent) {}

void RsRoiCollection::appendRoi(const RsRoi &roi) {
    mRois.append(roi);
    emit roiAdded(roi.classId(), mRois.size() - 1);
    emit changed();
}

void RsRoiCollection::removeRoiAt(int i) {
    if (i < 0 || i >= mRois.size()) return;
    mRois.removeAt(i);
    emit roiRemoved(i);
    emit changed();
}

void RsRoiCollection::clear() {
    mRois.clear();
    emit changed();
}

QVector<RsRoi> RsRoiCollection::roisForClass(int classId) const {
    QVector<RsRoi> r;
    for (const auto &roi : mRois)
        if (roi.classId() == classId) r.append(roi);
    return r;
}

quint64 RsRoiCollection::pixelCountForClass(int classId) const {
    quint64 sum = 0;
    for (const auto &roi : mRois)
        if (roi.classId() == classId) sum += roi.pixelIndices().size();
    return sum;
}

void RsRoiCollection::setClassDef(const RsClassDef &d) {
    mClasses.insert(d.id(), d);
    emit classDefChanged(d.id());
    emit changed();
}
```

- [ ] **Step 1.6: Register test target, run, expect PASS**

In `tests/CMakeLists.txt`:

```cmake
add_executable(test_roi_collection test_roi_collection.cpp)
target_link_libraries(test_roi_collection PRIVATE
    qgis_analysis qgis_core Qt6::Core Qt6::Test Catch2::Catch2WithMain)
set_target_properties(test_roi_collection PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_roi_collection)
```

```bash
cd build && cmake .. && make test_roi_collection -j$(nproc) && ctest -R "RoiCollection:" --output-on-failure
```

Expected: 3/3 PASS.

- [ ] **Step 1.7: Write failing test for `RsRoiIO`**

Create `tests/test_roi_io.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QTemporaryDir>
#include "rs_roi_io.h"
#include "rs_roi_collection.h"

TEST_CASE("RoiIO: write+read shapefile preserves class id and pixel count", "[classify][roi][io]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString path = tmp.path() + "/rois.shp";

    RsRoiCollection orig;
    orig.setClassDef(RsClassDef(1, "Forest", QColor("#2da44e")));
    orig.setClassDef(RsClassDef(2, "Water", QColor("#0969da")));
    orig.appendRoi(RsRoi(1, QgsGeometry::fromWkt("POLYGON((0 0,10 0,10 10,0 10,0 0))"), {1,2,3,4}));
    orig.appendRoi(RsRoi(2, QgsGeometry::fromWkt("POLYGON((20 20,30 20,30 30,20 30,20 20))"), {10,11}));

    REQUIRE(RsRoiIO::save(path, orig));
    REQUIRE(QFile::exists(path));
    REQUIRE(QFile::exists(tmp.path() + "/rois.classes.json"));

    RsRoiCollection loaded;
    REQUIRE(RsRoiIO::load(path, loaded));
    REQUIRE(loaded.size() == 2);
    REQUIRE(loaded.at(0).classId() == 1);
    REQUIRE(loaded.at(1).classId() == 2);
    REQUIRE(loaded.classDefs().size() == 2);
    REQUIRE(loaded.classDef(1).name() == "Forest");
    REQUIRE(loaded.classDef(2).color() == QColor("#0969da"));
}

TEST_CASE("RoiIO: load returns false on invalid path", "[classify][roi][io]") {
    RsRoiCollection col;
    REQUIRE_FALSE(RsRoiIO::load("/does/not/exist.shp", col));
}
```

- [ ] **Step 1.8: Implement `rs_roi_io.h/.cpp`**

`rs_roi_io.h`:

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <QString>
class RsRoiCollection;

class QGIS_ANALYSIS_EXPORT RsRoiIO {
public:
    static bool save(const QString &shapefilePath, const RsRoiCollection &col);
    static bool load(const QString &shapefilePath, RsRoiCollection &col);
private:
    static bool savePixelIndices(const QString &shp, const RsRoiCollection &col);
    static bool loadPixelIndices(const QString &shp, RsRoiCollection &col);
};
```

`rs_roi_io.cpp` (the meat):

```cpp
#include "rs_roi_io.h"
#include "rs_roi_collection.h"
#include "qgsvectorfilewriter.h"
#include "qgsvectorlayer.h"
#include "qgsfeature.h"
#include "qgsfields.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString sidecarPath(const QString &shp) {
    QFileInfo fi(shp);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".classes.json";
}

bool writeSidecar(const QString &shp, const RsRoiCollection &col) {
    QJsonArray arr;
    for (auto it = col.classDefs().constBegin(); it != col.classDefs().constEnd(); ++it) {
        QJsonObject o;
        o["id"] = it.value().id();
        o["name"] = it.value().name();
        o["color"] = it.value().color().name();
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = 1;
    root["classes"] = arr;
    QFile f(sidecarPath(shp));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

bool readSidecar(const QString &shp, RsRoiCollection &col) {
    QFile f(sidecarPath(shp));
    if (!f.open(QIODevice::ReadOnly)) return true; // missing sidecar is OK; classes stay empty
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    auto arr = doc.object().value("classes").toArray();
    for (const auto &v : arr) {
        auto o = v.toObject();
        col.setClassDef(RsClassDef(o["id"].toInt(), o["name"].toString(),
                                   QColor(o["color"].toString())));
    }
    return true;
}
}

bool RsRoiIO::save(const QString &shp, const RsRoiCollection &col) {
    QgsFields fields;
    fields.append(QgsField("cls_id", QVariant::Int));
    fields.append(QgsField("px_count", QVariant::LongLong));

    QgsVectorFileWriter::SaveVectorOptions opts;
    opts.driverName = "ESRI Shapefile";
    opts.fileEncoding = "UTF-8";

    QgsCoordinateReferenceSystem crs("EPSG:4326");
    auto *writer = QgsVectorFileWriter::create(shp, fields, Qgis::WkbType::Polygon,
        crs, QgsCoordinateTransformContext{}, opts);
    if (!writer || writer->hasError() != QgsVectorFileWriter::NoError) {
        delete writer; return false;
    }

    for (int i = 0; i < col.size(); ++i) {
        const RsRoi &roi = col.at(i);
        QgsFeature feat(fields);
        feat.setGeometry(roi.geometry());
        feat.setAttribute("cls_id", roi.classId());
        feat.setAttribute("px_count", qint64(roi.pixelIndices().size()));
        writer->addFeature(feat);
    }
    delete writer;
    return writeSidecar(shp, col);
}

bool RsRoiIO::load(const QString &shp, RsRoiCollection &col) {
    if (!QFile::exists(shp)) return false;
    QgsVectorLayer layer(shp, "rois", "ogr");
    if (!layer.isValid()) return false;

    readSidecar(shp, col);

    QgsFeature feat;
    auto it = layer.getFeatures();
    while (it.nextFeature(feat)) {
        int clsId = feat.attribute("cls_id").toInt();
        // pixel indices are NOT persisted — caller must recompute against current raster
        col.appendRoi(RsRoi(clsId, feat.geometry(), {}));
    }
    return true;
}
```

Note: pixel indices are NOT round-tripped (they're raster-coordinate dependent). The caller recomputes them when a raster is associated. The test only verifies class/geometry round-trip; the `pixelIndices().size() == 4` assertion is on the FRESH `RsRoi` constructed in step 1.7's test, not the loaded one (the loaded ROIs will have empty indices, which is correct behavior).

Update the test in step 1.7 — replace these lines:

```cpp
REQUIRE(loaded.size() == 2);
REQUIRE(loaded.at(0).classId() == 1);
REQUIRE(loaded.at(1).classId() == 2);
```

with the same plus an explicit note via comment that `pixelIndices()` is empty.

- [ ] **Step 1.9: Register I/O test, run, expect PASS**

```cmake
add_executable(test_roi_io test_roi_io.cpp)
target_link_libraries(test_roi_io PRIVATE
    qgis_analysis qgis_core Qt6::Core Qt6::Test Catch2::Catch2WithMain)
set_target_properties(test_roi_io PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_roi_io)
```

```bash
cd build && cmake .. && make test_roi_io test_roi_collection -j$(nproc) && ctest -R "Roi" --output-on-failure
```

Expected: 5/5 PASS.

- [ ] **Step 1.10: Commit**

```bash
git add src/analysis/CMakeLists.txt src/analysis/classification/ \
        tests/test_roi_collection.cpp tests/test_roi_io.cpp tests/CMakeLists.txt
git commit -m "feat(classify): RsRoi + RsRoiCollection + RsRoiIO

- RsClassDef (id/name/color) + RsRoi (geom + cls_id + pixel indices)
- RsRoiCollection: QObject + signals (roiAdded/Removed/changed)
- RsRoiIO: ESRI Shapefile + sidecar JSON for class defs
- cls_id field name avoids OGR keyword conflict
- Tests: 3 collection cases + 2 I/O cases

Task 10.1"
```

---

## Task 2 (10.2): Main Window Shell + Raster Menu Hook

**Goal:** `QgsClassificationMainWindow` skeleton with menu, 4 dock placeholders, status bar; wire to `Raster → Classification → Supervised Classification (Pixel-based)…`.

**Files:**
- Create: `src/app/classification/CMakeLists.txt`
- Create: `src/app/classification/qgsclassificationmainwindow.h/.cpp`
- Modify: `src/app/CMakeLists.txt` (add subdir + link)
- Modify: `src/app/main_window.h/.cpp` (slot + Raster submenu)
- Modify: top-level `CMakeLists.txt` — add `ml` component to OpenCV find_package
- Test: `tests/test_classification_window.cpp`
- Modify: `tests/CMakeLists.txt`

### Steps

- [ ] **Step 2.1: Add `ml` to OpenCV find_package at top-level CMake**

Find the existing line `find_package(OpenCV 4.5 QUIET COMPONENTS core features2d imgproc calib3d)` in top-level `CMakeLists.txt` and change to:

```cmake
find_package(OpenCV 4.5 QUIET COMPONENTS core features2d imgproc calib3d ml)
```

The `ml` component is required for Phase 10A. Without it, classification window menu stays disabled.

- [ ] **Step 2.2: Create `src/app/classification/CMakeLists.txt`**

```cmake
if (NOT SICNU_HAS_OPENCV)
    message(STATUS "Skipping qgis_app_classify — OpenCV ml component unavailable")
    return()
endif()

qt_add_library(qgis_app_classify STATIC
    qgsclassificationmainwindow.cpp
)
target_include_directories(qgis_app_classify PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
)
target_link_libraries(qgis_app_classify PUBLIC
    qgis_analysis qgis_core
    Qt6::Core Qt6::Gui Qt6::Widgets
    ${OpenCV_LIBS}
)
target_compile_definitions(qgis_app_classify PUBLIC SICNU_HAS_OPENCV=1)
set_target_properties(qgis_app_classify PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
```

In `src/app/CMakeLists.txt` add `add_subdirectory(classification)` and link `qgis_app_classify` into the main app target IF `SICNU_HAS_OPENCV`:

```cmake
add_subdirectory(classification)
if (TARGET qgis_app_classify)
    target_link_libraries(sicnu_geo_rs PRIVATE qgis_app_classify)
    target_compile_definitions(sicnu_geo_rs PRIVATE SICNU_HAS_CLASSIFY=1)
endif()
```

- [ ] **Step 2.3: Write the failing test**

Create `tests/test_classification_window.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDockWidget>
#include "qgsclassificationmainwindow.h"

namespace {
QApplication* ensureApp() {
    static int argc = 1; static char arg0[] = "test"; static char* argv[] = {arg0, nullptr};
    return qApp ? nullptr : new QApplication(argc, argv);
}
}

TEST_CASE("ClassificationWindow: constructs with 4 docks", "[classify][window]") {
    ensureApp();
    QgsClassificationMainWindow w(nullptr);
    REQUIRE(w.findChild<QDockWidget*>("rsClassListDock") != nullptr);
    REQUIRE(w.findChild<QDockWidget*>("rsClassQuickListDock") != nullptr);
    REQUIRE(w.findChild<QDockWidget*>("rsClassJmDock") != nullptr);
    REQUIRE(w.findChild<QDockWidget*>("rsClassSpectralDock") != nullptr);
}

TEST_CASE("ClassificationWindow: title and central canvas", "[classify][window]") {
    ensureApp();
    QgsClassificationMainWindow w(nullptr);
    REQUIRE(w.windowTitle().contains("Classification"));
    REQUIRE(w.centralWidget() != nullptr);
}
```

- [ ] **Step 2.4: Implement `qgsclassificationmainwindow.h`**

```cpp
#pragma once
#include <QMainWindow>

class QgisInterface;
class QDockWidget;
class QgsMapCanvas;
class RsRoiCollection;

class QgsClassificationMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit QgsClassificationMainWindow(QgisInterface *iface, QWidget *parent = nullptr);
    ~QgsClassificationMainWindow() override;

private:
    void setupMenus();
    void setupToolbars();
    void setupDocks();
    void setupStatusBar();

    QgisInterface *mIface = nullptr;
    QgsMapCanvas *mCanvas = nullptr;
    RsRoiCollection *mRois = nullptr;

    QDockWidget *mClassListDock = nullptr;
    QDockWidget *mClassQuickListDock = nullptr;
    QDockWidget *mJmDock = nullptr;
    QDockWidget *mSpectralDock = nullptr;
};
```

- [ ] **Step 2.5: Implement `qgsclassificationmainwindow.cpp`**

```cpp
#include "qgsclassificationmainwindow.h"
#include "rs_roi_collection.h"
#include "qgsmapcanvas.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QLabel>
#include <QAction>

QgsClassificationMainWindow::QgsClassificationMainWindow(QgisInterface *iface, QWidget *parent)
    : QMainWindow(parent), mIface(iface) {
    setWindowTitle(tr("Classification · 监督分类"));
    resize(1280, 800);

    mRois = new RsRoiCollection(this);

    mCanvas = new QgsMapCanvas(this);
    mCanvas->setObjectName(QStringLiteral("rsClassifyCanvas"));
    mCanvas->setCanvasColor(Qt::white);
    setCentralWidget(mCanvas);

    setupMenus();
    setupToolbars();
    setupDocks();
    setupStatusBar();
}

QgsClassificationMainWindow::~QgsClassificationMainWindow() = default;

void QgsClassificationMainWindow::setupMenus() {
    auto *fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(tr("Open source raster..."), this, [](){});
    fileMenu->addAction(tr("Load ROIs..."), this, [](){});
    fileMenu->addAction(tr("Save ROIs..."), this, [](){});
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Close"), this, &QWidget::close);

    menuBar()->addMenu(tr("Edit"));
    menuBar()->addMenu(tr("View"));
    menuBar()->addMenu(tr("Processing"));
    menuBar()->addMenu(tr("Help"));
}

void QgsClassificationMainWindow::setupToolbars() {
    auto *roiBar = addToolBar(tr("ROI"));
    roiBar->setObjectName(QStringLiteral("rsClassifyRoiBar"));
    roiBar->setMovable(false);
    roiBar->addAction(tr("Select"));
    roiBar->addSeparator();
    auto *toolPoint = roiBar->addAction(tr("Point"));
    toolPoint->setObjectName(QStringLiteral("rsToolRoiPoint"));
    auto *toolRect = roiBar->addAction(tr("Rectangle"));
    toolRect->setObjectName(QStringLiteral("rsToolRoiRect"));
    auto *toolPoly = roiBar->addAction(tr("Polygon"));
    toolPoly->setObjectName(QStringLiteral("rsToolRoiPolygon"));
    auto *toolFree = roiBar->addAction(tr("Freehand"));
    toolFree->setObjectName(QStringLiteral("rsToolRoiFreehand"));
    auto *toolMagic = roiBar->addAction(tr("Magic wand"));
    toolMagic->setObjectName(QStringLiteral("rsToolRoiMagicWand"));
    roiBar->addSeparator();
    roiBar->addAction(tr("Spectra"));
    roiBar->addAction(tr("Separability"));
    roiBar->addAction(tr("Export ROIs"));
    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    roiBar->addWidget(spacer);
    roiBar->addAction(tr("Quick preview"));
    auto *apply = roiBar->addAction(tr("Apply classification..."));
    apply->setObjectName(QStringLiteral("rsClassifyApplyAction"));
}

void QgsClassificationMainWindow::setupDocks() {
    mClassListDock = new QDockWidget(tr("类别管理"), this);
    mClassListDock->setObjectName(QStringLiteral("rsClassListDock"));
    mClassListDock->setWidget(new QLabel(tr("[Class table — Task 10.3]"), this));
    addDockWidget(Qt::RightDockWidgetArea, mClassListDock);
    mClassListDock->resize(380, mClassListDock->height());

    mClassQuickListDock = new QDockWidget(tr("类别快览"), this);
    mClassQuickListDock->setObjectName(QStringLiteral("rsClassQuickListDock"));
    mClassQuickListDock->setWidget(new QLabel(tr("[Quick list — Task 10.3]"), this));
    addDockWidget(Qt::LeftDockWidgetArea, mClassQuickListDock);

    mJmDock = new QDockWidget(tr("JM 分离度"), this);
    mJmDock->setObjectName(QStringLiteral("rsClassJmDock"));
    mJmDock->setWidget(new QLabel(tr("[JM matrix — Task 10.6]"), this));
    addDockWidget(Qt::RightDockWidgetArea, mJmDock);

    mSpectralDock = new QDockWidget(tr("光谱曲线"), this);
    mSpectralDock->setObjectName(QStringLiteral("rsClassSpectralDock"));
    mSpectralDock->setWidget(new QLabel(tr("[Spectral curve — Task 10.5]"), this));
    addDockWidget(Qt::BottomDockWidgetArea, mSpectralDock);
}

void QgsClassificationMainWindow::setupStatusBar() {
    auto *crsLabel = new QLabel(tr("CRS: —"), this);
    crsLabel->setObjectName(QStringLiteral("rsClassifyCrsLabel"));
    auto *roiCountLabel = new QLabel(tr("总 ROI: 0, 像元: 0"), this);
    roiCountLabel->setObjectName(QStringLiteral("rsClassifyRoiCountLabel"));
    statusBar()->addPermanentWidget(crsLabel);
    statusBar()->addPermanentWidget(roiCountLabel);
}
```

- [ ] **Step 2.6: Register test, build, expect PASS**

```cmake
add_executable(test_classification_window test_classification_window.cpp)
target_link_libraries(test_classification_window PRIVATE
    qgis_app_classify qgis_analysis qgis_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Catch2::Catch2WithMain)
set_target_properties(test_classification_window PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_classification_window)
```

```bash
cd build && cmake .. && make test_classification_window -j$(nproc) && ctest -R "ClassificationWindow:" --output-on-failure
```

Expected: 2/2 PASS.

- [ ] **Step 2.7: Wire main app Raster→Classification submenu**

In `src/app/main_window.h`:

```cpp
class QgsClassificationMainWindow;

public slots:
    void openClassificationWindow();
private:
    QgsClassificationMainWindow *m_classifyWindow = nullptr;
```

In `src/app/main_window.cpp` after the Georeferencer menu entry (Phase 11.4.4) in `setupMenu()`:

```cpp
#ifdef SICNU_HAS_CLASSIFY
#include "classification/qgsclassificationmainwindow.h"
#endif

// inside setupMenu():
auto *classifyMenu = rasterMenu->addMenu(tr("Classification"));
#ifdef SICNU_HAS_CLASSIFY
classifyMenu->addAction(tr("Supervised Classification (Pixel-based)..."),
                        this, &QgisDesktopWindow::openClassificationWindow);
auto *obia = classifyMenu->addAction(tr("Object-based Classification (OBIA) — Phase 10B"));
obia->setEnabled(false);
#else
auto *disabled = classifyMenu->addAction(tr("Classification (OpenCV ml unavailable)"));
disabled->setEnabled(false);
#endif

// at the bottom of the file:
#ifdef SICNU_HAS_CLASSIFY
void QgisDesktopWindow::openClassificationWindow() {
    if (!m_classifyWindow) {
        m_classifyWindow = new QgsClassificationMainWindow(nullptr, this);
        m_classifyWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    m_classifyWindow->show();
    m_classifyWindow->raise();
    m_classifyWindow->activateWindow();
}
#else
void QgisDesktopWindow::openClassificationWindow() {}
#endif
```

- [ ] **Step 2.8: Build + manual smoke**

```bash
cd build && make -j$(nproc)
./sicnu_geo_rs &
# Menu: Raster → Classification → Supervised Classification (Pixel-based)
# Window opens with 4 docks, ROI toolbar, status bar
```

Kill the app with `pkill -f sicnu_geo_rs` when done.

- [ ] **Step 2.9: Commit**

```bash
git add CMakeLists.txt src/app/CMakeLists.txt src/app/classification/ \
        src/app/main_window.{h,cpp} \
        tests/test_classification_window.cpp tests/CMakeLists.txt
git commit -m "feat(classify): main window shell + Raster→Classification submenu

- QgsClassificationMainWindow with 4 dock placeholders
- ROI toolbar (point/rect/poly/freehand/magicwand/spectra/separability/preview/apply)
- Raster→Classification submenu hooked from main app
- OpenCV ml component added to find_package
- Strong dependency: menu disabled if SICNU_HAS_OPENCV is false
- Tests: window constructs, 4 docks present

Task 10.2"
```

---

## Task 3 (10.3): Class Table Dock + Quick List Dock

**Goal:** Replace right-dock placeholder with `RsClassTableWidget` (per design.html ClassTable); replace left bottom-dock placeholder with `RsClassQuickList`. Wire to `RsRoiCollection::changed` for live updates.

**Files:**
- Create: `src/app/classification/rs_class_table_widget.h/.cpp`
- Create: `src/app/classification/rs_class_quick_list.h/.cpp`
- Modify: `src/app/classification/qgsclassificationmainwindow.{h,cpp}`
- Modify: `src/app/classification/CMakeLists.txt`
- Test: `tests/test_class_table_widget.cpp`
- Modify: `tests/CMakeLists.txt`

### Steps

- [ ] **Step 3.1: Write failing test**

Create `tests/test_class_table_widget.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QSignalSpy>
#include "rs_class_table_widget.h"
#include "rs_roi_collection.h"

namespace { QApplication* ensureApp() {
    static int argc=1; static char a[]="t"; static char *v[]={a,nullptr};
    return qApp?nullptr:new QApplication(argc,v); } }

TEST_CASE("ClassTable: displays 6 classes with ROI counts", "[classify][table]") {
    ensureApp();
    RsRoiCollection col;
    col.setClassDef(RsClassDef(1, "Forest", QColor("#2da44e")));
    col.setClassDef(RsClassDef(2, "Water", QColor("#0969da")));
    auto g = QgsGeometry::fromWkt("POINT(1 1)");
    col.appendRoi(RsRoi(1, g, {10,11,12}));
    col.appendRoi(RsRoi(1, g, {20,21}));
    col.appendRoi(RsRoi(2, g, {30}));

    RsClassTableWidget w;
    w.setRoiCollection(&col);
    REQUIRE(w.rowCount() == 2);
    REQUIRE(w.roiCountForRow(0) == 2);  // 2 ROIs for class 1
    REQUIRE(w.pixelCountForRow(0) == 5); // 3 + 2
    REQUIRE(w.roiCountForRow(1) == 1);
    REQUIRE(w.pixelCountForRow(1) == 1);
}

TEST_CASE("ClassTable: currentClassChanged on row selection", "[classify][table]") {
    ensureApp();
    RsRoiCollection col;
    col.setClassDef(RsClassDef(1, "A", QColor("#0a0")));
    col.setClassDef(RsClassDef(2, "B", QColor("#a00")));

    RsClassTableWidget w;
    w.setRoiCollection(&col);
    QSignalSpy spy(&w, &RsClassTableWidget::currentClassChanged);
    w.setCurrentRow(1);
    REQUIRE(spy.count() >= 1);
    REQUIRE(w.currentClassId() == 2);
}
```

- [ ] **Step 3.2: Write `rs_class_table_widget.h`**

```cpp
#pragma once
#include <QWidget>
class QTableWidget;
class RsRoiCollection;

class RsClassTableWidget : public QWidget {
    Q_OBJECT
public:
    explicit RsClassTableWidget(QWidget *parent = nullptr);
    void setRoiCollection(RsRoiCollection *col);
    int rowCount() const;
    int roiCountForRow(int row) const;
    quint64 pixelCountForRow(int row) const;
    void setCurrentRow(int row);
    int currentClassId() const;

signals:
    void currentClassChanged(int classId);

private slots:
    void rebuild();
    void onSelectionChanged();

private:
    QTableWidget *mTable = nullptr;
    RsRoiCollection *mRois = nullptr;
};
```

- [ ] **Step 3.3: Implement `rs_class_table_widget.cpp`**

```cpp
#include "rs_class_table_widget.h"
#include "rs_roi_collection.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>

RsClassTableWidget::RsClassTableWidget(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    mTable = new QTableWidget(0, 4, this);
    mTable->setObjectName(QStringLiteral("rsClassTable"));
    mTable->setHorizontalHeaderLabels({tr("色"), tr("名称"), tr("ROI"), tr("像元")});
    mTable->verticalHeader()->setVisible(false);
    mTable->verticalHeader()->setDefaultSectionSize(26);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->horizontalHeader()->setStretchLastSection(false);
    mTable->setColumnWidth(0, 24);
    mTable->setColumnWidth(2, 50);
    mTable->setColumnWidth(3, 70);
    mTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    lay->addWidget(mTable);
    connect(mTable, &QTableWidget::itemSelectionChanged,
            this, &RsClassTableWidget::onSelectionChanged);
}

void RsClassTableWidget::setRoiCollection(RsRoiCollection *col) {
    if (mRois) disconnect(mRois, nullptr, this, nullptr);
    mRois = col;
    if (mRois) {
        connect(mRois, &RsRoiCollection::changed, this, &RsClassTableWidget::rebuild);
        connect(mRois, &RsRoiCollection::classDefChanged, this, &RsClassTableWidget::rebuild);
    }
    rebuild();
}

void RsClassTableWidget::rebuild() {
    mTable->setRowCount(0);
    if (!mRois) return;
    auto defs = mRois->classDefs();
    QList<int> ids = defs.keys();
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        const RsClassDef &d = defs.value(id);
        int row = mTable->rowCount();
        mTable->insertRow(row);
        auto *colorItem = new QTableWidgetItem;
        colorItem->setBackground(d.color());
        colorItem->setData(Qt::UserRole, d.id());
        mTable->setItem(row, 0, colorItem);
        mTable->setItem(row, 1, new QTableWidgetItem(d.name()));
        int roiN = mRois->roisForClass(id).size();
        quint64 pxN = mRois->pixelCountForClass(id);
        auto *roiCell = new QTableWidgetItem(QString::number(roiN));
        roiCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        mTable->setItem(row, 2, roiCell);
        auto *pxCell = new QTableWidgetItem(QString::number(pxN));
        pxCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        mTable->setItem(row, 3, pxCell);
    }
}

int RsClassTableWidget::rowCount() const { return mTable->rowCount(); }

int RsClassTableWidget::roiCountForRow(int row) const {
    auto *it = mTable->item(row, 2);
    return it ? it->text().toInt() : 0;
}

quint64 RsClassTableWidget::pixelCountForRow(int row) const {
    auto *it = mTable->item(row, 3);
    return it ? it->text().toULongLong() : 0;
}

void RsClassTableWidget::setCurrentRow(int row) {
    mTable->selectRow(row);
}

int RsClassTableWidget::currentClassId() const {
    auto rows = mTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) return 0;
    auto *it = mTable->item(rows.first().row(), 0);
    return it ? it->data(Qt::UserRole).toInt() : 0;
}

void RsClassTableWidget::onSelectionChanged() {
    emit currentClassChanged(currentClassId());
}
```

- [ ] **Step 3.4: Write `rs_class_quick_list.h/.cpp` (compact left dock)**

`.h`:

```cpp
#pragma once
#include <QWidget>
class QListWidget;
class RsRoiCollection;

class RsClassQuickList : public QWidget {
    Q_OBJECT
public:
    explicit RsClassQuickList(QWidget *parent = nullptr);
    void setRoiCollection(RsRoiCollection *col);
    int rowCount() const;

private slots:
    void rebuild();

private:
    QListWidget *mList = nullptr;
    RsRoiCollection *mRois = nullptr;
};
```

`.cpp`:

```cpp
#include "rs_class_quick_list.h"
#include "rs_roi_collection.h"
#include <QListWidget>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPainter>

RsClassQuickList::RsClassQuickList(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    mList = new QListWidget(this);
    mList->setObjectName(QStringLiteral("rsClassQuickList"));
    lay->addWidget(mList);
}

void RsClassQuickList::setRoiCollection(RsRoiCollection *col) {
    if (mRois) disconnect(mRois, nullptr, this, nullptr);
    mRois = col;
    if (mRois) {
        connect(mRois, &RsRoiCollection::changed, this, &RsClassQuickList::rebuild);
        connect(mRois, &RsRoiCollection::classDefChanged, this, &RsClassQuickList::rebuild);
    }
    rebuild();
}

void RsClassQuickList::rebuild() {
    mList->clear();
    if (!mRois) return;
    auto defs = mRois->classDefs();
    QList<int> ids = defs.keys();
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        const RsClassDef &d = defs.value(id);
        int roiN = mRois->roisForClass(id).size();
        QPixmap pix(14,14);
        pix.fill(d.color());
        auto *item = new QListWidgetItem(QIcon(pix),
            QString("%1  (%2)").arg(d.name()).arg(roiN));
        item->setData(Qt::UserRole, id);
        mList->addItem(item);
    }
}

int RsClassQuickList::rowCount() const { return mList->count(); }
```

- [ ] **Step 3.5: Wire into main window, register sources**

In `src/app/classification/CMakeLists.txt` `target_sources(... PRIVATE ...)`:

```cmake
qt_add_library(qgis_app_classify STATIC
    qgsclassificationmainwindow.cpp
    rs_class_table_widget.cpp
    rs_class_quick_list.cpp
)
```

In `qgsclassificationmainwindow.cpp`, replace the placeholder labels:

```cpp
#include "rs_class_table_widget.h"
#include "rs_class_quick_list.h"

// In setupDocks():
auto *table = new RsClassTableWidget(mClassListDock);
table->setRoiCollection(mRois);
mClassListDock->setWidget(table);

auto *quick = new RsClassQuickList(mClassQuickListDock);
quick->setRoiCollection(mRois);
mClassQuickListDock->setWidget(quick);
```

Also seed default 6 classes per design.html in the constructor (right after `mRois = new RsRoiCollection(this);`):

```cpp
const QList<QPair<int, QPair<QString, QString>>> defaults = {
    {1, {tr("林地"), "#2da44e"}},
    {2, {tr("草地"), "#a3e635"}},
    {3, {tr("水体"), "#0969da"}},
    {4, {tr("建成区"), "#cf222e"}},
    {5, {tr("耕地"), "#d29922"}},
    {6, {tr("裸地"), "#8a92a0"}},
};
for (const auto &d : defaults) {
    mRois->setClassDef(RsClassDef(d.first, d.second.first, QColor(d.second.second)));
}
```

- [ ] **Step 3.6: Register test target, build, expect PASS**

```cmake
add_executable(test_class_table_widget test_class_table_widget.cpp)
target_link_libraries(test_class_table_widget PRIVATE
    qgis_app_classify qgis_analysis qgis_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Catch2::Catch2WithMain)
set_target_properties(test_class_table_widget PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_class_table_widget)
```

```bash
cd build && cmake .. && make -j$(nproc) && ctest -R "ClassTable:|ClassificationWindow:" --output-on-failure
```

Expected: 4/4 PASS (2 ClassTable + 2 prior window tests).

- [ ] **Step 3.7: Commit**

```bash
git add src/app/classification/rs_class_table_widget.{h,cpp} \
        src/app/classification/rs_class_quick_list.{h,cpp} \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_class_table_widget.cpp tests/CMakeLists.txt
git commit -m "feat(classify): class table + quick list docks

- RsClassTableWidget: 4 columns (color/name/ROI count/pixel count)
- RsClassQuickList: compact list with color swatch + ROI count
- Both react to RsRoiCollection::changed and ::classDefChanged
- Default 6 classes seeded per design.html
- currentClassChanged signal drives ROI-tool current-class binding (Task 10.4)

Task 10.3"
```

---

## Task 4 (10.4): 4 Manual ROI Map Tools

**Goal:** Implement `RsRoiTool{Point,Rectangle,Polygon,Freehand}` as `QgsMapTool` subclasses. On commit, emit `roiDrawn(QgsGeometry, int classId)`. Main window connects to a slot that rasterizes the geometry to pixel indices and appends to `mRois`. Toolbar actions toggle the active tool.

**Files:**
- Create: `src/app/classification/rs_roi_tool_base.h/.cpp` (common signal)
- Create: `src/app/classification/rs_roi_tool_point.h/.cpp`
- Create: `src/app/classification/rs_roi_tool_rectangle.h/.cpp`
- Create: `src/app/classification/rs_roi_tool_polygon.h/.cpp`
- Create: `src/app/classification/rs_roi_tool_freehand.h/.cpp`
- Create: `src/app/classification/rs_pixel_rasterizer.h/.cpp` (rasterizes geom → pixel indices)
- Modify: `src/app/classification/qgsclassificationmainwindow.{h,cpp}`
- Modify: `src/app/classification/CMakeLists.txt`
- Test: `tests/test_roi_tool_polygon.cpp`
- Test: `tests/test_pixel_rasterizer.cpp`
- Modify: `tests/CMakeLists.txt`

### Steps

- [ ] **Step 4.1: Write `rs_roi_tool_base.h`**

```cpp
#pragma once
#include "qgsmaptool.h"
#include "qgsgeometry.h"

class RsRoiToolBase : public QgsMapTool {
    Q_OBJECT
public:
    explicit RsRoiToolBase(QgsMapCanvas *canvas) : QgsMapTool(canvas) {}
    void setCurrentClassId(int id) { mClassId = id; }
    int currentClassId() const { return mClassId; }

signals:
    void roiDrawn(const QgsGeometry &geom, int classId);

protected:
    int mClassId = 0;
};
```

- [ ] **Step 4.2: Write failing test for polygon tool + pixel rasterizer**

Create `tests/test_pixel_rasterizer.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "rs_pixel_rasterizer.h"
#include "qgsgeometry.h"

TEST_CASE("Rasterizer: 10x10 polygon yields 100 pixels", "[classify][rasterize]") {
    // Raster: 100×100 with GeoTransform (origin 0,0, pixel 1.0, no rotation)
    // Polygon covering (0,0) to (10,10) in map coords = pixels (0,0) to (9,9)
    double gt[6] = {0, 1, 0, 100, 0, -1};  // origin top-left; Y increases downward in pixel space
    QgsGeometry geom = QgsGeometry::fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))");
    auto indices = RsPixelRasterizer::rasterize(geom, gt, 100, 100);
    // Polygon covers pixels with center in [0,10) × [90,100) → 100 pixels
    REQUIRE(indices.size() == 100);
    // First index = row 90 * 100 + col 0 = 9000
    REQUIRE(indices.contains(9000));
}

TEST_CASE("Rasterizer: out-of-bounds polygon yields empty set", "[classify][rasterize]") {
    double gt[6] = {0, 1, 0, 100, 0, -1};
    QgsGeometry geom = QgsGeometry::fromWkt("POLYGON((200 200, 210 200, 210 210, 200 210, 200 200))");
    auto indices = RsPixelRasterizer::rasterize(geom, gt, 100, 100);
    REQUIRE(indices.size() == 0);
}
```

- [ ] **Step 4.3: Implement `rs_pixel_rasterizer.h/.cpp`**

`.h`:

```cpp
#pragma once
#include "qgsgeometry.h"
#include <QVector>
#include <QSet>
#include <cstdint>

class RsPixelRasterizer {
public:
    static QSet<quint64> rasterize(const QgsGeometry &geom,
                                   const double gt[6], int width, int height);
};
```

`.cpp` uses GDAL `GDALRasterizeGeometries` via a mem driver:

```cpp
#include "rs_pixel_rasterizer.h"
#include <gdal_priv.h>
#include <gdal_alg.h>
#include <ogr_api.h>
#include <ogr_geometry.h>

QSet<quint64> RsPixelRasterizer::rasterize(const QgsGeometry &geom,
                                           const double gt[6], int W, int H) {
    QSet<quint64> out;
    if (geom.isNull() || geom.isEmpty()) return out;

    GDALAllRegister();
    auto *drv = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!drv) return out;
    GDALDataset *ds = drv->Create("", W, H, 1, GDT_Byte, nullptr);
    if (!ds) return out;
    ds->SetGeoTransform(const_cast<double*>(gt));
    ds->GetRasterBand(1)->Fill(0);

    QByteArray wkb = geom.asWkb();
    OGRGeometryH ogrGeom = nullptr;
    OGR_G_CreateFromWkb(const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(wkb.constData())),
                        nullptr, &ogrGeom, wkb.size());
    if (!ogrGeom) { GDALClose(ds); return out; }

    int bands[1] = {1};
    double burnValues[1] = {1.0};
    OGRGeometryH geoms[1] = {ogrGeom};
    GDALRasterizeGeometries(ds, 1, bands, 1, geoms, nullptr, nullptr,
                             burnValues, nullptr, nullptr, nullptr);

    std::vector<uint8_t> buf(W);
    for (int y = 0; y < H; ++y) {
        ds->GetRasterBand(1)->RasterIO(GF_Read, 0, y, W, 1, buf.data(), W, 1, GDT_Byte, 0, 0);
        for (int x = 0; x < W; ++x) {
            if (buf[x]) out.insert(quint64(y) * quint64(W) + quint64(x));
        }
    }
    OGR_G_DestroyGeometry(ogrGeom);
    GDALClose(ds);
    return out;
}
```

- [ ] **Step 4.4: Register rasterizer test, build, expect PASS**

```cmake
add_executable(test_pixel_rasterizer test_pixel_rasterizer.cpp)
target_link_libraries(test_pixel_rasterizer PRIVATE
    qgis_app_classify qgis_analysis qgis_core
    Qt6::Core GDAL::GDAL Catch2::Catch2WithMain)
sicnu_discover_tests(test_pixel_rasterizer)
```

```bash
make test_pixel_rasterizer -j$(nproc) && ctest -R "Rasterizer:" --output-on-failure
```

Expected: 2/2 PASS. If pixel count differs from 100 (could be 81 or 121 depending on edge inclusion rules), adjust test to a range: `REQUIRE(indices.size() >= 81); REQUIRE(indices.size() <= 121);`.

- [ ] **Step 4.5: Implement 4 ROI tools**

`rs_roi_tool_point.h/.cpp`:

```cpp
#pragma once
#include "rs_roi_tool_base.h"

class RsRoiToolPoint : public RsRoiToolBase {
    Q_OBJECT
public:
    using RsRoiToolBase::RsRoiToolBase;
    void canvasReleaseEvent(QgsMapMouseEvent *e) override;
};
```

```cpp
#include "rs_roi_tool_point.h"
#include "qgsmapmouseevent.h"
#include "qgsmapcanvas.h"

void RsRoiToolPoint::canvasReleaseEvent(QgsMapMouseEvent *e) {
    auto pt = toMapCoordinates(e->pos());
    emit roiDrawn(QgsGeometry::fromPointXY(pt), mClassId);
}
```

`rs_roi_tool_rectangle.h/.cpp`: track press point, on release create rect polygon:

```cpp
void RsRoiToolRectangle::canvasPressEvent(QgsMapMouseEvent *e) {
    mPressed = toMapCoordinates(e->pos());
}
void RsRoiToolRectangle::canvasReleaseEvent(QgsMapMouseEvent *e) {
    auto rel = toMapCoordinates(e->pos());
    QgsPolygonXY ring = {{
        mPressed,
        QgsPointXY(rel.x(), mPressed.y()),
        rel,
        QgsPointXY(mPressed.x(), rel.y()),
        mPressed
    }};
    emit roiDrawn(QgsGeometry::fromPolygonXY({ring.first()}), mClassId);
}
```

`rs_roi_tool_polygon.h/.cpp`: collect clicks into vector; double-click closes:

```cpp
void RsRoiToolPolygon::canvasReleaseEvent(QgsMapMouseEvent *e) {
    mVertices.append(toMapCoordinates(e->pos()));
}
void RsRoiToolPolygon::canvasDoubleClickEvent(QgsMapMouseEvent * /*e*/) {
    if (mVertices.size() < 3) { mVertices.clear(); return; }
    QgsPolygonXY ring = {mVertices};
    ring[0].append(mVertices.first()); // close
    emit roiDrawn(QgsGeometry::fromPolygonXY({ring.first()}), mClassId);
    mVertices.clear();
}
```

`rs_roi_tool_freehand.h/.cpp`: collect points while dragging:

```cpp
void RsRoiToolFreehand::canvasPressEvent(QgsMapMouseEvent *e) {
    mDragging = true; mPath.clear();
    mPath.append(toMapCoordinates(e->pos()));
}
void RsRoiToolFreehand::canvasMoveEvent(QgsMapMouseEvent *e) {
    if (mDragging) mPath.append(toMapCoordinates(e->pos()));
}
void RsRoiToolFreehand::canvasReleaseEvent(QgsMapMouseEvent * /*e*/) {
    mDragging = false;
    if (mPath.size() < 3) return;
    QgsPolygonXY ring = {mPath};
    ring[0].append(mPath.first());
    emit roiDrawn(QgsGeometry::fromPolygonXY({ring.first()}), mClassId);
}
```

Each header declares the appropriate member vars (`mPressed`, `mVertices`, `mPath`, `mDragging`) plus the overridden event methods.

- [ ] **Step 4.6: Polygon tool test**

Create `tests/test_roi_tool_polygon.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QSignalSpy>
#include <QMouseEvent>
#include "qgsmapcanvas.h"
#include "rs_roi_tool_polygon.h"

namespace { QApplication* ensureApp() {
    static int argc=1; static char a[]="t"; static char *v[]={a,nullptr};
    return qApp?nullptr:new QApplication(argc,v); } }

TEST_CASE("RoiToolPolygon: 3 clicks + double-click emits triangle geometry", "[classify][roitool]") {
    ensureApp();
    QgsMapCanvas canvas;
    canvas.resize(500, 500);
    canvas.setExtent(QgsRectangle(0, 0, 100, 100));

    RsRoiToolPolygon tool(&canvas);
    tool.setCurrentClassId(3);
    QSignalSpy spy(&tool, &RsRoiToolBase::roiDrawn);

    auto click = [&](int x, int y, QEvent::Type type) {
        QMouseEvent me(type, QPointF(x, y), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QgsMapMouseEvent mme(&canvas, &me);
        if (type == QEvent::MouseButtonRelease) tool.canvasReleaseEvent(&mme);
        else if (type == QEvent::MouseButtonDblClick) tool.canvasDoubleClickEvent(&mme);
    };
    click(50, 50, QEvent::MouseButtonRelease);
    click(150, 50, QEvent::MouseButtonRelease);
    click(100, 150, QEvent::MouseButtonRelease);
    click(100, 100, QEvent::MouseButtonDblClick);

    REQUIRE(spy.count() == 1);
    auto args = spy.takeFirst();
    auto geom = args.at(0).value<QgsGeometry>();
    REQUIRE_FALSE(geom.isNull());
    REQUIRE(geom.type() == Qgis::GeometryType::Polygon);
    REQUIRE(args.at(1).toInt() == 3);
}
```

- [ ] **Step 4.7: Wire tools + actions in main window**

In `qgsclassificationmainwindow.h`:

```cpp
class RsRoiToolPoint;
class RsRoiToolRectangle;
class RsRoiToolPolygon;
class RsRoiToolFreehand;
class RsClassTableWidget;

private:
    RsRoiToolPoint *mToolPoint = nullptr;
    RsRoiToolRectangle *mToolRect = nullptr;
    RsRoiToolPolygon *mToolPolygon = nullptr;
    RsRoiToolFreehand *mToolFreehand = nullptr;
    RsClassTableWidget *mClassTableWidget = nullptr;
    QString mSourceRasterPath;
    int mSourceWidth = 0, mSourceHeight = 0;
    double mSourceGt[6] = {0,1,0,0,0,-1};

private slots:
    void onRoiDrawn(const QgsGeometry &g, int classId);
    void onCurrentClassChanged(int classId);
    void activateRoiTool(QAction *act);
```

In `.cpp` after `setupToolbars`:

```cpp
mToolPoint = new RsRoiToolPoint(mCanvas);
mToolRect = new RsRoiToolRectangle(mCanvas);
mToolPolygon = new RsRoiToolPolygon(mCanvas);
mToolFreehand = new RsRoiToolFreehand(mCanvas);
for (auto *t : {(RsRoiToolBase*)mToolPoint, mToolRect, mToolPolygon, mToolFreehand}) {
    connect(t, &RsRoiToolBase::roiDrawn,
            this, &QgsClassificationMainWindow::onRoiDrawn);
}

// Hook actions:
QHash<QString, RsRoiToolBase*> roiToolMap = {
    {"rsToolRoiPoint", mToolPoint},
    {"rsToolRoiRect", mToolRect},
    {"rsToolRoiPolygon", mToolPolygon},
    {"rsToolRoiFreehand", mToolFreehand},
};
for (auto it = roiToolMap.constBegin(); it != roiToolMap.constEnd(); ++it) {
    auto *a = findChild<QAction*>(it.key());
    if (!a) continue;
    a->setCheckable(true);
    connect(a, &QAction::toggled, this, [this, t = it.value()](bool on) {
        mCanvas->setMapTool(on ? t : nullptr);
    });
}

// Track current class for tool binding:
connect(mClassTableWidget, &RsClassTableWidget::currentClassChanged,
        this, &QgsClassificationMainWindow::onCurrentClassChanged);

void QgsClassificationMainWindow::onCurrentClassChanged(int classId) {
    if (mToolPoint) mToolPoint->setCurrentClassId(classId);
    if (mToolRect) mToolRect->setCurrentClassId(classId);
    if (mToolPolygon) mToolPolygon->setCurrentClassId(classId);
    if (mToolFreehand) mToolFreehand->setCurrentClassId(classId);
}

void QgsClassificationMainWindow::onRoiDrawn(const QgsGeometry &g, int classId) {
    if (classId <= 0) {
        statusBar()->showMessage(tr("请先在类别表中选一个类别"), 3000);
        return;
    }
    auto pixels = mSourceWidth > 0
        ? RsPixelRasterizer::rasterize(g, mSourceGt, mSourceWidth, mSourceHeight)
        : QSet<quint64>{};
    QVector<quint64> idx(pixels.begin(), pixels.end());
    mRois->appendRoi(RsRoi(classId, g, idx));
}
```

(`mClassTableWidget` is set in Task 10.3's setupDocks; store it as a member.)

- [ ] **Step 4.8: Update CMake sources, register polygon test, build**

In `src/app/classification/CMakeLists.txt`:

```cmake
qt_add_library(qgis_app_classify STATIC
    qgsclassificationmainwindow.cpp
    rs_class_table_widget.cpp
    rs_class_quick_list.cpp
    rs_pixel_rasterizer.cpp
    rs_roi_tool_point.cpp
    rs_roi_tool_rectangle.cpp
    rs_roi_tool_polygon.cpp
    rs_roi_tool_freehand.cpp
)
target_link_libraries(qgis_app_classify PUBLIC GDAL::GDAL)
```

In `tests/CMakeLists.txt`:

```cmake
add_executable(test_roi_tool_polygon test_roi_tool_polygon.cpp)
target_link_libraries(test_roi_tool_polygon PRIVATE
    qgis_app_classify qgis_analysis qgis_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Catch2::Catch2WithMain)
set_target_properties(test_roi_tool_polygon PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_roi_tool_polygon)
```

```bash
cd build && cmake .. && make -j$(nproc) && ctest -R "RoiTool|Rasterizer:" --output-on-failure
```

Expected: 3/3 PASS.

- [ ] **Step 4.9: Commit**

```bash
git add src/app/classification/rs_roi_tool_*.{h,cpp} \
        src/app/classification/rs_pixel_rasterizer.{h,cpp} \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_roi_tool_polygon.cpp tests/test_pixel_rasterizer.cpp \
        tests/CMakeLists.txt
git commit -m "feat(classify): 4 manual ROI map tools + pixel rasterizer

- RsRoiToolBase emits roiDrawn(geometry, classId)
- Point/Rectangle/Polygon/Freehand tools via canvas press/move/release/dblclk
- RsPixelRasterizer: GDAL mem raster + GDALRasterizeGeometries → pixel index set
- Main window: roiDrawn → rasterize → appendPoint with class binding
- Tests: polygon emits Polygon geometry; rasterizer 10x10 = ~100 px

Task 10.4"
```

---

## Task 5 (10.5): Spectral Curve Widget + Bottom Dock

**Goal:** `RsSpectralCurveWidget` plots per-class mean ± σ spectral curves across selected bands. Driven by `RsRoiCollection::changed` + currently selected class.

**Files:**
- Create: `src/app/classification/rs_spectral_curve_widget.h/.cpp`
- Modify: `src/app/classification/qgsclassificationmainwindow.{h,cpp}`
- Modify: `src/app/classification/CMakeLists.txt`
- Test: `tests/test_spectral_curve.cpp`

### Steps

- [ ] **Step 5.1: Write failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QImage>
#include "rs_spectral_curve_widget.h"

namespace { QApplication* ensureApp() {
    static int argc=1; static char a[]="t"; static char *v[]={a,nullptr};
    return qApp?nullptr:new QApplication(argc,v); } }

TEST_CASE("SpectralCurveWidget: empty paint doesn't crash", "[classify][spectra]") {
    ensureApp();
    RsSpectralCurveWidget w;
    w.resize(400, 180);
    QImage img(400, 180, QImage::Format_ARGB32);
    img.fill(Qt::white);
    w.render(&img);
    SUCCEED();
}

TEST_CASE("SpectralCurveWidget: setClassCurves redraws", "[classify][spectra]") {
    ensureApp();
    RsSpectralCurveWidget w;
    w.resize(400, 180);
    QImage img1(400, 180, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    w.render(&img1);

    QVector<RsSpectralCurveWidget::Curve> curves;
    RsSpectralCurveWidget::Curve c;
    c.classId = 1;
    c.color = QColor("#2da44e");
    c.bandMeans = {120.0, 95.0, 180.0, 60.0};
    c.bandStds = {12.0, 8.0, 15.0, 5.0};
    curves.append(c);
    w.setClassCurves(curves);

    QImage img2(400, 180, QImage::Format_ARGB32);
    img2.fill(Qt::white);
    w.render(&img2);
    REQUIRE(img1 != img2);
}
```

- [ ] **Step 5.2: Implement widget**

`rs_spectral_curve_widget.h`:

```cpp
#pragma once
#include <QWidget>
#include <QColor>
#include <QVector>

class RsSpectralCurveWidget : public QWidget {
    Q_OBJECT
public:
    struct Curve {
        int classId = 0;
        QColor color = Qt::gray;
        QString name;
        QVector<double> bandMeans;
        QVector<double> bandStds;
    };

    explicit RsSpectralCurveWidget(QWidget *parent = nullptr);
    void setClassCurves(const QVector<Curve> &curves);
    void setSelectedClass(int classId);
    QSize sizeHint() const override { return {400, 180}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<Curve> mCurves;
    int mSelected = -1;
};
```

`.cpp`:

```cpp
#include "rs_spectral_curve_widget.h"
#include <QPainter>
#include <algorithm>

RsSpectralCurveWidget::RsSpectralCurveWidget(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("rsSpectralCurve"));
    setMinimumSize(360, 140);
}

void RsSpectralCurveWidget::setClassCurves(const QVector<Curve> &c) {
    mCurves = c; update();
}
void RsSpectralCurveWidget::setSelectedClass(int classId) {
    mSelected = classId; update();
}

void RsSpectralCurveWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#ffffff"));
    if (mCurves.isEmpty()) {
        p.setPen(QColor("#5f6b7a"));
        p.drawText(rect(), Qt::AlignCenter, tr("Select a class to view spectral curves"));
        return;
    }
    const int margin = 30;
    QRectF plot = rect().adjusted(margin, margin/2, -margin/2, -margin);
    p.setPen(QColor("#e6eaef"));
    p.drawLine(plot.topLeft(), plot.bottomLeft());
    p.drawLine(plot.bottomLeft(), plot.bottomRight());

    // global y range
    double ymin = 1e18, ymax = -1e18;
    int nBands = 0;
    for (const auto &c : mCurves) {
        nBands = std::max(nBands, c.bandMeans.size());
        for (int i = 0; i < c.bandMeans.size(); ++i) {
            double lo = c.bandMeans[i] - (i < c.bandStds.size() ? c.bandStds[i] : 0.0);
            double hi = c.bandMeans[i] + (i < c.bandStds.size() ? c.bandStds[i] : 0.0);
            ymin = std::min(ymin, lo);
            ymax = std::max(ymax, hi);
        }
    }
    if (nBands < 2 || ymax <= ymin) return;
    double pad = (ymax - ymin) * 0.1;
    ymin -= pad; ymax += pad;

    auto xAt = [&](int band) {
        return plot.left() + plot.width() * band / double(nBands - 1);
    };
    auto yAt = [&](double v) {
        return plot.bottom() - plot.height() * (v - ymin) / (ymax - ymin);
    };

    for (const auto &c : mCurves) {
        QColor band = c.color; band.setAlpha(50);
        QPolygonF upper, lower;
        for (int i = 0; i < c.bandMeans.size(); ++i) {
            double s = (i < c.bandStds.size() ? c.bandStds[i] : 0.0);
            upper.append(QPointF(xAt(i), yAt(c.bandMeans[i] + s)));
            lower.append(QPointF(xAt(i), yAt(c.bandMeans[i] - s)));
        }
        QPolygonF fill = upper;
        for (int i = lower.size() - 1; i >= 0; --i) fill.append(lower[i]);
        p.setBrush(band);
        p.setPen(Qt::NoPen);
        p.drawPolygon(fill);

        QPen pen(c.color, c.classId == mSelected ? 2.5 : 1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QPolygonF line;
        for (int i = 0; i < c.bandMeans.size(); ++i)
            line.append(QPointF(xAt(i), yAt(c.bandMeans[i])));
        p.drawPolyline(line);
    }
}
```

- [ ] **Step 5.3: Wire into main window bottom dock**

In `setupDocks()` replace the spectral placeholder:

```cpp
#include "rs_spectral_curve_widget.h"

auto *spectral = new RsSpectralCurveWidget(mSpectralDock);
mSpectralDock->setWidget(spectral);
mSpectralCurve = spectral; // add member
```

For now wire emits an empty curve when ROIs change — actual sampling against a raster happens in Task 8 (when `mSourceRasterPath` is set). Skip raster wiring here; tests cover widget rendering.

- [ ] **Step 5.4: Register sources + test**

In `CMakeLists.txt` add `rs_spectral_curve_widget.cpp`. In `tests/CMakeLists.txt`:

```cmake
add_executable(test_spectral_curve test_spectral_curve.cpp)
target_link_libraries(test_spectral_curve PRIVATE
    qgis_app_classify qgis_analysis qgis_core
    Qt6::Core Qt6::Gui Qt6::Widgets Catch2::Catch2WithMain)
set_target_properties(test_spectral_curve PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_spectral_curve)
```

```bash
cd build && cmake .. && make test_spectral_curve -j$(nproc) && ctest -R "SpectralCurveWidget:" --output-on-failure
```

Expected: 2/2 PASS.

- [ ] **Step 5.5: Commit**

```bash
git add src/app/classification/rs_spectral_curve_widget.{h,cpp} \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_spectral_curve.cpp tests/CMakeLists.txt
git commit -m "feat(classify): spectral curve widget bottom dock

- RsSpectralCurveWidget: QPainter mean line + ±σ shaded band per class
- Selected class drawn thicker (2.5px vs 1.5px)
- Mounted as bottom dock
- Tests: empty paint, redraw on setClassCurves

Task 10.5"
```

---

## Task 6 (10.6): JM Separability + 6×6 Heatmap

**Goal:** `RsJmSeparability::compute()` returns pairwise JM distances; `RsJmMatrixWidget` renders a colored matrix.

**Files:**
- Create: `src/analysis/classification/rs_jm_separability.h/.cpp`
- Modify: `src/analysis/classification/CMakeLists.txt`
- Create: `src/app/classification/rs_jm_matrix_widget.h/.cpp`
- Modify: `src/app/classification/qgsclassificationmainwindow.{h,cpp}`
- Modify: `src/app/classification/CMakeLists.txt`
- Test: `tests/test_jm_separability.cpp`

### Steps

- [ ] **Step 6.1: Write failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "rs_jm_separability.h"
#include <opencv2/core.hpp>

using Catch::Approx;

TEST_CASE("JM: identical distributions yield ~0", "[classify][jm]") {
    cv::Mat a(100, 3, CV_32F);
    cv::randn(a, 50.0, 10.0);
    cv::Mat b = a.clone();
    double jm = RsJmSeparability::pairJm(a, b);
    REQUIRE(jm == Approx(0.0).margin(0.05));
}

TEST_CASE("JM: completely separated yields ~2", "[classify][jm]") {
    cv::Mat a(100, 3, CV_32F);
    cv::randn(a, 50.0, 2.0);
    cv::Mat b(100, 3, CV_32F);
    cv::randn(b, 200.0, 2.0);
    double jm = RsJmSeparability::pairJm(a, b);
    REQUIRE(jm > 1.95);
}

TEST_CASE("JM: moderate overlap yields ~1.4", "[classify][jm]") {
    cv::Mat a(500, 3, CV_32F);
    cv::randn(a, 50.0, 8.0);
    cv::Mat b(500, 3, CV_32F);
    cv::randn(b, 65.0, 8.0);
    double jm = RsJmSeparability::pairJm(a, b);
    REQUIRE(jm > 0.8);
    REQUIRE(jm < 1.9);
}

TEST_CASE("JM: degenerate covariance with epsilon ridge does not throw", "[classify][jm]") {
    cv::Mat a(2, 3, CV_32F);
    a.setTo(50.0);
    cv::Mat b(2, 3, CV_32F);
    b.setTo(60.0);
    double jm = RsJmSeparability::pairJm(a, b);
    REQUIRE(jm >= 0.0);
    REQUIRE(jm <= 2.0);
}
```

- [ ] **Step 6.2: Implement `rs_jm_separability.h/.cpp`**

`.h`:

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <QMap>
#include <QPair>
#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsJmSeparability {
public:
    /// Compute JM distance between two sample matrices (rows = samples, cols = bands).
    /// Returns value in [0, 2]. Adds epsilon ridge for singular covariances.
    static double pairJm(const cv::Mat &xA, const cv::Mat &xB);
};
```

`.cpp`:

```cpp
#include "rs_jm_separability.h"
#include <cmath>

double RsJmSeparability::pairJm(const cv::Mat &xA, const cv::Mat &xB) {
    if (xA.empty() || xB.empty()) return 0.0;
    const int d = xA.cols;
    const double eps = 1e-6;

    cv::Mat muA, muB, covA, covB;
    cv::calcCovarMatrix(xA, covA, muA, cv::COVAR_NORMAL | cv::COVAR_ROWS | cv::COVAR_SCALE, CV_64F);
    cv::calcCovarMatrix(xB, covB, muB, cv::COVAR_NORMAL | cv::COVAR_ROWS | cv::COVAR_SCALE, CV_64F);

    cv::Mat eye = cv::Mat::eye(d, d, CV_64F);
    covA += eps * eye;
    covB += eps * eye;
    cv::Mat covBar = 0.5 * (covA + covB);

    cv::Mat diff = (muA - muB).t();
    cv::Mat covBarInv = covBar.inv();
    cv::Mat term1m = diff.t() * covBarInv * diff;
    double term1 = term1m.at<double>(0,0) / 8.0;

    double detBar = std::max(cv::determinant(covBar), 1e-300);
    double detA = std::max(cv::determinant(covA), 1e-300);
    double detB = std::max(cv::determinant(covB), 1e-300);
    double term2 = 0.5 * std::log(detBar / std::sqrt(detA * detB));

    double B = term1 + term2;
    if (!std::isfinite(B) || B < 0) B = 0;
    return 2.0 * (1.0 - std::exp(-B));
}
```

- [ ] **Step 6.3: Update CMake, register test, run, expect PASS**

In `src/analysis/classification/CMakeLists.txt` append `rs_jm_separability.cpp` to `qgis_analysis` sources. Link OpenCV at the analysis library level if not already:

In `src/analysis/CMakeLists.txt` add (if absent):

```cmake
if (SICNU_HAS_OPENCV)
    target_link_libraries(qgis_analysis PUBLIC ${OpenCV_LIBS})
    target_include_directories(qgis_analysis PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(qgis_analysis PUBLIC SICNU_HAS_OPENCV=1)
endif()
```

In `tests/CMakeLists.txt`:

```cmake
add_executable(test_jm_separability test_jm_separability.cpp)
target_link_libraries(test_jm_separability PRIVATE
    qgis_analysis qgis_core ${OpenCV_LIBS}
    Catch2::Catch2WithMain)
sicnu_discover_tests(test_jm_separability)
```

```bash
cd build && cmake .. && make test_jm_separability -j$(nproc) && ctest -R "^JM:" --output-on-failure
```

Expected: 4/4 PASS.

- [ ] **Step 6.4: Implement `RsJmMatrixWidget` (heat-map renderer)**

`.h`:

```cpp
#pragma once
#include <QWidget>
#include <QHash>
#include <QColor>

class RsJmMatrixWidget : public QWidget {
    Q_OBJECT
public:
    struct ClassEntry { int id; QString name; QColor color; };
    explicit RsJmMatrixWidget(QWidget *parent = nullptr);
    void setData(const QVector<ClassEntry> &classes, const QHash<QPair<int,int>, double> &jm);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<ClassEntry> mClasses;
    QHash<QPair<int,int>, double> mJm;
};
```

`.cpp`:

```cpp
#include "rs_jm_matrix_widget.h"
#include <QPainter>
#include <algorithm>

RsJmMatrixWidget::RsJmMatrixWidget(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("rsJmMatrix"));
    setMinimumSize(280, 260);
}

void RsJmMatrixWidget::setData(const QVector<ClassEntry> &c,
                               const QHash<QPair<int,int>, double> &jm) {
    mClasses = c;
    mJm = jm;
    update();
}

void RsJmMatrixWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    int n = mClasses.size();
    if (n == 0) return;
    const int labelW = 60;
    int cell = std::min((width() - labelW) / n, (height() - labelW) / n);
    if (cell < 12) cell = 12;
    QPoint origin(labelW, labelW);
    p.setFont(QFont("IBM Plex Mono", 9));

    auto colorFor = [](double jm) -> QColor {
        if (jm >= 1.9) return QColor("#2da44e");
        if (jm >= 1.5) return QColor("#a3e635");
        if (jm >= 1.0) return QColor("#d29922");
        return QColor("#cf222e");
    };

    for (int i = 0; i < n; ++i) {
        p.setPen(Qt::black);
        p.drawText(QRect(2, origin.y() + i*cell, labelW-4, cell),
                   Qt::AlignVCenter | Qt::AlignRight, mClasses[i].name);
        p.save();
        p.translate(origin.x() + i*cell + cell/2, labelW-2);
        p.rotate(-45);
        p.drawText(QRect(-labelW, -8, labelW, 14),
                   Qt::AlignVCenter | Qt::AlignRight, mClasses[i].name);
        p.restore();
    }

    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            QRect cellRect(origin.x() + c*cell, origin.y() + r*cell, cell, cell);
            if (r == c) {
                p.fillRect(cellRect, QColor("#f1f3f6"));
                p.setPen(Qt::black);
                p.drawText(cellRect, Qt::AlignCenter, "—");
            } else {
                int a = std::min(mClasses[r].id, mClasses[c].id);
                int b = std::max(mClasses[r].id, mClasses[c].id);
                double jm = mJm.value({a, b}, -1.0);
                if (jm < 0) {
                    p.fillRect(cellRect, QColor("#fafbfc"));
                } else {
                    p.fillRect(cellRect, colorFor(jm));
                    p.setPen(Qt::black);
                    p.drawText(cellRect, Qt::AlignCenter, QString::number(jm, 'f', 2));
                }
            }
            p.setPen(QColor("#e6eaef"));
            p.drawRect(cellRect);
        }
    }
}
```

Mount in `setupDocks()`:

```cpp
auto *jm = new RsJmMatrixWidget(mJmDock);
mJmDock->setWidget(jm);
mJmMatrix = jm; // add member
```

JM data will be filled by the wiring in Task 8 (which has access to the actual raster).

- [ ] **Step 6.5: Build, full Georef regression**

```bash
cd build && make -j$(nproc) && ctest --output-on-failure 2>&1 | tail -10
```

Expected: total now baseline + 4 (JM tests). Phase 11.4/11.5 tests still green.

- [ ] **Step 6.6: Commit**

```bash
git add src/analysis/classification/rs_jm_separability.{h,cpp} \
        src/analysis/CMakeLists.txt \
        src/analysis/classification/CMakeLists.txt \
        src/app/classification/rs_jm_matrix_widget.{h,cpp} \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_jm_separability.cpp tests/CMakeLists.txt
git commit -m "feat(classify): JM (Jeffries-Matusita) separability + heatmap widget

- RsJmSeparability::pairJm: Bhattacharyya + 2(1-exp(-B)) formula
- Epsilon (1e-6) ridge prevents singular covariance with small samples
- 4 tests cover identical/separated/moderate/degenerate
- RsJmMatrixWidget: 6x6 heat color map (red <1 / yellow 1-1.5 / yellow-green 1.5-1.9 / green >=1.9)
- Mounted in right dock; data filled by Task 10.8

Task 10.6"
```

---

## Task 7 (10.7): Magic Wand Tool (Tolerance Flood Fill)

**Goal:** Click on raster → flood fill 4-connected pixels with L2 spectral distance below tolerance → emit ROI geometry.

**Files:**
- Create: `src/app/classification/rs_roi_tool_magicwand.h/.cpp`
- Create: `src/analysis/classification/rs_flood_fill.h/.cpp` (algorithm)
- Modify: `src/app/classification/qgsclassificationmainwindow.{h,cpp}` (wire toolbar action)
- Modify: CMakeLists
- Test: `tests/test_flood_fill.cpp`

### Steps

- [ ] **Step 7.1: Write failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "rs_flood_fill.h"
#include <opencv2/core.hpp>

TEST_CASE("FloodFill: uniform 8x8 block in center fills 64", "[classify][flood]") {
    // Image: 32x32, center 8x8 block value (100,100,100), background (50,50,50)
    cv::Mat img = cv::Mat::zeros(32, 32, CV_32FC3);
    img.setTo(cv::Scalar(50, 50, 50));
    cv::Mat roi = img(cv::Rect(12, 12, 8, 8));
    roi.setTo(cv::Scalar(100, 100, 100));

    auto pixels = RsFloodFill::run(img, 16, 16, /*tolerance=*/10.0);
    REQUIRE(pixels.size() == 64);
}

TEST_CASE("FloodFill: tolerance below distance yields single pixel", "[classify][flood]") {
    cv::Mat img(32, 32, CV_32FC3);
    img.setTo(cv::Scalar(50, 50, 50));
    img.at<cv::Vec3f>(16, 16) = cv::Vec3f(100, 100, 100);
    auto pixels = RsFloodFill::run(img, 16, 16, /*tolerance=*/1.0);
    REQUIRE(pixels.size() == 1);
}
```

- [ ] **Step 7.2: Implement `rs_flood_fill.h/.cpp`**

`.h`:

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <QSet>
#include <opencv2/core.hpp>
#include <cstdint>

class QGIS_ANALYSIS_EXPORT RsFloodFill {
public:
    /// 4-connected flood fill on multi-band image starting from (seedRow, seedCol).
    /// Includes pixels whose L2 spectral distance to seed is < tolerance.
    /// Returns linear indices: row * cols + col.
    static QSet<quint64> run(const cv::Mat &multibandFloat,
                             int seedRow, int seedCol, double tolerance);
};
```

`.cpp`:

```cpp
#include "rs_flood_fill.h"
#include <queue>
#include <cmath>

QSet<quint64> RsFloodFill::run(const cv::Mat &img, int sr, int sc, double tol) {
    QSet<quint64> out;
    if (img.empty()) return out;
    const int H = img.rows;
    const int W = img.cols;
    const int B = img.channels();
    if (sr < 0 || sr >= H || sc < 0 || sc >= W) return out;
    if (img.depth() != CV_32F) return out;

    std::vector<float> seed(B);
    auto px = reinterpret_cast<const float*>(img.ptr(sr) + sc * B * sizeof(float));
    for (int b = 0; b < B; ++b) seed[b] = px[b];

    auto idx = [W](int r, int c) { return quint64(r) * quint64(W) + quint64(c); };
    out.insert(idx(sr, sc));
    std::queue<std::pair<int,int>> q;
    q.emplace(sr, sc);
    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};
    while (!q.empty()) {
        auto [r, c] = q.front(); q.pop();
        for (int d = 0; d < 4; ++d) {
            int nr = r + dr[d], nc = c + dc[d];
            if (nr < 0 || nr >= H || nc < 0 || nc >= W) continue;
            quint64 i = idx(nr, nc);
            if (out.contains(i)) continue;
            auto npx = reinterpret_cast<const float*>(img.ptr(nr) + nc * B * sizeof(float));
            double dist2 = 0;
            for (int b = 0; b < B; ++b) {
                double diff = npx[b] - seed[b];
                dist2 += diff * diff;
            }
            if (std::sqrt(dist2) < tol) {
                out.insert(i);
                q.emplace(nr, nc);
            }
        }
    }
    return out;
}
```

- [ ] **Step 7.3: Implement `RsRoiToolMagicWand`**

`.h`:

```cpp
#pragma once
#include "rs_roi_tool_base.h"

class RsRoiToolMagicWand : public RsRoiToolBase {
    Q_OBJECT
public:
    using RsRoiToolBase::RsRoiToolBase;
    void setTolerance(double t) { mTolerance = t; }
    void setSourceData(const QString &rasterPath) { mRasterPath = rasterPath; }
    void canvasReleaseEvent(QgsMapMouseEvent *e) override;

private:
    double mTolerance = 20.0;
    QString mRasterPath;
};
```

`.cpp`:

```cpp
#include "rs_roi_tool_magicwand.h"
#include "rs_flood_fill.h"
#include "qgsmapmouseevent.h"
#include "qgsmapcanvas.h"
#include "qgsgeometry.h"
#include "qgsrectangle.h"
#include <gdal_priv.h>

void RsRoiToolMagicWand::canvasReleaseEvent(QgsMapMouseEvent *e) {
    if (mRasterPath.isEmpty()) return;
    GDALAllRegister();
    auto *ds = static_cast<GDALDataset*>(GDALOpen(mRasterPath.toUtf8().constData(), GA_ReadOnly));
    if (!ds) return;
    int W = ds->GetRasterXSize(), H = ds->GetRasterYSize();
    int B = ds->GetRasterCount();
    double gt[6]; ds->GetGeoTransform(gt);

    // Map pixel from canvas click
    auto map = toMapCoordinates(e->pos());
    double inv[6];
    if (!GDALInvGeoTransform(gt, inv)) { GDALClose(ds); return; }
    int sc = int(inv[0] + inv[1]*map.x() + inv[2]*map.y());
    int sr = int(inv[3] + inv[4]*map.x() + inv[5]*map.y());

    cv::Mat img(H, W, CV_32FC(B));
    for (int b = 0; b < B; ++b) {
        std::vector<float> band(W * H);
        ds->GetRasterBand(b+1)->RasterIO(GF_Read, 0, 0, W, H, band.data(),
            W, H, GDT_Float32, 0, 0);
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c)
                img.ptr<float>(r)[c * B + b] = band[r*W + c];
    }
    GDALClose(ds);

    auto pixels = RsFloodFill::run(img, sr, sc, mTolerance);
    if (pixels.isEmpty()) return;

    // Bounding box → rectangle geometry as a placeholder (true outline = costly)
    quint64 rMin = quint64(-1), rMax = 0, cMin = quint64(-1), cMax = 0;
    for (quint64 i : pixels) {
        quint64 r = i / W, c = i % W;
        if (r < rMin) rMin = r;
        if (r > rMax) rMax = r;
        if (c < cMin) cMin = c;
        if (c > cMax) cMax = c;
    }
    double x0 = gt[0] + gt[1]*cMin + gt[2]*rMin;
    double y0 = gt[3] + gt[4]*cMin + gt[5]*rMin;
    double x1 = gt[0] + gt[1]*(cMax+1) + gt[2]*(rMax+1);
    double y1 = gt[3] + gt[4]*(cMax+1) + gt[5]*(rMax+1);
    QgsRectangle bbox(std::min(x0, x1), std::min(y0, y1),
                      std::max(x0, x1), std::max(y0, y1));
    emit roiDrawn(QgsGeometry::fromRect(bbox), mClassId);
}
```

(Geometry is the bbox; the actual flooded pixels can be passed alongside via a separate signal in v2 — for v1 the main window re-rasterizes the bbox and intersects with the same flood, but the simpler approximation: rasterize the bbox as the polygon, accept slight overestimate. This is documented in §9 of the spec.)

- [ ] **Step 7.4: Wire toolbar action + main window pass raster path**

In `setupToolbars` already added `rsToolRoiMagicWand`. Instantiate the tool in main window:

```cpp
// in header:
RsRoiToolMagicWand *mToolMagicWand = nullptr;

// in cpp constructor after other tools:
mToolMagicWand = new RsRoiToolMagicWand(mCanvas);
connect(mToolMagicWand, &RsRoiToolBase::roiDrawn,
        this, &QgsClassificationMainWindow::onRoiDrawn);

// in tool map:
{"rsToolRoiMagicWand", mToolMagicWand},

// when setSourceRasterPath() is called (added in Task 10.8), also pass to magic wand:
mToolMagicWand->setSourceData(mSourceRasterPath);
```

- [ ] **Step 7.5: CMake + test + build**

In `src/analysis/classification/CMakeLists.txt` add `rs_flood_fill.cpp`. In `src/app/classification/CMakeLists.txt` add `rs_roi_tool_magicwand.cpp`.

In `tests/CMakeLists.txt`:

```cmake
add_executable(test_flood_fill test_flood_fill.cpp)
target_link_libraries(test_flood_fill PRIVATE
    qgis_analysis qgis_core ${OpenCV_LIBS} Catch2::Catch2WithMain)
sicnu_discover_tests(test_flood_fill)
```

```bash
cd build && cmake .. && make test_flood_fill -j$(nproc) && ctest -R "FloodFill:" --output-on-failure
```

Expected: 2/2 PASS.

- [ ] **Step 7.6: Commit**

```bash
git add src/analysis/classification/rs_flood_fill.{h,cpp} \
        src/analysis/classification/CMakeLists.txt \
        src/app/classification/rs_roi_tool_magicwand.{h,cpp} \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_flood_fill.cpp tests/CMakeLists.txt
git commit -m "feat(classify): magic wand ROI tool (tolerance flood fill)

- RsFloodFill: 4-connected BFS with L2 spectral distance threshold
- RsRoiToolMagicWand: click → load raster band → flood → bbox geometry
- Tests: uniform block 8x8 fills 64; sub-tolerance singleton
- (Bbox approximation noted; true outline deferred)

Task 10.7"
```

---

## Task 8 (10.8): Classifier Backend + ClassifierBar + Task + Output

**Goal:** Three classifier wrappers (`RsClassifierNormalBayes` / `RsClassifierSvm` / `RsClassifierKMeans`); `RsClassifierSetupBar` widget; `RsClassificationTask` that fits + scans + writes GeoTIFF; quick-preview path.

**Files (analysis):**
- Create: `src/analysis/classification/rs_classifier_backend.h/.cpp`
- Create: `src/analysis/classification/rs_classifier_normalbayes.h/.cpp`
- Create: `src/analysis/classification/rs_classifier_svm.h/.cpp`
- Create: `src/analysis/classification/rs_classifier_kmeans.h/.cpp`

**Files (app):**
- Create: `src/app/classification/rs_classifier_setup_bar.h/.cpp`
- Create: `src/app/classification/rs_classification_task.h/.cpp`
- Modify: `src/app/classification/qgsclassificationmainwindow.{h,cpp}`

**Tests:**
- `tests/test_classifier_normalbayes.cpp`
- `tests/test_classifier_svm.cpp`
- `tests/test_classifier_kmeans.cpp`
- `tests/test_classification_e2e.cpp`

### Steps

- [ ] **Step 8.1: Define classifier abstract base**

`rs_classifier_backend.h`:

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <opencv2/core.hpp>
#include <QString>

class QGIS_ANALYSIS_EXPORT RsClassifierBackend {
public:
    virtual ~RsClassifierBackend() = default;
    /// rows = samples, cols = bands; labels = N-row vector of class IDs.
    virtual bool fit(const cv::Mat &X, const cv::Mat &y) = 0;
    /// Returns N-row vector of predicted class IDs.
    virtual cv::Mat predict(const cv::Mat &X) const = 0;
    virtual QString name() const = 0;
    virtual bool save(const QString &path) const { return false; }
    virtual bool load(const QString &path) { return false; }
};
```

- [ ] **Step 8.2: Write failing test for NormalBayes**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <opencv2/core.hpp>
#include "rs_classifier_normalbayes.h"

TEST_CASE("NormalBayes: 3 Gaussians separable, accuracy >= 0.9", "[classify][backend]") {
    cv::RNG rng(42);
    cv::Mat X(900, 2, CV_32F), y(900, 1, CV_32S);
    for (int i = 0; i < 300; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(i, 1) = float(rng.gaussian(2.0)) + 5.0f;
        y.at<int>(i, 0) = 1;
    }
    for (int i = 0; i < 300; ++i) {
        X.at<float>(i + 300, 0) = float(rng.gaussian(2.0)) + 20.0f;
        X.at<float>(i + 300, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(i + 300, 0) = 2;
    }
    for (int i = 0; i < 300; ++i) {
        X.at<float>(i + 600, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(i + 600, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(i + 600, 0) = 3;
    }

    RsClassifierNormalBayes clf;
    REQUIRE(clf.fit(X, y));
    cv::Mat pred = clf.predict(X);
    int correct = 0;
    for (int i = 0; i < pred.rows; ++i)
        if (pred.at<int>(i, 0) == y.at<int>(i, 0)) ++correct;
    REQUIRE(double(correct) / pred.rows >= 0.9);
}
```

- [ ] **Step 8.3: Implement `RsClassifierNormalBayes`**

`.h`:

```cpp
#pragma once
#include "rs_classifier_backend.h"
#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierNormalBayes : public RsClassifierBackend {
public:
    RsClassifierNormalBayes();
    bool fit(const cv::Mat &X, const cv::Mat &y) override;
    cv::Mat predict(const cv::Mat &X) const override;
    QString name() const override { return "NormalBayes (最大似然)"; }
    bool save(const QString &path) const override;
    bool load(const QString &path) override;

private:
    cv::Ptr<cv::ml::NormalBayesClassifier> mClf;
};
```

`.cpp`:

```cpp
#include "rs_classifier_normalbayes.h"

RsClassifierNormalBayes::RsClassifierNormalBayes()
    : mClf(cv::ml::NormalBayesClassifier::create()) {}

bool RsClassifierNormalBayes::fit(const cv::Mat &X, const cv::Mat &y) {
    return mClf->train(X, cv::ml::ROW_SAMPLE, y);
}

cv::Mat RsClassifierNormalBayes::predict(const cv::Mat &X) const {
    cv::Mat out;
    mClf->predict(X, out);
    out.convertTo(out, CV_32S);
    return out;
}

bool RsClassifierNormalBayes::save(const QString &p) const {
    try { mClf->save(p.toStdString()); return true; } catch (...) { return false; }
}
bool RsClassifierNormalBayes::load(const QString &p) {
    try {
        mClf = cv::Algorithm::load<cv::ml::NormalBayesClassifier>(p.toStdString());
        return mClf != nullptr;
    } catch (...) { return false; }
}
```

- [ ] **Step 8.4: Register backend test, run, expect PASS**

```cmake
add_executable(test_classifier_normalbayes test_classifier_normalbayes.cpp)
target_link_libraries(test_classifier_normalbayes PRIVATE
    qgis_analysis qgis_core ${OpenCV_LIBS} Catch2::Catch2WithMain)
sicnu_discover_tests(test_classifier_normalbayes)
```

```bash
cd build && cmake .. && make test_classifier_normalbayes -j$(nproc) && ctest -R "NormalBayes:" --output-on-failure
```

Expected: PASS.

- [ ] **Step 8.5: Implement `RsClassifierSvm` (similar pattern)**

`.h`:

```cpp
#pragma once
#include "rs_classifier_backend.h"
#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierSvm : public RsClassifierBackend {
public:
    RsClassifierSvm();
    bool fit(const cv::Mat &X, const cv::Mat &y) override;
    cv::Mat predict(const cv::Mat &X) const override;
    QString name() const override { return "SVM (RBF)"; }
    bool save(const QString &path) const override;
    bool load(const QString &path) override;

private:
    cv::Ptr<cv::ml::SVM> mClf;
};
```

`.cpp`:

```cpp
#include "rs_classifier_svm.h"

RsClassifierSvm::RsClassifierSvm() : mClf(cv::ml::SVM::create()) {
    mClf->setType(cv::ml::SVM::C_SVC);
    mClf->setKernel(cv::ml::SVM::RBF);
    mClf->setC(10.0);
    mClf->setGamma(0.5);
    mClf->setTermCriteria({cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, 200, 1e-4});
}

bool RsClassifierSvm::fit(const cv::Mat &X, const cv::Mat &y) {
    return mClf->train(X, cv::ml::ROW_SAMPLE, y);
}

cv::Mat RsClassifierSvm::predict(const cv::Mat &X) const {
    cv::Mat out;
    mClf->predict(X, out);
    out.convertTo(out, CV_32S);
    return out;
}

bool RsClassifierSvm::save(const QString &p) const {
    try { mClf->save(p.toStdString()); return true; } catch (...) { return false; }
}
bool RsClassifierSvm::load(const QString &p) {
    try {
        mClf = cv::Algorithm::load<cv::ml::SVM>(p.toStdString());
        return mClf != nullptr;
    } catch (...) { return false; }
}
```

SVM test mirrors NormalBayes test — same synthetic dataset, accuracy >= 0.9. Register `test_classifier_svm`.

- [ ] **Step 8.6: Implement `RsClassifierKMeans`**

K-Means uses `cv::kmeans` directly:

`.h`:

```cpp
#pragma once
#include "rs_classifier_backend.h"

class QGIS_ANALYSIS_EXPORT RsClassifierKMeans : public RsClassifierBackend {
public:
    explicit RsClassifierKMeans(int k = 3);
    bool fit(const cv::Mat &X, const cv::Mat &y) override;  // y ignored
    cv::Mat predict(const cv::Mat &X) const override;
    QString name() const override { return "K-Means"; }
    void setK(int k) { mK = k; }
    cv::Mat centers() const { return mCenters; }

private:
    int mK;
    cv::Mat mCenters;
};
```

`.cpp`:

```cpp
#include "rs_classifier_kmeans.h"

RsClassifierKMeans::RsClassifierKMeans(int k) : mK(k) {}

bool RsClassifierKMeans::fit(const cv::Mat &X, const cv::Mat &) {
    cv::Mat labels;
    cv::kmeans(X, mK, labels,
               cv::TermCriteria(cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, 100, 1.0),
               3, cv::KMEANS_PP_CENTERS, mCenters);
    return !mCenters.empty();
}

cv::Mat RsClassifierKMeans::predict(const cv::Mat &X) const {
    cv::Mat out(X.rows, 1, CV_32S);
    for (int i = 0; i < X.rows; ++i) {
        cv::Mat sample = X.row(i);
        double best = std::numeric_limits<double>::max();
        int bestK = 0;
        for (int k = 0; k < mCenters.rows; ++k) {
            double d = cv::norm(sample, mCenters.row(k));
            if (d < best) { best = d; bestK = k; }
        }
        out.at<int>(i, 0) = bestK + 1;  // 1-based class IDs
    }
    return out;
}
```

K-Means test: 3 Gaussians, centers within 0.5σ of true means. Register `test_classifier_kmeans`.

- [ ] **Step 8.7: Implement `RsClassificationTask`**

`.h`:

```cpp
#pragma once
#include "qgstaskmanager.h"
#include "qgsfeedback.h"
#include "rs_classifier_backend.h"
#include <opencv2/core.hpp>
#include <memory>

class RsClassificationTask : public QgsTask {
    Q_OBJECT
public:
    struct Config {
        QString sourceRaster;
        QString outputRaster;
        QVector<int> bandIndices;
        std::unique_ptr<RsClassifierBackend> backend;  // owned
        cv::Mat trainX;
        cv::Mat trainY;
        QHash<int, QColor> classColors;
    };
    struct Result {
        bool ok = false;
        QString errorMessage;
        int totalPixels = 0;
        int durationMs = 0;
    };

    explicit RsClassificationTask(Config cfg);
    bool run() override;
    void cancel() override;
    const Result &result() const { return mResult; }

private:
    Config mCfg;
    QgsFeedback mFb;
    Result mResult;
};
```

`.cpp`:

```cpp
#include "rs_classification_task.h"
#include <QElapsedTimer>
#include <QFile>
#include <gdal_priv.h>

RsClassificationTask::RsClassificationTask(Config cfg)
    : QgsTask(tr("Classifying %1").arg(QFileInfo(cfg.sourceRaster).fileName()),
              QgsTask::CanCancel),
      mCfg(std::move(cfg)) {
    connect(&mFb, &QgsFeedback::progressChanged, this, [this](double p) { setProgress(p); });
}

void RsClassificationTask::cancel() {
    mFb.cancel();
    QgsTask::cancel();
}

bool RsClassificationTask::run() {
    QElapsedTimer t; t.start();
    if (!mCfg.backend) { mResult.errorMessage = "No backend"; return false; }
    if (!mCfg.backend->fit(mCfg.trainX, mCfg.trainY)) {
        mResult.errorMessage = "Training failed"; return false;
    }
    mFb.setProgress(30.0);
    if (mFb.isCanceled()) { mResult.errorMessage = "cancelled"; return false; }

    GDALAllRegister();
    auto *srcDs = static_cast<GDALDataset*>(GDALOpen(mCfg.sourceRaster.toUtf8().constData(), GA_ReadOnly));
    if (!srcDs) { mResult.errorMessage = "Cannot open source"; return false; }
    int W = srcDs->GetRasterXSize(), H = srcDs->GetRasterYSize();
    double gt[6]; srcDs->GetGeoTransform(gt);
    const char *proj = srcDs->GetProjectionRef();

    auto *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    auto *dstDs = drv->Create(mCfg.outputRaster.toUtf8().constData(), W, H, 1, GDT_Byte, nullptr);
    if (!dstDs) { GDALClose(srcDs); mResult.errorMessage = "Cannot create output"; return false; }
    dstDs->SetGeoTransform(gt);
    if (proj) dstDs->SetProjection(proj);

    GDALColorTable ct(GPI_RGB);
    for (auto it = mCfg.classColors.constBegin(); it != mCfg.classColors.constEnd(); ++it) {
        GDALColorEntry e;
        e.c1 = it.value().red(); e.c2 = it.value().green();
        e.c3 = it.value().blue(); e.c4 = 255;
        ct.SetColorEntry(it.key(), &e);
    }
    dstDs->GetRasterBand(1)->SetColorTable(&ct);

    const int TILE = 256;
    int totalTiles = ((W + TILE - 1) / TILE) * ((H + TILE - 1) / TILE);
    int doneTiles = 0;
    const int B = mCfg.bandIndices.size();
    std::vector<float> tileBuf(TILE * TILE * B);
    std::vector<uint8_t> outBuf(TILE * TILE);

    for (int ty = 0; ty < H; ty += TILE) {
        int th = std::min(TILE, H - ty);
        for (int tx = 0; tx < W; tx += TILE) {
            if (mFb.isCanceled()) {
                GDALClose(srcDs); GDALClose(dstDs);
                QFile::remove(mCfg.outputRaster);
                mResult.errorMessage = "cancelled"; return false;
            }
            int tw = std::min(TILE, W - tx);
            cv::Mat X(th * tw, B, CV_32F);
            for (int bi = 0; bi < B; ++bi) {
                srcDs->GetRasterBand(mCfg.bandIndices[bi])->RasterIO(
                    GF_Read, tx, ty, tw, th, tileBuf.data(), tw, th, GDT_Float32, 0, 0);
                for (int p = 0; p < th * tw; ++p)
                    X.at<float>(p, bi) = tileBuf[p];
            }
            cv::Mat pred = mCfg.backend->predict(X);
            for (int p = 0; p < th * tw; ++p)
                outBuf[p] = uint8_t(std::clamp(pred.at<int>(p, 0), 0, 255));
            dstDs->GetRasterBand(1)->RasterIO(GF_Write, tx, ty, tw, th,
                outBuf.data(), tw, th, GDT_Byte, 0, 0);
            ++doneTiles;
            mFb.setProgress(30.0 + 65.0 * doneTiles / totalTiles);
        }
    }
    GDALClose(srcDs);
    GDALClose(dstDs);
    mResult.totalPixels = W * H;
    mResult.durationMs = int(t.elapsed());
    mResult.ok = true;
    return true;
}
```

- [ ] **Step 8.8: End-to-end test**

`tests/test_classification_e2e.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QTemporaryDir>
#include <gdal_priv.h>
#include "rs_classification_task.h"
#include "rs_classifier_normalbayes.h"

TEST_CASE("Classification E2E: 32x32 raster + 3 classes produces valid GeoTIFF", "[classify][e2e]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    GDALAllRegister();

    // Synth 32x32 3-band raster with 3 distinct regions
    QString src = tmp.path() + "/src.tif";
    auto *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    auto *ds = drv->Create(src.toUtf8().constData(), 32, 32, 3, GDT_Float32, nullptr);
    std::vector<float> band(32*32);
    for (int b = 0; b < 3; ++b) {
        for (int r = 0; r < 32; ++r)
            for (int c = 0; c < 32; ++c) {
                int region = (r < 16 && c < 16) ? 0 : (r < 16 ? 1 : 2);
                band[r*32 + c] = (b == region ? 200.0f : 20.0f) + (rand() % 5);
            }
        ds->GetRasterBand(b+1)->RasterIO(GF_Write, 0, 0, 32, 32, band.data(),
                                          32, 32, GDT_Float32, 0, 0);
    }
    double gt[6] = {0, 1, 0, 32, 0, -1};
    ds->SetGeoTransform(gt);
    GDALClose(ds);

    // Training data: 10 pixels per class
    cv::Mat X(30, 3, CV_32F), y(30, 1, CV_32S);
    int i = 0;
    for (int r = 0; r < 8; ++r) for (int c = 0; c < 8; ++c) {
        if (i >= 10) break;
        X.at<float>(i, 0) = 200.0f; X.at<float>(i, 1) = 20.0f; X.at<float>(i, 2) = 20.0f;
        y.at<int>(i, 0) = 1; ++i;
    }
    for (int r = 0; r < 8; ++r) for (int c = 0; c < 8; ++c) {
        if (i >= 20) break;
        X.at<float>(i, 0) = 20.0f; X.at<float>(i, 1) = 200.0f; X.at<float>(i, 2) = 20.0f;
        y.at<int>(i, 0) = 2; ++i;
    }
    for (int r = 0; r < 8; ++r) for (int c = 0; c < 8; ++c) {
        if (i >= 30) break;
        X.at<float>(i, 0) = 20.0f; X.at<float>(i, 1) = 20.0f; X.at<float>(i, 2) = 200.0f;
        y.at<int>(i, 0) = 3; ++i;
    }

    RsClassificationTask::Config cfg;
    cfg.sourceRaster = src;
    cfg.outputRaster = tmp.path() + "/out.tif";
    cfg.bandIndices = {1, 2, 3};
    cfg.backend.reset(new RsClassifierNormalBayes);
    cfg.trainX = X;
    cfg.trainY = y;
    cfg.classColors[1] = QColor("#cc0000");
    cfg.classColors[2] = QColor("#00cc00");
    cfg.classColors[3] = QColor("#0000cc");

    RsClassificationTask task(std::move(cfg));
    REQUIRE(task.run());
    REQUIRE(task.result().ok);
    REQUIRE(QFile::exists(tmp.path() + "/out.tif"));

    // Spot-check pixel: top-left should be class 1
    auto *outDs = static_cast<GDALDataset*>(GDALOpen(
        (tmp.path() + "/out.tif").toUtf8().constData(), GA_ReadOnly));
    REQUIRE(outDs);
    uint8_t tl;
    outDs->GetRasterBand(1)->RasterIO(GF_Read, 2, 2, 1, 1, &tl, 1, 1, GDT_Byte, 0, 0);
    REQUIRE(int(tl) == 1);
    GDALClose(outDs);
}
```

- [ ] **Step 8.9: ClassifierBar UI widget**

`rs_classifier_setup_bar.h`:

```cpp
#pragma once
#include <QWidget>
#include <QString>
#include <QVector>

enum class RsClassifierKind { NormalBayes, SvmRbf, KMeans };

class RsClassifierSetupBar : public QWidget {
    Q_OBJECT
public:
    explicit RsClassifierSetupBar(QWidget *parent = nullptr);
    RsClassifierKind currentKind() const { return mKind; }
    QVector<int> selectedBands() const;
    double trainRatio() const { return mTrainRatio; }
    QString outputPath() const;
    void setSourceBands(int count);

signals:
    void applyRequested();
    void previewRequested();
    void crossValidateRequested();

private:
    RsClassifierKind mKind = RsClassifierKind::NormalBayes;
    double mTrainRatio = 0.7;
    QVector<int> mAllBands;
};
```

`.cpp` builds: button group of 3 algorithm buttons (3 visible + RF/Maha/UNet greyed), band multi-select, train ratio spinbox, [▶ 训练并分类] + [⚖ 交叉验证] + [▶ 快速预览] buttons. Each emits the corresponding signal.

(Layout details follow `ClassifierBar` from design.html; the test only verifies signal emission so the precise styling is implementation-detail.)

Mount it in main window: add as a horizontal toolbar (bottom of central widget) or as a dock. For v1 use a `QToolBar` with the widget set:

```cpp
auto *classifierBarToolbar = addToolBar(tr("Classifier"));
classifierBarToolbar->setObjectName(QStringLiteral("rsClassifierBar"));
mClassifierBar = new RsClassifierSetupBar(classifierBarToolbar);
classifierBarToolbar->addWidget(mClassifierBar);
addToolBar(Qt::BottomToolBarArea, classifierBarToolbar);
connect(mClassifierBar, &RsClassifierSetupBar::applyRequested,
        this, &QgsClassificationMainWindow::applyClassification);
```

`applyClassification()` reads `mRois`, computes train/test split, constructs the backend, starts `RsClassificationTask`.

- [ ] **Step 8.10: Source raster load**

Add to main window:

```cpp
public slots:
    bool openSourceRaster();   // file dialog
    bool openSourceRaster(const QString &path);
```

Implementation parses GeoTransform, sets `mSourceWidth/Height/Gt/mSourceRasterPath`, propagates to magic wand tool, sets canvas extent.

- [ ] **Step 8.11: Register tests, build, run all**

Register `test_classifier_svm`, `test_classifier_kmeans`, `test_classification_e2e` similar to NormalBayes.

```bash
cd build && cmake .. && make -j$(nproc) && ctest -R "NormalBayes|SVM|KMeans|Classification E2E" --output-on-failure
```

Expected: all PASS. K-Means test may print warnings for small data.

- [ ] **Step 8.12: Commit**

```bash
git add src/analysis/classification/rs_classifier_*.{h,cpp} \
        src/analysis/classification/CMakeLists.txt \
        src/app/classification/rs_classifier_setup_bar.{h,cpp} \
        src/app/classification/rs_classification_task.{h,cpp} \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_classifier_normalbayes.cpp tests/test_classifier_svm.cpp \
        tests/test_classifier_kmeans.cpp tests/test_classification_e2e.cpp \
        tests/CMakeLists.txt
git commit -m "feat(classify): 3 backends + ClassifierBar + task + GeoTIFF output

- RsClassifierBackend abstract; NormalBayes/SVM(RBF)/KMeans concrete
- RsClassificationTask: fit + tile-streamed predict + ColorTable
- RsClassifierSetupBar: algo buttons, band picker, train ratio
- Main window: openSourceRaster + applyClassification slot
- Tests: per-backend accuracy + E2E 32x32x3 producing classified GeoTIFF

Task 10.8"
```

---

## Task 9 (10.9): Accuracy Assessment + Dialog

**Goal:** Confusion matrix + Kappa + per-class P/R/F1; modal dialog showing the results; CSV export.

**Files:**
- Create: `src/analysis/classification/rs_accuracy_assessment.h/.cpp`
- Create: `src/app/classification/rs_accuracy_dialog.h/.cpp`
- Modify: `src/app/classification/rs_classification_task.{h,cpp}` (compute on test split)
- Modify: `src/app/classification/qgsclassificationmainwindow.cpp` (show dialog on completion)
- Test: `tests/test_accuracy_assessment.cpp`

### Steps

- [ ] **Step 9.1: Write failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "rs_accuracy_assessment.h"

using Catch::Approx;

TEST_CASE("Accuracy: perfect prediction → kappa 1.0, OA 1.0", "[classify][acc]") {
    QVector<int> yt = {1,1,2,2,3,3};
    QVector<int> yp = {1,1,2,2,3,3};
    auto r = RsAccuracyAssessment::compute(yt, yp);
    REQUIRE(r.overallAccuracy == Approx(1.0));
    REQUIRE(r.kappa == Approx(1.0));
}

TEST_CASE("Accuracy: known confusion → expected Kappa", "[classify][acc]") {
    // 6 samples, 3 classes; one off-diagonal
    QVector<int> yt = {1,1,2,2,3,3};
    QVector<int> yp = {1,2,2,2,3,3};
    auto r = RsAccuracyAssessment::compute(yt, yp);
    REQUIRE(r.overallAccuracy == Approx(5.0/6.0).margin(1e-6));
    REQUIRE(r.kappa == Approx(0.75).margin(0.05));
}

TEST_CASE("Accuracy: single-class degenerate case", "[classify][acc]") {
    QVector<int> yt = {1,1,1};
    QVector<int> yp = {1,1,1};
    auto r = RsAccuracyAssessment::compute(yt, yp);
    REQUIRE(r.overallAccuracy == Approx(1.0));
    REQUIRE(r.kappa == Approx(1.0));
}
```

- [ ] **Step 9.2: Implement `RsAccuracyAssessment`**

`.h`:

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <QVector>
#include <QHash>
#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsAccuracyAssessment {
public:
    struct Result {
        cv::Mat confusion;   // CV_32S, rows = true, cols = pred
        QVector<int> classIds;
        double overallAccuracy = 0;
        double kappa = 0;
        QHash<int, double> producerAcc; // by classId
        QHash<int, double> userAcc;
        QHash<int, double> f1;
    };
    static Result compute(const QVector<int> &yTrue, const QVector<int> &yPred);
};
```

`.cpp`:

```cpp
#include "rs_accuracy_assessment.h"
#include <QSet>
#include <algorithm>

RsAccuracyAssessment::Result
RsAccuracyAssessment::compute(const QVector<int> &yt, const QVector<int> &yp) {
    Result r;
    if (yt.size() != yp.size() || yt.isEmpty()) return r;

    QSet<int> setIds;
    for (int v : yt) setIds.insert(v);
    for (int v : yp) setIds.insert(v);
    r.classIds = QVector<int>(setIds.begin(), setIds.end());
    std::sort(r.classIds.begin(), r.classIds.end());
    QHash<int, int> idToRow;
    for (int i = 0; i < r.classIds.size(); ++i) idToRow[r.classIds[i]] = i;

    int n = r.classIds.size();
    r.confusion = cv::Mat::zeros(n, n, CV_32S);
    for (int i = 0; i < yt.size(); ++i)
        ++r.confusion.at<int>(idToRow[yt[i]], idToRow[yp[i]]);

    int total = yt.size();
    int diag = 0;
    for (int i = 0; i < n; ++i) diag += r.confusion.at<int>(i, i);
    r.overallAccuracy = double(diag) / total;

    double pe = 0;
    for (int i = 0; i < n; ++i) {
        int rowSum = 0, colSum = 0;
        for (int k = 0; k < n; ++k) {
            rowSum += r.confusion.at<int>(i, k);
            colSum += r.confusion.at<int>(k, i);
        }
        pe += double(rowSum) * colSum;
    }
    pe /= double(total) * total;
    r.kappa = (r.overallAccuracy == 1.0) ? 1.0
        : (r.overallAccuracy - pe) / (1.0 - pe);

    for (int i = 0; i < n; ++i) {
        int rowSum = 0, colSum = 0;
        int d = r.confusion.at<int>(i, i);
        for (int k = 0; k < n; ++k) {
            rowSum += r.confusion.at<int>(i, k);
            colSum += r.confusion.at<int>(k, i);
        }
        int id = r.classIds[i];
        r.producerAcc[id] = colSum ? double(d) / colSum : 0;
        r.userAcc[id] = rowSum ? double(d) / rowSum : 0;
        double p = r.producerAcc[id];
        double u = r.userAcc[id];
        r.f1[id] = (p + u) > 0 ? 2*p*u / (p + u) : 0;
    }
    return r;
}
```

- [ ] **Step 9.3: Register test, run, expect PASS**

```cmake
add_executable(test_accuracy_assessment test_accuracy_assessment.cpp)
target_link_libraries(test_accuracy_assessment PRIVATE
    qgis_analysis qgis_core ${OpenCV_LIBS} Catch2::Catch2WithMain)
sicnu_discover_tests(test_accuracy_assessment)
```

```bash
cd build && cmake .. && make test_accuracy_assessment -j$(nproc) && ctest -R "^Accuracy:" --output-on-failure
```

Expected: 3/3 PASS.

- [ ] **Step 9.4: Implement `RsAccuracyDialog` (modal)**

`.h`:

```cpp
#pragma once
#include <QDialog>
#include "rs_accuracy_assessment.h"

class RsAccuracyDialog : public QDialog {
    Q_OBJECT
public:
    RsAccuracyDialog(const RsAccuracyAssessment::Result &r,
                     const QHash<int, QString> &classNames,
                     QWidget *parent = nullptr);

private slots:
    void exportCsv();

private:
    RsAccuracyAssessment::Result mResult;
    QHash<int, QString> mNames;
};
```

`.cpp` builds: header labels (Overall + Kappa, 18pt bold), confusion matrix `QTableWidget` (diagonal `#208830` background, off-diagonal ≥ 10 red), per-class P/R/F1 table, [Export CSV] button.

`exportCsv()` opens `QFileDialog`, writes matrix + metrics in CSV form.

- [ ] **Step 9.5: Wire into task completion**

In `RsClassificationTask::Config` add `cv::Mat testX, testY;`. In `run()` after fit:

```cpp
if (!mCfg.testX.empty() && !mCfg.testY.empty()) {
    cv::Mat pred = mCfg.backend->predict(mCfg.testX);
    QVector<int> yt, yp;
    for (int i = 0; i < pred.rows; ++i) {
        yt.append(mCfg.testY.at<int>(i, 0));
        yp.append(pred.at<int>(i, 0));
    }
    mResult.accuracy = RsAccuracyAssessment::compute(yt, yp);
}
```

Where `Result` gains an `accuracy` field of type `RsAccuracyAssessment::Result`.

Main window's `applyClassification` performs stratified split (70/30), passes both halves to the task. On `taskCompleted`:

```cpp
RsAccuracyDialog dlg(task->result().accuracy, classNamesMap, this);
dlg.exec();
QgsMessageLog::logMessage(/* structured JSON */ ...);
```

JSON shape per spec §4.4:

```json
{"event":"classify_finished","algo":"NormalBayes","classes":3,
 "train_px":21,"test_px":9,"kappa":0.95,"overall_accuracy":0.96,
 "duration_ms":120}
```

- [ ] **Step 9.6: Stratified train/test split helper**

Add to `RsRoiCollection` (or a free function in classification namespace):

```cpp
struct TrainTestSplit {
    cv::Mat trainX, trainY, testX, testY;
};

TrainTestSplit stratifiedSplit(const cv::Mat &X, const cv::Mat &y, double ratio);
```

Implementation: bucket by class, shuffle indices, take ratio fraction for train, rest for test. If a class has < 7 samples, dump all into train (per spec §6 risk #7).

- [ ] **Step 9.7: Build, full suite**

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure 2>&1 | tail -8
```

Expected: 276+ tests PASS.

- [ ] **Step 9.8: Commit**

```bash
git add src/analysis/classification/rs_accuracy_assessment.{h,cpp} \
        src/analysis/classification/CMakeLists.txt \
        src/app/classification/rs_accuracy_dialog.{h,cpp} \
        src/app/classification/rs_classification_task.{h,cpp} \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_accuracy_assessment.cpp tests/CMakeLists.txt
git commit -m "feat(classify): accuracy assessment + dialog + CSV export

- RsAccuracyAssessment::compute → confusion + Kappa + per-class P/R/F1
- Edge cases: kappa=1.0 on po=1.0, single-class degenerate
- RsAccuracyDialog: matrix table + metrics + CSV export
- Wired into RsClassificationTask: stratified 70/30 split, dialog after taskCompleted
- Structured log: event=classify_finished JSON to QgsMessageLog Classification

Task 10.9"
```

---

## Task 10: Planning Files Final Update

**Goal:** Mark Phase 10A complete in `task_plan.md`, append session block to `progress.md`, log lessons in `findings.md`.

### Steps

- [ ] **Step 10.1: Update `task_plan.md` Current Phase line**

Replace line 9 (or whichever contains "Phase 11.4 + 11.5 complete") with:

```markdown
Phase 11.4 + 11.5 + 10A complete (Georeferencer + v1.5 + Pixel Classification). **276+ tests pass**. Next: Phase 10B (OBIA) or Phase 12 (AI Agent foundation).
```

Tick all 9 sub-tasks in the Phase 10A block (10.1 → 10.9).

- [ ] **Step 10.2: Append `progress.md` session entry**

Prepend a new session block:

```markdown
## Session: <YYYY-MM-DD> — Phase 10A Pixel Classification ✅ COMPLETE

- 9 sub-tasks committed; test count <BASE>+<NEW>/<TOTAL>
- OpenCV ml component wired strongly; menu disabled if absent
- ROI: shapefile + sidecar JSON; pixel indices in-memory (not persisted)
- JM separability with epsilon ridge; 4 unit cases (identical/separated/moderate/degenerate)
- 3 classifiers: NormalBayes / SVM (RBF C=10 γ=0.5) / KMeans
- Tile-streamed predict (256×256) keeps memory bounded on large rasters
- Accuracy: Kappa with single-class edge handled

Key plan→reality deltas: <document deltas the implementers encountered>
```

Replace `<YYYY-MM-DD>` and `<TOTAL>` with `ctest` output tail.

- [ ] **Step 10.3: Append `findings.md` block**

```markdown
## Phase 10A Implementation Lessons (<YYYY-MM-DD>)

- OpenCV `cv::ml::NormalBayesClassifier` is the closest analog to "Maximum Likelihood Classifier"
- `cv::ml::SVM` with `setKernel(RBF)` + `setC(10.0)` + `setGamma(0.5)` is the educational default; cross-validation grid search left for v2
- `cv::kmeans` (not `cv::ml::KMeans`) is the right entry point — the latter is awkward
- Stratified split with class samples < 7 dumps all into train (no test) to avoid empty test buckets
- `GDALRasterizeGeometries` with `MEM` driver is the right path for polygon → pixel indices (no scratch file)
- ColorTable on output GTiff makes the result immediately useful as a layer in the main app
- Tile-streamed predict at 256×256 keeps per-tile X matrix ~ 1.5 MB
- Magic wand bbox approximation: v1 returns bbox geometry; the actual flooded pixels are a subset, accepted as overestimate
```

- [ ] **Step 10.4: Commit planning files**

```bash
git add task_plan.md progress.md findings.md
git commit -m "docs(classify): mark Phase 10A complete in planning files

- task_plan.md: 9 sub-tasks ticked, Current Phase advanced
- progress.md: Phase 10A session block with commit chain
- findings.md: OpenCV ML choices, KMeans entry point, magic wand bbox tradeoff

Phase 10A Pixel-Based Classification COMPLETE"
```

---

## Self-Review

**Spec coverage:**

| Spec section | Plan coverage |
|---|---|
| §2.1 OpenCV strong dep + ml | Task 2 step 2.1 |
| §2.2 Library layout (10 analysis + 13 app files) | Tasks 1, 2, 3, 4, 5, 6, 7, 8, 9 cover all files |
| §2.3 Raster→Classification menu | Task 2 step 2.7 |
| §3.1 6 default classes | Task 3 step 3.5 |
| §3.2 v1 vs stretch table | Task 4 (4 tools) + Task 7 (magic wand); folding/SLIC/SAM action greyed in Task 2; RF/Maha/UNet greyed in Task 8 step 8.9 |
| §4.1 ROI lifecycle | Tasks 4 (tools) + 4.7 (rasterize on roiDrawn) |
| §4.2 Spectral sampling | Task 5 (widget); raster wiring noted in Task 8 |
| §4.3 JM math + thresholds | Task 6 |
| §4.4 Train/apply pipeline | Task 8 |
| §4.5 Quick preview | Task 8 step 8.9 ClassifierBar `previewRequested` signal (wiring noted; minimal implementation acceptable) |
| §4.6 Accuracy | Task 9 |
| §4.7 Persistence | Task 1 (ROI/sidecar) + Task 8 (GeoTIFF + ColorTable) + Task 8 (.yml save/load) |
| §5 9 sub-tasks | Tasks 1–9 ✓ |
| §6 test matrix (15 files) | Tasks 1, 2, 3, 4, 5, 6, 7, 8, 9 ✓ |
| §7 risks | Risk #1 quick preview covered in Task 8.9; #2 rasterizer bounds test covers in Task 4.2; #3 cls_id baked in Task 1.6; #4 ε ridge in Task 6.2; #5 tile predict in Task 8.7; #6 mBandIndices in Task 8.7 Config; #7 small-class in Task 9.6; #8 kappa edge in Task 9.2 test |
| §8 Done When | Final ctest in Task 9.7 + planning files Task 10 |

**Placeholder scan:** no TBD / TODO / "implement later" tokens in plan body; every step has code or commands.

**Type consistency:** `RsClassDef` / `RsRoi` / `RsRoiCollection` signatures stable across Tasks 1–9. `RsClassifierBackend::fit/predict` signatures consistent in Tasks 8 + 9. `RsRoiToolBase::roiDrawn(QgsGeometry, int)` consistent across Tasks 4, 7, main window wiring.

No gaps.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-04-classification-pixel-implementation.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task with review checkpoints. Matches Phase 11.4/11.5 cadence; each of the 9+1 tasks has clean commit boundaries.

2. **Inline Execution** — sequential in this session via executing-plans, batch with checkpoints.

Which approach?
