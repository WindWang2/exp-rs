// src/agent/interaction_tool_registry.cpp
#include "interaction_tool_registry.h"
#include "view_control_service.h"
#include "raster_display_service.h"
#include "data/data_manager.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/framework/json_params_converter.h"

#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsrasterdataprovider.h>
#include <algorithm>
#include <sstream>
#include <QPointer>

namespace sicnu::agent {

namespace {

Json::Value createEmptyObjectSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = Json::Value( Json::objectValue );
  return schema;
}

Json::Value serviceUnavailableError()
{
  Json::Value result( Json::objectValue );
  result["status"] = "error";
  result["errorMessage"] = "Interactive tool service is no longer available";
  return result;
}

Json::Value createViewStateSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value viewIdProp( Json::objectValue );
  viewIdProp["type"] = "string";
  viewIdProp["description"] = "Optional unique Display View ID. When omitted, the active view is queried.";
  props["view_id"] = viewIdProp;

  schema["properties"] = props;
  return schema;
}

Json::Value createSetExtentSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value bboxProp( Json::objectValue );
  bboxProp["type"] = "object";
  bboxProp["description"] = "Bounding box coordinates with numeric xmin, ymin, xmax, ymax.";
  Json::Value bboxSubProps( Json::objectValue );
  for ( const auto &coord : { "xmin", "ymin", "xmax", "ymax" } )
  {
    Json::Value numProp( Json::objectValue );
    numProp["type"] = "number";
    bboxSubProps[coord] = numProp;
  }
  bboxProp["properties"] = bboxSubProps;
  props["bbox"] = bboxProp;

  Json::Value extentProp( Json::objectValue );
  extentProp["type"] = "array";
  extentProp["description"] = "Bounding box coordinate array [xmin, ymin, xmax, ymax].";
  extentProp["minItems"] = 4;
  extentProp["maxItems"] = 4;
  Json::Value itemNum( Json::objectValue );
  itemNum["type"] = "number";
  extentProp["items"] = itemNum;
  props["extent"] = extentProp;

  Json::Value geomProp( Json::objectValue );
  geomProp["type"] = "string";
  geomProp["description"] = "WKT geometry string defining the target spatial extent.";
  props["geometry"] = geomProp;

  Json::Value crsProp( Json::objectValue );
  crsProp["type"] = "string";
  crsProp["description"] = "Coordinate reference system auth ID (e.g. 'EPSG:4326'). When omitted, canvas CRS is assumed.";
  props["crs"] = crsProp;

  schema["properties"] = props;
  return schema;
}

Json::Value createZoomToLayerSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value layerIdProp( Json::objectValue );
  layerIdProp["type"] = "string";
  layerIdProp["description"] = "Unique DisplayLayerId, QGIS layer ID, or layer display name.";
  props["layer_id"] = layerIdProp;

  Json::Value layerNameProp( Json::objectValue );
  layerNameProp["type"] = "string";
  layerNameProp["description"] = "Layer display name.";
  props["layer_name"] = layerNameProp;

  schema["properties"] = props;
  return schema;
}

Json::Value createZoomToAssetSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value assetIdProp( Json::objectValue );
  assetIdProp["type"] = "string";
  assetIdProp["description"] = "Data Manager asset ID (UUID string) or registered asset name.";
  props["asset_id"] = assetIdProp;

  schema["properties"] = props;
  Json::Value req( Json::arrayValue );
  req.append( "asset_id" );
  schema["required"] = req;
  return schema;
}

Json::Value createSetScaleSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value scaleProp( Json::objectValue );
  scaleProp["type"] = "number";
  scaleProp["description"] = "Positive map scale denominator value (e.g. 50000 for 1:50000).";
  props["scale"] = scaleProp;

  schema["properties"] = props;
  Json::Value req( Json::arrayValue );
  req.append( "scale" );
  schema["required"] = req;
  return schema;
}

