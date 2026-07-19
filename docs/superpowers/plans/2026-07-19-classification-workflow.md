# Classification 10A.3 Workflow + Post-Process Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver hybrid wizard/expert classification UX (7 steps), soft gates, full post-process suite, embedded accuracy + export, and complete remaining v1.1 production hardening.

**Architecture:** Keep `QgsClassificationMainWindow` as shell. Add pure `RsClassifyWorkflowController` + `RsClassifyStepperBar` + right-side `QStackedWidget` step panels. Algorithms stay in `qgis_analysis` (`RsPostProcess`, v1.1 helpers). `RsClassificationTask` / new `RsPostProcessTask` run on worker threads. PR1 finishes v1.1 data layer; PR2–4 layer UX and post-process on top.

**Tech Stack:** C++17 / Qt6 Widgets / OpenCV ml / GDAL ≥ 3.4 (`GDALSieveFilter`, `GDALPolygonize`) / Catch2 / QgsTask

**Spec:** `docs/superpowers/specs/2026-07-19-classification-workflow-design.md`  
**v1.1 detail plan (PR1):** `docs/superpowers/plans/2026-07-16-classification-v11-production-hardening.md`

---

## Global Constraints

- OpenCV required (`SICNU_HAS_OPENCV`); analysis sources inside existing `if(SICNU_HAS_OPENCV)` in `src/analysis/classification/CMakeLists.txt`
- App classify sources only when `qgis_app_classify` builds (`src/app/classification/CMakeLists.txt`)
- Settings prefix: `Classification/`
- Naming: `Rs*` types; no RF/UNet/OBIA
- Build: `cd /home/kevin/projects/exp-rs/build && cmake .. -DOpenCV_DIR=/usr/lib/cmake/opencv5 && make -j$(nproc) <target>`
- Test: `cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R '<pattern>'`
- Commits: `feat(classify):` / `test(classify):` / `docs(classify):`
- Do **not** wholesale split `qgsclassificationmainwindow.cpp`; put new logic in new files
- TDD: red → green → commit per task

---

## File map

| Path | PR | Action | Responsibility |
|------|----|--------|----------------|
| `src/analysis/classification/rs_feature_scaler.*` | 1 | Exists — verify + wire | Z-score + JSON |
| `src/analysis/classification/rs_pixel_window.*` | 1 | Create | Viewport → pixel rect |
| `src/analysis/classification/rs_hungarian_assignment.*` | 1 | Modify | Rectangular cost + pad |
| `src/analysis/classification/rs_roi_io.*` | 1 | Verify/fix mainwindow callers | Source CRS |
| `src/app/classification/rs_classification_task.*` | 1 | Modify | Scaler tiles, GTiff options, crop window |
| `src/app/classification/rs_classify_session_state.*` | 1 | Create | Dirty + QSettings |
| `src/app/classification/qgsclassificationmainwindow.*` | 1–4 | Modify | Wire all features |
| `src/app/classification/rs_classify_workflow_controller.*` | 2 | Create | Steps, completion, soft gates, mode |
| `src/app/classification/rs_classify_stepper_bar.*` | 2 | Create | Top step UI + expert toggle |
| `src/app/classification/rs_classify_step_host.*` | 2 | Create | `QStackedWidget` + 7 panels skeleton |
| `src/analysis/classification/rs_post_process.*` | 3 | Create | Sieve / majority / clump / recode pure ops |
| `src/app/classification/rs_post_process_task.*` | 3 | Create | QgsTask chain |
| `src/app/classification/rs_accuracy_panel.*` | 4 | Create | Embedded metrics UI |
| `src/analysis/classification/rs_classification_project.*` | 4 | Modify | Workflow + path fields |
| `tests/test_classify_workflow_controller.cpp` | 2 | Create | Gate/completion unit tests |
| `tests/test_post_process.cpp` | 3 | Create | Label-image ops |
| `tests/test_pixel_window.cpp` | 1 | Create | Clamp geometry |
| `tests/test_classify_session_state.cpp` | 1 | Create | Dirty/snapshot |
| CMakeLists (analysis / app_classify / tests) | * | Modify | Register sources/tests |

---

## PR1 status (v1.1) — start here

Already present (do not re-implement from scratch):

