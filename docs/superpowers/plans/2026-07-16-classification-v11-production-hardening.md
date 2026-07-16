# Classification v1.1 Production Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship Phase 10A.2 production hardening for pixel classification: feature scaling, ROI source-CRS I/O, safe rectangular Hungarian remap, tiled/compressed GTiff output, viewport-cropped preview, and dirty-close session state.

**Architecture:** Surgical patches plus thin helpers — `RsFeatureScaler` and `RsPixelWindow` in `qgis_analysis`; `RsClassifySessionState` in `qgis_app_classify`; extend `RsClassificationTask::Config` for scaler params, GDAL creation options, and optional crop window. No new ML backends; no main-window file split.

**Tech Stack:** C++17 / Qt6 / OpenCV 4–5 (`ml`) / GDAL ≥ 3.4 / Catch2 / vendored QGIS core+gui

**Spec:** `docs/superpowers/specs/2026-07-16-classification-v11-production-hardening-design.md`

## Global Constraints

- OpenCV required (`SICNU_HAS_OPENCV`); all new analysis sources go inside the existing `if(SICNU_HAS_OPENCV)` block in `src/analysis/classification/CMakeLists.txt`
- Settings keys under `Classification/` via `QSettings`
- Naming: new types `Rs*`; do not introduce RF/UNet/OBIA
- Build: `cd /home/kevin/projects/exp-rs/build && cmake .. -DOpenCV_DIR=/usr/lib/cmake/opencv5 && make -j$(nproc) <target>`
- Test: `cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R '<TEST_CASE substring>'`
- Commits: `feat(classify):` / `test(classify):` / `chore(classify):`
- GUI tests: org/app `SicnuRsTest` / `ClassifyTest` when needed; prefer FastExitListener if linking qgis_core

---

## File map

| Path | Action | Responsibility |
|------|--------|----------------|
| `src/analysis/classification/rs_feature_scaler.h` | Create | Column z-score API + JSON I/O |
| `src/analysis/classification/rs_feature_scaler.cpp` | Create | Implementation |
| `src/analysis/classification/rs_pixel_window.h` | Create | `PixelWindow` + `mapExtentToPixelWindow` |
| `src/analysis/classification/rs_pixel_window.cpp` | Create | GT inverse → clamped pixel rect |
| `src/analysis/classification/rs_hungarian_assignment.h/.cpp` | Modify | Accept rectangular cost; safe pad |
| `src/analysis/classification/rs_roi_io.h/.cpp` | Modify | Document + enforce source CRS save/load transform |
| `src/analysis/classification/CMakeLists.txt` | Modify | Register scaler + pixel_window sources |
| `src/app/classification/rs_classification_task.h/.cpp` | Modify | Config fields; scale tiles; creation options; crop window |
| `src/app/classification/rs_classify_session_state.h/.cpp` | Create | Dirty + settings snapshot |
| `src/app/classification/qgsclassificationmainwindow.h/.cpp` | Modify | Wire scaler, preview window, session, closeEvent, ROI CRS |
| `src/app/classification/CMakeLists.txt` | Modify | Add session_state sources |
| `src/app/classification/rs_classifier_load_dialog.cpp` | Modify | Optional: surface scale.json path note (if needed) |
| `tests/test_feature_scaler.cpp` | Create | Scaler unit tests |
| `tests/test_pixel_window.cpp` | Create | Viewport→pixel clamp |
| `tests/test_classify_session_state.cpp` | Create | Dirty + snapshot |
| `tests/test_hungarian_assignment.cpp` | Modify | Rectangular cases |
| `tests/test_roi_io.cpp` | Modify | Source CRS round-trip |
| `tests/test_classifier_svm.cpp` | Modify | Multi-scale bands + scaler |
| `tests/test_classification_e2e.cpp` | Modify | Smoke creation options path |
| `tests/CMakeLists.txt` | Modify | Register new tests |

---

## Conventions

