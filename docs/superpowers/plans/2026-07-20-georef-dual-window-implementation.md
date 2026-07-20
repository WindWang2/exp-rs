# Georeferencer Dual-Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single Georeferencer shell (toolbar mode toggle) with two singleton windows—**Image 2 Image** (twin rasters) and **Image 2 Map** (SRC + main-project map preview)—opened from **Image Registration** submenu, with RPC only as an I2M transform method.

**Architecture:** Keep warp/GCP/SIFT logic inside `src/app/georeferencer/`. Introduce a small shared base (or shared helper) for GCP list, fit, warp, and session; specialize layout and File menus per window. Main app holds two lazy singletons. Do **not** copy `qgsgeoreferencermainwindow.cpp` wholesale into two 1500-line forks.

**Tech Stack:** C++20 / Qt6 / Catch2 / existing `qgis_app_georef` static lib / GDAL / OpenCV (SIFT, optional)

**Spec:** `docs/superpowers/specs/2026-07-20-georef-dual-window-design.md`

---

## Conventions

- **Build:** `cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) qgis_app_georef sicnu_geo_rs`
- **Tests:** `cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R '<name>'` or run Catch binary under `build/` with `LD_LIBRARY_PATH` set like other app tests
- **Commits:** `feat(georef):`, `test(georef):`, `chore(georef):`
- **TDD:** write/adjust failing tests first when adding behavior; green before commit
- **YAGNI:** no multi-instance same mode; no full core extraction library unless a step requires it—prefer progressive extraction inside `georeferencer/`

---

## File map

| Path | Role |
|------|------|
| `src/app/main_window.h` | Replace `m_georefWindow` with `m_georefI2I` + `m_georefI2M`; declare two open slots |
| `src/app/main_window_menus.cpp` | Image Registration submenu |
| `src/app/main_window_view.cpp` | `openGeorefImageToImage()` / `openGeorefImageToMap()` |
| `src/app/main_window.cpp` | Dispose both windows on close |
| `src/app/georeferencer/qgsgeoreferencermainwindow.h/.cpp` | Becomes **I2I shell** (or rename later); remove mode-toggle as primary UX |
| `src/app/georeferencer/qgsgeoref_image_to_map_window.h/.cpp` | **New** I2M shell (vertical SRC+Map, RPC via params) |
| `src/app/georeferencer/rs_georef_mode_toggle.*` | Stop using in UI; optional delete after tests updated |
| `src/app/georeferencer/rs_georef_params_panel.*` | RPC visibility driven by transform method, not window mode enum |
| `src/app/georeferencer/CMakeLists.txt` | Add I2M sources |
| `tests/test_georef_dual_window.cpp` | **New** singleton + layout smoke tests |
| `tests/test_georef_window*.cpp` | Point at I2I class; drop “mode toggle RPC” assumptions where obsolete |
| `docs/labs/lab6_georeferencing.md` | Menu path update |

---

### Task 1: Menu dual entry + dual singletons (wire to existing window twice temporarily)

**Files:**
- Modify: `src/app/main_window.h`
- Modify: `src/app/main_window_menus.cpp`
- Modify: `src/app/main_window_view.cpp`
- Modify: `src/app/main_window.cpp`
- Test: `tests/test_georef_dual_window.cpp` (create in Task 2; for Task 1 manual verify is OK if no QTest harness for menus)

- [ ] **Step 1: Update `main_window.h` declarations**

Replace georef open API and member:

```cpp
// was:
// class QgsGeoreferencerMainWindow;
// void openGeoreferencer();
// QgsGeoreferencerMainWindow *m_georefWindow = nullptr;

class QgsGeoreferencerMainWindow; // I2I shell (existing class, fixed I2I behavior after Task 3)
class QgsGeorefImageToMapWindow;  // I2M shell (Task 4+)

void openGeorefImageToImage();
void openGeorefImageToMap();
// keep openGeoreferencer() as thin alias calling openGeorefImageToImage() for one release if needed

QgsGeoreferencerMainWindow *m_georefI2I = nullptr;
QgsGeorefImageToMapWindow *m_georefI2M = nullptr;
```