Json::Value createRoiSetSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value geomProp( Json::objectValue );
  geomProp["type"] = "string";
  geomProp["description"] = "WKT polygon or geometry string defining the Region of Interest.";
  props["geometry"] = geomProp;

  Json::Value bboxProp( Json::objectValue );
  bboxProp["type"] = "object";
  bboxProp["description"] = "Bounding box coordinates {xmin, ymin, xmax, ymax}.";
  props["bbox"] = bboxProp;

  Json::Value crsProp( Json::objectValue );
  crsProp["type"] = "string";
  crsProp["description"] = "Coordinate reference system auth ID (e.g. 'EPSG:4326'). When omitted, canvas CRS is assumed.";
  props["crs"] = crsProp;

  schema["properties"] = props;
  return schema;
}

Json::Value createRasterGetDisplaySchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value layerProp( Json::objectValue );
  layerProp["type"] = "string";
  layerProp["description"] = "Optional raster layer ID, layer display name, or asset ID. When omitted, the active raster layer is queried.";
  props["layer"] = layerProp;

  schema["properties"] = props;
  return schema;
}

Json::Value createRasterSetBandCompositeSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value layerProp( Json::objectValue );
  layerProp["type"] = "string";
  layerProp["description"] = "Optional raster layer ID, layer display name, or asset ID. When omitted, the active raster layer is used.";
  props["layer"] = layerProp;

  Json::Value redProp( Json::objectValue );
  redProp["description"] = "Red channel band role (e.g. 'red', 'nir', 'swir1', 'swir2') or 1-based band number.";
  props["red"] = redProp;

  Json::Value greenProp( Json::objectValue );
  greenProp["description"] = "Green channel band role (e.g. 'green', 'red') or 1-based band number.";
  props["green"] = greenProp;

  Json::Value blueProp( Json::objectValue );
  blueProp["description"] = "Blue channel band role (e.g. 'blue', 'green') or 1-based band number.";
  props["blue"] = blueProp;

  Json::Value grayProp( Json::objectValue );
  grayProp["description"] = "Optional single channel band role or 1-based band number for grayscale rendering.";
  props["gray"] = grayProp;

  Json::Value opacityProp( Json::objectValue );
  opacityProp["type"] = "number";
  opacityProp["description"] = "Optional layer opacity value between 0.0 (transparent) and 1.0 (opaque).";
  props["opacity"] = opacityProp;

  schema["properties"] = props;
  return schema;
}

Json::Value createRasterSetStretchSchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value layerProp( Json::objectValue );
  layerProp["type"] = "string";
  layerProp["description"] = "Optional raster layer ID, layer display name, or asset ID. When omitted, the active raster layer is used.";
  props["layer"] = layerProp;

  Json::Value methodProp( Json::objectValue );
  methodProp["type"] = "string";
  methodProp["description"] = "Display stretch method: 'minimum_maximum', 'percent_clip', 'stddev', or 'none'.";
  Json::Value enumVals( Json::arrayValue );
  enumVals.append( "minimum_maximum" );
  enumVals.append( "percent_clip" );
  enumVals.append( "stddev" );
  enumVals.append( "none" );
  methodProp["enum"] = enumVals;
  props["method"] = methodProp;

  Json::Value lowerProp( Json::objectValue );
  lowerProp["type"] = "number";
  lowerProp["description"] = "Lower clip percentile for percent_clip (e.g. 2 for 2%).";
  props["lower"] = lowerProp;

  Json::Value upperProp( Json::objectValue );
  upperProp["type"] = "number";
  upperProp["description"] = "Upper clip percentile for percent_clip (e.g. 98 for 98%).";
  props["upper"] = upperProp;

  Json::Value factorProp( Json::objectValue );
  factorProp["type"] = "number";
  factorProp["description"] = "Standard deviation multiplier factor for stddev (e.g. 2.0).";
  props["factor"] = factorProp;

  Json::Value minProp( Json::objectValue );
  minProp["type"] = "number";
  minProp["description"] = "Explicit minimum display value for minimum_maximum.";
  props["min"] = minProp;

  Json::Value maxProp( Json::objectValue );
  maxProp["type"] = "number";
  maxProp["description"] = "Explicit maximum display value for minimum_maximum.";
  props["max"] = maxProp;

  schema["properties"] = props;
  Json::Value req( Json::arrayValue );
  req.append( "method" );
  schema["required"] = req;
  return schema;
}