- **TDD:** Red → Green → Refactor per task
- **Catch2 tags:** `[classify][scaler]`, `[classify][window]`, `[classify][session]`, existing tags unchanged
- **Lessons:** `ctest -R` matches TEST_CASE name; OpenCV must be ON; GUI tests may need `std::_Exit` FastExitListener

---

### Task 1: `RsFeatureScaler`

**Files:**
- Create: `src/analysis/classification/rs_feature_scaler.h`
- Create: `src/analysis/classification/rs_feature_scaler.cpp`
- Modify: `src/analysis/classification/CMakeLists.txt`
- Create: `tests/test_feature_scaler.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  class RsFeatureScaler {
  public:
    bool fit( const cv::Mat &trainX );           // CV_32F NxB
    cv::Mat transform( const cv::Mat &X ) const; // same shape CV_32F
    bool isFitted() const;
    int bandCount() const;
    bool saveJson( const QString &path ) const;
    bool loadJson( const QString &path );
  };
  ```
- Consumes: OpenCV `cv::Mat`, Qt `QString`/`QJsonDocument`

- [ ] **Step 1.1: Write failing test**

Create `tests/test_feature_scaler.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <opencv2/core.hpp>
#include "rs_feature_scaler.h"

using Catch::Approx;

TEST_CASE( "FeatureScaler: fit then transform has ~0 mean", "[classify][scaler]" )
{
  cv::Mat X( 100, 2, CV_32F );
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( i, 0 ) = 10.0f + static_cast<float>( i % 5 );
    X.at<float>( i, 1 ) = 1000.0f + static_cast<float>( i % 7 );
  }
  RsFeatureScaler s;
  REQUIRE( s.fit( X ) );
  REQUIRE( s.isFitted() );
  const cv::Mat Z = s.transform( X );
  REQUIRE( Z.rows == 100 );
  REQUIRE( Z.cols == 2 );
  cv::Scalar mean, stddev;
  cv::meanStdDev( Z.col( 0 ), mean, stddev );
  REQUIRE( mean[0] == Approx( 0.0 ).margin( 1e-3 ) );
  REQUIRE( stddev[0] == Approx( 1.0 ).margin( 1e-2 ) );
}

TEST_CASE( "FeatureScaler: constant column uses std=1", "[classify][scaler]" )
{
  cv::Mat X( 20, 1, CV_32F, cv::Scalar( 5.0f ) );
  RsFeatureScaler s;
  REQUIRE( s.fit( X ) );
  const cv::Mat Z = s.transform( X );
  for ( int i = 0; i < 20; ++i )
    REQUIRE( Z.at<float>( i, 0 ) == Approx( 0.0f ).margin( 1e-5f ) );
}

TEST_CASE( "FeatureScaler: JSON round-trip", "[classify][scaler]" )
{
  cv::Mat X( 50, 2, CV_32F );
  cv::randn( X, 0, 1 );
  X.col( 0 ) *= 50.0f;
  X.col( 0 ) += 100.0f;
  RsFeatureScaler a;
  REQUIRE( a.fit( X ) );
  QTemporaryDir dir;
  const QString path = dir.filePath( QStringLiteral( "m.scale.json" ) );
  REQUIRE( a.saveJson( path ) );
  RsFeatureScaler b;
  REQUIRE( b.loadJson( path ) );
  const cv::Mat za = a.transform( X.rowRange( 0, 5 ) );
  const cv::Mat zb = b.transform( X.rowRange( 0, 5 ) );
  for ( int i = 0; i < 5; ++i )
    for ( int j = 0; j < 2; ++j )
      REQUIRE( za.at<float>( i, j ) == Approx( zb.at<float>( i, j ) ).margin( 1e-5 ) );
}
```

- [ ] **Step 1.2: Register test + source in CMake**

In `src/analysis/classification/CMakeLists.txt` inside `if(SICNU_HAS_OPENCV)` `target_sources`:

```cmake
        rs_feature_scaler.cpp
```

In `tests/CMakeLists.txt` near other classify tests (inside OpenCV block):

