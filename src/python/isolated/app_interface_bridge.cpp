// src/python/isolated/app_interface_bridge.cpp
#include "app_interface_bridge.h"
#include "active_view_host.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "python_ipc_server.h"
#include "shared_memory_segment.h"
#include "processing/framework/python_algorithm_adapter.h"
#include "processing/framework/python_processing_provider_adapter.h"
#include "processing/framework/algorithm_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/json_params_converter.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <qgsmaplayer.h>

#include <QJsonArray>
#include <QHash>

#include <cstring>
#include <optional>
#include <variant>
#include <vector>

namespace sicnu::python::isolated
{
namespace {

/// ADR 0064 opt-in zero-copy delivery. When a caller sets
/// `__shm_key__: true` on the request (the opt-in flag) alongside a raster
/// file-path input, migrate that raster into a shared-memory segment so the
/// Python worker receives it as a zero-copy numpy array (__shm_array__)
/// instead of opening the file itself. Returns the segment (kept alive for
/// the duration of the RPC) or nullptr if migration did not apply / failed
/// (on failure the path is left untouched so the plugin falls back to GDAL).
///
/// The raster's NATIVE dtype is preserved for the common integer and float
/// types (Byte->UInt8, UInt16->UInt16, Int32->Int32, Float32->Float32); any
/// other type (Float64, Int16, ...) falls back to the float32 conversion
/// path. The migrated metadata (`__shm_key__` + width/height/bands/dtype)
/// overwrites the opt-in flag in \a params so the daemon mounts exactly one
/// array.
std::unique_ptr<SharedMemorySegment> migrateRasterInputToShm( const Json::Value &execParams,
                                                               QVariantMap &params )
{
  // Opt-in flag must be explicitly true.
  if ( !execParams.isMember( "__shm_key__" ) || !execParams["__shm_key__"].isBool()
       || !execParams["__shm_key__"].asBool() )
    return nullptr;

  // The conventional raster input key is "input" (see operators/framework/rs_schema.h
  // makeRasterParam("input", ...)). Only migrate when it is present and looks like a path.
  if ( !execParams.isMember( "input" ) || !execParams["input"].isString() )
    return nullptr;
  const QString path = QString::fromStdString( execParams["input"].asString() );
  if ( path.isEmpty() )
    return nullptr;

  GdalDatasetWrapper ds;
  if ( !ds.open( path ) || !ds.isValid() )
    return nullptr; // let the plugin surface the GDAL error itself

  const int width = ds.width();
  const int height = ds.height();
  const int bands = ds.bandCount();
  if ( width <= 0 || height <= 0 || bands <= 0 )
    return nullptr;

  // Map the band's native GDAL type to a segment dtype. Byte/UInt16/Int32/
  // Float32 map 1:1 (preserving the source dtype, byte-exact); anything else
  // keeps the float32 conversion path (readBandData below).
  bool useNative = true;
  SharedMemorySegment::DType segDtype = SharedMemorySegment::DType::Float32;
  switch ( ds.bandDataType( 1 ) )
  {
    case GDT_Byte:
      segDtype = SharedMemorySegment::DType::UInt8;
      break;
    case GDT_UInt16:
      segDtype = SharedMemorySegment::DType::UInt16;
      break;
    case GDT_Int32:
      segDtype = SharedMemorySegment::DType::Int32;
      break;
    case GDT_Float32:
      segDtype = SharedMemorySegment::DType::Float32;
      break;
    default:
      useNative = false; // Float64/Int16/etc. -> float32 conversion
      break;
  }

  auto seg = std::make_unique<SharedMemorySegment>();
  if ( !seg->create( width, height, bands, segDtype ) )
    return nullptr;

  // Interleaved-by-pixel layout: [H, W, bands] matches the Python mount
  // np.ndarray((height, width, bands), ...). Read each band then scatter into
  // the (y, x, b) layout the numpy view expects.
  const size_t elemSize = SharedMemorySegment::dtypeSize( segDtype );
  char *payload = static_cast<char *>( seg->payload() );
  if ( useNative )
  {
    std::vector<unsigned char> bandPlane( static_cast<size_t>( width ) * height * elemSize );
    for ( int b = 0; b < bands; ++b )
    {
      if ( !ds.readBandDataNative( b + 1, bandPlane.data(), width, height ) )
        return nullptr; // fall back; the segment is reclaimed by unique_ptr
      const unsigned char *src = bandPlane.data();
      for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
          std::memcpy( payload + ( ( static_cast<size_t>( y ) * width + x ) * bands + b ) * elemSize,
                       src, elemSize );
          src += elemSize;
        }
    }
  }
  else
  {
    std::vector<float> bandPlane( static_cast<size_t>( width ) * height );
    float *floatPayload = reinterpret_cast<float *>( payload );
    for ( int b = 0; b < bands; ++b )
    {
      if ( !ds.readBandData( b + 1, bandPlane.data(), width, height ) )
        return nullptr; // fall back; the segment is reclaimed by unique_ptr
      const float *src = bandPlane.data();
      for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
          floatPayload[( static_cast<size_t>( y ) * width + x ) * bands + b] = *src++;
    }
  }

