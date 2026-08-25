// src/agent/view_control_service.cpp
#include "view_control_service.h"

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "display/qgis_display_manager.h"

#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsrectangle.h>
#include <qgsrubberband.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>

#include <QColor>
#include <QThread>
#include <cmath>

namespace sicnu::agent {

namespace {

bool parseNumber( const Json::Value &val, double &out )
{
  if ( val.isNumeric() )
  {
    out = val.asDouble();
    return std::isfinite( out );
  }
  return false;
}

bool parseBboxObject( const Json::Value &bboxVal, QgsRectangle &outRect )
{
  if ( !bboxVal.isObject() )
    return false;

  double xmin = 0.0, ymin = 0.0, xmax = 0.0, ymax = 0.0;
  if ( parseNumber( bboxVal["xmin"], xmin ) &&
       parseNumber( bboxVal["ymin"], ymin ) &&
       parseNumber( bboxVal["xmax"], xmax ) &&
       parseNumber( bboxVal["ymax"], ymax ) )
  {
    if ( xmin < xmax && ymin < ymax )
    {
      outRect = QgsRectangle( xmin, ymin, xmax, ymax );
      return true;
    }
  }
  return false;
}

bool parseExtentArray( const Json::Value &arrVal, QgsRectangle &outRect )
{
  if ( !arrVal.isArray() || arrVal.size() != 4 )
    return false;

  double xmin = 0.0, ymin = 0.0, xmax = 0.0, ymax = 0.0;
  if ( parseNumber( arrVal[0], xmin ) &&
       parseNumber( arrVal[1], ymin ) &&
       parseNumber( arrVal[2], xmax ) &&
       parseNumber( arrVal[3], ymax ) )
  {
    if ( xmin < xmax && ymin < ymax )
    {
      outRect = QgsRectangle( xmin, ymin, xmax, ymax );
      return true;
    }
  }
  return false;
}

Json::Value rectangleToJson( const QgsRectangle &rect )
{
  Json::Value val( Json::objectValue );
  val["xmin"] = rect.xMinimum();
  val["ymin"] = rect.yMinimum();
  val["xmax"] = rect.xMaximum();
  val["ymax"] = rect.yMaximum();
  return val;
}

} // namespace

ViewControlService::ViewControlService( QObject *parent )
  : QObject( parent )
{
}

ViewControlService::ViewControlService( sicnu::display::QgisDisplayManager *displayManager,
                                        QgsMapCanvas *canvas,
                                        sicnu::data::DataManager *dataManager,
                                        QObject *parent )
  : QObject( parent )
  , m_displayManager( displayManager )
  , m_canvas( canvas )
  , m_dataManager( dataManager )
{
}

ViewControlService::~ViewControlService()
{
  if ( m_roiRubberBand )
  {
    delete m_roiRubberBand;
    m_roiRubberBand = nullptr;
  }
}

void ViewControlService::setDisplayManager( sicnu::display::QgisDisplayManager *dm )
{
  m_displayManager = dm;
}

void ViewControlService::setMapCanvas( QgsMapCanvas *canvas )
{
  if ( m_canvas != canvas )
  {
    if ( m_roiRubberBand )
    {
      delete m_roiRubberBand;
      m_roiRubberBand = nullptr;
    }
    m_canvas = canvas;
  }
}

void ViewControlService::setDataManager( sicnu::data::DataManager *dm )
{
  m_dataManager = dm;
}

void ViewControlService::setActiveLayerName( const QString &name )
{
  m_activeLayerName = name;
}

QString ViewControlService::activeLayerName() const
{
  return m_activeLayerName;
}

QString ViewControlService::lastRoiWkt() const
{
  return m_lastRoiWkt;
}

QString ViewControlService::lastRoiCrs() const
{
  return m_lastRoiCrs;
}

QgsMapLayer *ViewControlService::findLayer( const QString &identifier ) const
{
  if ( identifier.isEmpty() )
    return nullptr;

  // 1. Try display manager DisplayLayerId
  if ( m_displayManager )
  {
    if ( auto layerIdOpt = display::DisplayLayerId::fromString( identifier ) )
    {
      if ( QgsMapLayer *ml = m_displayManager->mapLayer( *layerIdOpt ) )
        return ml;
    }
  }

  // 2. Try map canvas layers
  if ( m_canvas )
  {
    const QList<QgsMapLayer *> layers = m_canvas->layers();
    for ( QgsMapLayer *layer : layers )
    {
      if ( layer && ( layer->id() == identifier || layer->name() == identifier ) )
        return layer;
    }
  }

  // 3. Try QgsProject map layers
  if ( QgsProject::instance() )
  {
    if ( QgsMapLayer *ml = QgsProject::instance()->mapLayer( identifier ) )
      return ml;

    const auto projectLayers = QgsProject::instance()->mapLayersByName( identifier );
    if ( !projectLayers.isEmpty() && projectLayers.first() )
      return projectLayers.first();
  }

  return nullptr;
}

Json::Value ViewControlService::getState( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !m_canvas )
    {
      result["status"] = "error";
      result["errorMessage"] = "No active map canvas available";
      return result;
    }

