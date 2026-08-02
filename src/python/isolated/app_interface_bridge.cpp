// src/python/isolated/app_interface_bridge.cpp
#include "app_interface_bridge.h"
#include "active_view_host.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "python_ipc_server.h"
#include "processing/framework/python_algorithm_adapter.h"
#include "processing/framework/python_processing_provider_adapter.h"
#include "processing/framework/algorithm_engine.h"

#include <qgsmaplayer.h>

#include <QJsonArray>
#include <QHash>

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

AppInterfaceBridge::AppInterfaceBridge( sicnu::data::DataManager *dataManager, ActiveViewHost *activeViewHost, QMenu *parentMenu, QObject *parent )
  : QObject( parent )
  , m_dataManager( dataManager )
  , m_activeViewHost( activeViewHost )
  , m_parentMenu( parentMenu )
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

void AppInterfaceBridge::setParentMenu( QMenu *parentMenu )
{
  m_parentMenu = parentMenu;
}

QMenu *AppInterfaceBridge::parentMenu() const
{
  return m_parentMenu;
}

void AppInterfaceBridge::bindIpcServer( PythonIpcServer *ipcServer )
{
  m_ipcServer = ipcServer;
  if ( m_ipcServer )
  {
    connect( m_ipcServer, &PythonIpcServer::messageReceived, this, &AppInterfaceBridge::handleIpcMessage );
    setupDefaultAlgorithmHandler();
  }
}

PythonIpcServer *AppInterfaceBridge::ipcServer() const
{
  return m_ipcServer;
}

int AppInterfaceBridge::registeredActionCount() const
{
  return m_registeredActions.size();
}

void AppInterfaceBridge::setupDefaultAlgorithmHandler()
{
  setAlgorithmRegisterHandler( [this]( const QString &algoId, const QString &name, const QString &group, const QString &desc ) -> bool {
    sicnu::AlgorithmDescriptor algoDesc;
    algoDesc.id = algoId;
    algoDesc.name = name.isEmpty() ? algoId : name;
    algoDesc.group = group.isEmpty() ? QStringLiteral( "Python Plugins" ) : group;
    algoDesc.description = desc;
    algoDesc.resourceProfile = sicnu::ProviderResourceProfile::PythonWorkerProcess;

    auto adapter = std::make_shared<sicnu::PythonAlgorithmAdapter>(
      algoDesc,
      [this, algoId]( const QVariantMap &execParams, std::function<void(double)> progress, QString &err ) -> bool {
        if ( !m_ipcServer )
        {
          err = QStringLiteral( "IPC Server not available" );
          return false;
        }
        QJsonObject req;
        req[QStringLiteral( "id" )] = algoId;
        req[QStringLiteral( "params" )] = QJsonObject::fromVariantMap( execParams );

        QJsonObject execResult;
        bool execIsError = false;
        const AwaitStatus awaitStatus = m_ipcServer->sendRequestAndAwait(
          QStringLiteral( "processing.execute_algorithm" ), req, execResult, execIsError, 300000 );
        switch ( awaitStatus )
        {
          case AwaitStatus::NoClient:
            err = QStringLiteral( "IPC client not connected" );
            return false;
          case AwaitStatus::Disconnected:
            err = QStringLiteral( "Python worker disconnected during algorithm execution" );
            return false;
          case AwaitStatus::Timeout:
            err = QStringLiteral( "Python algorithm execution timed out" );
            return false;
          case AwaitStatus::Ok:
            break;
        }
        if ( execIsError )
        {
          err = execResult[QStringLiteral( "message" )].toString( QStringLiteral( "Python algorithm execution failed" ) );
          return false;
        }
        if ( execResult[QStringLiteral( "status" )].toString() != QStringLiteral( "ok" ) )
        {
          err = QStringLiteral( "Python algorithm execution failed" );
          return false;
        }
        if ( progress ) progress( 1.0 );
        return true;
      }
    );

    auto providers = sicnu::AlgorithmEngine::instance().registeredProviders();
    std::shared_ptr<sicnu::PythonProcessingProviderAdapter> pythonProvider;
    for ( const auto &provider : providers )
    {
      if ( provider && provider->providerId() == QStringLiteral( "python_plugins" ) )
      {
        pythonProvider = std::dynamic_pointer_cast<sicnu::PythonProcessingProviderAdapter>( provider );
        break;
      }
    }

    if ( pythonProvider )
    {
      pythonProvider->addAlgorithm( adapter );
    }
    else
    {
      sicnu::AlgorithmEngine::instance().registerAlgorithm( adapter );
    }
    return true;
  } );
}