| Item | State |
|------|--------|
| `RsFeatureScaler` + `test_feature_scaler` + CMake | **Done** (API + unit tests) |
| `Config.scaler` field on `RsClassificationTask` | **Partial** — field exists; `run()` does **not** transform tiles |
| Hungarian non-square | **Not done** — still rejects non-square |
| ROI CRS API | **Done** in `RsRoiIO`; verify mainwindow passes source CRS |
| GTiff creation options | **Not done** |
| Pixel window / crop preview | **Not done** |
| `RsClassifySessionState` | **Not done** |

**PR1 execution rule:** For Tasks 1–8 below, follow the corresponding Task in  
`docs/superpowers/plans/2026-07-16-classification-v11-production-hardening.md`  
**verbatim** when this plan says “use v1.1 plan Task N”. That plan already has full test code and implementation snippets. This document adds a **completion gate** and only expands steps that the v1.1 plan left incomplete relative to today’s tree.

---

### Task 1: Wire scaler into train + tile predict (v1.1 Task 2 residual)

**Files:**
- Modify: `src/app/classification/rs_classification_task.cpp`
- Modify: `src/app/classification/qgsclassificationmainwindow.cpp` (`applyClassification`, `applyPreview`, `buildTrainingData` callers)
- Modify: `tests/test_classifier_svm.cpp` (append multi-scale case from v1.1 plan)
- Optional: model save path writes `*.scale.json` next to model YAML

- [ ] **Step 1.1: Failing test for tile-scale path (if no e2e hook)**

Append to `tests/test_classifier_svm.cpp` the case  
`SVM RBF: multi-scale bands need scaler for high accuracy`  
from v1.1 plan Task 2 Step 2.3 (full code is in that file — copy exactly).

- [ ] **Step 1.2: In `RsClassificationTask::run`, before each tile `predict`**

```cpp
cv::Mat Xtile = /* assembled CV_32F features */;
if ( mCfg.scaler.isFitted() )
{
  Xtile = mCfg.scaler.transform( Xtile );
  if ( Xtile.empty() )
  {
    mResult.errorMessage = QStringLiteral( "Feature scaler transform failed" );
    return false;
  }
}
const cv::Mat pred = mCfg.backend->predict( Xtile );
```

Also transform `testX` before accuracy if scaler fitted and test non-empty (caller may already transform — **document one place only**: prefer caller scales train/test; Task only scales tiles. Match v1.1: caller fit+transform train/test; Task transforms tiles).

- [ ] **Step 1.3: Main window train path**

After stratified split in `applyClassification` / `applyPreview`:

```cpp
RsFeatureScaler scaler;
if ( !trainX.empty() )
{
  scaler.fit( trainX );
  trainX = scaler.transform( trainX );
  if ( !testX.empty() )
    testX = scaler.transform( testX );
}
cfg.scaler = scaler;
cfg.trainX = trainX;
cfg.trainY = trainY;
cfg.testX = testX;
cfg.testY = testY;
```

On model save: `scaler.saveJson( stem + ".scale.json" )`.  
On model load: if sidecar exists, `loadJson` into `cfg.scaler`.

- [ ] **Step 1.4: Build + test**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) qgis_app_classify test_classifier_svm test_classification_e2e test_feature_scaler && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'FeatureScaler|SVM|Classification E2E'
```

Expected: PASS

- [ ] **Step 1.5: Commit**

```bash
git add src/app/classification/rs_classification_task.cpp \
        src/app/classification/qgsclassificationmainwindow.cpp \
        tests/test_classifier_svm.cpp
git commit -m "feat(classify): wire feature scaling into train and tile predict"
```

---

### Task 2: Rectangular Hungarian (v1.1 Task 3)

**Files:**
- Modify: `src/analysis/classification/rs_hungarian_assignment.h/.cpp`
- Modify: `tests/test_hungarian_assignment.cpp`
- Modify: `src/app/classification/rs_classification_task.cpp` if needed

- [ ] **Step 2.1–2.5:** Execute **v1.1 plan Task 3** steps in full (failing 2×3 / 3×2 tests, pad with `1e9`, solve, strip to real columns).

- [ ] **Step 2.6: Commit**

```bash
git commit -m "feat(classify): rectangular Hungarian assignment with safe pad"
```

---

### Task 3: ROI CRS callers + GTiff options + pixel window + session (v1.1 Tasks 4–7)

**Files:** as listed in v1.1 plan Tasks 4–7 file maps.

- [ ] **Step 3.1:** v1.1 Task 4 — ensure `exportRois` / `loadRois` pass `m_sourceLayer->crs()` (or active CRS); extend `test_roi_io` if missing UTM round-trip.
- [ ] **Step 3.2:** v1.1 Task 5 — `Config.creationOptions` default `TILED=YES`, `COMPRESS=DEFLATE`, `PREDICTOR=2`; Create fail → retry empty options.
- [ ] **Step 3.3:** v1.1 Task 6 — create `rs_pixel_window.h/.cpp` + `test_pixel_window.cpp`; Preview sets `cropToWindow` + window from canvas extent; Apply full image.
- [ ] **Step 3.4:** v1.1 Task 7 — create `rs_classify_session_state.*` modeled on `src/app/georeferencer/rs_georef_session_state.*`; `closeEvent` Save/Discard/Cancel; mark dirty on ROI/class change.
- [ ] **Step 3.5: Integration gate (v1.1 Task 8)**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) qgis_app_classify && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'classify|FeatureScaler|Hungarian|ROI|pixel|session|Classification'
```