    result["status"] = "success";

    if ( m_displayManager && !m_displayManager->activeViewId().isNull() )
    {
      result["view_id"] = m_displayManager->activeViewId().toString().toStdString();
    }
    else
    {
      result["view_id"] = "";
    }

    const QgsCoordinateReferenceSystem crs = m_canvas->mapSettings().destinationCrs();
    result["crs"] = crs.isValid() ? crs.authid().toStdString() : "";

    const QgsRectangle extent = m_canvas->extent();
    result["extent"] = rectangleToJson( extent );
    result["scale"] = m_canvas->scale();
    result["rotation"] = m_canvas->rotation();

    QString activeName = m_activeLayerName;
    QString activeId;
    if ( m_canvas->currentLayer() )
    {
      if ( activeName.isEmpty() )
        activeName = m_canvas->currentLayer()->name();
      activeId = m_canvas->currentLayer()->id();
    }
    result["active_layer"] = activeName.toStdString();
    result["active_layer_id"] = activeId.toStdString();

    if ( !m_lastRoiWkt.isEmpty() )
    {
      Json::Value roiVal( Json::objectValue );
      roiVal["geometry"] = m_lastRoiWkt.toStdString();
      roiVal["crs"] = m_lastRoiCrs.toStdString();
      result["roi"] = roiVal;
    }
    else
    {
      result["roi"] = Json::Value::null;
    }

    return result;
  } );
}

Json::Value ViewControlService::setExtent( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !m_canvas )
    {
      result["status"] = "error";
      result["errorMessage"] = "No active map canvas available";
      return result;
    }

    const QgsCoordinateReferenceSystem canvasCrs = m_canvas->mapSettings().destinationCrs();
    if ( !canvasCrs.isValid() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Map canvas does not have a valid destination CRS";
      return result;
    }

    QgsRectangle targetRect;
    bool parsed = false;

    if ( params.isMember( "bbox" ) )
    {
      parsed = parseBboxObject( params["bbox"], targetRect );
    }
    else if ( params.isMember( "extent" ) )
    {
      if ( params["extent"].isArray() )
        parsed = parseExtentArray( params["extent"], targetRect );
      else if ( params["extent"].isObject() )
        parsed = parseBboxObject( params["extent"], targetRect );
    }
    else if ( parseBboxObject( params, targetRect ) )
    {
      parsed = true;
    }
    else if ( params.isMember( "geometry" ) && params["geometry"].isString() )
    {
      const QString wkt = QString::fromStdString( params["geometry"].asString() );
      const QgsGeometry geom = QgsGeometry::fromWkt( wkt );
      if ( !geom.isNull() && !geom.isEmpty() )
      {
        targetRect = geom.boundingBox();
        parsed = ( targetRect.xMinimum() < targetRect.xMaximum() &&
                   targetRect.yMinimum() < targetRect.yMaximum() );
      }
    }

    if ( !parsed )
    {
      result["status"] = "error";
      result["errorMessage"] = "Invalid extent parameters: expected valid bounding box coordinates (xmin < xmax, ymin < ymax)";
      return result;
    }

    // Coordinate transformation if CRS specified and different from canvas CRS
    if ( params.isMember( "crs" ) && params["crs"].isString() && !params["crs"].asString().empty() )
    {
      const QString sourceCrsStr = QString::fromStdString( params["crs"].asString() );
      const QgsCoordinateReferenceSystem sourceCrs = QgsCoordinateReferenceSystem::fromOgcWmsCrs( sourceCrsStr );
      if ( !sourceCrs.isValid() )
      {
        result["status"] = "error";
        result["errorMessage"] = "Invalid or unsupported CRS: " + params["crs"].asString();
        return result;
      }

      if ( sourceCrs != canvasCrs )
      {
        try
        {
          const QgsCoordinateTransform ct( sourceCrs, canvasCrs, QgsProject::instance() );
          targetRect = ct.transformBoundingBox( targetRect );
        }
        catch ( const std::exception &e )
        {
          result["status"] = "error";
          result["errorMessage"] = std::string( "Failed to reproject extent: " ) + e.what();
          return result;
        }
      }
    }

    m_canvas->setExtent( targetRect );
    m_canvas->refresh();

    result["status"] = "success";
    result["extent"] = rectangleToJson( m_canvas->extent() );
    result["crs"] = canvasCrs.authid().toStdString();
    return result;
  } );
}

