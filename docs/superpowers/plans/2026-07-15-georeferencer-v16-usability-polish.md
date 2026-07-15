# Georeferencer v1.6 Usability Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close production-usability gaps in the Georeferencer: dirty close prompt, workflow settings persistence, REF marker CRS reprojection, RPC refinement before/after RMS, mode-aware map pick canvas, and Move/Delete hit-testing.

**Architecture:** Introduce a thin `RsGeorefSessionState` (dirty flag + `QSettings` under `Georeferencer/`) owned by `QgsGeoreferencerMainWindow`. Wire Move/Delete tools and `contains()` like upstream QGIS. Fix `updateMarkers()` CRS via `transformedDestinationPoint()`. Dual-run RPC transformers only in `recomputeFit()` for refinement RMS display. No main-window file split.

**Tech Stack:** C++17 / Qt6 (Widgets, QSettings) / Catch2 / GDAL ≥ 3.4 / vendored QGIS core+gui+analysis (`QgsMapTool`, `QgsCoordinateTransform`, `QgisInterface::mapCanvas`)

**Spec:** `docs/superpowers/specs/2026-07-15-georeferencer-v16-usability-polish-design.md`

---

## File map

| Path | Action | Responsibility |
|------|--------|----------------|
| `src/app/georeferencer/rs_georef_session_state.h` | Create | Dirty + settings API |
| `src/app/georeferencer/rs_georef_session_state.cpp` | Create | Implementation |
| `src/app/georeferencer/CMakeLists.txt` | Modify | Add session_state sources |
| `src/app/georeferencer/qgsgeoreferencermainwindow.h` | Modify | Session, tools, pick/find/move slots |
| `src/app/georeferencer/qgsgeoreferencermainwindow.cpp` | Modify | Wire all polish behaviors |
| `src/app/georeferencer/qgsgeorefdatapoint.cpp` | Modify | `contains()`, CRS `updateMarkers` |
| `src/app/georeferencer/rs_georef_params_panel.h/.cpp` | Modify | `clearRefinementRms()`, optional workflow getters/setters |
| `src/app/georeferencer/qgsmapcoordsdialog.cpp` | Modify | Button label「从地图取点」 |
| `tests/test_georef_session_state.cpp` | Create | Session unit tests |
| `tests/test_gcp_contains.cpp` | Create | Hit-test |
| `tests/test_gcp_canvas_crs.cpp` | Create | REF CRS reprojection |
| `tests/test_pick_canvas_mode.cpp` | Create | Mode → canvas mapping (via test hooks) |
| `tests/test_rpc_gcp_refine.cpp` | Modify | Optional RMS comparison helper case |
| `tests/CMakeLists.txt` | Modify | Register new test binaries |

---

## Conventions

- **TDD:** Red → Green → Refactor per task. Run failing test before implementation.
- **Build:** `cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) <target>`
- **Test:** `cd build && ctest --output-on-failure -R '<substring of TEST_CASE>'`
- **Commits:** `feat(georef):` / `test(georef):` / `chore(georef):`
- **GUI tests:** Copy `ensureApp()` + `FastExitListener` from `tests/test_crs_picker_persists.cpp` (set org/app to `SicnuRsTest` / `GeorefTest`).
- **Settings keys:** all under `Georeferencer/` via `QSettings` (same as existing `lastDestCrs`).
- **Naming:** new types `Rs*`; ported stay `Qgs*`.

### Lessons from 11.4 / 11.5

1. `QgsGCPList` owns `QgsGcpPoint*`; `appendPoint` by value.
2. Residual is `QPointF`; magnitude `std::hypot`.
3. Qt `isVisible()` false until shown — use `!isHidden()` for panel intent.
4. Catch2 `ctest -R` matches **TEST_CASE name**, not binary name.
5. `QgsGeorefToolMovePoint` uses two-click begin/end (not drag-hold); `setStartPoint` required after begin.
6. `QgisInterface::mapCanvas()` exists in `src/gui/qgisinterface.h`.

---

### Task 1: `RsGeorefSessionState` (dirty + settings)

**Files:**
- Create: `src/app/georeferencer/rs_georef_session_state.h`
- Create: `src/app/georeferencer/rs_georef_session_state.cpp`
- Modify: `src/app/georeferencer/CMakeLists.txt`
- Create: `tests/test_georef_session_state.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1.1: Write failing test**

Create `tests/test_georef_session_state.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QSettings>
#include <QWidget>

#include "rs_georef_session_state.h"