Json::Value createRasterResetDisplaySchema()
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value layerProp( Json::objectValue );
  layerProp["type"] = "string";
  layerProp["description"] = "Optional raster layer ID, layer display name, or asset ID. When omitted, the active raster layer is reset.";
  props["layer"] = layerProp;

  schema["properties"] = props;
  return schema;
}

} // namespace

InteractionToolRegistry::InteractionToolRegistry()
{
}

InteractionToolRegistry &InteractionToolRegistry::instance()
{
  static InteractionToolRegistry s_instance;
  return s_instance;
}

void InteractionToolRegistry::reset()
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_tools.clear();
}

void InteractionToolRegistry::registerTool( InteractionToolDefinition toolDef )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_tools[toolDef.name] = std::move( toolDef );
}

bool InteractionToolRegistry::unregisterTool( const std::string &name )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_tools.erase( name ) > 0;
}

std::optional<InteractionToolDefinition> InteractionToolRegistry::findTool( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  auto it = m_tools.find( name );
  if ( it != m_tools.end() )
    return it->second;

  // Name normalization: support underscore in place of colon for LLM tool formats (e.g. view_get_state)
  const auto underscorePos = name.find( '_' );
  if ( underscorePos != std::string::npos )
  {
    std::string altName = name;
    altName[underscorePos] = ':';
    auto itAlt = m_tools.find( altName );
    if ( itAlt != m_tools.end() )
      return itAlt->second;
  }

  return std::nullopt;
}

bool InteractionToolRegistry::hasTool( const std::string &name ) const
{
  return findTool( name ).has_value();
}

std::vector<InteractionToolDefinition> InteractionToolRegistry::listTools() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::vector<InteractionToolDefinition> result;
  result.reserve( m_tools.size() );
  for ( const auto &[name, def] : m_tools )
  {
    result.push_back( def );
  }
  return result;
}

size_t InteractionToolRegistry::toolCount() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_tools.size();
}

Json::Value InteractionToolRegistry::execute( const std::string &name, const Json::Value &parameters ) const
{
  const auto toolOpt = findTool( name );
  if ( !toolOpt.has_value() )
  {
    Json::Value errorResult( Json::objectValue );
    errorResult["status"] = "error";
    errorResult["errorMessage"] = "Interaction tool not registered: " + name;
    return errorResult;
  }

  if ( !toolOpt->handler )
  {
    Json::Value errorResult( Json::objectValue );
    errorResult["status"] = "error";
    errorResult["errorMessage"] = "Interaction tool has no handler configured: " + name;
    return errorResult;
  }

  return toolOpt->handler( parameters );
}