Json::Value ViewControlService::zoomToLayer( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !m_canvas )
    {
      result["status"] = "error";
      result["errorMessage"] = "No active map canvas available";
      return result;
    }

    std::string layerIdStr;
    if ( params.isMember( "layer_id" ) && params["layer_id"].isString() )
      layerIdStr = params["layer_id"].asString();
    else if ( params.isMember( "layer_name" ) && params["layer_name"].isString() )
      layerIdStr = params["layer_name"].asString();
    else if ( params.isMember( "name" ) && params["name"].isString() )
      layerIdStr = params["name"].asString();

    if ( layerIdStr.empty() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Missing required parameter 'layer_id' or 'layer_name'";
      return result;
    }

    const QString identifier = QString::fromStdString( layerIdStr );
    QgsMapLayer *layer = findLayer( identifier );
    if ( !layer )
    {
      result["status"] = "error";
      result["errorMessage"] = "Layer not found: " + layerIdStr;
      return result;
    }

    QgsRectangle layerExtent = layer->extent();
    if ( layerExtent.isNull() || layerExtent.isEmpty() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Layer has empty or invalid spatial extent: " + layerIdStr;
      return result;
    }

    const QgsCoordinateReferenceSystem canvasCrs = m_canvas->mapSettings().destinationCrs();
    if ( layer->crs().isValid() && canvasCrs.isValid() && layer->crs() != canvasCrs )
    {
      try
      {
        const QgsCoordinateTransform ct( layer->crs(), canvasCrs, QgsProject::instance() );
        layerExtent = ct.transformBoundingBox( layerExtent );
      }
      catch ( const std::exception &e )
      {
        result["status"] = "error";
        result["errorMessage"] = std::string( "Failed to transform layer extent to canvas CRS: " ) + e.what();
        return result;
      }
    }

    m_canvas->setExtent( layerExtent );
    m_canvas->refresh();

    result["status"] = "success";
    result["layer_id"] = layer->id().toStdString();
    result["layer_name"] = layer->name().toStdString();
    result["extent"] = rectangleToJson( m_canvas->extent() );
    result["crs"] = canvasCrs.isValid() ? canvasCrs.authid().toStdString() : "";
    return result;
  } );
}