```cmake
  add_executable(test_feature_scaler test_feature_scaler.cpp)
  target_link_libraries(test_feature_scaler PRIVATE
    Catch2::Catch2WithMain
    qgis_analysis
    Qt6::Core
    ${OpenCV_LIBS}
  )
  target_include_directories(test_feature_scaler PRIVATE
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_feature_scaler)
```

- [ ] **Step 1.3: Run test — expect fail**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. -DOpenCV_DIR=/usr/lib/cmake/opencv5 && make -j$(nproc) test_feature_scaler 2>&1 | tail -20
```

Expected: compile/link fail (missing header/source).

- [ ] **Step 1.4: Implement header + cpp**

`rs_feature_scaler.h`:

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <QString>
#include <opencv2/core.hpp>
#include <vector>

class QGIS_ANALYSIS_EXPORT RsFeatureScaler
{
  public:
    bool fit( const cv::Mat &trainX );
    cv::Mat transform( const cv::Mat &X ) const;
    bool isFitted() const { return mFitted; }
    int bandCount() const { return static_cast<int>( mMean.size() ); }
    bool saveJson( const QString &path ) const;
    bool loadJson( const QString &path );

  private:
    bool mFitted = false;
    std::vector<double> mMean;
    std::vector<double> mStd;
    static constexpr double kMinStd = 1e-6;
};
```

`rs_feature_scaler.cpp` (minimal):

```cpp
#include "rs_feature_scaler.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

bool RsFeatureScaler::fit( const cv::Mat &trainX )
{
  mFitted = false;
  mMean.clear();
  mStd.clear();
  if ( trainX.empty() || trainX.type() != CV_32F || trainX.cols < 1 )
    return false;
  const int B = trainX.cols;
  mMean.resize( B );
  mStd.resize( B );
  for ( int j = 0; j < B; ++j )
  {
    cv::Scalar mean, stddev;
    cv::meanStdDev( trainX.col( j ), mean, stddev );
    mMean[j] = mean[0];
    mStd[j] = ( stddev[0] < kMinStd ) ? 1.0 : stddev[0];
  }
  mFitted = true;
  return true;
}

cv::Mat RsFeatureScaler::transform( const cv::Mat &X ) const
{
  if ( !mFitted || X.empty() || X.cols != static_cast<int>( mMean.size() ) )
    return cv::Mat();
  cv::Mat in = X;
  if ( in.type() != CV_32F )
    X.convertTo( in, CV_32F );
  cv::Mat out = in.clone();
  for ( int j = 0; j < out.cols; ++j )
  {
    const float mean = static_cast<float>( mMean[j] );
    const float stdv = static_cast<float>( mStd[j] );
    for ( int i = 0; i < out.rows; ++i )
      out.at<float>( i, j ) = ( out.at<float>( i, j ) - mean ) / stdv;
  }
  return out;
}

bool RsFeatureScaler::saveJson( const QString &path ) const
{
  if ( !mFitted )
    return false;
  QJsonArray meanArr, stdArr;
  for ( double v : mMean )
    meanArr.append( v );
  for ( double v : mStd )
    stdArr.append( v );
  QJsonObject root;
  root.insert( QStringLiteral( "version" ), 1 );
  root.insert( QStringLiteral( "mean" ), meanArr );
  root.insert( QStringLiteral( "std" ), stdArr );
  QFile f( path );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  f.write( QJsonDocument( root ).toJson( QJsonDocument::Compact ) );
  return true;
}

bool RsFeatureScaler::loadJson( const QString &path )
{
  mFitted = false;
  mMean.clear();
  mStd.clear();
  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return false;
  const QJsonDocument doc = QJsonDocument::fromJson( f.readAll() );
  if ( !doc.isObject() )
    return false;
  const QJsonArray meanArr = doc.object().value( QStringLiteral( "mean" ) ).toArray();
  const QJsonArray stdArr = doc.object().value( QStringLiteral( "std" ) ).toArray();
  if ( meanArr.isEmpty() || meanArr.size() != stdArr.size() )
    return false;
  mMean.resize( meanArr.size() );
  mStd.resize( stdArr.size() );
  for ( int i = 0; i < meanArr.size(); ++i )
  {
    mMean[i] = meanArr[i].toDouble();
    const double s = stdArr[i].toDouble();
    mStd[i] = ( s < kMinStd ) ? 1.0 : s;
  }
  mFitted = true;
  return true;
}
```

