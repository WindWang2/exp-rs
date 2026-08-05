// src/agent/workspace_snapshot.cpp
#include "workspace_snapshot.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "active_view_host.h"
#include <qgsmaplayer.h>
#include <qgsrectangle.h>

#include <QStringList>

namespace sicnu::agent
{

namespace
{

/// Stringify at the JSON / prompt boundary; unset or unrecognized kinds become "Unknown".
QString assetKindToString( const std::optional<data::AssetKind> &kind )
{
  if ( !kind.has_value() )
    return QStringLiteral( "Unknown" );

  switch ( *kind )
  {
    case data::AssetKind::Raster:
      return QStringLiteral( "Raster" );
    case data::AssetKind::Vector:
      return QStringLiteral( "Vector" );
    case data::AssetKind::RemoteMap:
      return QStringLiteral( "RemoteMap" );
    case data::AssetKind::VirtualRaster:
      return QStringLiteral( "VirtualRaster" );
  }
  return QStringLiteral( "Unknown" );
}

} // namespace

WorkspaceSnapshot WorkspaceSnapshot::capture( data::DataManager *dataManager, ActiveViewHost *viewHost )
{
  WorkspaceSnapshot snapshot;

  if ( dataManager )
  {
    auto assets = dataManager->assets();
    for ( const auto &asset : assets )
    {
      DataAssetInfo info;
      info.id = asset.id().toString();
      info.displayName = asset.displayName();
      info.path = asset.source().canonicalSource;

      info.kind = asset.kind();

      const auto &structure = asset.structure();
      if ( const auto *raster = std::get_if<data::RasterStructure>( &structure ) )
      {
        info.width = raster->width;
        info.height = raster->height;
        info.bandCount = raster->bandCount;
        info.crsWkt = raster->crsWkt;
      }
      else if ( const auto *vector = std::get_if<data::VectorStructure>( &structure ) )
      {
        info.layerCount = vector->layerCount;
        if ( !vector->layers.isEmpty() )
        {
          info.crsWkt = vector->layers.first().crsWkt;
        }
      }

      snapshot.assets.append( info );
    }
  }

  if ( viewHost )
  {
    snapshot.mapView.crsAuthId = viewHost->mapCanvasCrsAuthId();
    QgsRectangle extent = viewHost->mapCanvasExtent();
    if ( !extent.isEmpty() && !extent.isNull() )
    {
      snapshot.mapView.extentStr = QString( "%1,%2,%3,%4" )
                                     .arg( extent.xMinimum() )
                                     .arg( extent.yMinimum() )
                                     .arg( extent.xMaximum() )
                                     .arg( extent.yMaximum() );
    }
    snapshot.mapView.scale = viewHost->mapCanvasScale();
    snapshot.mapView.activeLayerName = viewHost->activeLayerName();
  }

  return snapshot;
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
      prompt += QString( "- Asset '%1' (%2) [%3]" ).arg( asset.id, asset.displayName, assetKindToString( asset.kind ) );

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