void InteractionToolRegistry::registerBuiltinTools( ViewControlService *service, RasterDisplayService *rasterService, sicnu::data::DataManager *dataManager )
{
  if ( service )
  {

    QPointer<ViewControlService> safeService( service );

  // 1. view:get_state
  {
    InteractionToolDefinition def;
    def.name = "view:get_state";
    def.displayName = "Get Map View State";
    def.category = "view";
    def.description = "Query the current active map view state, including CRS, extent bounding box, scale, rotation, and active layer.";
    def.inputSchema = createViewStateSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->getState( params );
    };
    registerTool( std::move( def ) );
  }

  // 2. view:set_extent
  {
    InteractionToolDefinition def;
    def.name = "view:set_extent";
    def.displayName = "Set Map Extent";
    def.category = "view";
    def.description = "Set the spatial extent / bounding box of the map canvas (via bbox object, extent array, or WKT geometry).";
    def.inputSchema = createSetExtentSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->setExtent( params );
    };
    registerTool( std::move( def ) );
  }

  // 3. view:zoom_to_layer
  {
    InteractionToolDefinition def;
    def.name = "view:zoom_to_layer";
    def.displayName = "Zoom to Layer";
    def.category = "view";
    def.description = "Zoom the map canvas to the spatial extent of a specified layer by ID or layer name.";
    def.inputSchema = createZoomToLayerSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->zoomToLayer( params );
    };
    registerTool( std::move( def ) );
  }

  // 4. view:zoom_to_asset
  {
    InteractionToolDefinition def;
    def.name = "view:zoom_to_asset";
    def.displayName = "Zoom to Asset";
    def.category = "view";
    def.description = "Zoom the map canvas to the spatial extent of a registered Data Manager asset.";
    def.inputSchema = createZoomToAssetSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->zoomToAsset( params );
    };
    registerTool( std::move( def ) );
  }

  // 5. view:fit_all
  {
    InteractionToolDefinition def;
    def.name = "view:fit_all";
    def.displayName = "Fit All Layers";
    def.category = "view";
    def.description = "Zoom the map canvas to fit the full bounding extent of all loaded and visible layers.";
    def.inputSchema = createEmptyObjectSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->fitAll( params );
    };
    registerTool( std::move( def ) );
  }

  // 5b. canvas:zoom_to_extent
  {
    InteractionToolDefinition def;
    def.name = "canvas:zoom_to_extent";
    def.displayName = "Zoom to Extent";
    def.category = "canvas";
    def.description = "Zoom the map canvas to a specific spatial extent [xmin, ymin, xmax, ymax].";
    def.inputSchema = createSetExtentSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->setExtent( params );
    };
    registerTool( std::move( def ) );
  }

  // 6. view:set_scale
  {
    InteractionToolDefinition def;
    def.name = "view:set_scale";
    def.displayName = "Set Map Scale";
    def.category = "view";
    def.description = "Set the map canvas scale denominator (e.g. 50000 for 1:50000 scale).";
    def.inputSchema = createSetScaleSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->setScale( params );
    };
    registerTool( std::move( def ) );
  }

  // 7. roi:set
  {
    InteractionToolDefinition def;
    def.name = "roi:set";
    def.displayName = "Set Canvas ROI";
    def.category = "roi";
    def.description = "Draw or set a Region of Interest (ROI) polygon or bounding box on the map canvas.";
    def.inputSchema = createRoiSetSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->setRoi( params );
    };
    registerTool( std::move( def ) );
  }

  // 8. roi:clear
  {
    InteractionToolDefinition def;
    def.name = "roi:clear";
    def.displayName = "Clear Canvas ROI";
    def.category = "roi";
    def.description = "Clear the active Region of Interest (ROI) rubberband from the map canvas.";
    def.inputSchema = createEmptyObjectSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->clearRoi( params );
    };
    registerTool( std::move( def ) );
  }

  // 9. canvas:draw_roi (backward compatibility alias)
  {
    InteractionToolDefinition def;
    def.name = "canvas:draw_roi";
    def.displayName = "Draw ROI (Legacy)";
    def.category = "canvas";
    def.description = "Draw a Region of Interest on the map canvas (backward compatibility alias for roi:set).";
    def.inputSchema = createRoiSetSchema();
    def.handler = [safeService]( const Json::Value &params ) {
      if ( !safeService ) return serviceUnavailableError();
      return safeService->setRoi( params );
    };
    registerTool( std::move( def ) );
  }
  }

  if ( rasterService )
  {
    registerRasterTools( rasterService );
  }

  registerDataTools( dataManager );
}

