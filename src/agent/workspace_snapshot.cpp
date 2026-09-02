// src/agent/workspace_snapshot.cpp
#include "workspace_snapshot.h"

#include <QSet>

#include <algorithm>
#include "data/band_role.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_workspace.h"
#include <qgscontrastenhancement.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgsrectangle.h>
#include <qgssinglebandgrayrenderer.h>
#include <qgsmultibandcolorrenderer.h>

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

/// Capture the active raster layer's band composition, Real Data Range, and
/// display stretch. Returns an inactive ActiveRasterDisplay when the layer is
/// not a raster (vector/remote-map) or has no renderer. Read-only: never writes
/// back to the canvas. Uses only QGIS native renderer APIs (qgis_core/qgis_gui)
/// so the agent library need not link the display library — the snapshot is a
/// read-only observer of what the renderer already holds.
MapViewSnapshot::ActiveRasterDisplay captureActiveRaster( QgsMapLayer *layer )
{
  MapViewSnapshot::ActiveRasterDisplay out;
  auto *raster = qobject_cast<QgsRasterLayer *>( layer );
  if ( !raster || !raster->isValid() )
    return out;

  QgsRasterRenderer *renderer = raster->renderer();
  if ( !renderer )
    return out;

  const QgsContrastEnhancement *enhancement = nullptr;
  if ( auto *gray = dynamic_cast<QgsSingleBandGrayRenderer *>( renderer ) )
  {
    out.renderer = QStringLiteral( "SingleBandGray" );
    out.grayBand = gray->inputBand();
    enhancement = gray->contrastEnhancement();
  }
  else if ( auto *rgb = dynamic_cast<QgsMultiBandColorRenderer *>( renderer ) )
  {
    out.renderer = QStringLiteral( "MultiBandColor" );
    out.redBand = rgb->redBand();
    out.greenBand = rgb->greenBand();
    out.blueBand = rgb->blueBand();
    enhancement = rgb->redContrastEnhancement();
  }
  else
  {
    return out; // Unsupported renderer — leave inactive.
  }

  // Real Data Range: deliberately NOT sampled via bandStatistics() on the
  // GUI thread — that GDAL scan blocks the event loop on large rasters.
  // dataMin/dataMax therefore stay unset on this capture path; the prompt's
  // "Real Data Range" line (resolvePromptHeader) is emitted only when
  // another (off-thread) producer fills the fields. (#634: tracker owned
  // by the off-thread refresh - no action here)
  Q_UNUSED( raster );

  // Stretch algorithm + display window from the renderer's contrast enhancement
  // (the same object the display-stretch pipeline applies). For MultiBandColor
  // the red channel's enhancement is the representative window.
  if ( enhancement )
  {
    const auto algo = enhancement->contrastEnhancementAlgorithm();
    if ( algo != QgsContrastEnhancement::NoEnhancement )
    {
      out.stretchAlgorithm =
        QgsContrastEnhancement::contrastEnhancementAlgorithmString( algo );
      out.displayMin = enhancement->minimumValue();
      out.displayMax = enhancement->maximumValue();
    }
  }

  out.valid = true;
  return out;
}

} // namespace