- [ ] **Step 1.5: Run tests — expect pass**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) test_feature_scaler && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'FeatureScaler'
```

Expected: 100% pass.

- [ ] **Step 1.6: Commit**

```bash
git add src/analysis/classification/rs_feature_scaler.h \
        src/analysis/classification/rs_feature_scaler.cpp \
        src/analysis/classification/CMakeLists.txt \
        tests/test_feature_scaler.cpp tests/CMakeLists.txt
git commit -m "feat(classify): add RsFeatureScaler with JSON round-trip"
```

---

### Task 2: Wire scaler into train path + Task predict

**Files:**
- Modify: `src/app/classification/rs_classification_task.h`
- Modify: `src/app/classification/rs_classification_task.cpp`
- Modify: `src/app/classification/qgsclassificationmainwindow.cpp` (`applyClassification`, `applyPreview`)
- Modify: `tests/test_classifier_svm.cpp`

**Interfaces:**
- Consumes: `RsFeatureScaler`
- Produces: `Config` fields:
  ```cpp
  bool useScaler = false;
  std::vector<double> scaleMean; // empty if !useScaler
  std::vector<double> scaleStd;
  ```
  Or store fitted `RsFeatureScaler` by value in Config (preferred if copyable — implement copy of mean/std vectors).

**Preferred Config addition:**

```cpp
RsFeatureScaler scaler; // default not fitted → Task skips transform
```

- [ ] **Step 2.1: Extend Config and Task**

In `rs_classification_task.h` Config add:

```cpp
#include "rs_feature_scaler.h"
// ...
RsFeatureScaler scaler; // if isFitted(), transform train already done by caller;
                        // Task still transforms tile X before predict
```

In `run()` after building tile `X` and before `predict`:

```cpp
if ( mCfg.scaler.isFitted() )
{
  X = mCfg.scaler.transform( X );
  if ( X.empty() )
  {
    // close datasets, remove output, set error, return false
  }
}
```

Note: Caller must fit scaler on trainX and replace `cfg.trainX`/`cfg.testX` with scaled matrices before constructing Task. Task only scales **tiles** (and should scale testX again only if caller left them unscaled — **convention: caller scales train/test; Task scales tiles only**).

- [ ] **Step 2.2: Main window train path**

In `applyClassification` and `applyPreview` after `stratifiedSplit`:

```cpp
RsFeatureScaler scaler;
if ( !scaler.fit( split.trainX ) )
{
  statusBar()->showMessage( tr( "特征标准化失败" ), 5000 );
  return;
}
cfg.trainX = scaler.transform( split.trainX );
cfg.trainY = split.trainY;
if ( !split.testX.empty() )
  cfg.testX = scaler.transform( split.testX );
cfg.testY = split.testY;
cfg.scaler = scaler;
```

- [ ] **Step 2.3: SVM multi-scale test**

Append to `tests/test_classifier_svm.cpp`:

```cpp
#include "rs_feature_scaler.h"

