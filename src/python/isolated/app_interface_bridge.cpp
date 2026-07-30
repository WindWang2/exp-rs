// src/python/isolated/app_interface_bridge.cpp
#include "app_interface_bridge.h"
#include "active_view_host.h"

#include <qgsmaplayer.h>

#include <QJsonArray>

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

AppInterfaceBridge::AppInterfaceBridge( ActiveViewHost *activeViewHost, QObject *parent )
  : QObject( parent )
  , m_activeViewHost( activeViewHost )
{
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
  if ( !m_activeViewHost )
  {
    return summary;
  }

  QgsMapLayer *activeLyr = m_activeViewHost->activeLayer();
  if ( activeLyr )
  {
    summary.isValid = true;
    summary.name = activeLyr->name();
    summary.source = activeLyr->source();
    summary.type = ( activeLyr->type() == Qgis::LayerType::Raster ) ? QStringLiteral( "raster" ) : QStringLiteral( "vector" );
    summary.crs = activeLyr->crs().authid();
  }
  return summary;
}

QgsMapLayer *AppInterfaceBridge::activeLayer() const
{
  return m_activeViewHost ? m_activeViewHost->activeLayer() : nullptr;
}

bool AppInterfaceBridge::openPath( const QString &path )
{
  if ( !m_activeViewHost || path.isEmpty() )
  {
    return false;
  }
  auto res = m_activeViewHost->openPath( path );
  return static_cast<bool>( res );
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