namespace {
  int argc = 1;
  char arg0[] = "test";
  char *argv[] = { arg0, nullptr };
  void ensureCore()
  {
    if ( !QCoreApplication::instance() )
      static QCoreApplication app( argc, argv );
    QCoreApplication::setOrganizationName( QStringLiteral( "SicnuRsTest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "GeorefTest" ) );
  }
}

TEST_CASE( "SessionState: dirty mark and clear", "[georef][session]" )
{
  ensureCore();
  RsGeorefSessionState s;
  REQUIRE_FALSE( s.isDirty() );
  s.markDirty();
  REQUIRE( s.isDirty() );
  s.clearDirty();
  REQUIRE_FALSE( s.isDirty() );
}

TEST_CASE( "SessionState: workflow snapshot round-trip", "[georef][session]" )
{
  ensureCore();
  QSettings().clear();

  RsGeorefSessionState::WorkflowSnapshot in;
  in.mode = 1;
  in.transformMethod = 2;
  in.resamplingMethod = 1;
  in.lastSourcePath = QStringLiteral( "/tmp/src.tif" );
  in.lastRefPath = QStringLiteral( "/tmp/ref.tif" );
  in.lastOutputPath = QStringLiteral( "/tmp/out.tif" );
  in.lastDemPath = QStringLiteral( "/tmp/dem.tif" );
  in.lastPointsPath = QStringLiteral( "/tmp/a.points" );
  in.syncZoom = false;

  RsGeorefSessionState s;
  s.saveWorkflow( in );
  s.setLastPointsPath( in.lastPointsPath );

  const auto out = s.restoreWorkflow();
  REQUIRE( out.mode == 1 );
  REQUIRE( out.transformMethod == 2 );
  REQUIRE( out.resamplingMethod == 1 );
  REQUIRE( out.lastSourcePath == QStringLiteral( "/tmp/src.tif" ) );
  REQUIRE( out.lastRefPath == QStringLiteral( "/tmp/ref.tif" ) );
  REQUIRE( out.lastOutputPath == QStringLiteral( "/tmp/out.tif" ) );
  REQUIRE( out.lastDemPath == QStringLiteral( "/tmp/dem.tif" ) );
  REQUIRE( out.lastPointsPath == QStringLiteral( "/tmp/a.points" ) );
  REQUIRE( out.syncZoom == false );
  REQUIRE( s.lastPointsPath() == QStringLiteral( "/tmp/a.points" ) );
}

TEST_CASE( "SessionState: window geometry save/restore", "[georef][session]" )
{
  ensureCore();
  QSettings().remove( QStringLiteral( "Georeferencer/geometry" ) );

  QWidget w;
  w.resize( 640, 480 );
  w.move( 12, 34 );
  RsGeorefSessionState s;
  s.saveWindow( &w );

  QWidget w2;
  s.restoreWindow( &w2 );
  // restoreGeometry may be platform-sensitive; at least settings key must exist
  QSettings st;
  REQUIRE( st.contains( QStringLiteral( "Georeferencer/geometry" ) ) );
}
```

- [ ] **Step 1.2: Register test in CMake (expect link fail until sources exist)**

In `tests/CMakeLists.txt`, after `test_crs_picker_persists` block, add:

```cmake
# Task 11.6.1 — Georef session state (dirty + settings)
add_executable(test_georef_session_state test_georef_session_state.cpp)
target_link_libraries(test_georef_session_state PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_app_georef
)
target_include_directories(test_georef_session_state PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_georef_session_state)
```

- [ ] **Step 1.3: Run test — expect compile/link failure**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) test_georef_session_state 2>&1 | tail -30
```

Expected: fails (missing `rs_georef_session_state` / header).

- [ ] **Step 1.4: Implement header**

Create `src/app/georeferencer/rs_georef_session_state.h`:

```cpp
#ifndef RS_GEOREF_SESSION_STATE_H
#define RS_GEOREF_SESSION_STATE_H

#include <QString>

class QWidget;

/**
 * \brief Dirty flag + Georeferencer/* QSettings for workflow continuity.
 * Does not own GCPs or canvases.
 */
class RsGeorefSessionState
{
  public:
    struct WorkflowSnapshot
    {
      int mode = 0;
      int transformMethod = 0;
      int resamplingMethod = 0;
      QString lastSourcePath;
      QString lastRefPath;
      QString lastOutputPath;
      QString lastDemPath;
      QString lastPointsPath;
      bool syncZoom = true;
    };

    bool isDirty() const { return mDirty; }
    void markDirty() { mDirty = true; }
    void clearDirty() { mDirty = false; }

    QString lastPointsPath() const { return mLastPointsPath; }
    void setLastPointsPath( const QString &path );

    void saveWindow( QWidget *w );
    void restoreWindow( QWidget *w );

    void saveWorkflow( const WorkflowSnapshot &s );
    WorkflowSnapshot restoreWorkflow() const;

  private:
    bool mDirty = false;
    QString mLastPointsPath;
};

#endif
```

- [ ] **Step 1.5: Implement cpp**

Create `src/app/georeferencer/rs_georef_session_state.cpp`:

```cpp
#include "rs_georef_session_state.h"

#include <QByteArray>
#include <QSettings>
#include <QWidget>

namespace {
  constexpr auto kPrefix = "Georeferencer/";
}

void RsGeorefSessionState::setLastPointsPath( const QString &path )
{
  mLastPointsPath = path;
  QSettings().setValue( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ), path );
}

void RsGeorefSessionState::saveWindow( QWidget *w )
{
  if ( !w )
    return;
  QSettings s;
  s.setValue( QStringLiteral( "%1geometry" ).arg( QLatin1String( kPrefix ) ), w->saveGeometry() );
  // QMainWindow::saveState only if cast succeeds — callers may pass QMainWindow*
  if ( auto *mw = qobject_cast<QMainWindow *>( w ) )
    s.setValue( QStringLiteral( "%1windowState" ).arg( QLatin1String( kPrefix ) ), mw->saveState() );
}

void RsGeorefSessionState::restoreWindow( QWidget *w )
{
  if ( !w )
    return;
  QSettings s;
  const QByteArray geo = s.value( QStringLiteral( "%1geometry" ).arg( QLatin1String( kPrefix ) ) ).toByteArray();
  if ( !geo.isEmpty() )
    w->restoreGeometry( geo );
  if ( auto *mw = qobject_cast<QMainWindow *>( w ) )
  {
    const QByteArray st = s.value( QStringLiteral( "%1windowState" ).arg( QLatin1String( kPrefix ) ) ).toByteArray();
    if ( !st.isEmpty() )
      mw->restoreState( st );
  }
}

void RsGeorefSessionState::saveWorkflow( const WorkflowSnapshot &snap )
{
  QSettings s;
  s.setValue( QStringLiteral( "%1mode" ).arg( QLatin1String( kPrefix ) ), snap.mode );
  s.setValue( QStringLiteral( "%1transformMethod" ).arg( QLatin1String( kPrefix ) ), snap.transformMethod );
  s.setValue( QStringLiteral( "%1resamplingMethod" ).arg( QLatin1String( kPrefix ) ), snap.resamplingMethod );
  s.setValue( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ), snap.lastSourcePath );
  s.setValue( QStringLiteral( "%1lastRefPath" ).arg( QLatin1String( kPrefix ) ), snap.lastRefPath );
  s.setValue( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ), snap.lastOutputPath );
  s.setValue( QStringLiteral( "%1lastDemPath" ).arg( QLatin1String( kPrefix ) ), snap.lastDemPath );
  s.setValue( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ), snap.lastPointsPath );
  s.setValue( QStringLiteral( "%1syncZoom" ).arg( QLatin1String( kPrefix ) ), snap.syncZoom );
  mLastPointsPath = snap.lastPointsPath;
}

RsGeorefSessionState::WorkflowSnapshot RsGeorefSessionState::restoreWorkflow() const
{
  QSettings s;
  WorkflowSnapshot o;
  o.mode = s.value( QStringLiteral( "%1mode" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.transformMethod = s.value( QStringLiteral( "%1transformMethod" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.resamplingMethod = s.value( QStringLiteral( "%1resamplingMethod" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.lastSourcePath = s.value( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastRefPath = s.value( QStringLiteral( "%1lastRefPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastOutputPath = s.value( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastDemPath = s.value( QStringLiteral( "%1lastDemPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastPointsPath = s.value( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.syncZoom = s.value( QStringLiteral( "%1syncZoom" ).arg( QLatin1String( kPrefix ) ), true ).toBool();
  return o;
}
```

**Note:** `#include <QMainWindow>` is required in the cpp for `qobject_cast<QMainWindow*>`.

- [ ] **Step 1.6: Add sources to georef library**

In `src/app/georeferencer/CMakeLists.txt`, add to `add_library(qgis_app_georef STATIC ...)`:

```
    rs_georef_session_state.cpp
```

- [ ] **Step 1.7: Build and run tests**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) test_georef_session_state && ctest --output-on-failure -R 'SessionState'
```

Expected: all `SessionState:*` PASS.

- [ ] **Step 1.8: Commit**

```bash
git add src/app/georeferencer/rs_georef_session_state.h \
        src/app/georeferencer/rs_georef_session_state.cpp \
        src/app/georeferencer/CMakeLists.txt \
        tests/test_georef_session_state.cpp \
        tests/CMakeLists.txt
git commit -m "feat(georef): add RsGeorefSessionState for dirty flag and settings"
```

---

### Task 2: Close event + dirty wiring + restore settings

**Files:**
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.h`
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.cpp`
- Modify: `src/app/georeferencer/rs_georef_params_panel.h/.cpp` (getters/setters if missing for method/path restore)
- Modify: `tests/test_georef_window.cpp` (optional dirty hooks)

- [ ] **Step 2.1: Add members and test hooks to header**

In `qgsgeoreferencermainwindow.h`:

```cpp
#include "rs_georef_session_state.h"
// ...
public:
  /// Test hook: expose dirty state without UI.
  bool isDirtyForTest() const;
  void markDirtyForTest();
  RsGeorefSessionState *sessionStateForTest() { return &mSession; }
// ...
private:
  RsGeorefSessionState mSession;
  bool mSuppressDirtyFromList = false; // true during load/save that mutates list
  bool mWarpInProgress = false;        // set true around applyTransform task