void InteractionToolRegistry::registerDataTools( sicnu::data::DataManager *dataManager )
{
  // 1. data:list_layers
  {
    InteractionToolDefinition def;
    def.name = "data:list_layers";
    def.displayName = "List Loaded Map Layers";
    def.category = "data";
    def.description = "List all raster and vector layers currently loaded in the project and map canvas with layer IDs, names, types, and CRS.";
    def.inputSchema = createEmptyObjectSchema();
    def.handler = []( const Json::Value & ) {
      Json::Value result( Json::objectValue );
      Json::Value layers( Json::arrayValue );
      if ( QgsProject::instance() )
      {
        QMap<QString, QgsMapLayer *> mapLayers = QgsProject::instance()->mapLayers();
        for ( auto it = mapLayers.begin(); it != mapLayers.end(); ++it )
        {
          QgsMapLayer *layer = it.value();
          if ( !layer ) continue;
          Json::Value l( Json::objectValue );
          l["id"] = layer->id().toStdString();
          l["name"] = layer->name().toStdString();
          l["type"] = (layer->type() == Qgis::LayerType::Raster) ? "raster" : "vector";
          l["source"] = layer->source().toStdString();
          l["crs"] = layer->crs().authid().toStdString();
          layers.append( l );
        }
      }
      result["layers"] = layers;
      result["status"] = "success";
      return result;
    };
    registerTool( std::move( def ) );
  }

  // 2. data:describe_dataset
  {
    InteractionToolDefinition def;
    def.name = "data:describe_dataset";
    def.displayName = "Describe Dataset Metadata";
    def.category = "data";
    def.description = "Get detailed dataset metadata for a layer or file: dimensions, bounding box, CRS, resolution, data types, and band/attribute information.";
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    Json::Value layerId( Json::objectValue );
    layerId["type"] = "string";
    layerId["description"] = "Layer ID, layer name, or file path to describe";
    props["layer_id"] = layerId;
    schema["properties"] = props;
    Json::Value req( Json::arrayValue );
    req.append( "layer_id" );
    schema["required"] = req;
    def.inputSchema = schema;

    def.handler = []( const Json::Value &params ) {
      std::string targetId;
      if ( params.isMember( "layer_id" ) && params["layer_id"].isString() )
        targetId = params["layer_id"].asString();
      else if ( params.isMember( "layer" ) && params["layer"].isString() )
        targetId = params["layer"].asString();
      else if ( params.isMember( "dataset" ) && params["dataset"].isString() )
        targetId = params["dataset"].asString();

      QString qTarget = QString::fromStdString( targetId );
      QgsMapLayer *layer = nullptr;
      if ( QgsProject::instance() )
      {
        layer = QgsProject::instance()->mapLayer( qTarget );
        if ( !layer )
        {
          QList<QgsMapLayer *> layers = QgsProject::instance()->mapLayersByName( qTarget );
          if ( !layers.isEmpty() )
            layer = layers.first();
        }
      }

      if ( !layer )
      {
        Json::Value err( Json::objectValue );
        err["status"] = "error";
        err["errorMessage"] = "Layer not found: " + targetId;
        return err;
      }

      Json::Value result( Json::objectValue );
      result["id"] = layer->id().toStdString();
      result["name"] = layer->name().toStdString();
      result["type"] = (layer->type() == Qgis::LayerType::Raster) ? "raster" : "vector";
      result["crs"] = layer->crs().authid().toStdString();

      QgsRectangle extent = layer->extent();
      Json::Value ext( Json::objectValue );
      ext["xmin"] = extent.xMinimum();
      ext["ymin"] = extent.yMinimum();
      ext["xmax"] = extent.xMaximum();
      ext["ymax"] = extent.yMaximum();
      result["extent"] = ext;

      if ( layer->type() == Qgis::LayerType::Raster )
      {
        QgsRasterLayer *raster = qobject_cast<QgsRasterLayer *>( layer );
        if ( raster && raster->dataProvider() )
        {
          QgsRasterDataProvider *provider = raster->dataProvider();
          result["width"] = raster->width();
          result["height"] = raster->height();
          result["band_count"] = provider->bandCount();

          GdalDatasetWrapper ds;
          const bool rolesAvailable = ds.open( raster->source() );

          Json::Value bands( Json::arrayValue );
          for ( int i = 1; i <= provider->bandCount(); ++i )
          {
            Json::Value b( Json::objectValue );
            b["index"] = i;
            b["color_interpretation"] = provider->colorInterpretationName( i ).toStdString();
            b["has_nodata"] = provider->sourceHasNoDataValue( i );
            if ( provider->sourceHasNoDataValue( i ) )
              b["nodata_value"] = provider->sourceNoDataValue( i );
            if ( rolesAvailable )
              b["role"] = ds.bandMetadataItem( i, "SICNU_BAND_ROLE" ).toStdString();
            bands.append( b );
          }
          result["bands"] = bands;
        }
      }
      else if ( layer->type() == Qgis::LayerType::Vector )
      {
        QgsVectorLayer *vector = qobject_cast<QgsVectorLayer *>( layer );
        if ( vector )
        {
          result["feature_count"] = static_cast<Json::Int64>( vector->featureCount() );
          QString geomType = QStringLiteral( "Unknown" );
          switch ( vector->geometryType() )
          {
            case Qgis::GeometryType::Point: geomType = QStringLiteral( "Point" ); break;
            case Qgis::GeometryType::Line: geomType = QStringLiteral( "Line" ); break;
            case Qgis::GeometryType::Polygon: geomType = QStringLiteral( "Polygon" ); break;
            case Qgis::GeometryType::Null: geomType = QStringLiteral( "Null" ); break;
            default: break;
          }
          result["geometry_type"] = geomType.toStdString();

          Json::Value fields( Json::arrayValue );
          QgsFields layerFields = vector->fields();
          for ( int i = 0; i < layerFields.count(); ++i )
          {
            Json::Value f( Json::objectValue );
            f["name"] = layerFields.at( i ).name().toStdString();
            f["type"] = layerFields.at( i ).typeName().toStdString();
            fields.append( f );
          }
          result["fields"] = fields;
        }
      }
      result["status"] = "success";
      return result;
    };
    registerTool( std::move( def ) );
  }

  // 3. data:get_lineage
  {
    InteractionToolDefinition def;
    def.name = "data:get_lineage";
    def.displayName = "Get Asset Lineage and Provenance";
    def.category = "data";
    def.description = "Query the processing provenance and derivation history for a DataManager asset: source inputs, algorithm, parameters, task reference, and downstream outputs.";
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    Json::Value assetId( Json::objectValue );
    assetId["type"] = "string";
    assetId["description"] = "UUID string of the DataManager asset";
    props["asset_id"] = assetId;
    schema["properties"] = props;
    Json::Value req( Json::arrayValue );
    req.append( "asset_id" );
    schema["required"] = req;
    def.inputSchema = schema;

    def.handler = [dataManager]( const Json::Value &params ) {
      if ( !dataManager )
      {
        Json::Value err( Json::objectValue );
        err["status"] = "error";
        err["errorMessage"] = "Data manager is not available";
        return err;
      }
      std::string assetIdText = params.isMember( "asset_id" ) ? params["asset_id"].asString() : "";
      const auto id = sicnu::data::AssetId::fromString( QString::fromStdString( assetIdText ) );
      if ( !id )
      {
        Json::Value err( Json::objectValue );
        err["status"] = "error";
        err["errorMessage"] = "Invalid asset id: " + assetIdText;
        return err;
      }
      const auto snapshot = dataManager->asset( *id );
      if ( !snapshot )
      {
        Json::Value err( Json::objectValue );
        err["status"] = "error";
        err["errorMessage"] = "Asset not found: " + assetIdText;
        return err;
      }

      Json::Value result( Json::objectValue );
      result["id"] = snapshot->id().toString().toStdString();
      result["name"] = snapshot->displayName().toStdString();
      result["source"] = snapshot->source().canonicalSource.toStdString();

      if ( const auto prov = dataManager->provenance( *id ) )
      {
        result["provenance"] = sicnu::processing::variantToJsonValue( prov->toJson().toVariantMap() );
      }

      Json::Value inputs( Json::arrayValue );
      for ( const auto &inputId : dataManager->derivedFrom( *id ) )
      {
        Json::Value entry( Json::objectValue );
        entry["id"] = inputId.toString().toStdString();
        if ( const auto inputSnapshot = dataManager->asset( inputId ) )
          entry["name"] = inputSnapshot->displayName().toStdString();
        inputs.append( entry );
      }
      result["derivedFrom"] = inputs;

      Json::Value outputs( Json::arrayValue );
      for ( const auto &outputId : dataManager->derivedOutputsOf( *id ) )
      {
        Json::Value entry( Json::objectValue );
        entry["id"] = outputId.toString().toStdString();
        if ( const auto outputSnapshot = dataManager->asset( outputId ) )
          entry["name"] = outputSnapshot->displayName().toStdString();
        outputs.append( entry );
      }
      result["derivedOutputsOf"] = outputs;
      result["status"] = "success";
      return result;
    };
    registerTool( std::move( def ) );
  }
}

