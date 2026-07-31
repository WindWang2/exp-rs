# Headless Asset Seam for Python App Interface Proxy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebind `AppInterfaceBridge` from `ActiveViewHost*` to `DataManager*` (required asset authority) + optional view host, so the out-of-process Python plugin IPC seam works without Qt Widgets.

**Architecture:** `AppInterfaceBridge` inverts its dependency: `DataManager*` becomes the first constructor argument and owns asset registration/catalog queries; `ActiveViewHost*` demotes to an optional display/canvas/message-bar enhancement. An explicit `m_activeAssetId` replaces "canvas current layer" as the plugin-visible meaning of "active layer" in both GUI and headless modes. `PythonAppInterfaceProxy` and `PythonPluginAdapter` adapt to the new constructor and gain the additive `catalog.set_active_layer` IPC method.

**Tech Stack:** C++17, Qt 6 (Core/Widgets), QGIS core (vendored), Catch2, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-08-01-headless-asset-seam-spec.md` (commit `ae8da38d6f`)

## Global Constraints

- Do NOT modify `worker_daemon.py`, `PythonIpcServer`, `DataManager`, or `ActiveViewHost` themselves.
- Do NOT fix production wiring of `SicnuAppInterface` in `src/gui/main_window.cpp` (follow-up ticket 1).
- Do NOT implement `processing.execute_algorithm` in the daemon (follow-up ticket 2).
- Python-side IPC contract unchanged except one additive method `catalog.set_active_layer`; existing response shapes (`no_active_layer`, `no_canvas`) must be preserved verbatim.
- Match the surrounding code style: 2-space indent, `QStringLiteral` for string literals, brace-on-new-line, `m_` member prefix.
- Test binary: `./build/tests/test_python_plugin_manager` (Catch2, tag-filtered). Build: `cmake --build build --target test_python_plugin_manager -j"$(nproc)"`.
- Sample fixture: `samples/dem_sample.tif` under `TEST_DATA_DIR` (= `<repo>/data`), already used by the `[python][iface][contract]` test.

---

### Task 1: Rebind `AppInterfaceBridge` to `DataManager*`

**Files:**
- Modify: `src/python/isolated/app_interface_bridge.h`
- Modify: `src/python/isolated/app_interface_bridge.cpp`
- Test: `tests/test_python_plugin_manager.cpp` (new TEST_CASE after the existing `[python][bridge]` case at line 453)

**Interfaces:**
- Consumes: `sicnu::data::DataManager::registerSource(const RegisterRequest&) → RegisterResult{AssetId assetId; ...}` (`src/data/data_manager.h:79`, `src/data/data_asset.h:39`); `DataManager::asset(AssetId) → std::optional<AssetSnapshot>` (`src/data/data_manager.h:82`); `AssetSnapshot::displayName()/source().canonicalSource/kind()/structure()` (`src/data/data_asset.h:236-294`); `AssetId::isNull()/toString()/fromString()/generate()` (`src/data/asset_types.h:13-30`).
- Produces (later tasks rely on these exact signatures):
  - `AppInterfaceBridge( sicnu::data::DataManager *dataManager = nullptr, ActiveViewHost *activeViewHost = nullptr, QObject *parent = nullptr )`
  - `void setDataManager( sicnu::data::DataManager * );` / `sicnu::data::DataManager *dataManager() const;`
  - `bool setActiveAsset( const sicnu::data::AssetId &assetId );` — validates the asset exists in the catalog
  - `sicnu::data::AssetId activeAssetId() const;`
  - `bool openPath( const QString &path )` — now registers via `DataManager` when bound and auto-sets `m_activeAssetId`
  - Unchanged: `getActiveLayerSummary()`, `activeLayer()`, `getCanvasViewportSummary()`, `pushMessageBarAlert(...)`, `setActiveViewHost(...)`, `activeViewHost()`.

**Design decision (deviates from spec wording, keeps contract):** `pushMessageBarAlert` keeps returning `false` when no view host is bound (honest failure; the daemon already tolerates a `failed` status). The existing test assertions at `tests/test_python_plugin_manager.cpp:422-424` therefore stay untouched.

- [ ] **Step 1: Write the failing test**

Add includes at the top of `tests/test_python_plugin_manager.cpp` (after line 21 `#include "project_context.h"`):

```cpp
#include "data/data_manager.h"
#include "data/asset_types.h"
```

Append this TEST_CASE immediately after the closing brace of the existing `[python][bridge]` case (after line 453):