TEST_CASE( "SVM RBF: multi-scale bands need scaler for high accuracy",
           "[classify][backend][scaler]" )
{
  cv::RNG rng( 7 );
  cv::Mat X( 600, 2, CV_32F ), y( 600, 1, CV_32S );
  // class1: band0 ~ N(5,1), band1 ~ N(5000,100)
  // class2: band0 ~ N(8,1), band1 ~ N(5200,100)
  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i, 0 ) = 5.0f + static_cast<float>( rng.gaussian( 1.0 ) );
    X.at<float>( i, 1 ) = 5000.0f + static_cast<float>( rng.gaussian( 100.0 ) );
    y.at<int>( i, 0 ) = 1;
    X.at<float>( i + 300, 0 ) = 8.0f + static_cast<float>( rng.gaussian( 1.0 ) );
    X.at<float>( i + 300, 1 ) = 5200.0f + static_cast<float>( rng.gaussian( 100.0 ) );
    y.at<int>( i + 300, 0 ) = 2;
  }
  RsFeatureScaler sc;
  REQUIRE( sc.fit( X ) );
  const cv::Mat Xs = sc.transform( X );
  RsClassifierSvm clf;
  REQUIRE( clf.fit( Xs, y ) );
  const cv::Mat pred = clf.predict( Xs );
  int correct = 0;
  for ( int i = 0; i < pred.rows; ++i )
    if ( pred.at<int>( i, 0 ) == y.at<int>( i, 0 ) )
      ++correct;
  REQUIRE( static_cast<double>( correct ) / pred.rows >= 0.9 );
}
```

- [ ] **Step 2.4: Build and test**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) qgis_app_classify test_classifier_svm test_classification_e2e && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'SVM|Classification E2E|FeatureScaler'
```

- [ ] **Step 2.5: Commit**

```bash
git add src/app/classification/rs_classification_task.h \
        src/app/classification/rs_classification_task.cpp \
        src/app/classification/qgsclassificationmainwindow.cpp \
        tests/test_classifier_svm.cpp
git commit -m "feat(classify): wire feature scaling into train and tile predict"
```

---

### Task 3: Rectangular Hungarian + safe pad

**Files:**
- Modify: `src/analysis/classification/rs_hungarian_assignment.h`
- Modify: `src/analysis/classification/rs_hungarian_assignment.cpp`
- Modify: `tests/test_hungarian_assignment.cpp`
- Modify: `src/app/classification/rs_classification_task.cpp` (if it assumed square only — already pads; ensure uses new API)

**Interfaces:**
- Produces: `solve` accepts non-square `cost`; returns `QVector<int>` length = rows of **padded** matrix, or document: returns length `n` (original rows) with column indices in `[0,m)` or -1.

**Spec choice:** pad to `sz=max(n,m)`, solve, then for `i in [0,n)` if `assign[i] < m` map.

- [ ] **Step 3.1: Failing rectangular tests**

Append to `tests/test_hungarian_assignment.cpp`:

```cpp
TEST_CASE( "Hungarian: 2x3 rectangular maps rows to real columns", "[classify][hungarian]" )
{
  // 2 true classes, 3 clusters; best: class0->c0, class1->c1
  cv::Mat cost = ( cv::Mat_<double>( 2, 3 ) <<
                   0, 5, 5,
                   5, 0, 5 );
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a.size() == 2 );
  REQUIRE( a[0] == 0 );
  REQUIRE( a[1] == 1 );
}

TEST_CASE( "Hungarian: 3x2 rectangular", "[classify][hungarian]" )
{
  cv::Mat cost = ( cv::Mat_<double>( 3, 2 ) <<
                   0, 9,
                   9, 0,
                   5, 5 );
  auto a = RsHungarianAssignment::solve( cost );
  REQUIRE( a.size() == 3 );
  // first two rows should take the two columns
  REQUIRE( ( a[0] == 0 || a[0] == 1 ) );
  REQUIRE( ( a[1] == 0 || a[1] == 1 ) );
  REQUIRE( a[0] != a[1] );
}
```

- [ ] **Step 3.2: Implement pad in `solve`**

Replace non-square rejection with:

```cpp
QVector<int> RsHungarianAssignment::solve( const cv::Mat &cost )
{
  if ( cost.empty() )
    return {};
  const int n = cost.rows;
  const int m = cost.cols;
  const int sz = std::max( n, m );
  constexpr double kPad = 1e9;
  cv::Mat square( sz, sz, CV_64F, cv::Scalar( kPad ) );
  cv::Mat tmp;
  cost.convertTo( tmp, CV_64F );
  tmp.copyTo( square( cv::Rect( 0, 0, m, n ) ) );
  QVector<int> full = solveImplFromSquare( square ); // extract existing impl
  QVector<int> out( n, -1 );
  for ( int i = 0; i < n; ++i )
  {
    const int col = full[i];
    if ( col >= 0 && col < m )
      out[i] = col;
  }
  return out;
}
```