  void applyWorkflowSnapshot( const RsGeorefSessionState::WorkflowSnapshot &s );
  RsGeorefSessionState::WorkflowSnapshot captureWorkflowSnapshot() const;
  bool promptSaveGcpsIfDirty(); // returns false if user cancelled close
```

- [ ] **Step 2.2: Wire dirty on list change; clear on load/save**

In constructor after `mGcps` creation:

```cpp
connect( mGcps, &QgsGCPList::changed, this, [this]() {
  if ( !mSuppressDirtyFromList )
    mSession.markDirty();
} );
```

In `loadPoints()` success path:

```cpp
mSuppressDirtyFromList = true;
const bool ok = mGcps->loadGcps( path, destCrs );
mSuppressDirtyFromList = false;
if ( ok ) {
  mSession.setLastPointsPath( path );
  mSession.clearDirty();
  // existing log...
}
```

In `savePoints()` success path:

```cpp
if ( mGcps->saveGcps( finalPath ) ) {
  mSession.setLastPointsPath( finalPath );
  mSession.clearDirty();
  // existing log...
}
```

Also set `mWarpInProgress = true` at start of `applyTransform` and false in finalize lambda (alongside `setWarpInProgressForTest`).

- [ ] **Step 2.3: Implement closeEvent**

Replace the TODO `closeEvent`:

```cpp
void QgsGeoreferencerMainWindow::closeEvent( QCloseEvent *e )
{
  if ( mWarpInProgress )
  {
    const auto ans = QMessageBox::question(
      this, tr( "几何校正" ),
      tr( "校正任务仍在运行，仍要关闭？" ),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
    if ( ans != QMessageBox::Yes )
    {
      e->ignore();
      return;
    }
  }

  if ( mSession.isDirty() )
  {
    const auto ans = QMessageBox::question(
      this, tr( "未保存的控制点" ),
      tr( "GCP 列表有未保存的更改。是否保存？" ),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save );
    if ( ans == QMessageBox::Cancel )
    {
      e->ignore();
      return;
    }
    if ( ans == QMessageBox::Save )
    {
      // Prefer last path; else file dialog via existing savePoints()
      QString path = mSession.lastPointsPath();
      if ( path.isEmpty() )
      {
        path = QFileDialog::getSaveFileName(
          this, tr( "Save GCP points" ), QString(),
          tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
        if ( path.isEmpty() )
        {
          e->ignore();
          return;
        }
        if ( QFileInfo( path ).suffix().isEmpty() )
          path += QStringLiteral( ".points" );
      }
      if ( !mGcps || !mGcps->saveGcps( path ) )
      {
        QMessageBox::warning( this, tr( "Save GCPs" ),
                              tr( "保存失败，窗口未关闭。" ) );
        e->ignore();
        return;
      }
      mSession.setLastPointsPath( path );
      mSession.clearDirty();
    }
  }

  mSession.saveWorkflow( captureWorkflowSnapshot() );
  mSession.saveWindow( this );
  e->accept();
}
```

Implement `captureWorkflowSnapshot` / `applyWorkflowSnapshot` using panel getters (`transformMethod`, `resamplingMethod`, `outputPath`, `demPath`), `mModeToggle->currentMode()`, `mSourceRasterPath`, ref path if stored, `mSyncZoomAction->isChecked()`.

- [ ] **Step 2.4: Restore at end of constructor**

After docks/tools exist:

```cpp
mSession.restoreWindow( this );
applyWorkflowSnapshot( mSession.restoreWorkflow() );
// if lastPointsPath non-empty, do NOT auto-load GCPs (spec: paths only; loading points is explicit)
```

Add panel helpers if needed:

```cpp
// rs_georef_params_panel.h
void setTransformMethod( QgsGcpTransformerInterface::TransformMethod m );
void setResamplingMethod( QgsImageWarper::ResamplingMethod m );
void setOutputPath( const QString &path );
void setDemPath( const QString &path );
```

Implement by setting combo indices / line edits and emitting change signals as appropriate.

- [ ] **Step 2.5: Persist paths on successful file dialogs**

When `openSourceRaster` / `loadReferenceRaster` / output browse succeeds, update snapshot fields via `mSession.saveWorkflow( captureWorkflowSnapshot() )` or set individual keys.

- [ ] **Step 2.6: Manual smoke (no automated dialog test required)**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) sicnu_geo_rs test_georef_session_state
ctest --output-on-failure -R 'SessionState'
```

Expected: session tests still pass; app builds.

- [ ] **Step 2.7: Commit**

```bash
git add src/app/georeferencer/qgsgeoreferencermainwindow.h \
        src/app/georeferencer/qgsgeoreferencermainwindow.cpp \
        src/app/georeferencer/rs_georef_params_panel.h \
        src/app/georeferencer/rs_georef_params_panel.cpp
git commit -m "feat(georef): dirty close prompt and workflow settings restore"
```

---

### Task 3: `contains()` + Move/Delete tools

**Files:**
- Modify: `src/app/georeferencer/qgsgeorefdatapoint.cpp`
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.h/.cpp`
- Create: `tests/test_gcp_contains.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 3.1: Write failing contains test**

Create `tests/test_gcp_contains.cpp` (with FastExitListener + ensureApp like canvas tests):

```cpp
TEST_CASE( "GCP contains: hit inside search radius on SRC", "[georef][contains]" )
{
  ensureApp();
  QgsMapCanvas src;
  src.resize( 400, 400 );
  src.mapSettings().setOutputSize( QSize( 400, 400 ) );
  src.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  QgsMapCanvas dst;
  dst.resize( 400, 400 );

  QgsGcpPoint gcp( QgsPointXY( 50, 50 ), QgsPointXY( 10, 10 ),
                   QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ), true );
  QgsGeorefDataPoint dp( &src, &dst, &gcp );
  dp.setId( 1 );
  dp.updateMarkers();

  double dist = -1;
  REQUIRE( dp.contains( QgsPointXY( 50, 50 ), QgsGcpPoint::PointType::Source, dist ) );
  REQUIRE( dist >= 0.0 );
  REQUIRE( dist < 1.0 );

  REQUIRE_FALSE( dp.contains( QgsPointXY( 0, 0 ), QgsGcpPoint::PointType::Source, dist ) );
}
```

- [ ] **Step 3.2: Register CMake target** (same pattern as `test_gcp_canvas_item`: link `qgis_app_georef`, AUTOMOC ON).

- [ ] **Step 3.3: Run — expect FAIL (contains always false)**

```bash
cd build && make -j$(nproc) test_gcp_contains && ctest --output-on-failure -R 'GCP contains'
```

- [ ] **Step 3.4: Implement `contains`**

In `qgsgeorefdatapoint.cpp`:

```cpp
#include "qgsmaptool.h"

bool QgsGeorefDataPoint::contains( const QgsPointXY &p, QgsGcpPoint::PointType type, double &distance )
{
  QgsGCPCanvasItem *item = nullptr;
  switch ( type )
  {
    case QgsGcpPoint::PointType::Source:
      item = mGCPSourceItem;
      break;
    case QgsGcpPoint::PointType::Destination:
      item = mGCPDestinationItem;
      break;
  }
  if ( !item || !item->canvas() )
    return false;

  const double searchRadiusMM = QgsMapTool::searchRadiusMM();
  const double pixelsPerMM = item->canvas()->logicalDpiX() / 25.4;
  const double searchRadiusPx = searchRadiusMM * pixelsPerMM;

  const QPointF pPos = item->toCanvasCoordinates( p );
  const QPointF itemPos = item->pos();
  const double dx = pPos.x() - itemPos.x();
  const double dy = pPos.y() - itemPos.y();
  const double d = std::hypot( dx, dy );
  if ( d <= searchRadiusPx )
  {
    distance = d;
    return true;
  }
  return false;
}
```

- [ ] **Step 3.5: Run contains test — PASS**

- [ ] **Step 3.6: Wire Move/Delete in main window**

Header private members:

```cpp
QgsGeorefToolMovePoint *mToolMoveSrc = nullptr;
QgsGeorefToolMovePoint *mToolMoveDst = nullptr;
QgsGeorefToolDeletePoint *mToolDeleteSrc = nullptr;
QgsGeorefToolDeletePoint *mToolDeleteDst = nullptr;
QAction *mMovePointAction = nullptr;
QAction *mDeletePointAction = nullptr;
QgsGeorefDataPoint *mMovingPoint = nullptr;
QgsGeorefDataPoint *mHoveredPoint = nullptr;
QgsPointXY mMoveOrigin;

QgsGeorefDataPoint *findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type );
void selectPoint( const QgsPointXY &p );
void movePoint( const QgsPointXY &p );
void releasePoint( const QgsPointXY &p );
void cancelPoint( const QgsPointXY &p );
void hoverPoint( const QgsPointXY &p );
void deletePointAt( const QgsPointXY &p );
```

In `setupToolbars` / `setupCentralWidget` after add-point wiring:

1. Create tools on SRC and REF canvases.
2. Create checkable Move/Delete actions in mode toolbar; put Add/Move/Delete in `QActionGroup`.
3. Connect:

```cpp
connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointBeginMove, this, &QgsGeoreferencerMainWindow::selectPoint );
connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointMoving, this, &QgsGeoreferencerMainWindow::movePoint );
connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointEndMove, this, &QgsGeoreferencerMainWindow::releasePoint );
connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointCancelMove, this, &QgsGeoreferencerMainWindow::cancelPoint );
// same for mToolMoveDst
connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::deletePoint, this, &QgsGeoreferencerMainWindow::deletePointAt );
connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::hoverPoint, this, &QgsGeoreferencerMainWindow::hoverPoint );
// same for mToolDeleteDst
```

`findDataPoint`: iterate `mDataPoints`, call `contains`, pick minimum distance.

`selectPoint`: determine PointType from `sender() == mToolMoveSrc` → Source else Destination; set `mMovingPoint`, call `tool->setStartPoint(...)`.

`movePoint`: if moving, `mMovingPoint->moveTo(p, type)` **without** full residual recompute (markers only).

`releasePoint`: clear start point on tool; `recomputeFit()`; dirty already from list if move mutates via setSourcePoint (ensure `moveTo` updates `QgsGcpPoint` which should emit list changed — if not, call `mSession.markDirty()` and `mGcps` signal manually).

**Important:** After `moveTo`, if `QgsGCPList` does not emit `changed`, call something that does or `markDirty()` + `recomputeFit()`.

`deletePointAt`: find → get `QgsGcpPoint*` → find row index in list → `removePointAt(row)`.

- [ ] **Step 3.7: Build app + contains tests**

```bash
cd build && make -j$(nproc) test_gcp_contains sicnu_geo_rs && ctest --output-on-failure -R 'GCP contains'
```

- [ ] **Step 3.8: Commit**

```bash
git add src/app/georeferencer/qgsgeorefdatapoint.cpp \
        src/app/georeferencer/qgsgeoreferencermainwindow.h \
        src/app/georeferencer/qgsgeoreferencermainwindow.cpp \
        tests/test_gcp_contains.cpp tests/CMakeLists.txt
git commit -m "feat(georef): GCP hit-test and Move/Delete map tools"
```

---

### Task 4: REF marker CRS reprojection

**Files:**
- Modify: `src/app/georeferencer/qgsgeorefdatapoint.cpp`
- Create: `tests/test_gcp_canvas_crs.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 4.1: Write failing CRS test**

```cpp
TEST_CASE( "GCP markers: REF item uses transformed destination CRS", "[georef][canvas][crs]" )
{
  ensureApp();
  // Minimal project context for transforms
  QgsProject::instance()->setCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );

  QgsMapCanvas src;
  src.resize( 200, 200 );
  src.setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  QgsMapCanvas ref;
  ref.resize( 200, 200 );
  // Destination canvas in Web Mercator
  ref.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3857" ) ) );
  ref.setExtent( QgsRectangle( 1e6, 1e6, 2e6, 2e6 ) );

  // Lon/lat point that is NOT valid as if it were already 3857 meters
  const QgsPointXY ll( 116.0, 39.0 );
  QgsGcpPoint gcp( QgsPointXY( 10, 10 ), ll,
                   QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ), true );
  QgsGeorefDataPoint dp( &src, &ref, &gcp );
  dp.setId( 0 );
  dp.updateMarkers();

  REQUIRE( dp.destinationItem() );
  const QgsPointXY world = dp.destinationItem()->worldPos();
  // Expected ~ mercator of 116E 39N (roughly 1.29e7, 4.7e6)
  REQUIRE( world.x() > 1.0e6 );
  REQUIRE( std::abs( world.x() - ll.x() ) > 1000.0 ); // must not leave raw lon as x
}
```

Confirm `QgsMapCanvas::setDestinationCrs` exists; if API is `mapSettings` only, set via `QgsMapSettings` copy + `setMapSettings`.

- [ ] **Step 4.2: Run — expect FAIL (worldPos still 116,39)**

- [ ] **Step 4.3: Fix `updateMarkers`**

```cpp
void QgsGeorefDataPoint::updateMarkers()
{
  if ( !mGcpPoint )
    return;
  if ( mGCPSourceItem )
  {
    mGCPSourceItem->setId( mId );
    mGCPSourceItem->setWorldPos( mGcpPoint->sourcePoint() );
    mGCPSourceItem->setEnabled( mGcpPoint->isEnabled() );
    mGCPSourceItem->setResidual( mGcpPoint->residual() );
  }
  if ( mGCPDestinationItem )
  {
    mGCPDestinationItem->setId( mId );
    QgsPointXY dest = mGcpPoint->destinationPoint();
    if ( mDstCanvas && mDstCanvas->mapSettings().destinationCrs().isValid() )
    {
      dest = mGcpPoint->transformedDestinationPoint(
        mDstCanvas->mapSettings().destinationCrs(),
        QgsProject::instance()->transformContext() );
    }
    mGCPDestinationItem->setWorldPos( dest );
    mGCPDestinationItem->setEnabled( mGcpPoint->isEnabled() );
  }
}
```

Include `qgsproject.h`.

- [ ] **Step 4.4: Run CRS test — PASS**

```bash
cd build && make -j$(nproc) test_gcp_canvas_crs && ctest --output-on-failure -R 'GCP markers'
```

- [ ] **Step 4.5: Commit**

```bash
git add src/app/georeferencer/qgsgeorefdatapoint.cpp \
        tests/test_gcp_canvas_crs.cpp tests/CMakeLists.txt
git commit -m "feat(georef): reproject REF GCP markers to canvas CRS"
```

---

### Task 5: Mode-aware pick canvas + dialog label

**Files:**
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.h/.cpp`
- Modify: `src/app/georeferencer/qgsmapcoordsdialog.cpp`
- Create: `tests/test_pick_canvas_mode.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 5.1: Expose testable pick helper**

In header public or test hook:

```cpp
/// Returns canvas used for MapCoords "从地图取点". Null-safe.
QgsMapCanvas *pickCanvasForMode( RsGeorefModeToggle::Mode m ) const;
QgsMapCanvas *pickCanvas() const; // uses current mode
```

Implementation:

```cpp
QgsMapCanvas *QgsGeoreferencerMainWindow::pickCanvasForMode( RsGeorefModeToggle::Mode m ) const
{
  if ( m == RsGeorefModeToggle::ImageToImage )
    return mRefCanvas;
  if ( mIface && mIface->mapCanvas() )
    return mIface->mapCanvas();
  return mRefCanvas; // fallback
}

QgsMapCanvas *QgsGeoreferencerMainWindow::pickCanvas() const
{
  const auto mode = mModeToggle ? mModeToggle->currentMode()
                                : RsGeorefModeToggle::ImageToMap;
  return pickCanvasForMode( mode );
}
```

- [ ] **Step 5.2: Test with null iface**

```cpp
TEST_CASE( "pickCanvas: ImageToImage uses REF; ImageToMap falls back without iface", "[georef][pick]" )
{
  ensureApp();
  // Construct window with nullptr iface if supported; else skip.
  QgsGeoreferencerMainWindow w( nullptr );
  REQUIRE( w.pickCanvasForMode( RsGeorefModeToggle::ImageToImage ) == /* ref canvas via objectName */
           w.findChild<QgsMapCanvas *>( QStringLiteral( "rsRefCanvas" ) ) );
  // ImageToMap without iface → same REF fallback
  REQUIRE( w.pickCanvasForMode( RsGeorefModeToggle::ImageToMap )
           == w.findChild<QgsMapCanvas *>( QStringLiteral( "rsRefCanvas" ) ) );
}
```

If constructor requires non-null iface, use a minimal mock implementing `mapCanvas()` returning a stack canvas — only if project already has a test double; otherwise document manual smoke and unit-test pure logic by extracting free function in cpp:

```cpp
// in anonymous namespace or as static method for tests
QgsMapCanvas *resolvePickCanvas( RsGeorefModeToggle::Mode m,
                                 QgsMapCanvas *ref,
                                 QgsMapCanvas *mainApp )
{
  if ( m == RsGeorefModeToggle::ImageToImage )
    return ref;
  return mainApp ? mainApp : ref;
}
```

Prefer the free-function test if window ctor is heavy — put `resolvePickCanvas` in `rs_georef_session_state` or a tiny `rs_georef_pick_canvas.h` **only if needed**; YAGNI: keep as private static in mainwindow cpp and test via public `pickCanvasForMode`.

- [ ] **Step 5.3: Change `showCoordDialog`**

```cpp
auto *dlg = new QgsMapCoordsDialog( pickCanvas(), tempDataPoint, rasterCrs, this );
```

If fallback used and mode is ImageToMap, once:

```cpp
if ( statusBar() && mode != ImageToImage && !( mIface && mIface->mapCanvas() ) )
  statusBar()->showMessage( tr( "主地图不可用，改用参考画布取点" ), 4000 );
```

- [ ] **Step 5.4: Rename dialog button**

In `qgsmapcoordsdialog.cpp`:

```cpp
mPointFromCanvasPushButton = new QPushButton(
  QgsApplication::getThemeIcon( "georeferencer/mPushButtonPencil.svg" ),
  tr( "从地图取点" ) );
```

- [ ] **Step 5.5: Build + test**

```bash
cd build && make -j$(nproc) test_pick_canvas_mode sicnu_geo_rs && ctest --output-on-failure -R 'pickCanvas'
```

- [ ] **Step 5.6: Commit**

```bash
git add src/app/georeferencer/qgsgeoreferencermainwindow.h \
        src/app/georeferencer/qgsgeoreferencermainwindow.cpp \
        src/app/georeferencer/qgsmapcoordsdialog.cpp \
        tests/test_pick_canvas_mode.cpp tests/CMakeLists.txt
git commit -m "feat(georef): mode-aware map pick canvas for GCP destination"
```

---

### Task 6: RPC refinement before/after RMS

**Files:**
- Modify: `src/app/georeferencer/rs_georef_params_panel.h/.cpp`
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.cpp` (`recomputeFit`)
- Modify: `tests/test_rpc_gcp_refine.cpp` (add RMS helper assertion case optional)

- [ ] **Step 6.1: Add `clearRefinementRms`**

In `rs_georef_params_panel.h`:

```cpp
void clearRefinementRms();
```

In cpp:

```cpp
void RsGeorefParamsPanel::clearRefinementRms()
{
  if ( mRmsBefore )
    mRmsBefore->setText( tr( "精化前 RMS: —" ) );
  if ( mRmsAfter )
  {
    mRmsAfter->setText( tr( "精化后 RMS: —" ) );
    mRmsAfter->setStyleSheet( QString() );
  }
}
```

- [ ] **Step 6.2: Extract residual RMS helper in mainwindow cpp**

Anonymous namespace:

```cpp
double computeEnabledRms( QgsGCPList *gcps )
{
  if ( !gcps ) return 0.0;
  double totalSq = 0.0;
  int n = 0;
  for ( const QgsGcpPoint *p : std::as_const( *gcps ) )
  {
    if ( !p || !p->isEnabled() ) continue;
    const QPointF r = p->residual();
    totalSq += r.x() * r.x() + r.y() * r.y();
    ++n;
  }
  return n > 0 ? std::sqrt( totalSq / n ) : 0.0;
}
```

- [ ] **Step 6.3: Dual-run in `recomputeFit` for RpcPhysical**

After collecting `src`/`dst` and `enabledCount >= minN`:

```cpp
double rmsBefore = -1.0;
double rmsAfter = -1.0;

if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical
     && enabledCount >= 3 )
{
  // BEFORE
  auto beforeXf = std::make_unique<QgsGeorefTransform>( method );
  if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>( beforeXf->gcpTransformer() ) )
  {
    rpc->setSourceRasterPath( mSourceRasterPath );
    rpc->setRpcOptions( mParamsPanel->demPath(), mParamsPanel->demZOffset(), false );
  }
  if ( beforeXf->updateParametersFromGcps( src, dst, false ) )
  {
    const auto dstCrs = mParamsPanel->destCrs();
    mGcps->updateResiduals( beforeXf.get(), dstCrs, dstCrs );
    rmsBefore = computeEnabledRms( mGcps );
  }

  // AFTER (working transform)
  mTransform.reset( new QgsGeorefTransform( method ) );
  if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>( mTransform->gcpTransformer() ) )
  {
    rpc->setSourceRasterPath( mSourceRasterPath );
    rpc->setRpcOptions( mParamsPanel->demPath(), mParamsPanel->demZOffset(), true );
  }
  fitOk = mTransform->updateParametersFromGcps( src, dst, false );
  if ( fitOk )
  {
    const auto dstCrs = mParamsPanel->destCrs();
    mGcps->updateResiduals( mTransform.get(), dstCrs, dstCrs );
    rmsAfter = computeEnabledRms( mGcps );
  }

  if ( rmsBefore >= 0.0 && rmsAfter >= 0.0 )
    mParamsPanel->setRefinementRms( rmsBefore, rmsAfter );
  else
    mParamsPanel->clearRefinementRms();
}
else
{
  mParamsPanel->clearRefinementRms();
  // existing single-transform path for non-RPC methods...
}
```

Refactor carefully so non-RPC path remains a single `mTransform` fit (do not double work). Structure:

```
if (RPC && enabled>=3) { dual path }
else {
  clearRefinementRms();
  existing single fit + residual loop
}
// shared: setRmsValues / scatter / status bar / apply enable — use residuals from final mTransform
```

For dual path, after AFTER residuals, still populate scatter/`setRmsValues` from `rmsAfter` side (same loop as today).

- [ ] **Step 6.4: Unit-level RMS ordering (algorithm already tested)**

Add to `tests/test_rpc_gcp_refine.cpp`:

```cpp
TEST_CASE( "RPC refinement: residual magnitude non-increasing vs unrefined",
           "[georef][rpc][refine][rms]" )
{
  // Reuse same synthetic setup as existing case; assert residWithRef <= residNoRef
  // (already in first TEST_CASE — if redundant, skip this step)
}
```

If first case already asserts `residWithRef < residNoRef`, **skip** new case (YAGNI). Instead run existing:

```bash
ctest --output-on-failure -R 'RPC refinement'
```

- [ ] **Step 6.5: Build regression**

```bash
cd build && make -j$(nproc) test_rpc_gcp_refine test_georef_window_rpc_mode sicnu_geo_rs
ctest --output-on-failure -R 'rpc|RPC|georef'
```

- [ ] **Step 6.6: Commit**

```bash
git add src/app/georeferencer/rs_georef_params_panel.h \
        src/app/georeferencer/rs_georef_params_panel.cpp \
        src/app/georeferencer/qgsgeoreferencermainwindow.cpp
git commit -m "feat(georef): show RPC refinement before/after RMS in panel"
```

---

### Task 7: Full regression + polish checklist

**Files:** none new (verification only)

- [ ] **Step 7.1: Rebuild all georef tests**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) \
  test_georef_session_state test_gcp_contains test_gcp_canvas_crs \
  test_pick_canvas_mode test_gcp_canvas_item test_gcp_list test_gcp_points_file \
  test_georef_window test_georef_window_rpc_mode test_georef_window_warp_lock \
  test_rpc_transformer test_rpc_gcp_refine test_rpc_golden test_image_warper \
  test_sift_matcher test_crs_picker_persists test_image_to_image_load \
  sicnu_geo_rs