```cpp
TEST_CASE( "AppInterfaceBridge headless asset seam via DataManager", "[python][bridge][headless]" )
{
  using namespace sicnu::python::isolated;

  sicnu::data::DataManager dataManager;
  AppInterfaceBridge bridge( &dataManager );

  const QString demPath = fixturePath( QStringLiteral( "samples/dem_sample.tif" ) );

  SECTION( "openPath registers the asset headlessly and auto-sets the active asset" )
  {
    REQUIRE( bridge.openPath( demPath ) );
    CHECK( !bridge.activeAssetId().isNull() );
    CHECK( dataManager.asset( bridge.activeAssetId() ).has_value() );

    const auto summary = bridge.getActiveLayerSummary();
    REQUIRE( summary.isValid );
    CHECK( summary.source == demPath );
    CHECK( summary.type == QStringLiteral( "raster" ) );
    CHECK( summary.toJsonObject()[QStringLiteral( "status" )].toString() == QStringLiteral( "ok" ) );
  }

  SECTION( "setActiveAsset validates against the catalog" )
  {
    CHECK( bridge.getActiveLayerSummary().toJsonObject()[QStringLiteral( "status" )].toString()
           == QStringLiteral( "no_active_layer" ) );

    CHECK_FALSE( bridge.setActiveAsset( sicnu::data::AssetId() ) );
    CHECK_FALSE( bridge.setActiveAsset( sicnu::data::AssetId::generate() ) );

    REQUIRE( bridge.openPath( demPath ) );
    const sicnu::data::AssetId registeredId = bridge.activeAssetId();
    CHECK( bridge.setActiveAsset( registeredId ) );
    CHECK( bridge.activeAssetId() == registeredId );
    CHECK( bridge.getActiveLayerSummary().isValid );
  }

  SECTION( "Null view host degrades canvas and message bar gracefully" )
  {
    const auto canvasSummary = bridge.getCanvasViewportSummary();
    CHECK( !canvasSummary.isValid );
    CHECK( canvasSummary.toJsonObject()[QStringLiteral( "status" )].toString()
           == QStringLiteral( "no_canvas" ) );
    CHECK( bridge.activeLayer() == nullptr );
    CHECK_FALSE( bridge.pushMessageBarAlert( QStringLiteral( "Title" ), QStringLiteral( "Message" ) ) );
  }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][bridge][headless]"`
Expected: BUILD FAILURE — `AppInterfaceBridge` has no constructor taking `DataManager*`, and no `activeAssetId()`/`setActiveAsset()` members.

- [ ] **Step 3: Rewrite the bridge header**

Replace the entire content of `src/python/isolated/app_interface_bridge.h` with:

```cpp
// src/python/isolated/app_interface_bridge.h
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "data/asset_types.h"
#include "data/data_result.h"

class ActiveViewHost;
class QgsMapLayer;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::python::isolated
{

struct ActiveLayerSummary
{
  bool isValid = false;
  QString name;
  QString source;
  QString type; // "raster" or "vector"
  QString crs;

  QJsonObject toJsonObject() const;
};

struct CanvasViewportSummary
{
  bool isValid = false;
  double xMin = 0.0;
  double yMin = 0.0;
  double xMax = 0.0;
  double yMax = 0.0;
  double scale = 1.0;

  QJsonObject toJsonObject() const;
};

/**
 * AppInterfaceBridge — JSON-RPC serialization bridge module.
 *
 * Headless asset seam: `DataManager` is the required asset authority
 * (catalog queries, source registration, active-asset tracking), while
 * `ActiveViewHost` is an optional display/canvas/message-bar enhancement
 * bound only in GUI mode. Serves as the dedicated JSON-RPC IPC presentation
 * & state serialization layer consumed by PythonAppInterfaceProxy for
 * out-of-process Python plugin workers (ADR 0014/0015).
 */
class AppInterfaceBridge : public QObject
{
  Q_OBJECT

  public:
    explicit AppInterfaceBridge( sicnu::data::DataManager *dataManager = nullptr,
                                 ActiveViewHost *activeViewHost = nullptr,
                                 QObject *parent = nullptr );
    ~AppInterfaceBridge() override = default;

    void setDataManager( sicnu::data::DataManager *dataManager );
    sicnu::data::DataManager *dataManager() const;

    void setActiveViewHost( ActiveViewHost *host );
    ActiveViewHost *activeViewHost() const;

    ActiveLayerSummary getActiveLayerSummary() const;
    QgsMapLayer *activeLayer() const;

    bool openPath( const QString &path );

    /// Plugin-driven "active layer": validates the asset exists in the
    /// catalog, then makes it the active asset. Returns false otherwise.
    bool setActiveAsset( const sicnu::data::AssetId &assetId );
    sicnu::data::AssetId activeAssetId() const;

    CanvasViewportSummary getCanvasViewportSummary() const;

    bool pushMessageBarAlert( const QString &title, const QString &text, int level = 0 );

  private:
    sicnu::data::DataManager *m_dataManager = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
    sicnu::data::AssetId m_activeAssetId;
};

} // namespace sicnu::python::isolated
```