- [ ] **Step 3.6: Commit** each logical unit separately if large; final:

```bash
git commit -m "feat(classify): complete v1.1 production hardening (window, gtiff, session)"
```

**PR1 exit criteria:** v1.1 design §1.5 + all tests green. Do not start PR2 UI shell until PR1 gate passes (or worktree-isolate PR2 only if pure controller has zero dependency — preferred serial).

---

## PR2 — Workflow shell

### Task 4: `RsClassifyWorkflowController` (pure logic)

**Files:**
- Create: `src/app/classification/rs_classify_workflow_controller.h`
- Create: `src/app/classification/rs_classify_workflow_controller.cpp`
- Create: `tests/test_classify_workflow_controller.cpp`
- Modify: `src/app/classification/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interface:**

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

enum class RsClassifyStep {
  ClassSystem = 0,
  Samples,
  Evaluate,
  TrainClassify,
  Accuracy,
  PostProcess,
  Export,
  Count
};

enum class RsClassifyUiMode { Wizard, Expert };

class RsClassifyWorkflowController : public QObject
{
  Q_OBJECT
public:
  explicit RsClassifyWorkflowController( QObject *parent = nullptr );

  RsClassifyStep currentStep() const;
  void setCurrentStep( RsClassifyStep s );

  RsClassifyUiMode mode() const;
  void setMode( RsClassifyUiMode m );

  // Inputs updated by main window
  void setHasSourceRaster( bool v );
  void setClassCount( int n );
  void setTrainingClassCountWithPixels( int n );
  void setTrainingPixelCount( int n );
  void setEvaluateReviewed( bool v );
  void setHasFullClassifyResult( bool v ); // Apply path only, not preview
  void setHasAccuracyMetrics( bool v );
  void setPostProcessSkipped( bool v );
  void setHasPostProcessResult( bool v );
  void setHasExportedOrLoadedToMain( bool v );

  bool isStepComplete( RsClassifyStep s ) const;
  bool canRunPrimaryAction( RsClassifyStep s ) const;
  QStringList missingRequirements( RsClassifyStep s ) const;

  // Convenience for step 4 actions
  bool canTrainOrClassify() const; // source + train pixels >= 10
  bool canRunPostProcess() const;  // has classify result path (tracked via hasFullClassifyResult)
  bool canExport() const;

signals:
  void currentStepChanged( RsClassifyStep s );
  void completionChanged();
  void modeChanged( RsClassifyUiMode m );

private:
  RsClassifyStep mStep = RsClassifyStep::ClassSystem;
  RsClassifyUiMode mMode = RsClassifyUiMode::Wizard;
  bool mHasSource = false;
  int mClassCount = 0;
  int mTrainClasses = 0;
  int mTrainPixels = 0;
  bool mEvalReviewed = false;
  bool mHasFullResult = false;
  bool mHasAccuracy = false;
  bool mPostSkipped = false;
  bool mHasPost = false;
  bool mExported = false;
};
```

**Completion rules (must match spec):**

| Step | Complete when |
|------|----------------|
| ClassSystem | `mClassCount >= 2` |
| Samples | `mTrainClasses >= 2` |
| Evaluate | `mEvalReviewed` |
| TrainClassify | `mHasFullResult` |
| Accuracy | `mHasAccuracy` |
| PostProcess | `mPostSkipped \|\| mHasPost` |
| Export | `mExported` |

