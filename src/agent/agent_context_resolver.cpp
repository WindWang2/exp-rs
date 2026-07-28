// src/agent/agent_context_resolver.cpp
#include "agent_context_resolver.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

#include "active_view_host.h"
#include <qgscoordinatereferencesystem.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsrectangle.h>

#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::agent
{

QJsonObject AgentContextResolver::buildContextSnapshot( data::DataManager *dataManager, ActiveViewHost *viewHost )
{
  QJsonObject snapshot;
  QJsonArray assetsArr;

  if ( dataManager )
  {
    auto assets = dataManager->assets();
    for ( const auto &asset : assets )
    {
      QJsonObject assetObj;
      assetObj[QStringLiteral( "id" )] = asset.id().toString();
      assetObj[QStringLiteral( "displayName" )] = asset.displayName();
      assetObj[QStringLiteral( "path" )] = asset.source().canonicalSource;

      QString kindStr = QStringLiteral( "Unknown" );
      if ( asset.kind() == data::AssetKind::Raster )
        kindStr = QStringLiteral( "Raster" );
      else if ( asset.kind() == data::AssetKind::Vector )
        kindStr = QStringLiteral( "Vector" );
      else if ( asset.kind() == data::AssetKind::RemoteMap )
        kindStr = QStringLiteral( "RemoteMap" );
      else if ( asset.kind() == data::AssetKind::VirtualRaster )
        kindStr = QStringLiteral( "VirtualRaster" );

      assetObj[QStringLiteral( "kind" )] = kindStr;

      const auto &structure = asset.structure();
      if ( const auto *raster = std::get_if<data::RasterStructure>( &structure ) )
      {
        assetObj[QStringLiteral( "width" )] = raster->width;
        assetObj[QStringLiteral( "height" )] = raster->height;
        assetObj[QStringLiteral( "bands" )] = raster->bandCount;
        assetObj[QStringLiteral( "crsWkt" )] = raster->crsWkt;
      }
      else if ( const auto *vector = std::get_if<data::VectorStructure>( &structure ) )
      {
        assetObj[QStringLiteral( "layerCount" )] = vector->layerCount;
        if ( !vector->layers.isEmpty() )
        {
          assetObj[QStringLiteral( "crsWkt" )] = vector->layers.first().crsWkt;
        }
      }

      assetsArr.append( assetObj );
    }
  }

  snapshot[QStringLiteral( "assets" )] = assetsArr;

  if ( viewHost && viewHost->mapCanvas() )
  {
    QgsMapCanvas *canvas = viewHost->mapCanvas();
    QJsonObject mapObj;

    mapObj[QStringLiteral( "crs" )] = canvas->mapSettings().destinationCrs().authid();

    QgsRectangle extent = canvas->extent();
    mapObj[QStringLiteral( "extent" )] = QString( "%1,%2,%3,%4" )
                                           .arg( extent.xMinimum() )
                                           .arg( extent.yMinimum() )
                                           .arg( extent.xMaximum() )
                                           .arg( extent.yMaximum() );

    if ( canvas->currentLayer() )
    {
      mapObj[QStringLiteral( "selectedLayer" )] = canvas->currentLayer()->name();
    }

    snapshot[QStringLiteral( "mapView" )] = mapObj;
  }

  return snapshot;
}

QString AgentContextResolver::formatSystemContextPrompt( const QJsonObject &snapshot )
{
  QString prompt;
  prompt += QStringLiteral( "[WORKSPACE CONTEXT]\n" );

  if ( snapshot.contains( QStringLiteral( "assets" ) ) )
  {
    QJsonArray assets = snapshot[QStringLiteral( "assets" )].toArray();
    if ( assets.isEmpty() )
    {
      prompt += QStringLiteral( "Loaded Data Assets: (None)\n" );
    }
    else
    {
      prompt += QStringLiteral( "Loaded Data Assets:\n" );
      for ( const auto &val : assets )
      {
        QJsonObject obj = val.toObject();
        QString id = obj[QStringLiteral( "id" )].toString();
        QString name = obj[QStringLiteral( "displayName" )].toString();
        QString path = obj[QStringLiteral( "path" )].toString();
        QString kind = obj[QStringLiteral( "kind" )].toString();

        prompt += QString( "- Asset '%1' (%2) [%3]" ).arg( id, name, kind );

        if ( obj.contains( QStringLiteral( "bands" ) ) )
        {
          prompt += QString( " %1x%2, %3 bands, CRS: %4" )
                      .arg( obj[QStringLiteral( "width" )].toInt() )
                      .arg( obj[QStringLiteral( "height" )].toInt() )
                      .arg( obj[QStringLiteral( "bands" )].toInt() )
                      .arg( obj[QStringLiteral( "crsWkt" )].toString() );
        }
        else if ( obj.contains( QStringLiteral( "layerCount" ) ) )
        {
          prompt += QString( " %1 layers, CRS: %2" )
                      .arg( obj[QStringLiteral( "layerCount" )].toInt() )
                      .arg( obj[QStringLiteral( "crsWkt" )].toString() );
        }

        prompt += QString( " Path: %1\n" ).arg( path );
      }
    }
  }

  if ( snapshot.contains( QStringLiteral( "mapView" ) ) )
  {
    QJsonObject mapObj = snapshot[QStringLiteral( "mapView" )].toObject();
    prompt += QString( "Map View: CRS %1, Extent [%2]" )
                .arg( mapObj[QStringLiteral( "crs" )].toString(),
                      mapObj[QStringLiteral( "extent" )].toString() );

    if ( mapObj.contains( QStringLiteral( "selectedLayer" ) ) )
    {
      prompt += QString( ", Selected Layer: %1" ).arg( mapObj[QStringLiteral( "selectedLayer" )].toString() );
    }
    prompt += QStringLiteral( "\n" );
  }

  return prompt;
}

} // namespace sicnu::agent