void AppInterfaceBridge::handleIpcMessage( const QJsonObject &message )
{
  if ( !message.contains( QStringLiteral( "method" ) ) )
    return;

  QString method = message[QStringLiteral( "method" )].toString();
  int msgId = message.value( QStringLiteral( "id" ) ).toInt( 0 );
  QJsonObject params = message[QStringLiteral( "params" )].toObject();

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
    return;
  }

  QJsonObject response;
  if ( dispatchIpcMessage( message, response ) )
  {
    if ( m_ipcServer && msgId > 0 )
    {
      m_ipcServer->sendResponse( msgId, response );
    }
    return;
  }
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

bool AppInterfaceBridge::dispatchIpcMessage( const QJsonObject &message, QJsonObject &response )
{
  if ( !message.contains( QStringLiteral( "method" ) ) )
    return false;

  const QString method = message[QStringLiteral( "method" )].toString();
  const QJsonObject params = message[QStringLiteral( "params" )].toObject();

  using HandlerFn = std::function<void( const QJsonObject &p, QJsonObject &r )>;
  const QHash<QString, HandlerFn> handlers = {
    { QStringLiteral( "catalog.get_active_layer" ), [this]( const QJsonObject &, QJsonObject &r ) {
        r = getActiveLayerSummary().toJsonObject();
      } },
    { QStringLiteral( "catalog.set_active_layer" ), [this]( const QJsonObject &p, QJsonObject &r ) {
        const QString assetIdText = p[QStringLiteral( "asset_id" )].toString();
        const std::optional<sicnu::data::AssetId> assetId = sicnu::data::AssetId::fromString( assetIdText );
        const bool ok = assetId.has_value() && setActiveAsset( *assetId );
        r[QStringLiteral( "status" )] = ok ? QStringLiteral( "ok" ) : QStringLiteral( "unknown_asset" );
        r[QStringLiteral( "asset_id" )] = assetIdText;
      } },
    { QStringLiteral( "data.add_layer" ), [this]( const QJsonObject &p, QJsonObject &r ) {
        const QString path = p[QStringLiteral( "path" )].toString();
        const bool ok = openPath( path );
        r[QStringLiteral( "status" )] = ok ? QStringLiteral( "added" ) : QStringLiteral( "failed" );
        r[QStringLiteral( "path" )] = path;
      } },
    { QStringLiteral( "canvas.get_state" ), [this]( const QJsonObject &, QJsonObject &r ) {
        r = getCanvasViewportSummary().toJsonObject();
      } },
    { QStringLiteral( "ui.push_message_bar" ), [this]( const QJsonObject &p, QJsonObject &r ) {
        const QString title = p[QStringLiteral( "title" )].toString();
        const QString text = p[QStringLiteral( "text" )].toString();
        int level = 0;
        const QJsonValue levelVal = p.value( QStringLiteral( "level" ) );
        if ( levelVal.isString() )
        {
          const QString levelStr = levelVal.toString().toLower();
          if ( levelStr == QStringLiteral( "warning" ) || levelStr == QStringLiteral( "warn" ) )
            level = static_cast<int>( Qgis::MessageLevel::Warning );
          else if ( levelStr == QStringLiteral( "critical" ) || levelStr == QStringLiteral( "error" ) )
            level = static_cast<int>( Qgis::MessageLevel::Critical );
          else if ( levelStr == QStringLiteral( "success" ) )
            level = static_cast<int>( Qgis::MessageLevel::Success );
          else
            level = static_cast<int>( Qgis::MessageLevel::Info );
        }
        else if ( levelVal.isDouble() )
        {
          level = levelVal.toInt();
        }
        const bool ok = pushMessageBarAlert( title, text, level );
        r[QStringLiteral( "status" )] = ok ? QStringLiteral( "pushed" ) : QStringLiteral( "failed" );
      } },
    { QStringLiteral( "processing.register_algorithm" ), [this]( const QJsonObject &p, QJsonObject &r ) {
        const QString algoId = p[QStringLiteral( "id" )].toString();
        const QString name = p[QStringLiteral( "name" )].toString();
        const QString group = p[QStringLiteral( "group" )].toString();
        const QString desc = p[QStringLiteral( "description" )].toString();

        bool registered = false;
        if ( m_algoRegisterHandler )
        {
          registered = m_algoRegisterHandler( algoId, name, group, desc );
        }
        r[QStringLiteral( "status" )] = registered ? QStringLiteral( "registered" ) : QStringLiteral( "failed" );
        r[QStringLiteral( "id" )] = algoId;
      } }
  };

  const auto it = handlers.constFind( method );
  if ( it != handlers.constEnd() )
  {
    it.value()( params, response );
    return true;
  }

  return false;
}

} // namespace sicnu::python::isolated