- [ ] **Step 4: Rewrite the bridge implementation**

Replace the entire content of `src/python/isolated/app_interface_bridge.cpp` with:

```cpp
// src/python/isolated/app_interface_bridge.cpp
#include "app_interface_bridge.h"
#include "active_view_host.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

#include <qgsmaplayer.h>

#include <QJsonArray>

#include <optional>
#include <variant>

namespace sicnu::python::isolated
{

QJsonObject ActiveLayerSummary::toJsonObject() const
{
  QJsonObject res;
  if ( isValid )
  {
    res[QStringLiteral( "name" )] = name;
    res[QStringLiteral( "source" )] = source;
    res[QStringLiteral( "type" )] = type;
    res[QStringLiteral( "crs" )] = crs;
    res[QStringLiteral( "status" )] = QStringLiteral( "ok" );
  }
  else
  {
    res[QStringLiteral( "status" )] = QStringLiteral( "no_active_layer" );
  }
  return res;
}

QJsonObject CanvasViewportSummary::toJsonObject() const
{
  QJsonObject res;
  if ( isValid )
  {
    QJsonArray extentArr;
    extentArr.append( xMin );
    extentArr.append( yMin );
    extentArr.append( xMax );
    extentArr.append( yMax );

    res[QStringLiteral( "extent" )] = extentArr;
    res[QStringLiteral( "scale" )] = scale;
    res[QStringLiteral( "status" )] = QStringLiteral( "ok" );
  }
  else
  {
    res[QStringLiteral( "status" )] = QStringLiteral( "no_canvas" );
  }
  return res;
}

AppInterfaceBridge::AppInterfaceBridge( sicnu::data::DataManager *dataManager, ActiveViewHost *activeViewHost, QObject *parent )
  : QObject( parent )
  , m_dataManager( dataManager )
  , m_activeViewHost( activeViewHost )
{
}

void AppInterfaceBridge::setDataManager( sicnu::data::DataManager *dataManager )
{
  m_dataManager = dataManager;
}

sicnu::data::DataManager *AppInterfaceBridge::dataManager() const
{
  return m_dataManager;
}

void AppInterfaceBridge::setActiveViewHost( ActiveViewHost *host )
{
  m_activeViewHost = host;
}

ActiveViewHost *AppInterfaceBridge::activeViewHost() const
{
  return m_activeViewHost;
}

ActiveLayerSummary AppInterfaceBridge::getActiveLayerSummary() const
{
  ActiveLayerSummary summary;
  if ( !m_dataManager || m_activeAssetId.isNull() )
  {
    return summary;
  }

  const std::optional<sicnu::data::AssetSnapshot> asset = m_dataManager->asset( m_activeAssetId );
  if ( !asset )
  {
    return summary;
  }

  summary.isValid = true;
  summary.name = asset->displayName();
  summary.source = asset->source().canonicalSource;
  const bool isRaster = asset->kind() == sicnu::data::AssetKind::Raster ||
                        asset->kind() == sicnu::data::AssetKind::VirtualRaster;
  summary.type = isRaster ? QStringLiteral( "raster" ) : QStringLiteral( "vector" );

  // CRS is reported as WKT from the probed structure (previously authid from
  // the QgsMapLayer); the Python side treats it as an opaque string.
  if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &asset->structure() ) )
  {
    summary.crs = raster->crsWkt;
  }
  else if ( const auto *vector = std::get_if<sicnu::data::VectorStructure>( &asset->structure() ) )
  {
    if ( !vector->layers.isEmpty() )
    {
      summary.crs = vector->layers.first().crsWkt;
    }
  }
  return summary;
}

QgsMapLayer *AppInterfaceBridge::activeLayer() const
{
  return m_activeViewHost ? m_activeViewHost->activeLayer() : nullptr;
}

bool AppInterfaceBridge::openPath( const QString &path )
{
  if ( path.isEmpty() )
  {
    return false;
  }

  // Asset authority (headless-safe): register through the Data Manager and
  // auto-set the freshly added asset as the plugin-visible active one.
  if ( m_dataManager )
  {
    sicnu::data::SourceDescriptor source;
    source.canonicalSource = path;
    const sicnu::data::RegisterResult registered =
      m_dataManager->registerSource( sicnu::data::RegisterRequest{ std::move( source ) } );
    if ( registered.assetId.isNull() )
    {
      return false;
    }
    m_activeAssetId = registered.assetId;
  }
  else if ( !m_activeViewHost )
  {
    return false;
  }

  // Optional display enhancement: route through the view host when bound.
  // Registration via the view host dedups against the same SourceKey.
  if ( m_activeViewHost )
  {
    const auto displayed = m_activeViewHost->openPath( path );
    if ( !m_dataManager )
    {
      return static_cast<bool>( displayed );
    }
  }
  return true;
}

bool AppInterfaceBridge::setActiveAsset( const sicnu::data::AssetId &assetId )
{
  if ( !m_dataManager || assetId.isNull() || !m_dataManager->asset( assetId ) )
  {
    return false;
  }
  m_activeAssetId = assetId;
  return true;
}

sicnu::data::AssetId AppInterfaceBridge::activeAssetId() const
{
  return m_activeAssetId;
}

CanvasViewportSummary AppInterfaceBridge::getCanvasViewportSummary() const
{
  CanvasViewportSummary summary;
  if ( !m_activeViewHost || !m_activeViewHost->mapCanvas() )
  {
    return summary;
  }

  QgsRectangle extent = m_activeViewHost->mapCanvasExtent();
  summary.isValid = true;
  summary.xMin = extent.xMinimum();
  summary.yMin = extent.yMinimum();
  summary.xMax = extent.xMaximum();
  summary.yMax = extent.yMaximum();
  summary.scale = m_activeViewHost->mapCanvasScale();
  return summary;
}

bool AppInterfaceBridge::pushMessageBarAlert( const QString &title, const QString &text, int level )
{
  if ( !m_activeViewHost )
  {
    return false;
  }
  // Map integer levels onto Qgis::MessageLevel (Info/Warning/Critical/Success/…).
  Qgis::MessageLevel qLevel = Qgis::MessageLevel::Info;
  switch ( level )
  {
    case static_cast<int>( Qgis::MessageLevel::Warning ):
      qLevel = Qgis::MessageLevel::Warning;
      break;
    case static_cast<int>( Qgis::MessageLevel::Critical ):
      qLevel = Qgis::MessageLevel::Critical;
      break;
    case static_cast<int>( Qgis::MessageLevel::Success ):
      qLevel = Qgis::MessageLevel::Success;
      break;
    default:
      qLevel = Qgis::MessageLevel::Info;
      break;
  }
  m_activeViewHost->pushMessageBarAlert( title, text, qLevel );
  return true;
}

} // namespace sicnu::python::isolated
```