**`canTrainOrClassify`:** `mHasSource && mTrainPixels >= 10`  
**`canRunPostProcess`:** `mHasFullResult`  
**`canExport`:** `mHasFullResult || mHasPost`  
**`canRunPrimaryAction(Evaluate)`:** `mTrainPixels > 0`  
**`canRunPrimaryAction(Samples)` digitizing:** `mHasSource && mClassCount >= 1` (main window still enforces current class)

- [ ] **Step 4.1: Write failing tests**

Create `tests/test_classify_workflow_controller.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "rs_classify_workflow_controller.h"

TEST_CASE( "Workflow: ClassSystem complete needs >=2 classes", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::ClassSystem ) );
  w.setClassCount( 1 );
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::ClassSystem ) );
  w.setClassCount( 2 );
  REQUIRE( w.isStepComplete( RsClassifyStep::ClassSystem ) );
}

TEST_CASE( "Workflow: Preview does not complete TrainClassify", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  w.setHasSourceRaster( true );
  w.setTrainingPixelCount( 100 );
  w.setTrainingClassCountWithPixels( 2 );
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::TrainClassify ) );
  // Soft gate allows navigation but primary needs data
  REQUIRE( w.canTrainOrClassify() );
  w.setHasFullClassifyResult( true );
  REQUIRE( w.isStepComplete( RsClassifyStep::TrainClassify ) );
}

TEST_CASE( "Workflow: Evaluate only via explicit review flag", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  w.setTrainingPixelCount( 50 );
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::Evaluate ) );
  w.setEvaluateReviewed( true );
  REQUIRE( w.isStepComplete( RsClassifyStep::Evaluate ) );
}

TEST_CASE( "Workflow: PostProcess skip completes step", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  REQUIRE_FALSE( w.isStepComplete( RsClassifyStep::PostProcess ) );
  w.setPostProcessSkipped( true );
  REQUIRE( w.isStepComplete( RsClassifyStep::PostProcess ) );
}

TEST_CASE( "Workflow: missingRequirements lists source for train", "[classify][workflow]" )
{
  RsClassifyWorkflowController w;
  w.setTrainingPixelCount( 100 );
  const auto miss = w.missingRequirements( RsClassifyStep::TrainClassify );
  REQUIRE_FALSE( miss.isEmpty() );
  w.setHasSourceRaster( true );
  REQUIRE( w.canTrainOrClassify() );
}
```

- [ ] **Step 4.2: Register CMake**

In `src/app/classification/CMakeLists.txt` `qt_add_library` sources:

```cmake
    rs_classify_workflow_controller.cpp
```

In `tests/CMakeLists.txt` inside `if(TARGET qgis_app_classify)`:

```cmake
  add_executable(test_classify_workflow_controller test_classify_workflow_controller.cpp)
  target_link_libraries(test_classify_workflow_controller PRIVATE
    Catch2::Catch2WithMain
    qgis_app_classify
    Qt6::Core
  )
  target_include_directories(test_classify_workflow_controller PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_classify_workflow_controller PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_classify_workflow_controller)
```

- [ ] **Step 4.3: Run — expect fail**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. -DOpenCV_DIR=/usr/lib/cmake/opencv5 && make -j$(nproc) test_classify_workflow_controller 2>&1 | tail -30
```

Expected: compile fail (missing sources) or link fail.

- [ ] **Step 4.4: Implement controller**

Implement methods per table above. `missingRequirements` returns Chinese strings used in UI, e.g. `QStringLiteral("打开源影像")`, `QStringLiteral("训练像元 ≥ 10")`, `QStringLiteral("至少 2 个类别")`.

Emit `completionChanged` when any setter changes completion bits; `currentStepChanged` on step change.

- [ ] **Step 4.5: Tests pass**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) test_classify_workflow_controller && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'Workflow'
```

- [ ] **Step 4.6: Commit**

```bash
git add src/app/classification/rs_classify_workflow_controller.* \
        tests/test_classify_workflow_controller.cpp \
        src/app/classification/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(classify): workflow controller with soft gates and completion"
```

---

### Task 5: Stepper bar + step host skeleton

**Files:**
- Create: `src/app/classification/rs_classify_stepper_bar.h/.cpp`
- Create: `src/app/classification/rs_classify_step_host.h/.cpp`
- Modify: `src/app/classification/CMakeLists.txt`
- Modify: `src/app/classification/qgsclassificationmainwindow.h/.cpp`

**Stepper UI:**

