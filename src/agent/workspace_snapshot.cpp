// src/agent/workspace_snapshot.cpp
#include "workspace_snapshot.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace sicnu::agent
{

QJsonObject WorkspaceSnapshot::toJson() const
{
  QJsonObject obj;
  QJsonArray assetsArr;

  for ( const auto &asset : assets )
  {
    QJsonObject assetObj;
    assetObj[QStringLiteral( "id" )] = asset.id;
    assetObj[QStringLiteral( "displayName" )] = asset.displayName;
    assetObj[QStringLiteral( "path" )] = asset.path;
    assetObj[QStringLiteral( "kind" )] = asset.kind;

    if ( asset.kind == QStringLiteral( "Raster" ) || asset.kind == QStringLiteral( "VirtualRaster" ) )
    {
      assetObj[QStringLiteral( "width" )] = asset.width;
      assetObj[QStringLiteral( "height" )] = asset.height;
      assetObj[QStringLiteral( "bands" )] = asset.bandCount;
      if ( !asset.crsWkt.isEmpty() )
        assetObj[QStringLiteral( "crsWkt" )] = asset.crsWkt;
    }
    else if ( asset.kind == QStringLiteral( "Vector" ) )
    {
      assetObj[QStringLiteral( "layerCount" )] = asset.layerCount;
      if ( !asset.crsWkt.isEmpty() )
        assetObj[QStringLiteral( "crsWkt" )] = asset.crsWkt;
    }

    assetsArr.append( assetObj );
  }

  obj[QStringLiteral( "assets" )] = assetsArr;

  if ( !mapView.crsAuthId.isEmpty() || !mapView.extentStr.isEmpty() || !mapView.activeLayerName.isEmpty() )
  {
    QJsonObject mapObj;
    if ( !mapView.crsAuthId.isEmpty() )
      mapObj[QStringLiteral( "crs" )] = mapView.crsAuthId;
    if ( !mapView.extentStr.isEmpty() )
      mapObj[QStringLiteral( "extent" )] = mapView.extentStr;
    if ( mapView.scale > 0.0 )
      mapObj[QStringLiteral( "scale" )] = mapView.scale;
    if ( !mapView.activeLayerName.isEmpty() )
      mapObj[QStringLiteral( "selectedLayer" )] = mapView.activeLayerName;

    obj[QStringLiteral( "mapView" )] = mapObj;
  }

  return obj;
}

QString WorkspaceSnapshot::toSystemPromptHeader() const
{
  QString prompt;
  prompt += QStringLiteral( "[WORKSPACE CONTEXT]\n" );

  if ( assets.isEmpty() )
  {
    prompt += QStringLiteral( "Loaded Data Assets: (None)\n" );
  }
  else
  {
    prompt += QStringLiteral( "Loaded Data Assets:\n" );
    for ( const auto &asset : assets )
    {
      prompt += QString( "- Asset '%1' (%2) [%3]" ).arg( asset.id, asset.displayName, asset.kind );

      if ( asset.bandCount > 0 )
      {
        prompt += QString( " %1x%2, %3 bands" ).arg( asset.width ).arg( asset.height ).arg( asset.bandCount );
      }
      else if ( asset.layerCount > 0 )
      {
        prompt += QString( " %1 vector layers" ).arg( asset.layerCount );
      }

      if ( !asset.path.isEmpty() )
      {
        prompt += QString( ", Path: %1" ).arg( asset.path );
      }
      prompt += QStringLiteral( "\n" );
    }
  }

  if ( !mapView.crsAuthId.isEmpty() || !mapView.extentStr.isEmpty() || !mapView.activeLayerName.isEmpty() )
  {
    prompt += QStringLiteral( "Map View State:\n" );
    if ( !mapView.crsAuthId.isEmpty() )
      prompt += QString( "- CRS: %1\n" ).arg( mapView.crsAuthId );
    if ( !mapView.extentStr.isEmpty() )
      prompt += QString( "- Extent: %1\n" ).arg( mapView.extentStr );
    if ( !mapView.activeLayerName.isEmpty() )
      prompt += QString( "- Selected Layer: %1\n" ).arg( mapView.activeLayerName );
  }

  return prompt;
}

} // namespace sicnu::agent