- [ ] **Step 5: Run the new test and the full bridge suite**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][bridge]"`
Expected: PASS — the new `[python][bridge][headless]` case (3 sections) AND the pre-existing `[python][bridge]` case (3 sections; its `AppInterfaceBridge bridge( nullptr )` construction at line 406 now binds a null `DataManager` and its assertions still hold).

- [ ] **Step 6: Commit**

```bash
git add src/python/isolated/app_interface_bridge.h src/python/isolated/app_interface_bridge.cpp tests/test_python_plugin_manager.cpp
git commit -m "feat(python): rebind AppInterfaceBridge to DataManager headless asset seam

DataManager becomes the required asset authority (registration, catalog
queries, explicit active-asset tracking); ActiveViewHost demotes to an
optional display/canvas/message-bar enhancement. getActiveLayerSummary
now resolves the plugin-driven activeAssetId instead of the canvas
current layer, giving identical semantics in GUI and headless modes."
```

---

### Task 2: Adapt `PythonAppInterfaceProxy` (new ctor, `catalog.set_active_layer`, headless UI degradation)

**Files:**
- Modify: `src/python/isolated/python_app_interface_proxy.h:22`
- Modify: `src/python/isolated/python_app_interface_proxy.cpp:16-26,52-91`
- Test: `tests/test_python_plugin_manager.cpp:182,347` (ctor call sites) + new TEST_CASE