```cpp
// rs_classify_stepper_bar.h
class RsClassifyStepperBar : public QWidget
{
  Q_OBJECT
public:
  explicit RsClassifyStepperBar( QWidget *parent = nullptr );
  void setCurrentStep( RsClassifyStep s );
  void setStepComplete( RsClassifyStep s, bool complete );
  void setMode( RsClassifyUiMode m );
signals:
  void stepClicked( RsClassifyStep s );
  void modeToggled( RsClassifyUiMode m );
};
```

Seven `QToolButton`s (checkable exclusive group) labeled:
`1 体系` `2 样本` `3 评价` `4 训练` `5 精度` `6 后处理` `7 输出`  
Plus `QCheckBox` or toggle `专家模式`.

**Step host:**

```cpp
class RsClassifyStepHost : public QWidget
{
  Q_OBJECT
public:
  explicit RsClassifyStepHost( QWidget *parent = nullptr );
  void setCurrentStep( RsClassifyStep s );
  QWidget *panel( RsClassifyStep s ) const;
  // Each panel is a QWidget with objectName "classifyStepN" for tests
private:
  QStackedWidget *mStack = nullptr;
  QVector<QWidget *> mPanels; // size Count
};
```

Each panel initially:

```cpp
auto *layout = new QVBoxLayout( panel );
layout->addWidget( new QLabel( tr( "步骤标题" ) ) );
layout->addWidget( new QLabel( tr( "完成条件：…" ) ) );
auto *status = new QLabel; // objectName classifyStepGate
layout->addWidget( status );
layout->addStretch();
auto *nav = new QHBoxLayout;
auto *prev = new QPushButton( tr( "上一步" ) );
auto *next = new QPushButton( tr( "下一步" ) );
// connect later in main window
```

- [ ] **Step 5.1: Implement stepper + host, register CMake**

- [ ] **Step 5.2: Integrate in main window constructor after `setupUi` docks**

```cpp
m_workflow = new RsClassifyWorkflowController( this );
m_stepper = new RsClassifyStepperBar( this );
addToolBarBreak();
auto *wfBar = addToolBar( tr( "工作流" ) );
wfBar->addWidget( m_stepper );

m_stepHost = new RsClassifyStepHost( this );
auto *rightDock = new QDockWidget( tr( "工作流步骤" ), this );
rightDock->setObjectName( QStringLiteral( "ClassifyWorkflowDock" ) );
rightDock->setWidget( m_stepHost );
addDockWidget( Qt::RightDockWidgetArea, rightDock );
```

Connect:

```cpp
connect( m_stepper, &RsClassifyStepperBar::stepClicked, this, [this]( RsClassifyStep s ) {
  m_workflow->setCurrentStep( s );
});
connect( m_workflow, &RsClassifyWorkflowController::currentStepChanged, this, [this]( RsClassifyStep s ) {
  m_stepper->setCurrentStep( s );
  m_stepHost->setCurrentStep( s );
  refreshWorkflowUi();
});
connect( m_stepper, &RsClassifyStepperBar::modeToggled, m_workflow, &RsClassifyWorkflowController::setMode );
```

`refreshWorkflowUi()`:
- update each step complete on stepper
- set gate label text from `missingRequirements`
- enable/disable Apply/Preview from `canTrainOrClassify()`
- wizard mode: hide JM/spectral docks unless Evaluate step (optional soft hide); expert: show all

- [ ] **Step 5.3: Wire existing signals → controller setters**

On open raster → `setHasSourceRaster(true)`  
On class table change → `setClassCount(scheme.size())`  
On ROI change → count train classes with pixels + pixel total → setters  
On successful Apply → `setHasFullClassifyResult(true)`  
On preview only → do **not** set full result  
On accuracy dialog/metrics → `setHasAccuracyMetrics(true)` (temporary until panel)