Refactor existing square body into `solveImplFromSquare` private free function.

Update header comment: rectangular accepted.

- [ ] **Step 3.3: Run tests**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) test_hungarian_assignment && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'Hungarian'
```

- [ ] **Step 3.4: Commit**

```bash
git add src/analysis/classification/rs_hungarian_assignment.h \
        src/analysis/classification/rs_hungarian_assignment.cpp \
        tests/test_hungarian_assignment.cpp
git commit -m "feat(classify): rectangular Hungarian with safe pad cost"
```

---

### Task 4: ROI I/O source CRS

**Files:**
- Modify: `src/analysis/classification/rs_roi_io.h` (docs only if signature already has crs)
- Modify: `src/analysis/classification/rs_roi_io.cpp`
- Modify: `src/app/classification/qgsclassificationmainwindow.cpp` (`exportRois`, `loadRois`)
- Modify: `tests/test_roi_io.cpp`

**Interfaces:**
- Consumes: existing `save(path, col, crs)`, `load(path, col, targetCrs)`
- Behavior change:
  - `save`: if `crs.isValid()` write that CRS; else EPSG:4326 + `qWarning`
  - `load`: read layer CRS; if `targetCrs` valid and differs, transform geometries before append

- [ ] **Step 4.1: Read current `save`/`load` implementation; write CRS round-trip test**

Add test that builds collection with a point in EPSG:32650 meters, saves with that CRS, loads with same target, checks coordinates within 1e-3.

Use patterns from existing `tests/test_roi_io.cpp` (QTemporaryDir, QgsApplication if required).

- [ ] **Step 4.2: Implement transform on load**

When reading features:

```cpp
QgsCoordinateTransform xform( layerCrs, targetCrs, QgsProject::instance()->transformContext() );
// if target invalid, skip transform
geom.transform( xform ); // catch QgsCsException → skip feature + log
```

- [ ] **Step 4.3: exportRois passes `m_sourceLayer->crs()`**

Already partially there for load; ensure save uses source CRS not empty/4326 by default.

- [ ] **Step 4.4: Test + commit**

```bash
make -j$(nproc) test_roi_io && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'ROI|roi'
git add src/analysis/classification/rs_roi_io.cpp \
        src/app/classification/qgsclassificationmainwindow.cpp \
        tests/test_roi_io.cpp
git commit -m "feat(classify): ROI shapefile default to source raster CRS"
```

---

### Task 5: GTiff creation options + e2e smoke

**Files:**
- Modify: `src/app/classification/rs_classification_task.h` Config
- Modify: `src/app/classification/rs_classification_task.cpp` Create path
- Modify: `tests/test_classification_e2e.cpp`

**Interfaces:**

```cpp
// Config default
QStringList creationOptions{
  QStringLiteral( "TILED=YES" ),
  QStringLiteral( "COMPRESS=DEFLATE" ),
  QStringLiteral( "PREDICTOR=2" )
};
```

- [ ] **Step 5.1: Implement Create with options + fallback**

```cpp
char **papsz = nullptr;
for ( const QString &o : mCfg.creationOptions )
  papsz = CSLAddString( papsz, o.toUtf8().constData() );
GDALDataset *dstDs = drv->Create( path, W, H, 1, GDT_Byte, papsz );
if ( !dstDs && papsz )
{
  CSLDestroy( papsz );
  papsz = nullptr;
  qWarning() << "Create with options failed; retrying without options";
  dstDs = drv->Create( path, W, H, 1, GDT_Byte, nullptr );
}
else
{
  CSLDestroy( papsz );
}
```

Include `cpl_string.h` for CSL*.

- [ ] **Step 5.2: e2e still passes**

```bash
make -j$(nproc) test_classification_e2e && \
  QT_QPA_PLATFORM=offscreen ./bin/test_classification_e2e