**Interfaces:**
- Consumes: Task 1's `AppInterfaceBridge( DataManager*, ActiveViewHost*, QObject* )`, `setActiveAsset(const AssetId&)`, `activeAssetId()`; `sicnu::data::AssetId::fromString(const QString&) → std::optional<AssetId>`.
- Produces:
  - `PythonAppInterfaceProxy( PythonIpcServer *ipcServer, sicnu::data::DataManager *dataManager = nullptr, QMenu *parentMenu = nullptr, ActiveViewHost *activeViewHost = nullptr, QObject *parent = nullptr )`
  - New IPC method `catalog.set_active_layer` with params `{ "asset_id": "<uuid string>" }`; response `{ "status": "ok" | "unknown_asset", "asset_id": "<echo>" }`.
  - `ui.add_plugin_menu` with no parent menu responds `{ "status": "ui_unavailable", "callback_id": "<echo>" }` and registers no `QAction`.

- [ ] **Step 1: Update the two existing ctor call sites in tests**

In `tests/test_python_plugin_manager.cpp`:

Line 182 (in `[python][isolated][ui]`):

```cpp
  PythonAppInterfaceProxy uiProxy( &server, nullptr, &parentMenu );
```

Line 347 (in `[python][isolated][api]`):

```cpp
  PythonAppInterfaceProxy uiProxy( &server, nullptr, &parentMenu, &activeViewHost );
```

- [ ] **Step 2: Write the failing headless proxy test**

Append after the `[python][bridge][headless]` case added in Task 1:

```cpp
TEST_CASE( "PythonAppInterfaceProxy serves the asset IPC chain without any QWidget", "[python][isolated][api][headless]" )
{
  using namespace sicnu::python::isolated;

  PythonIpcServer server;
  sicnu::data::DataManager dataManager;
  PythonAppInterfaceProxy proxy( &server, &dataManager );

  const QString demPath = fixturePath( QStringLiteral( "samples/dem_sample.tif" ) );

  // data.add_layer registers through the Data Manager and auto-sets active.
  QJsonObject addMsg;
  addMsg[QStringLiteral( "method" )] = QStringLiteral( "data.add_layer" );
  addMsg[QStringLiteral( "id" )] = 201;
  QJsonObject addParams;
  addParams[QStringLiteral( "path" )] = demPath;
  addMsg[QStringLiteral( "params" )] = addParams;
  proxy.handleIpcMessage( addMsg );

  const sicnu::data::AssetId addedId = proxy.bridge().activeAssetId();
  REQUIRE( !addedId.isNull() );

  // catalog.get_active_layer resolves the active asset from the catalog.
  QJsonObject getMsg;
  getMsg[QStringLiteral( "method" )] = QStringLiteral( "catalog.get_active_layer" );
  getMsg[QStringLiteral( "id" )] = 202;
  proxy.handleIpcMessage( getMsg );
  const auto summary = proxy.bridge().getActiveLayerSummary();
  REQUIRE( summary.isValid );
  CHECK( summary.source == demPath );

  // catalog.set_active_layer with an unknown id fails cleanly and changes nothing.
  QJsonObject setMsg;
  setMsg[QStringLiteral( "method" )] = QStringLiteral( "catalog.set_active_layer" );
  setMsg[QStringLiteral( "id" )] = 203;
  QJsonObject setParams;
  setParams[QStringLiteral( "asset_id" )] = sicnu::data::AssetId::generate().toString();
  setMsg[QStringLiteral( "params" )] = setParams;
  proxy.handleIpcMessage( setMsg );
  CHECK( proxy.bridge().activeAssetId() == addedId );

  // catalog.set_active_layer with the registered id succeeds.
  setParams[QStringLiteral( "asset_id" )] = addedId.toString();
  setMsg[QStringLiteral( "params" )] = setParams;
  setMsg[QStringLiteral( "id" )] = 204;
  proxy.handleIpcMessage( setMsg );
  CHECK( proxy.bridge().activeAssetId() == addedId );

  // ui.add_plugin_menu with no menu host degrades without registering an action.
  QJsonObject menuMsg;
  menuMsg[QStringLiteral( "method" )] = QStringLiteral( "ui.add_plugin_menu" );
  menuMsg[QStringLiteral( "id" )] = 205;
  QJsonObject menuParams;
  menuParams[QStringLiteral( "menu_title" )] = QStringLiteral( "Plugins" );
  menuParams[QStringLiteral( "action_title" )] = QStringLiteral( "Headless Action" );
  menuParams[QStringLiteral( "callback_id" )] = QStringLiteral( "cb_headless_001" );
  menuMsg[QStringLiteral( "params" )] = menuParams;
  proxy.handleIpcMessage( menuMsg );
  CHECK( proxy.registeredActionCount() == 0 );
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][isolated][api][headless]"`
Expected: BUILD FAILURE — `PythonAppInterfaceProxy` constructor does not accept `DataManager*` as second argument.