- [ ] **Step 5.4: Build main target**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) sicnu_geo_rs qgis_app_classify 2>&1 | tail -40
```

Expected: success

- [ ] **Step 5.5: Commit**

```bash
git commit -m "feat(classify): stepper bar and step host skeleton with soft gates"
```

---

### Task 6: Populate steps 1–4 panels with real controls

**Files:**
- Modify: `rs_classify_step_host.cpp` / mainwindow
- Reuse: `RsClassTableWidget`, sample toolbar actions, `RsClassifierSetupBar`, spectral/JM widgets

- [ ] **Step 6.1: Step1 panel**

Embed or deep-link: open raster button (`m_openRasterAction`), pointer to class table (`m_classTableWidget` can stay in dock; panel has “打开类别管理” that raises dock + short help). Prefer **moving** class table into Step1 panel in wizard mode and leaving dock for expert — implement simplest: panel hosts **actions** that trigger existing slots; class table remains right-stacked above step host if needed.

Minimal acceptable: Step1 contains:
- `QPushButton` → `openSourceRaster`
- Read-only class count label bound to controller
- `QPushButton` “添加默认 6 类” if scheme empty (call existing default insert)

- [ ] **Step 6.2: Step2 panel**

- Train/Valid mode buttons (same as toolbar)
- Stats label (mirror status bar ROI counts)
- Export/Load ROI buttons
- Gate: digitizing tools disabled when `!canRunPrimaryAction(Samples)` equivalent

- [ ] **Step 6.3: Step3 panel**

- Buttons: recompute spectral, recompute JM, raise docks
- `QPushButton` “标记已审阅” → `setEvaluateReviewed(true)`

- [ ] **Step 6.4: Step4 panel**

- Embed `m_classifierBar` into panel layout **or** keep bottom bar and panel has CV / Preview / Apply buttons calling existing slots
- Disable Apply/Preview when `!canTrainOrClassify()`; tooltip = `missingRequirements` join

- [ ] **Step 6.5: Manual smoke (document in commit body)**

Open window → stepper shows 7 steps → click each → expert toggle shows docks.

- [ ] **Step 6.6: Commit**

```bash
git commit -m "feat(classify): wire steps 1-4 panels to existing classify actions"
```

**PR2 exit:** Controller tests green; app builds; soft gate disables Apply without raster/samples.

---

## PR3 — Post-process

### Task 7: `RsPostProcess` pure operators

**Files:**
- Create: `src/analysis/classification/rs_post_process.h`
- Create: `src/analysis/classification/rs_post_process.cpp`
- Create: `tests/test_post_process.cpp`
- Modify: `src/analysis/classification/CMakeLists.txt` (inside OpenCV **or** GDAL-only — prefer GDAL+OpenCV block; majority uses raw buffers)
- Modify: `tests/CMakeLists.txt`

**API:**

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <QMap>
#include <QString>
#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsPostProcess
{
public:
  // labels: CV_32S or CV_8U single channel class ids; nodata optional
  static bool sieve( const cv::Mat &src, cv::Mat &dst, int threshold, int connectedness /*4 or 8*/, QString *err = nullptr );
  static bool majorityFilter( const cv::Mat &src, cv::Mat &dst, int kernelOdd, QString *err = nullptr );
  static bool clump( const cv::Mat &src, cv::Mat &dst, int connectedness, QString *err = nullptr );
  static bool recode( const cv::Mat &src, cv::Mat &dst, const QMap<int, int> &map, QString *err = nullptr );

  // File helpers using GDAL (uint8 class raster)
  static bool loadLabelRaster( const QString &path, cv::Mat &labels, double gt[6], QString &wkt, QString *err = nullptr );
  static bool saveLabelRaster( const QString &path, const cv::Mat &labels, const double gt[6], const QString &wkt,
                               const QVector<QRgb> &colorTable, const QStringList &creationOptions, QString *err = nullptr );
  static bool polygonize( const QString &labelRasterPath, const QString &vectorPath, const QString &classField, QString *err = nullptr );
};
```

**Implementation notes:**

- **Sieve:** Prefer `GDALSieveFilter` on open datasets; for pure cv::Mat path, write temp MEM/GTiff or implement connected-component area filter with OpenCV `connectedComponentsWithStats` and replace small components with neighbor majority.
- **Majority:** For each pixel, count labels in `k×k` window (k odd ≥ 3), pick mode; skip center if all nodata.
- **Clump:** `connectedComponents` per class or global on labeled image — global CC on equality with 4/8 connectivity; output component ids as CV_32S.
- **Recode:** `dst(i)=map.value(src(i), src(i))`.
- **Polygonize:** `GDALPolygonize` / `GDALFPolygonize` to GPKG (`GPKG` driver) or ESRI Shapefile.

- [ ] **Step 7.1: Failing tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "rs_post_process.h"

TEST_CASE( "PostProcess: recode maps ids", "[classify][post]" )
{
  cv::Mat src = ( cv::Mat_<int>( 2, 2 ) << 1, 1, 2, 2 );
  cv::Mat dst;
  QMap<int, int> m; m[1] = 10; m[2] = 20;
  REQUIRE( RsPostProcess::recode( src, dst, m, nullptr ) );
  REQUIRE( dst.at<int>( 0, 0 ) == 10 );
  REQUIRE( dst.at<int>( 1, 1 ) == 20 );
}