```

- [ ] **Step 5.3: Commit**

```bash
git commit -am "feat(classify): default tiled DEFLATE GeoTIFF creation options"
```

---

### Task 6: Viewport pixel window + cropped preview

**Files:**
- Create: `src/analysis/classification/rs_pixel_window.h`
- Create: `src/analysis/classification/rs_pixel_window.cpp`
- Modify: `src/analysis/classification/CMakeLists.txt`
- Create: `tests/test_pixel_window.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `rs_classification_task.h/.cpp` (crop fields + loop bounds + output size/GT)
- Modify: `qgsclassificationmainwindow.cpp` (`applyPreview`)

**Interfaces:**

```cpp
struct RsPixelWindow {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // inclusive x1/y1 or exclusive — pick exclusive: [x0,x1)
  bool valid = false;
  int width() const { return x1 - x0; }
  int height() const { return y1 - y0; }
};

RsPixelWindow rsMapExtentToPixelWindow( const QgsRectangle &extent,
                                        const double gt[6],
                                        int W, int H );
```

Use **half-open** `[x0,x1) × [y0,y1)` consistently in Task loops.

- [ ] **Step 6.1: Failing pixel window tests**

```cpp
TEST_CASE( "PixelWindow: identity GT maps extent to pixels", "[classify][window]" )
{
  double gt[6] = { 0, 1, 0, 100, 0, -1 }; // north-up
  QgsRectangle ext( 10, 40, 30, 80 ); // xmin,ymin,xmax,ymax
  const auto w = rsMapExtentToPixelWindow( ext, gt, 100, 100 );
  REQUIRE( w.valid );
  REQUIRE( w.x0 == 10 );
  REQUIRE( w.x1 == 30 );
  // y: map 80 → row 20, map 40 → row 60
  REQUIRE( w.y0 == 20 );
  REQUIRE( w.y1 == 60 );
}

TEST_CASE( "PixelWindow: disjoint extent invalid", "[classify][window]" )
{
  double gt[6] = { 0, 1, 0, 100, 0, -1 };
  QgsRectangle ext( 1000, 1000, 1100, 1100 );
  const auto w = rsMapExtentToPixelWindow( ext, gt, 100, 100 );
  REQUIRE_FALSE( w.valid );
}
```

- [ ] **Step 6.2: Implement `rs_pixel_window`**

Invert GT with `GDALInvGeoTransform`; map four corners; floor min / ceil max; clamp to `[0,W]` / `[0,H]`; valid if width>0 && height>0.

- [ ] **Step 6.3: Task crop mode**

Config:

```cpp
bool cropToWindow = false;
RsPixelWindow window;
```

When `cropToWindow && window.valid`:
- Output `Wout=window.width()`, `Hout=window.height()`
- Window GT origin at pixel (x0,y0)
- Tile loop over window only; write relative offsets

- [ ] **Step 6.4: `applyPreview`**

```cpp
const RsPixelWindow win = rsMapExtentToPixelWindow(
  m_canvas->extent(), m_sourceGt, m_sourceWidth, m_sourceHeight );
if ( !win.valid ) {
  statusBar()->showMessage( tr( "视口不在影像范围内" ), 5000 );
  return;
}
cfg.cropToWindow = true;
cfg.window = win;
// do not show accuracy dialog on preview (existing behavior)
```

- [ ] **Step 6.5: Test + commit**

```bash
make -j$(nproc) test_pixel_window qgis_app_classify && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'PixelWindow|Classification E2E'
git add src/analysis/classification/rs_pixel_window.* \
        src/app/classification/rs_classification_task.* \
        src/app/classification/qgsclassificationmainwindow.cpp \
        tests/test_pixel_window.cpp tests/CMakeLists.txt
git commit -m "feat(classify): viewport-cropped classification preview"
```