  // Replace the opt-in flag with the real key + metadata the daemon expects.
  params.insert( QStringLiteral( "__shm_key__" ), seg->nativeKey() );
  params.insert( QStringLiteral( "width" ), width );
  params.insert( QStringLiteral( "height" ), height );
  params.insert( QStringLiteral( "bands" ), bands );
  params.insert( QStringLiteral( "dtype" ), static_cast<int>( segDtype ) );
  return seg;
}

} // namespace


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
    sicnu::processing::AlgorithmDescriptor algoDesc;
    algoDesc.id = algoId.toStdString();
    algoDesc.displayName = name.isEmpty() ? algoId.toStdString() : name.toStdString();
    algoDesc.group = group.isEmpty() ? "Python Plugins" : group.toStdString();
    algoDesc.description = desc.toStdString();

    auto adapter = std::make_shared<sicnu::processing::PythonAlgorithmAdapter>(
      algoDesc,
      [this, algoId]( const Json::Value &execParams, sicnu::processing::ProgressCallback progress ) -> Json::Value {
        if ( !m_ipcServer )
        {
          throw std::runtime_error( "IPC Server not available" );
        }
        QJsonObject req;
        req[QStringLiteral( "id" )] = algoId;
        QVariantMap params = sicnu::processing::jsonParamsToVariantMap( execParams );

        // ADR 0064 opt-in: if the caller asked for zero-copy delivery
        // (__shm_key__: true) and supplied a raster "input" path, migrate the
        // raster into a shared-memory segment now. The segment is held alive
        // for the duration of the RPC below; RAII reclaims it (detach+unlink)
        // on return or exception, so the /dev/shm backing objects never leak.
        std::unique_ptr<SharedMemorySegment> shmSeg = migrateRasterInputToShm( execParams, params );

        req[QStringLiteral( "params" )] = QJsonObject::fromVariantMap( params );

        QJsonObject execResult;
        bool execIsError = false;
        const AwaitStatus awaitStatus = m_ipcServer->sendRequestSync(
          QStringLiteral( "processing.execute_algorithm" ), req, execResult, execIsError, 300000 );
        // Drop the mapping before returning; the daemon has already
        // close()+unlink()ed on its side too (best-effort, idempotent).
        shmSeg.reset();
        switch ( awaitStatus )
        {
          case AwaitStatus::NoClient:
            throw std::runtime_error( "IPC client not connected" );
          case AwaitStatus::Disconnected:
            throw std::runtime_error( "Python worker disconnected during algorithm execution" );
          case AwaitStatus::Timeout:
            throw std::runtime_error( "Python algorithm execution timed out" );
          case AwaitStatus::Ok:
            break;
        }
        if ( execIsError )
        {
          std::string errMsg = execResult[QStringLiteral( "message" )].toString( QStringLiteral( "Python algorithm execution failed" ) ).toStdString();
          throw std::runtime_error( errMsg );
        }
        if ( execResult[QStringLiteral( "status" )].toString() != QStringLiteral( "ok" ) )
        {
          throw std::runtime_error( "Python algorithm execution failed" );
        }
        if ( progress ) progress( 100, "Completed" );
        return sicnu::processing::jsonValueFromQJson( execResult );
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
      sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter( adapter );
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