- [ ] **Step 4: Update the proxy header**

In `src/python/isolated/python_app_interface_proxy.h`, add the forward declaration after line 12 (`class ActiveViewHost;`):

```cpp
namespace sicnu::data
{
class DataManager;
}
```

Replace the constructor declaration (line 22) with:

```cpp
    explicit PythonAppInterfaceProxy( PythonIpcServer *ipcServer,
                                      sicnu::data::DataManager *dataManager = nullptr,
                                      QMenu *parentMenu = nullptr,
                                      ActiveViewHost *activeViewHost = nullptr,
                                      QObject *parent = nullptr );
```

- [ ] **Step 5: Update the proxy implementation**

In `src/python/isolated/python_app_interface_proxy.cpp`:

Add include after line 2 (`#include "active_view_host.h"`):

```cpp
#include "data/asset_types.h"
#include "data/data_manager.h"
```

Replace the constructor (lines 16-26) with:

```cpp
PythonAppInterfaceProxy::PythonAppInterfaceProxy( PythonIpcServer *ipcServer, sicnu::data::DataManager *dataManager, QMenu *parentMenu, ActiveViewHost *activeViewHost, QObject *parent )
  : QObject( parent )
  , m_ipcServer( ipcServer )
  , m_parentMenu( parentMenu )
  , m_bridge( dataManager, activeViewHost, this )
{
  if ( m_ipcServer )
  {
    connect( m_ipcServer, &PythonIpcServer::messageReceived, this, &PythonAppInterfaceProxy::handleIpcMessage );
  }
}
```

Replace the `ui.add_plugin_menu` branch (lines 52-83) with:

```cpp
  if ( method == QStringLiteral( "ui.add_plugin_menu" ) )
  {
    QString menuTitle = params[QStringLiteral( "menu_title" )].toString();
    QString actionTitle = params[QStringLiteral( "action_title" )].toString();
    QString callbackId = params[QStringLiteral( "callback_id" )].toString();

    if ( !m_parentMenu )
    {
      // Headless mode: no menu host — report ui_unavailable instead of
      // registering a dead QAction.
      if ( m_ipcServer && msgId > 0 )
      {
        QJsonObject res;
        res[QStringLiteral( "status" )] = QStringLiteral( "ui_unavailable" );
        res[QStringLiteral( "callback_id" )] = callbackId;
        m_ipcServer->sendResponse( msgId, res );
      }
      return;
    }

    auto *action = new QAction( actionTitle, this );
    m_registeredActions[callbackId] = action;
    m_parentMenu->addAction( action );

    connect( action, &QAction::triggered, this, [this, callbackId]() {
      emit actionTriggered( callbackId );
      if ( m_ipcServer )
      {
        QJsonObject triggerParams;
        triggerParams[QStringLiteral( "callback_id" )] = callbackId;
        m_ipcServer->sendRequest( QStringLiteral( "ui.on_action_triggered" ), triggerParams );
      }
    } );

    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = QStringLiteral( "registered" );
      res[QStringLiteral( "callback_id" )] = callbackId;
      m_ipcServer->sendResponse( msgId, res );
    }
  }
```

Add the new `catalog.set_active_layer` branch immediately after the `catalog.get_active_layer` branch (after line 91):

```cpp
  else if ( method == QStringLiteral( "catalog.set_active_layer" ) )
  {
    const QString assetIdText = params[QStringLiteral( "asset_id" )].toString();
    const std::optional<sicnu::data::AssetId> assetId = sicnu::data::AssetId::fromString( assetIdText );
    const bool ok = assetId.has_value() && m_bridge.setActiveAsset( *assetId );
    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = ok ? QStringLiteral( "ok" ) : QStringLiteral( "unknown_asset" );
      res[QStringLiteral( "asset_id" )] = assetIdText;
      m_ipcServer->sendResponse( msgId, res );
    }
  }
```

- [ ] **Step 6: Run headless + existing proxy tests**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][isolated][api],[python][isolated][api][headless],[python][isolated][ui],[python][bridge]"`
Expected: PASS for all — including the pre-existing `[python][isolated][ui]` (menu bound → `registered` path unchanged) and `[python][isolated][api]` (assertions unchanged).

- [ ] **Step 7: Commit**