Until I2M class exists, implement `openGeorefImageToMap()` as a `QMessageBox::information` stub **only if** you split tasks across PRs; prefer Task 1+3+4 in one branch with I2M skeleton in Task 4 first. **Preferred order on one branch:** Task 4 skeleton (empty window) → Task 1 menu → Task 3 I2I polish.

- [ ] **Step 2: Menu — Image Registration submenu**

In `main_window_menus.cpp`, replace the single Georeferencer action under raster menu:

```cpp
QMenu *regMenu = rasterMenu->addMenu( tr( "Image Registration" ) );
regMenu->setObjectName( QStringLiteral( "mImageRegistrationMenu" ) );
regMenu->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
                    tr( "Image 2 Image" ),
                    this, &QgisDesktopWindow::openGeorefImageToImage );
regMenu->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
                    tr( "Image 2 Map" ),
                    this, &QgisDesktopWindow::openGeorefImageToMap );
```

Remove: `rasterMenu->addAction(..., tr("Georeferencer..."), ..., openGeoreferencer)`.

- [ ] **Step 3: Implement open slots in `main_window_view.cpp`**

```cpp
#include <georeferencer/qgsgeoreferencermainwindow.h>
// after Task 4:
// #include <georeferencer/qgsgeoref_image_to_map_window.h>

void QgisDesktopWindow::openGeorefImageToImage()
{
  if ( !m_georefI2I )
  {
    m_georefI2I = new QgsGeoreferencerMainWindow( nullptr, this );
    m_georefI2I->setAttribute( Qt::WA_DeleteOnClose, false );
    m_georefI2I->setWindowTitle( tr( "Image Registration · Image 2 Image" ) );
  }
  m_georefI2I->show();
  m_georefI2I->raise();
  m_georefI2I->activateWindow();
}

void QgisDesktopWindow::openGeorefImageToMap()
{
  if ( !m_georefI2M )
  {
    m_georefI2M = new QgsGeorefImageToMapWindow( this /* iface if needed */, this );
    m_georefI2M->setAttribute( Qt::WA_DeleteOnClose, false );
    m_georefI2M->setWindowTitle( tr( "Image Registration · Image 2 Map" ) );
  }
  m_georefI2M->show();
  m_georefI2M->raise();
  m_georefI2M->activateWindow();
}

// Optional compatibility:
void QgisDesktopWindow::openGeoreferencer()
{
  openGeorefImageToImage();
}
```

- [ ] **Step 4: Dispose both in destructor path**

In `main_window.cpp` where `m_georefWindow` is disposed:

```cpp
disposeChildWindow( static_cast<QWidget *>( static_cast<void *>( m_georefI2I ) ) );
m_georefI2I = nullptr;
disposeChildWindow( static_cast<QWidget *>( static_cast<void *>( m_georefI2M ) ) );
m_georefI2M = nullptr;
```

- [ ] **Step 5: Build**

```bash
cd /home/kevin/projects/exp-rs/build && make -j$(nproc) sicnu_geo_rs
```

Expected: links successfully (I2M class must exist—complete Task 4 skeleton first if needed).

- [ ] **Step 6: Commit**

```bash
git add src/app/main_window.h src/app/main_window_menus.cpp src/app/main_window_view.cpp src/app/main_window.cpp
git commit -m "feat(georef): Image Registration menu opens I2I/I2M windows"
```

---

### Task 2: Catch2 dual-window / singleton smoke tests

**Files:**
- Create: `tests/test_georef_dual_window.cpp`
- Modify: `tests/CMakeLists.txt` — register target linking `qgis_app_georef` (same pattern as `test_georef_window`)