TEST_CASE( "PostProcess: majority 3x3 smooths single speck", "[classify][post]" )
{
  cv::Mat src( 5, 5, CV_32S, cv::Scalar( 1 ) );
  src.at<int>( 2, 2 ) = 9; // speck
  cv::Mat dst;
  REQUIRE( RsPostProcess::majorityFilter( src, dst, 3, nullptr ) );
  REQUIRE( dst.at<int>( 2, 2 ) == 1 );
}

TEST_CASE( "PostProcess: sieve removes small component", "[classify][post]" )
{
  cv::Mat src( 10, 10, CV_32S, cv::Scalar( 1 ) );
  src.at<int>( 0, 0 ) = 2; // 1-pixel class 2
  cv::Mat dst;
  REQUIRE( RsPostProcess::sieve( src, dst, 2, 8, nullptr ) );
  REQUIRE( dst.at<int>( 0, 0 ) != 2 ); // replaced
}
```

- [ ] **Step 7.2: Implement + CMake + pass tests**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) test_post_process && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'PostProcess'
```

- [ ] **Step 7.3: Commit**

```bash
git commit -m "feat(classify): post-process operators sieve majority clump recode"
```

---

### Task 8: `RsPostProcessTask` + Step 6 UI

**Files:**
- Create: `src/app/classification/rs_post_process_task.h/.cpp`
- Modify: `rs_classify_step_host` Step6 panel
- Modify: `qgsclassificationmainwindow.*`
- Modify: CMake app_classify

**Config:**

```cpp
struct RsPostProcessConfig {
  QString inputPath;
  QString outputRasterPath;
  QString outputVectorPath;
  bool runSieve = true;
  int sieveThreshold = 10;
  int connectedness = 8;
  bool runMajority = true;
  int majorityKernel = 3;
  bool runClump = false;
  bool runRecode = false;
  QMap<int, int> recodeMap;
  bool runPolygonize = false;
  QStringList creationOptions = { QStringLiteral("TILED=YES"), QStringLiteral("COMPRESS=DEFLATE"), QStringLiteral("PREDICTOR=2") };
};
```

`run()` order: load → sieve? → majority? → clump? → recode? → save raster → polygonize?

- [ ] **Step 8.1: Implement task**

- [ ] **Step 8.2: Step6 panel widgets**

- Input path line edit (default last Apply path)
- Checkboxes for each op + spinboxes for threshold/kernel
- Recode table (optional simple: two columns old/new `QTableWidget`)
- Buttons: `运行后处理`, `跳过后处理`
- Skip → `m_workflow->setPostProcessSkipped(true)`
- Success → `setHasPostProcessResult(true)`; load result on canvas

- [ ] **Step 8.3: Build + smoke**

```bash
make -j$(nproc) qgis_app_classify test_post_process
```

- [ ] **Step 8.4: Commit**

```bash
git commit -m "feat(classify): post-process task and step 6 panel"
```

**PR3 exit:** Unit tests green; Step6 can skip or run sieve on a synthetic raster path in a small e2e optional test if time permits.

---

## PR4 — Accuracy panel, export step, project fields, review

### Task 9: `RsAccuracyPanel` embedded Step 5

**Files:**
- Create: `src/app/classification/rs_accuracy_panel.h/.cpp`
- Modify: `rs_accuracy_dialog.cpp` — extract shared fill helpers **or** construct panel inside dialog
- Modify: mainwindow Apply completion path
- Modify: CMake

**API:**

```cpp
class RsAccuracyPanel : public QWidget
{
  Q_OBJECT
public:
  explicit RsAccuracyPanel( QWidget *parent = nullptr );
  void setResult( const RsAccuracyAssessment::Result &r, const QHash<int, QString> &classNames );
  void clear();
  bool hasResult() const;
signals:
  void exportCsvRequested();
private:
  // tables + OA/Kappa labels; Export CSV button
};
```

- [ ] **Step 9.1: Implement panel by factoring dialog table-building code into shared static functions in `rs_accuracy_dialog.cpp` or new `rs_accuracy_ui_utils.*`**

- [ ] **Step 9.2: On Apply success with metrics**

```cpp
m_accuracyPanel->setResult( r.accuracy, classNames );
m_workflow->setHasAccuracyMetrics( true );
// Do not only rely on modal dialog; optional: still offer "弹出窗口" button
```