void InteractionToolRegistry::registerRasterTools( RasterDisplayService *service )
{
  if ( !service )
    return;

  QPointer<RasterDisplayService> safeRaster( service );

  // 1. raster:get_display
  {
    InteractionToolDefinition def;
    def.name = "raster:get_display";
    def.displayName = "Get Raster Display Properties";
    def.category = "raster";
    def.description = "Get the current raster layer display configuration including renderer type, band composition, stretch algorithm/range, and opacity.";
    def.inputSchema = createRasterGetDisplaySchema();
    def.handler = [safeRaster]( const Json::Value &params ) {
      if ( !safeRaster ) return serviceUnavailableError();
      return safeRaster->getDisplay( params );
    };
    registerTool( std::move( def ) );
  }

  // 2. raster:set_band_composite
  {
    InteractionToolDefinition def;
    def.name = "raster:set_band_composite";
    def.displayName = "Set Raster Band Composite";
    def.category = "raster";
    def.description = "Set RGB band composition for a raster layer using semantic band roles (e.g. 'red', 'green', 'blue', 'nir', 'swir1', 'swir2') or 1-based band numbers.";
    def.inputSchema = createRasterSetBandCompositeSchema();
    def.handler = [safeRaster]( const Json::Value &params ) {
      if ( !safeRaster ) return serviceUnavailableError();
      return safeRaster->setBandComposite( params );
    };
    registerTool( std::move( def ) );
  }

  // 3. raster:set_stretch
  {
    InteractionToolDefinition def;
    def.name = "raster:set_stretch";
    def.displayName = "Set Raster Display Stretch";
    def.category = "raster";
    def.description = "Adjust the display contrast stretch of a raster layer. Supports 'minimum_maximum', 'percent_clip' (with lower/upper percentiles), and 'stddev' (with factor/k).";
    def.inputSchema = createRasterSetStretchSchema();
    def.handler = [safeRaster]( const Json::Value &params ) {
      if ( !safeRaster ) return serviceUnavailableError();
      return safeRaster->setStretch( params );
    };
    registerTool( std::move( def ) );
  }

  // 4. raster:reset_display
  {
    InteractionToolDefinition def;
    def.name = "raster:reset_display";
    def.displayName = "Reset Raster Display";
    def.category = "raster";
    def.description = "Reset a raster layer's display presentation to its default renderer, standard RGB/grayscale bands, default stretch, and full opacity.";
    def.inputSchema = createRasterResetDisplaySchema();
    def.handler = [safeRaster]( const Json::Value &params ) {
      if ( !safeRaster ) return serviceUnavailableError();
      return safeRaster->resetDisplay( params );
    };
    registerTool( std::move( def ) );
  }
}

Json::Value InteractionToolRegistry::exportOpenAiToolDefinitions() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  Json::Value toolsArray( Json::arrayValue );

  for ( const auto &[name, def] : m_tools )
  {
    Json::Value toolObj( Json::objectValue );
    toolObj["type"] = "function";

    Json::Value funcObj( Json::objectValue );
    // Normalize colon to underscore for OpenAI strict name compliance
    std::string funcName = def.name;
    std::replace( funcName.begin(), funcName.end(), ':', '_' );
    funcObj["name"] = funcName;
    funcObj["description"] = def.description;
    funcObj["parameters"] = def.inputSchema.isObject() ? def.inputSchema : createEmptyObjectSchema();

    toolObj["function"] = funcObj;
    toolsArray.append( toolObj );
  }

  return toolsArray;
}

std::string InteractionToolRegistry::exportSystemPromptCatalog() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::ostringstream ss;
  ss << "| Category | Tool Name | Description |\n";
  ss << "| :--- | :--- | :--- |\n";

  for ( const auto &[name, def] : m_tools )
  {
    ss << "| `" << def.category << "` | `" << def.name << "` | " << def.description << " |\n";
  }

  return ss.str();
}

} // namespace sicnu::agent