- [ ] **Step 1: Write test file**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "georeferencer/qgsgeoreferencermainwindow.h"
#include "georeferencer/qgsgeoref_image_to_map_window.h"

#include <QApplication>
#include <QSplitter>

// Ensure QApplication exists (same pattern as other GUI Catch tests in this repo).
static int argc = 1;
static char arg0[] = "test_georef_dual_window";
static char *argv[] = { arg0, nullptr };
static QApplication *app = []() {
  static QApplication a( argc, argv );
  return &a;
}();

TEST_CASE( "I2I window has horizontal twin canvases", "[georef][dual]" )
{
  QgsGeoreferencerMainWindow w( nullptr, nullptr );
  w.setWindowTitle( QStringLiteral( "Image Registration · Image 2 Image" ) );
  auto *src = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefSrcCanvas" ) );
  auto *ref = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefRefCanvas" ) );
  REQUIRE( src != nullptr );
  REQUIRE( ref != nullptr );
  // Mode toggle must not be the primary UX (hidden or absent)
  auto *toggle = w.findChild<QWidget *>( QStringLiteral( "rsGeorefModeToggle" ) );
  if ( toggle )
    REQUIRE( toggle->isHidden() );
}

TEST_CASE( "I2M window has SRC and Map canvases", "[georef][dual]" )
{
  QgsGeorefImageToMapWindow w( nullptr, nullptr );
  auto *src = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefI2MSrcCanvas" ) );
  auto *map = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefI2MMapCanvas" ) );
  REQUIRE( src != nullptr );
  REQUIRE( map != nullptr );
  auto *splitter = w.findChild<QSplitter *>( QStringLiteral( "rsGeorefI2MSplitter" ) );
  REQUIRE( splitter != nullptr );
  REQUIRE( splitter->orientation() == Qt::Vertical );
}
```

Adjust objectNames to match what you set in Task 3/4 (keep names stable).

- [ ] **Step 2: Register in `tests/CMakeLists.txt`**

Copy the `test_georef_window` target block; rename to `test_georef_dual_window`; link same libs (`qgis_app_georef`, Qt, Catch2, qgis_core/gui).

- [ ] **Step 3: Build and run (expect fail until Task 3/4)**

```bash
cd build && make -j$(nproc) test_georef_dual_window && \
  QT_QPA_PLATFORM=offscreen ./test_georef_dual_window --reporter compact
```

Expected after Task 3–4: all pass.

- [ ] **Step 4: Commit when green**

```bash
git add tests/test_georef_dual_window.cpp tests/CMakeLists.txt
git commit -m "test(georef): dual-window smoke for I2I and I2M shells"
```

---

### Task 3: Fix existing main window as I2I-only shell

**Files:**
- Modify: `src/app/georeferencer/qgsgeoreferencermainwindow.cpp` / `.h`
- Modify: `src/app/georeferencer/rs_georef_mode_toggle.*` (optional hide only)
- Modify: `tests/test_georef_window_rpc_mode.cpp` — RPC no longer via mode toggle on I2I

- [ ] **Step 1: Constructor — force Image-to-Image layout**

In constructor / `setupCentralWidget` / after mode toggle creation:

1. Set title default: `Image Registration · Image 2 Image` (app may override).
2. Ensure horizontal splitter with SRC + REF always visible.
3. **Hide** mode toolbar widget:

```cpp
if ( mModeBar && mModeToggle )
{
  mModeToggle->setObjectName( QStringLiteral( "rsGeorefModeToggle" ) );
  mModeToggle->hide();           // product: no entry toggle
  mModeToggle->setMode( RsGeorefModeToggle::ImageToImage );
  // Optionally hide entire mode segment if only toggle lived there
}
```

4. Call `onModeChanged( ImageToImage )` once at end of setup so REF store path is active.
5. Ensure File menu still has Open Source + Open Reference.

- [ ] **Step 2: Params panel — no RPC section on I2I**

After creating `mParamsPanel`:

```cpp
mParamsPanel->setRpcMode( false ); // force hide DEM/RPC UI for I2I window
```

If transform combo still lists RPC, either:

- Remove RPC from combo when panel is in “I2I profile”, **or**
- Add `RsGeorefParamsPanel::setProfile(I2I|I2M)` that filters methods.

Recommended API:

```cpp
// rs_georef_params_panel.h
enum class Profile { ImageToImage, ImageToMap };
void setProfile( Profile p );
```

`ImageToImage`: hide RPC method row entry + DEM section.  
`ImageToMap`: show all methods including RPC; DEM follows method==RPC.

- [ ] **Step 3: Update RPC mode test**

`tests/test_georef_window_rpc_mode.cpp` currently may call mode toggle → RPC. Change to:

- Construct **I2M** window and select RPC method on panel, **or**
- Call `paramsPanel->setProfile(ImageToMap); setTransformMethod(RpcPhysical);` and assert DEM visible.

Do not require `RsGeorefModeToggle::RpcPhysical` on I2I window.

- [ ] **Step 4: Build + run georef tests**

```bash
cd build && make -j$(nproc) qgis_app_georef test_georef_window test_georef_window_rpc_mode test_georef_window_warp_lock && \
  QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'georef|rpc'