- [ ] **Step 9.3: Step5 host embeds panel + “重新评估” if valid ROIs exist**

- [ ] **Step 9.4: Commit**

```bash
git commit -m "feat(classify): embed accuracy panel in workflow step 5"
```

---

### Task 10: Step 7 export + project persistence

**Files:**
- Modify: `src/analysis/classification/rs_classification_project.h/.cpp`
- Modify: step 7 panel + mainwindow
- Modify: tests if project JSON tested

**Extend `RsClassificationProjectData`:**

```cpp
int workflowStep = 0;
QString workflowMode; // "wizard" | "expert"
QString classifiedRasterPath;
QString postProcessRasterPath;
QString postProcessVectorPath;
bool evaluateReviewed = false;
QString accuracySource; // "holdout" | "valid_layer"
// existing results vector still used
```

JSON keys in manifest load/save (backward compatible defaults).

- [ ] **Step 10.1: Manifest round-trip fields**

- [ ] **Step 10.2: Step7 checklist**

```text
[ ] 分类 GeoTIFF（打开目录 / 复制路径）
[ ] 后处理栅格
[ ] 后处理矢量
[ ] ROI（调用 exportRois）
[ ] 模型 + scale.json（if saved）
[ ] 精度 CSV
[ ] 分类项目 .rscproj
[按钮] 导出所选
[按钮] 加载分类结果到主窗口
```

On successful export or load-to-main → `setHasExportedOrLoadedToMain(true)`.

Load-to-main: use `m_iface` to add `QgsRasterLayer` if iface non-null (pattern from other dialogs).

- [ ] **Step 10.3: Restore workflow step/mode on `loadProjectFromFile`**

- [ ] **Step 10.4: Commit**

```bash
git commit -m "feat(classify): export step and project workflow persistence"
```

---

### Task 11: Operation-logic review pass + Lab checklist

**Files:** mainwindow primarily

Checklist (fix if broken):

| Item | Expected |
|------|----------|
| Train/Valid toolbar exclusive highlight | Mutual exclusive checked |
| Preview does not set `hasFullClassifyResult` | Controller incomplete until Apply |
| Double-click Apply while running | Disabled / ignored |
| Dirty close after ROI edit | Prompt |
| Expert mode | All docks visible; gates still apply to unsafe ops |
| Wizard mode | Step dock visible; stepper drives panels |
| Post-process default input | Last Apply path |
| Chinese gate messages | Non-empty `missingRequirements` |

- [ ] **Step 11.1: Walk checklist; fix defects with small commits**

- [ ] **Step 11.2: Full test suite subset**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) qgis_app_classify && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'classify|FeatureScaler|Hungarian|PostProcess|Workflow|Classification|ROI|session|pixel'
```

- [ ] **Step 11.3: Manual Lab path (human or documented)**

开图 → ≥2 类 → 训练样本两类 → 标记评价 → SVM Apply → 精度面板 → Sieve → 导出 → 脏关闭

- [ ] **Step 11.4: Final commit if fixes**

```bash
git commit -m "fix(classify): workflow operation-logic review fixes"
```

**PR4 / Phase exit:** Spec §1.5 complete.

---

## Testing matrix (phase-level)

| Layer | Tests |
|-------|--------|
| v1.1 | FeatureScaler, Hungarian rect, ROI CRS, pixel window, session, SVM scaled, e2e GTiff |
| Workflow | Controller completion/gates |
| Post | recode, majority, sieve (+ polygonize file test if GDAL drivers allow temp GPKG) |
| UI smoke | Existing `test_classification_window` still constructs; extend findChild for `ClassifyWorkflowDock` if stable |

---

## Spec coverage self-check

| Spec ID | Task(s) |
|---------|---------|
| W1 Controller | Task 4 |
| W2 Stepper | Task 5 |
| W3 Step panels | Tasks 5–6, 8–10 |
| W4 Accuracy embed | Task 9 |
| W5 Post-process | Tasks 7–8 |
| W6 Export | Task 10 |
| W7 Op logic | Tasks 6, 11 |
| V1–V6 v1.1 | Tasks 1–3 |
| Soft gates | Task 4–5 |
| Hybrid mode | Task 5 |
| Project fields | Task 10 |
| PR split | PR1=T1–3, PR2=T4–6, PR3=T7–8, PR4=T9–11 |

---

## Out of scope (do not implement)

- New ML backends, OBIA, hard step locks, medianBlur-as-majority, full mainwindow file split