```bash
git add src/python/isolated/python_app_interface_proxy.h src/python/isolated/python_app_interface_proxy.cpp tests/test_python_plugin_manager.cpp
git commit -m "feat(python): adapt PythonAppInterfaceProxy to headless asset seam

Constructor takes DataManager* (asset authority) with QMenu*/ActiveViewHost*
demoted to optional GUI enhancements. Adds additive catalog.set_active_layer
IPC method; ui.add_plugin_menu degrades to ui_unavailable when no menu host
is bound."
```

---

### Task 3: Inject `DataManager` in `PythonPluginAdapter::initialize`

**Files:**
- Modify: `src/app/python/python_plugin_adapter.cpp:89-95`
- Test: `tests/test_python_plugin_manager.cpp` (existing `[python][adapter][isolated]` case at line 296 — no edits, regression only)

**Interfaces:**
- Consumes: Task 2's proxy ctor `PythonAppInterfaceProxy( PythonIpcServer*, sicnu::data::DataManager*, QMenu*, ActiveViewHost*, QObject* )`; `SicnuAppInterface::projectContext() → sicnu::app::ProjectContext*` (already exists, `src/app/python/sicnu_app_interface.h:48`); `ProjectContext::dataManager() → sicnu::data::DataManager&` (used at `tests/test_python_plugin_manager.cpp:471`).
- Produces: adapter builds the proxy with the asset seam whenever the app interface carries a `ProjectContext`; behavior without an interface (current production state) unchanged.

- [ ] **Step 1: Update the adapter**

In `src/app/python/python_plugin_adapter.cpp`, add include after line 3 (`#include "sicnu_app_interface.h"`):

```cpp
#include "project_context.h"
```

Replace lines 89-95 with:

```cpp
    // Attach UI RPC Proxy Facade (headless asset seam: DataManager is the
    // asset authority; menu and view host remain optional GUI enhancements).
    QMenu *pluginMenu = m_appInterface ? m_appInterface->pluginMenu() : nullptr;
    sicnu::data::DataManager *dataManager = nullptr;
    if ( m_appInterface && m_appInterface->projectContext() )
    {
        dataManager = &m_appInterface->projectContext()->dataManager();
    }
    m_uiProxy = std::make_unique<PythonAppInterfaceProxy>( m_workerNode->server, dataManager, pluginMenu );
    if ( m_appInterface && m_appInterface->activeViewHost() )
    {
        m_uiProxy->setActiveViewHost( m_appInterface->activeViewHost() );
    }
```

- [ ] **Step 2: Build and run the adapter + full python test suite**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS — all cases including `[python][adapter][isolated]` (constructed with a null-ProjectContext interface → null `DataManager`, same degradation as before) and the subprocess cases (`[python][isolated]`, `[python][isolated][fault]`).

- [ ] **Step 3: Commit**

```bash
git add src/app/python/python_plugin_adapter.cpp
git commit -m "feat(python): inject DataManager asset seam into plugin adapter proxy

PythonPluginAdapter::initialize sources DataManager from the app interface's
ProjectContext so out-of-process plugins get working catalog/asset IPC
methods; absent an interface, degradation is unchanged."
```

---

### Task 4: Glossary update + follow-up tickets + full verification

**Files:**
- Modify: `CONTEXT.md:117-125` (Python Plugin Infrastructure section)
- Create: `.scratch/headless_asset_seam/issues/01-wire-app-interface-production.md`
- Create: `.scratch/headless_asset_seam/issues/02-implement-execute-algorithm-daemon.md`

**Interfaces:**
- Consumes: Tasks 1-3 (final API shapes for accurate glossary wording).
- Produces: documentation only.

- [ ] **Step 1: Update CONTEXT.md glossary**

In `CONTEXT.md`, insert a new entry into the `## Python Plugin Infrastructure` section immediately after the **Application Interface Facade (`iface`)** entry (after line 125):

```markdown
**Headless Asset Seam (`AppInterfaceBridge`)**:
The JSON-RPC serialization bridge (`AppInterfaceBridge` in `src/python/isolated`) consumed by `PythonAppInterfaceProxy` for out-of-process Python plugin workers. `DataManager` is its required asset authority — catalog queries, source registration (`openPath`), and the explicit plugin-driven active asset (`setActiveAsset`/`activeAssetId`, replacing the canvas current layer) — while `ActiveViewHost` is an optional enhancement bound only in GUI mode for display, canvas state, and message bar. IPC methods degrade gracefully without a view host (`no_canvas`, `no_active_layer`, `ui_unavailable`), so the seam works without any QWidget.
_Avoid_: GUI proxy, QgisInterface IPC shim
```

- [ ] **Step 2: Write follow-up ticket 1**

Create `.scratch/headless_asset_seam/issues/01-wire-app-interface-production.md`:

```markdown
# Ticket 01: Wire `SicnuAppInterface` in production

**Status:** pending
**Priority:** high
**Discovered:** 2026-08-01 headless asset seam exploration

## Problem

`src/gui/main_window.cpp` (~line 273) constructs `PluginManager(m_mapCanvas, m_layerTree, this)` but never calls `PluginManager::setAppInterface()`. Only tests wire it. Consequences in the shipped app:

- `PythonPluginAdapter::initialize` builds the proxy with a null menu and null `ActiveViewHost`, so `ui.add_plugin_menu` always degrades to `ui_unavailable` and canvas/message-bar IPC methods are dead.
- Without a `ProjectContext` on the interface, the new headless asset seam also receives a null `DataManager`, so `data.add_layer` / `catalog.*` fail too.

## Scope

- Instantiate `SicnuAppInterface` with the real main window, `ActiveViewHost`, and `ProjectContext` in `SicnuMainWindow::loadPlugins()` (or its caller) and pass it to `PluginManager::setAppInterface()`.
- Verify a sample Python plugin's menu action appears and `catalog.get_active_layer` works in the running GUI app.

## Out of scope

- `processing.execute_algorithm` daemon stub (ticket 02).
```

- [ ] **Step 3: Write follow-up ticket 2**

Create `.scratch/headless_asset_seam/issues/02-implement-execute-algorithm-daemon.md`:

```markdown
# Ticket 02: Implement `processing.execute_algorithm` in `worker_daemon.py`

**Status:** pending
**Priority:** high
**Discovered:** 2026-08-01 headless asset seam exploration

## Problem

Algorithms registered via `processing.register_algorithm` produce a `PythonAlgorithmAdapter` whose execute lambda sends `processing.execute_algorithm` over IPC. `src/python/scripts/worker_daemon.py` (~line 245) answers with JSON-RPC `-32601 Method not found`, and the adapter's lambda ignores the response and reports success with progress 1.0. Registered Python algorithms therefore "succeed" without ever executing. `tests/test_python_plugin_manager.cpp:155-166` currently asserts this stub behavior and must be updated when the ticket lands.

## Scope

- Implement `processing.execute_algorithm` in `worker_daemon.py`: dispatch to the registered plugin algorithm's execution entry point, report progress, return structured results/errors.
- Make `PythonAlgorithmAdapter`'s execute lambda await the response (request/response correlation) and propagate real failure instead of unconditional success.
- Update the `[python][isolated]` ping/pong test's `-32601` assertion for `processing.execute_algorithm`.

## Out of scope

- Algorithm progress streaming UI.
```

- [ ] **Step 4: Run the full test suite for the touched area**

Run: `./build/tests/test_python_plugin_manager` (no tag filter — entire file including subprocess/fault cases)
Expected: PASS, all assertions.

- [ ] **Step 5: Commit**

```bash
git add CONTEXT.md .scratch/headless_asset_seam/
git commit -m "docs: glossary entry for headless asset seam + follow-up tickets

Records the AppInterfaceBridge headless asset seam in CONTEXT.md and files
tickets for the two defects found during exploration: missing production
SicnuAppInterface wiring and the unimplemented processing.execute_algorithm
daemon method."
```

---

## Self-Review Notes (completed by plan author)

- **Spec coverage:** Bridge rebind (Task 1), proxy adaptation + `catalog.set_active_layer` + `ui_unavailable` (Task 2), adapter injection (Task 3), `SicnuAppInterface` accessor (none needed — `projectContext()` already exists at `sicnu_app_interface.h:48`), testing decisions 1-6 (Tasks 1-2 new cases + unchanged existing cases), follow-up tickets + CONTEXT.md (Task 4). Behavior-change rationale recorded in spec §5.
- **Known serialization change:** `ActiveLayerSummary.crs` now carries WKT from the probed asset structure instead of a `authid` from `QgsMapLayer`; Python side treats it as opaque. Documented inline in the bridge implementation.
- **Deviation from spec wording:** `pushMessageBarAlert` keeps returning `false` (not silent success) with no view host, preserving existing test assertions at `tests/test_python_plugin_manager.cpp:422-424`; daemon contract unaffected.
- **Type consistency:** `setActiveAsset(const sicnu::data::AssetId&)` / `activeAssetId() → sicnu::data::AssetId` used identically in Tasks 1, 2 and their tests; proxy ctor parameter order `(ipcServer, dataManager, parentMenu, activeViewHost, parent)` consistent between header, implementation, adapter, and test call sites.