---

### Task 7: `RsClassifySessionState` + dirty close

**Files:**
- Create: `src/app/classification/rs_classify_session_state.h`
- Create: `src/app/classification/rs_classify_session_state.cpp`
- Modify: `src/app/classification/CMakeLists.txt`
- Create: `tests/test_classify_session_state.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `qgsclassificationmainwindow.h/.cpp`

**Interfaces:** Mirror georef session (dirty, save/restore window, snapshot paths/kind/ratio/tolerance). Settings prefix `Classification/`.

- [ ] **Step 7.1: Session unit tests** (dirty + snapshot round-trip) using `QCoreApplication`/`QSettings` clear — copy pattern from `tests/test_georef_session_state.cpp`.

- [ ] **Step 7.2: Implement session class**

- [ ] **Step 7.3: Wire main window**

- Member `RsClassifySessionState mSession;`
- `connect(m_rois, &RsRoiCollection::changed, ... markDirty)` with suppress flag on load/save
- `closeEvent`: dirty → Save/Discard/Cancel; Save calls export path; then `saveSnapshot` + `saveWindow`
- ctor: `restoreWindow` + restore snapshot into classifier bar fields if widgets exist
- After successful exportRois / loadRois: `clearDirty`

- [ ] **Step 7.4: Build classify app + session test**

```bash
make -j$(nproc) qgis_app_classify test_classify_session_state sicnu_geo_rs && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'SessionState|FeatureScaler|Hungarian|PixelWindow|SVM|Accuracy|Classification'
```

- [ ] **Step 7.5: Commit**

```bash
git add src/app/classification/rs_classify_session_state.* \
        src/app/classification/qgsclassificationmainwindow.* \
        src/app/classification/CMakeLists.txt \
        tests/test_classify_session_state.cpp tests/CMakeLists.txt
git commit -m "feat(classify): dirty close prompt and workflow settings session"
```

---

### Task 8: Integration gate + Lab3 checklist

**Files:** none new (docs optional)

- [ ] **Step 8.1: Full classify ctest band**

```bash
cd /home/kevin/projects/exp-rs/build && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure \
  -R 'FeatureScaler|PixelWindow|Hungarian|ROI|SVM|NormalBayes|KMeans|Accuracy|FloodFill|Rasterizer|Classification|SessionState|save\+load'
```

Expected: all run tests pass (ignore unrelated NOT_BUILT georef/obia placeholders if filter is tight).

- [ ] **Step 8.2: Manual Lab3 smoke (operator)**

Checklist (document result in commit message or progress note):

1. Open sample multi-band raster  
2. Draw ≥2 class ROIs  
3. Train SVM with scaling (default)  
4. Preview with map zoomed to subset — output small, fast  
5. Apply full image — tiled DEFLATE file  
6. Accuracy dialog appears  
7. Export ROI shapefile; re-open project path; reload ROIs without CRS jump  
8. Close with dirty ROI → prompt  

- [ ] **Step 8.3: Final commit if any fixups**

```bash
git status
# fix only if tests failed
```

---

## Spec coverage checklist

| Spec ID | Task |
|---------|------|
| P1 Feature scaler | Task 1–2 |
| P2 ROI CRS | Task 4 |
| P3 Hungarian rect | Task 3 |
| P4 GTiff options | Task 5 |
| P5 Viewport preview | Task 6 |
| P6 Session/dirty close | Task 7 |
| Done: ctest + Lab3 | Task 8 |
| Out of scope RF/OBIA/split | Not scheduled |

## Placeholder scan

None intentional. All steps include concrete paths, commands, and code.

## Type consistency

- `RsFeatureScaler` used in Config by value  
- `RsPixelWindow` half-open intervals  
- Hungarian `solve` returns length = original row count  
- Settings prefix `Classification/`  
- Session type name `RsClassifySessionState` (not Georef)

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-16-classification-v11-production-hardening.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session with executing-plans and checkpoints  

Which approach?