```

Expected: pass (adjust any remaining mode-toggle assertions).

- [ ] **Step 5: Commit**

```bash
git add src/app/georeferencer/ tests/
git commit -m "feat(georef): pin existing window to Image 2 Image profile"
```

---

### Task 4: Image 2 Map window skeleton + map layer mirror

**Files:**
- Create: `src/app/georeferencer/qgsgeoref_image_to_map_window.h`
- Create: `src/app/georeferencer/qgsgeoref_image_to_map_window.cpp`
- Modify: `src/app/georeferencer/CMakeLists.txt`

- [ ] **Step 1: Header skeleton**

```cpp
#pragma once
#include <QMainWindow>
class QgisInterface;
class QgsMapCanvas;
class QgsMapLayerStore;
class QgsGCPList;
class RsGeorefParamsPanel;
// ... other forward decls as needed

class QgsGeorefImageToMapWindow : public QMainWindow
{
  Q_OBJECT
public:
  explicit QgsGeorefImageToMapWindow( QgisInterface *iface, QWidget *parent = nullptr );
  ~QgsGeorefImageToMapWindow() override;

  QgsMapCanvas *srcCanvas() const { return mSrcCanvas; }
  QgsMapCanvas *mapCanvas() const { return mMapCanvas; }

public slots:
  void openSourceRaster();
  void refreshMapLayersFromProject();
  void applyTransform();

private:
  void setupUi();
  void setupMenus();
  void setupToolbars();