Json::Value ViewControlService::zoomToAsset( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !m_canvas )
    {
      result["status"] = "error";
      result["errorMessage"] = "No active map canvas available";
      return result;
    }

    std::string assetIdStr;
    if ( params.isMember( "asset_id" ) && params["asset_id"].isString() )
      assetIdStr = params["asset_id"].asString();
    else if ( params.isMember( "id" ) && params["id"].isString() )
      assetIdStr = params["id"].asString();

    if ( assetIdStr.empty() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Missing required parameter 'asset_id'";
      return result;
    }

    const QString identifier = QString::fromStdString( assetIdStr );

    // 1. If display manager has layer associated with this asset
    if ( m_displayManager )
    {
      const auto viewIds = m_displayManager->listViews();
      for ( const auto &vId : viewIds )
      {
        if ( const auto viewSnap = m_displayManager->view( vId ) )
        {
          for ( const auto &lId : viewSnap->layerIds() )
          {
            if ( const auto layerSnap = m_displayManager->layer( lId ) )
            {
              if ( layerSnap->assetId().toString() == identifier )
              {
                if ( QgsMapLayer *ml = m_displayManager->mapLayer( lId ) )
                {
                  Json::Value layerParam( Json::objectValue );
                  layerParam["layer_id"] = ml->id().toStdString();
                  return zoomToLayer( layerParam );
                }
              }
            }
          }
        }
      }
    }

    // 2. Check DataManager asset directly
    if ( m_dataManager )
    {
      if ( auto assetIdOpt = data::AssetId::fromString( identifier ) )
      {
        if ( const auto asset = m_dataManager->asset( *assetIdOpt ) )
        {
          // Also try finding layer by asset name / display name
          if ( QgsMapLayer *ml = findLayer( asset->displayName() ) )
          {
            Json::Value layerParam( Json::objectValue );
            layerParam["layer_id"] = ml->id().toStdString();
            return zoomToLayer( layerParam );
          }
        }
      }
    }

    // 3. Fallback: try finding layer by name or id directly
    if ( QgsMapLayer *ml = findLayer( identifier ) )
    {
      Json::Value layerParam( Json::objectValue );
      layerParam["layer_id"] = ml->id().toStdString();
      return zoomToLayer( layerParam );
    }

    result["status"] = "error";
    result["errorMessage"] = "Asset not found or not currently displayed: " + assetIdStr;
    return result;
  } );
}

Json::Value ViewControlService::fitAll( const Json::Value & )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !m_canvas )
    {
      result["status"] = "error";
      result["errorMessage"] = "No active map canvas available";
      return result;
    }

    QgsRectangle full = m_canvas->fullExtent();
    if ( full.isNull() || full.isEmpty() )
    {
      // Try union of all layer extents
      const QList<QgsMapLayer *> layers = m_canvas->layers();
      const QgsCoordinateReferenceSystem canvasCrs = m_canvas->mapSettings().destinationCrs();
      for ( QgsMapLayer *layer : layers )
      {
        if ( layer && layer->isValid() && !layer->extent().isEmpty() )
        {
          QgsRectangle ext = layer->extent();
          if ( layer->crs().isValid() && canvasCrs.isValid() && layer->crs() != canvasCrs )
          {
            try
            {
              const QgsCoordinateTransform ct( layer->crs(), canvasCrs, QgsProject::instance() );
              ext = ct.transformBoundingBox( ext );
            }
            catch ( ... )
            {
              continue;
            }
          }
          if ( full.isEmpty() )
            full = ext;
          else
            full.combineExtentWith( ext );
        }
      }
    }

    if ( full.isNull() || full.isEmpty() )
    {
      result["status"] = "error";
      result["errorMessage"] = "No visible layers available to compute full extent";
      return result;
    }

    m_canvas->setExtent( full );
    m_canvas->refresh();

    result["status"] = "success";
    result["extent"] = rectangleToJson( m_canvas->extent() );
    const QgsCoordinateReferenceSystem canvasCrs = m_canvas->mapSettings().destinationCrs();
    result["crs"] = canvasCrs.isValid() ? canvasCrs.authid().toStdString() : "";
    return result;
  } );
}

