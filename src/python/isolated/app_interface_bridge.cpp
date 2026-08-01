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

bool AppInterfaceBridge::dispatchIpcMessage( const QJsonObject &message, QJsonObject &response )
{
  if ( !message.contains( QStringLiteral( "method" ) ) )
    return false;

  QString method = message[QStringLiteral( "method" )].toString();
  QJsonObject params = message[QStringLiteral( "params" )].toObject();

  if ( method == QStringLiteral( "catalog.get_active_layer" ) )
  {
    response = getActiveLayerSummary().toJsonObject();
    return true;
  }
  else if ( method == QStringLiteral( "catalog.set_active_layer" ) )
  {
    const QString assetIdText = params[QStringLiteral( "asset_id" )].toString();
    const std::optional<sicnu::data::AssetId> assetId = sicnu::data::AssetId::fromString( assetIdText );
    const bool ok = assetId.has_value() && setActiveAsset( *assetId );
    response[QStringLiteral( "status" )] = ok ? QStringLiteral( "ok" ) : QStringLiteral( "unknown_asset" );
    response[QStringLiteral( "asset_id" )] = assetIdText;
    return true;
  }
  else if ( method == QStringLiteral( "data.add_layer" ) )
  {
    QString path = params[QStringLiteral( "path" )].toString();
    bool ok = openPath( path );
    response[QStringLiteral( "status" )] = ok ? QStringLiteral( "added" ) : QStringLiteral( "failed" );
    response[QStringLiteral( "path" )] = path;
    return true;
  }
  else if ( method == QStringLiteral( "canvas.get_state" ) )
  {
    response = getCanvasViewportSummary().toJsonObject();
    return true;
  }
  else if ( method == QStringLiteral( "ui.push_message_bar" ) )
  {
    QString title = params[QStringLiteral( "title" )].toString();
    QString text = params[QStringLiteral( "text" )].toString();
    int level = 0;
    const QJsonValue levelVal = params.value( QStringLiteral( "level" ) );
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
    bool ok = pushMessageBarAlert( title, text, level );
    response[QStringLiteral( "status" )] = ok ? QStringLiteral( "pushed" ) : QStringLiteral( "failed" );
    return true;
  }

  return false;
}

} // namespace sicnu::python::isolated