  QgisInterface *mIface = nullptr;
  QgsMapCanvas *mSrcCanvas = nullptr;
  QgsMapCanvas *mMapCanvas = nullptr; // mirrors QgsProject layers
  QgsMapLayerStore *mSrcStore = nullptr; // owns source raster only
  QgsGCPList *mGcps = nullptr;
  RsGeorefParamsPanel *mParamsPanel = nullptr;
  // tools, data points, transform, session — mirror I2I patterns
};
```

- [ ] **Step 2: Central layout — vertical splitter**

```cpp
auto *splitter = new QSplitter( Qt::Vertical, this );
splitter->setObjectName( QStringLiteral( "rsGeorefI2MSplitter" ) );
mSrcCanvas = new QgsMapCanvas( splitter );
mSrcCanvas->setObjectName( QStringLiteral( "rsGeorefI2MSrcCanvas" ) );
mMapCanvas = new QgsMapCanvas( splitter );
mMapCanvas->setObjectName( QStringLiteral( "rsGeorefI2MMapCanvas" ) );
splitter->addWidget( mSrcCanvas );
splitter->addWidget( mMapCanvas );
splitter->setStretchFactor( 0, 1 );
splitter->setStretchFactor( 1, 1 );
setCentralWidget( splitter );
```

Also: bottom GCP dock + right params dock (copy wiring pattern from I2I constructor, **do not** copy entire 1400-line file—extract helpers if duplication exceeds ~100 lines of identical connect glue).

- [ ] **Step 3: `refreshMapLayersFromProject`**

```cpp
void QgsGeorefImageToMapWindow::refreshMapLayersFromProject()
{
  if ( !mMapCanvas )
    return;
  QList<QgsMapLayer *> layers;
  const auto projectLayers = QgsProject::instance()->mapLayers().values();
  // Prefer layer-tree order + visibility if available:
  QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
  const QList<QgsMapLayer *> ordered = root->checkedLayers();
  for ( QgsMapLayer *l : ordered )
  {
    if ( l && l->isValid() )
      layers << l;
  }
  mMapCanvas->setLayers( layers );
  if ( mParamsPanel && mParamsPanel->destCrs().isValid() )
    mMapCanvas->setDestinationCrs( mParamsPanel->destCrs() );
  mMapCanvas->refresh();
}
```

Connect:

```cpp
connect( QgsProject::instance(), &QgsProject::layersAdded, this, [this]( auto ) { refreshMapLayersFromProject(); } );
connect( QgsProject::instance(), &QgsProject::layersWillBeRemoved, this, [this]( auto ) { refreshMapLayersFromProject(); } );
// Also layer tree visibility if signal available via QgsLayerTreeNode::visibilityChanged on root
```

Call once at end of constructor.

- [ ] **Step 4: Params profile I2M + RPC**

```cpp
mParamsPanel->setProfile( RsGeorefParamsPanel::Profile::ImageToMap );
// When transformMethodChanged to RpcPhysical → setRpcMode(true); else false
connect( mParamsPanel, &RsGeorefParamsPanel::transformMethodChanged, this, [this]( auto method ) {
  mParamsPanel->setRpcMode( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  recomputeFit();
});
```

- [ ] **Step 5: File menu — only Open Source** (no Open Reference)

- [ ] **Step 6: GCP tools** — Add point: click SRC → click Map (reuse/adapt add-point tools with second canvas = map). Destination CRS = `mParamsPanel->destCrs()`.

- [ ] **Step 7: Hide SIFT** toolbar action on I2M.

- [ ] **Step 8: CMake**

Add `qgsgeoref_image_to_map_window.cpp` to `qgis_app_georef` target in `src/app/georeferencer/CMakeLists.txt`.

- [ ] **Step 9: Build + dual-window tests green**

```bash
cd build && make -j$(nproc) qgis_app_georef test_georef_dual_window sicnu_geo_rs && \
  QT_QPA_PLATFORM=offscreen ./test_georef_dual_window --reporter compact
```

- [ ] **Step 10: Commit**

```bash
git add src/app/georeferencer/ tests/
git commit -m "feat(georef): Image 2 Map window with project layer mirror"
```

---

### Task 5: Params panel `setProfile` + method-driven RPC

**Files:**
- Modify: `src/app/georeferencer/rs_georef_params_panel.h/.cpp`
- Test: extend `tests/test_georef_window_rpc_mode.cpp` or small unit on panel alone

- [ ] **Step 1: Add Profile API**

```cpp
enum class Profile { ImageToImage, ImageToMap };
void setProfile( Profile p );
Profile profile() const { return mProfile; }
```

- [ ] **Step 2: Implementation**

When building / updating transform method combo:

- I2I profile: remove or hide `RpcPhysical` item  
- I2M profile: ensure `RpcPhysical` present  

`setRpcMode(bool)` remains for DEM section visibility; I2M window drives it from selected method.

- [ ] **Step 3: Test**

```cpp
TEST_CASE( "params panel I2I hides RPC dem section", "[georef][panel]" )
{
  RsGeorefParamsPanel p;
  p.setProfile( RsGeorefParamsPanel::Profile::ImageToImage );
  p.setRpcMode( false );
  REQUIRE_FALSE( p.isDemSectionVisible() );
}

TEST_CASE( "params panel I2M can show dem for RPC", "[georef][panel]" )
{
  RsGeorefParamsPanel p;
  p.setProfile( RsGeorefParamsPanel::Profile::ImageToMap );
  p.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  p.setRpcMode( true );
  REQUIRE( p.isDemSectionVisible() ); // uses !isHidden() pattern if needed
}
```

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(georef): params panel profiles for I2I vs I2M RPC"
```

---

### Task 6: Apply/warp path smoke + docs

**Files:**
- Modify: `docs/labs/lab6_georeferencing.md` — menu path
- Manual: launch app, open both windows

- [ ] **Step 1: Update lab doc**

Replace `Raster > Georeferencer` with:

```markdown
1. 菜单: Raster → Image Registration → Image 2 Map（或 Image 2 Image）
```

Document briefly:

- I2I: open SRC + REF, pick GCPs on both, Apply  
- I2M: open SRC, GCPs on SRC + map preview (main project layers), methods include RPC  

- [ ] **Step 2: Manual checklist**

1. Start `sicnu_geo_rs`  
2. Open I2I and I2M both — both visible  
3. I2I: no mode toggle; dual rasters  
4. I2M: vertical SRC/Map; load a project raster on main map → appears on Map canvas after refresh  
5. Re-click menu items → same window raised  

- [ ] **Step 3: Commit docs**

```bash
git add docs/labs/lab6_georeferencing.md
git commit -m "docs(georef): lab6 dual-window Image Registration menu"
```

---

### Task 7 (optional polish): Shared fit/warp helper extraction

**Only if** I2M and I2I duplicated >~150 lines of identical `recomputeFit` / `applyTransform` / GCP tool wiring.

**Files:**
- Create: `src/app/georeferencer/rs_georef_session_controller.h/.cpp` (name flexible)
- Move: shared slots that operate on `mGcps`, `mTransform`, `mParamsPanel`, canvases passed by pointer

- [ ] **Step 1: Extract without behavior change**  
- [ ] **Step 2: Both windows call controller**  
- [ ] **Step 3: Full georef ctest green**  
- [ ] **Step 4: Commit** `refactor(georef): share fit/warp controller between I2I and I2M`

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| Image Registration submenu | Task 1 |
| I2I / I2M singletons, concurrent | Task 1, 2 |
| I2I horizontal twin rasters, no entry mode toggle | Task 3 |
| I2I SIFT, Open REF, no RPC UI | Task 3, 5 |
| I2M vertical SRC+Map | Task 4 |
| Map mirrors main project layers | Task 4 |
| RPC only on I2M as method | Task 4, 5 |
| SIFT only I2I | Task 3, 4 |
| Dirty close independent | inherits existing session code per window |
| Lab docs | Task 6 |
| Shared core / no full fork | Task 7 optional; Tasks 3–4 progressive |

## Placeholder scan

Plan contains no TBD/TODO implementation holes; objectNames are specified; commands are concrete.

## Type consistency

- Window classes: `QgsGeoreferencerMainWindow` (I2I), `QgsGeorefImageToMapWindow` (I2M)  
- Canvas names: `rsGeorefSrcCanvas` / `rsGeorefRefCanvas` (I2I existing—verify in code and align tests); I2M: `rsGeorefI2MSrcCanvas` / `rsGeorefI2MMapCanvas`  
- Panel: `RsGeorefParamsPanel::Profile::{ImageToImage,ImageToMap}`  

**Note:** Before Task 2 tests, grep existing objectNames:

```bash
rg "setObjectName" src/app/georeferencer/qgsgeoreferencermainwindow.cpp
```

Align test strings to reality (do not invent conflicting names).

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-20-georef-dual-window-implementation.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session with executing-plans and checkpoints  

Which approach?
