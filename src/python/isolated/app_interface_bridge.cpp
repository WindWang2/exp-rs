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

/// Per-segment height cap (ADR 0064 tile-by-tile delivery). A raster taller
/// than this splits into row-chunk tiles — one shared-memory segment per tile —
/// so a single giant segment never exceeds practical /dev/shm object sizes and
/// the worker still sees the whole raster zero-copy (one array per tile).
constexpr int kSegmentHeightCap = 1024;

/// ADR 0064 opt-in zero-copy delivery. When a caller sets
/// `__shm_key__: true` on the request (the opt-in flag) alongside a raster
/// file-path input, migrate that raster into shared-memory segment(s) so the
/// Python worker receives it as zero-copy numpy arrays instead of opening the
/// file itself. Returns the segments (kept alive for the duration of the RPC)
/// or an empty vector if migration did not apply / failed (on failure the path
/// is left untouched so the plugin falls back to GDAL).
///
/// Rasters not taller than kSegmentHeightCap migrate as ONE segment, delivered
/// as `__shm_array__` (key + width/height/bands/dtype in \a params). Taller
/// rasters split into ceil(height / kSegmentHeightCap) row-chunk tiles, each a
/// segment of its own; \a params then carries a `__shm_tiles__` array (one
/// entry per tile: key + tile width/height/bands/dtype + the tile's first row
/// in the source raster) and the daemon mounts each tile as an array.
///
/// The raster's NATIVE dtype is preserved for the common integer and float
/// types (Byte->UInt8, UInt16->UInt16, Int32->Int32, Float32->Float32); any
/// other type (Float64, Int16, ...) falls back to the float32 conversion
/// path.
std::vector<std::unique_ptr<SharedMemorySegment>> migrateRasterInputToShm(
  const Json::Value &execParams, QVariantMap &params )
{
  // Opt-in flag must be explicitly true.
  if ( !execParams.isMember( "__shm_key__" ) || !execParams["__shm_key__"].isBool()
       || !execParams["__shm_key__"].asBool() )
    return {};

  // The conventional raster input key is "input" (see operators/framework/rs_schema.h
  // makeRasterParam("input", ...)). Only migrate when it is present and looks like a path.
  if ( !execParams.isMember( "input" ) || !execParams["input"].isString() )
    return {};
  const QString path = QString::fromStdString( execParams["input"].asString() );
  if ( path.isEmpty() )
    return {};

  GdalDatasetWrapper ds;
  if ( !ds.open( path ) || !ds.isValid() )
    return {}; // let the plugin surface the GDAL error itself

  const int width = ds.width();
  const int height = ds.height();
  const int bands = ds.bandCount();
  if ( width <= 0 || height <= 0 || bands <= 0 )
    return {};

  // Map the band's native GDAL type to a segment dtype. Byte/UInt16/Int32/
  // Float32/Float64 map 1:1 (preserving the source dtype, byte-exact); any
  // remaining type keeps the float32 conversion path (readBandData below).
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
    case GDT_Float64:
      segDtype = SharedMemorySegment::DType::Float64;
      break;
    default:
      useNative = false; // Int16/etc. -> float32 conversion
      break;
  }

  const size_t elemSize = SharedMemorySegment::dtypeSize( segDtype );

  // Read each band ONCE into a full-height plane, then scatter rows into the
  // per-tile segments (the wrapper has no window read, so tile splitting is a
  // memory operation on the already-loaded plane). Interleaved-by-pixel layout
  // [H, W, bands] matches the Python mount np.ndarray((height, width, bands)).
  std::vector<std::vector<unsigned char>> nativePlanes;
  std::vector<std::vector<float>> floatPlanes;
  if ( useNative )
  {
    nativePlanes.resize( bands, std::vector<unsigned char>(
                                   static_cast<size_t>( width ) * height * elemSize ) );
    for ( int b = 0; b < bands; ++b )
      if ( !ds.readBandDataNative( b + 1, nativePlanes[b].data(), width, height ) )
        return {}; // fall back; no segments created yet
  }
  else
  {
    floatPlanes.resize( bands, std::vector<float>( static_cast<size_t>( width ) * height ) );
    for ( int b = 0; b < bands; ++b )
      if ( !ds.readBandData( b + 1, floatPlanes[b].data(), width, height ) )
        return {}; // fall back; no segments created yet
  }

  // Split the raster into row-chunk tiles. A single segment when the raster
  // fits under the cap; otherwise one segment per ceil(height/cap) tile.
  std::vector<std::unique_ptr<SharedMemorySegment>> segments;
  std::vector<QVariantMap> tileManifest;
  const int tileHeight = height <= kSegmentHeightCap ? height : kSegmentHeightCap;
  for ( int rowStart = 0; rowStart < height; rowStart += tileHeight )
  {
    const int tileRows = std::min( tileHeight, height - rowStart );
    auto seg = std::make_unique<SharedMemorySegment>();
    if ( !seg->create( width, tileRows, bands, segDtype ) )
      return {}; // fall back; created segments are reclaimed by the vector

    char *payload = static_cast<char *>( seg->payload() );
    for ( int y = 0; y < tileRows; ++y )
    {
      const size_t srcRow = static_cast<size_t>( rowStart + y ) * width;
      const size_t dstRow = static_cast<size_t>( y ) * width;
      for ( int b = 0; b < bands; ++b )
      {
        if ( useNative )
        {
          const unsigned char *src = nativePlanes[b].data() + srcRow * elemSize;
          for ( int x = 0; x < width; ++x )
          {
            std::memcpy( payload + ( ( dstRow + x ) * bands + b ) * elemSize, src, elemSize );
            src += elemSize;
          }
        }
        else
        {
          const float *src = floatPlanes[b].data() + srcRow;
          float *floatPayload = reinterpret_cast<float *>( payload );
          for ( int x = 0; x < width; ++x )
            floatPayload[( dstRow + x ) * bands + b] = *src++;
        }
      }
    }

    if ( height > kSegmentHeightCap )
    {
      // Tile path: manifest entry per segment so the daemon can mount each tile.
      QVariantMap tile;
      tile.insert( QStringLiteral( "key" ), seg->nativeKey() );
      tile.insert( QStringLiteral( "width" ), width );
      tile.insert( QStringLiteral( "height" ), tileRows );
      tile.insert( QStringLiteral( "bands" ), bands );
      tile.insert( QStringLiteral( "dtype" ), static_cast<int>( segDtype ) );
      tile.insert( QStringLiteral( "row" ), rowStart );
      tileManifest.push_back( tile );
    }

    segments.push_back( std::move( seg ) );
  }

  // Replace the opt-in flag with the real keys + metadata the daemon expects.
  if ( height > kSegmentHeightCap )
  {
    // Tile path: send the __shm_tiles__ manifest and DROP the __shm_key__ opt-in
    // flag (still boolean true in params) — the daemon checks __shm_key__ BEFORE
    // __shm_tiles__, so a leftover bool would be mounted as a bogus segment key.
    QVariantList manifest;
    for ( const QVariantMap &tile : tileManifest )
      manifest.append( tile );
    params.insert( QStringLiteral( "__shm_tiles__" ), manifest );
    params.remove( QStringLiteral( "__shm_key__" ) );
  }
  else
  {
    params.insert( QStringLiteral( "__shm_key__" ), segments.front()->nativeKey() );
    params.insert( QStringLiteral( "width" ), width );
    params.insert( QStringLiteral( "height" ), height );
    params.insert( QStringLiteral( "bands" ), bands );
    params.insert( QStringLiteral( "dtype" ), static_cast<int>( segDtype ) );
  }
  return segments;
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
      [this, algoId]( const Json::Value &execParams, sicnu::processing::ProgressCallback progress,
                      std::function<bool()> isCancelled ) -> Json::Value {
        if ( isCancelled && isCancelled() )
        {
          throw std::runtime_error( "Python algorithm cancelled before execution" );
        }
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
        // for the duration of the RPC below; RAII reclaims them (detach+unlink)
        // on return or exception, so the /dev/shm backing objects never leak.
        // One segment for a short raster, one per row-chunk tile for a tall one
        // (ADR 0064 tile-by-tile delivery).
        std::vector<std::unique_ptr<SharedMemorySegment>> shmSegs =
          migrateRasterInputToShm( execParams, params );

        req[QStringLiteral( "params" )] = QJsonObject::fromVariantMap( params );

        QJsonObject execResult;
        bool execIsError = false;
        const AwaitStatus awaitStatus = m_ipcServer->sendRequestSync(
          QStringLiteral( "processing.execute_algorithm" ), req, execResult, execIsError, 300000 );
        // Drop the mapping before returning; the daemon has already
        // close()+unlink()ed on its side too (best-effort, idempotent).
        shmSegs.clear();
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