Json::Value ViewControlService::setScale( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !m_canvas )
    {
      result["status"] = "error";
      result["errorMessage"] = "No active map canvas available";
      return result;
    }

    double targetScale = 0.0;
    if ( !params.isMember( "scale" ) || !parseNumber( params["scale"], targetScale ) || targetScale <= 0.0 )
    {
      result["status"] = "error";
      result["errorMessage"] = "Invalid scale parameter: scale must be a positive number";
      return result;
    }

    m_canvas->zoomScale( targetScale );
    m_canvas->refresh();

    result["status"] = "success";
    result["scale"] = m_canvas->scale();
    return result;
  } );
}

Json::Value ViewControlService::setRoi( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !m_canvas )
    {
      result["status"] = "error";
      result["errorMessage"] = "No active map canvas available";
      return result;
    }

    const QgsCoordinateReferenceSystem canvasCrs = m_canvas->mapSettings().destinationCrs();
    if ( !canvasCrs.isValid() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Map canvas does not have a valid destination CRS; cannot place ROI";
      return result;
    }

    QgsGeometry geom;
    if ( params.isMember( "geometry" ) && params["geometry"].isString() && !params["geometry"].asString().empty() )
    {
      geom = QgsGeometry::fromWkt( QString::fromStdString( params["geometry"].asString() ) );
    }
    else if ( params.isMember( "bbox" ) )
    {
      QgsRectangle rect;
      if ( parseBboxObject( params["bbox"], rect ) )
      {
        geom = QgsGeometry::fromRect( rect );
      }
    }
    else
    {
      QgsRectangle rect;
      if ( parseBboxObject( params, rect ) )
      {
        geom = QgsGeometry::fromRect( rect );
      }
    }

    if ( geom.isNull() || geom.isEmpty() )
    {
      result["status"] = "error";
      result["errorMessage"] = "ROI geometry is missing or invalid: provide 'geometry' (WKT) or 'bbox' {xmin, ymin, xmax, ymax}";
      return result;
    }

    QgsCoordinateReferenceSystem sourceCrs = canvasCrs;
    if ( params.isMember( "crs" ) && params["crs"].isString() && !params["crs"].asString().empty() )
    {
      const QString crsStr = QString::fromStdString( params["crs"].asString() );
      sourceCrs = QgsCoordinateReferenceSystem::fromOgcWmsCrs( crsStr );
      if ( !sourceCrs.isValid() )
      {
        result["status"] = "error";
        result["errorMessage"] = "Invalid or unsupported CRS for ROI: " + params["crs"].asString();
        return result;
      }
    }

    if ( sourceCrs != canvasCrs )
    {
      try
      {
        const QgsCoordinateTransform ct( sourceCrs, canvasCrs, QgsProject::instance() );
        geom.transform( ct );
      }
      catch ( const std::exception &e )
      {
        result["status"] = "error";
        result["errorMessage"] = std::string( "Failed to transform ROI geometry to canvas CRS: " ) + e.what();
        return result;
      }
    }

    if ( m_roiRubberBand )
    {
      delete m_roiRubberBand;
      m_roiRubberBand = nullptr;
    }
    m_roiRubberBand = new QgsRubberBand( m_canvas, Qgis::GeometryType::Polygon );
    m_roiRubberBand->setColor( QColor( 255, 80, 0, 120 ) );
    m_roiRubberBand->setStrokeColor( QColor( 255, 80, 0 ) );
    m_roiRubberBand->setToGeometry( geom, canvasCrs );
    m_roiRubberBand->show();
    m_canvas->refresh();

    m_lastRoiWkt = geom.asWkt();
    m_lastRoiCrs = canvasCrs.authid();

    result["status"] = "success";
    result["geometry"] = m_lastRoiWkt.toStdString();
    result["crs"] = m_lastRoiCrs.toStdString();
    return result;
  } );
}

Json::Value ViewControlService::clearRoi( const Json::Value & )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( m_roiRubberBand )
    {
      delete m_roiRubberBand;
      m_roiRubberBand = nullptr;
    }
    m_lastRoiWkt.clear();
    m_lastRoiCrs.clear();

    if ( m_canvas )
      m_canvas->refresh();

    result["status"] = "success";
    return result;
  } );
}

} // namespace sicnu::agent