```

- [ ] **Step 7.2: Run georef ctest suite**

```bash
cd build && ctest --output-on-failure -R 'georef|GCP|RPC|SessionState|pickCanvas|SIFT|CRS picker|Image-to-Image|warper|warp'
```

Expected: all green (SIFT may SKIP without OpenCV).

- [ ] **Step 7.3: Manual checklist (engineer)**

| # | Check |
|---|--------|
| 1 | Add GCPs, close → Save/Discard/Cancel works; Cancel keeps window |
| 2 | Save points, edit, close → prompts again |
| 3 | Restart app → geometry/mode/paths restored |
| 4 | Image→Map: 从地图取点 clicks on **main** map |
| 5 | Image→Image: 从地图取点 clicks on REF |
| 6 | Move tool drags SRC point; Delete removes |
| 7 | I2I with dest CRS ≠ ref CRS: markers align |
| 8 | RPC mode ≥3 GCP: 精化前/后 RMS labels populated |

- [ ] **Step 7.4: Final docs touch (optional)**

If `task_plan.md` / `progress.md` are maintained this sprint, add Phase 11.6 complete note pointing to this plan + spec. Skip if out of habit for this branch.

- [ ] **Step 7.5: Commit only if docs updated; else done**

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| P1 dirty close Save/Discard/Cancel | Task 2 |
| P2 workflow settings keys + restore | Task 1–2 |
| P3 REF CRS markers | Task 4 |
| P4 RPC before/after RMS | Task 6 |
| B mode-aware pick canvas | Task 5 |
| C contains + Move/Delete | Task 3 |
| No main-window split / no SIFT change | All tasks (out of scope) |
| Tests matrix §7 | Tasks 1,3,4,5,6,7 |

## Placeholder / consistency self-review

- No TBD steps; APIs named consistently: `RsGeorefSessionState`, `pickCanvasForMode`, `clearRefinementRms`, `isDirtyForTest`.
- Settings prefix always `Georeferencer/`.
- Dual RPC path keeps `mTransform` as refined model for Apply.
- Move tool uses existing two-phase click API (`setStartPoint`).

---

## Execution handoff

Plan saved. Choose how to implement:

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session with executing-plans and checkpoints  