WorkspaceSnapshot WorkspaceSnapshot::capture( data::DataManager *dataManager,
                                              QgsMapCanvas *canvas,
                                              const QString &activeLayerName,
                                              quint64 displayRevision )
{
  WorkspaceSnapshot snapshot;
  snapshot.displayRevision = displayRevision;

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
        // Surface semantic band roles (stable ids, empty for roles unknown) so
        // the agent can reason about "NIR" instead of a band number.
        info.bandRoles.reserve( raster->bands.size() );
        for ( const auto &band : raster->bands )
        {
          info.bandRoles.append( band.role == data::BandRole::Unknown
                                   ? QString()
                                   : data::bandRoleToString( band.role ) );
        }
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

  // Temporal collection records: light descriptor summaries (no raster I/O)
  // so the agent can discover multi-temporal inputs by name/time-range and
  // address them by workspace id through the temporal:* tools.
  if ( dataManager )
  {
    for ( const auto &record : dataManager->temporalCollections() )
    {
      TemporalCollectionInfo info;
      info.id = record.id.toString();
      info.name = record.displayName;
      info.revision = static_cast<int>( record.revision );
      sicnu::temporal::TemporalCollection parsed;
      QString parseError;
      if ( sicnu::temporal::collectionFromDescriptorText( record.descriptor, &parsed, &parseError ) )
      {
        info.sceneCount = parsed.sceneCount();
        info.timeStart = parsed.timeRangeStartIso();
        info.timeEnd = parsed.timeRangeEndIso();
        QSet<QString> platforms;
        for ( const auto &scene : parsed.scenes() )
        {
          if ( !scene.assetId.isEmpty() )
            ++info.scenesBound;
          if ( !scene.platform.isEmpty() )
            platforms.insert( scene.platform );
        }
        info.platforms = QStringList( platforms.cbegin(), platforms.cend() );
        std::sort( info.platforms.begin(), info.platforms.end() );
      }
      snapshot.temporalCollections.append( info );
    }
  }

  if ( canvas )
  {
    snapshot.mapView.crsAuthId =
      canvas->mapSettings().destinationCrs().authid();
    QgsRectangle extent = canvas->extent();
    if ( !extent.isEmpty() && !extent.isNull() )
    {
      snapshot.mapView.extentStr = QString( "%1,%2,%3,%4" )
                                     .arg( extent.xMinimum() )
                                     .arg( extent.yMinimum() )
                                     .arg( extent.xMaximum() )
                                     .arg( extent.yMaximum() );
    }
    snapshot.mapView.scale = canvas->scale();
    // The agent consumes only QgsMapCanvas (a qgis_gui shared type it links);
    // the canvas current-layer is the primary path ActiveViewHost::activeLayerName()
    // mirrors, so no dependency on the app-executable ActiveViewHost class.
    QgsMapLayer *activeLayer = canvas->currentLayer();
    snapshot.mapView.activeRaster = captureActiveRaster( activeLayer );
  }

  if ( !activeLayerName.isEmpty() )
  {
    snapshot.mapView.activeLayerName = activeLayerName;
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
        if ( !asset.bandRoles.isEmpty() )
        {
          // Stable semantic roles (e.g. "nir, red, green, blue") so the agent
          // can select bands by role; empty entries (roles unknown) render as
          // "unknown" and keep the list aligned with band order.
          QStringList roles = asset.bandRoles;
          for ( QString &role : roles )
          {
            if ( role.isEmpty() )
              role = QStringLiteral( "unknown" );
          }
          prompt += QString( " (roles: %1)" ).arg( roles.join( QStringLiteral( ", " ) ) );
        }
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

  if ( !mapView.crsAuthId.isEmpty() || !mapView.extentStr.isEmpty() || !mapView.activeLayerName.isEmpty() || displayRevision > 0 )
  {
    prompt += QStringLiteral( "Map View State:\n" );
    if ( !mapView.crsAuthId.isEmpty() )
      prompt += QString( "- CRS: %1\n" ).arg( mapView.crsAuthId );
    if ( !mapView.extentStr.isEmpty() )
      prompt += QString( "- Extent: %1\n" ).arg( mapView.extentStr );
    if ( !mapView.activeLayerName.isEmpty() )
      prompt += QString( "- Selected Layer: %1\n" ).arg( mapView.activeLayerName );
    if ( displayRevision > 0 )
      prompt += QString( "- Display Revision: %1\n" ).arg( displayRevision );
    if ( mapView.activeRaster.valid )
    {
      prompt += QStringLiteral( "- Active Raster Display:\n" );
      if ( mapView.activeRaster.renderer == QStringLiteral( "SingleBandGray" ) )
      {
        if ( mapView.activeRaster.grayBand > 0 )
          prompt += QString( "  - Bands: gray=%1\n" ).arg( mapView.activeRaster.grayBand );
      }
      else if ( mapView.activeRaster.renderer == QStringLiteral( "MultiBandColor" ) )
      {
        // A default multiband renderer assigns -1 to channels that exceed the
        // band count (e.g. the blue channel of a 2-band raster): treat any
        // non-positive band as unset so the prompt never names a non-existent band.
        QStringList channels;
        if ( mapView.activeRaster.redBand > 0 )
          channels << QStringLiteral( "R=%1" ).arg( mapView.activeRaster.redBand );
        if ( mapView.activeRaster.greenBand > 0 )
          channels << QStringLiteral( "G=%1" ).arg( mapView.activeRaster.greenBand );
        if ( mapView.activeRaster.blueBand > 0 )
          channels << QStringLiteral( "B=%1" ).arg( mapView.activeRaster.blueBand );
        if ( !channels.isEmpty() )
          prompt += QString( "  - Bands: %1\n" ).arg( channels.join( QStringLiteral( " " ) ) );
      }
      // Emitted only when dataMin/dataMax are populated: the GUI-thread
      // capture path leaves them unset (see captureActiveRaster), so this
      // line appears only for snapshots filled by an off-thread producer.
      if ( mapView.activeRaster.dataMin && mapView.activeRaster.dataMax )
        prompt += QString( "  - Real Data Range: [%1, %2]\n" )
                    .arg( *mapView.activeRaster.dataMin )
                    .arg( *mapView.activeRaster.dataMax );
      if ( !mapView.activeRaster.stretchAlgorithm.isEmpty() &&
           mapView.activeRaster.displayMin && mapView.activeRaster.displayMax )
      {
        prompt += QString( "  - Stretch: %1 [%2, %3]\n" )
                    .arg( mapView.activeRaster.stretchAlgorithm )
                    .arg( *mapView.activeRaster.displayMin )
                    .arg( *mapView.activeRaster.displayMax );
      }
    }
  }

  if ( !temporalCollections.isEmpty() )
  {
    prompt += QStringLiteral( "Temporal Collections (workspace records — address by id via "
                              "temporal:get_collection / temporal:* tools; run rs:temporal_* "
                              "operators with collection=<id>):\n" );
    for ( const auto &collection : temporalCollections )
    {
      prompt += QString( "  - '%1' [%2] revision %3: %4 scene(s), %5 bound" )
                  .arg( collection.name, collection.id )
                  .arg( collection.revision )
                  .arg( collection.sceneCount )
                  .arg( collection.scenesBound );
      if ( !collection.timeStart.isEmpty() )
        prompt += QString( ", %1 … %2" ).arg( collection.timeStart, collection.timeEnd );
      if ( !collection.platforms.isEmpty() )
        prompt += QString( ", platform: %1" ).arg( collection.platforms.join( "/" ) );
      prompt += QLatin1Char( '\n' );
    }
  }

  return prompt;
}

} // namespace sicnu::agent
