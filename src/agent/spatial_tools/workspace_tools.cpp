// src/agent/spatial_tools/workspace_tools.cpp
#include "workspace_tools.h"

#include "../workspace_state.h"
#include "data/asset_types.h"
#include "data/band_role.h"
#include "data/data_manager.h"

#include <QFileInfo>

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsfields.h>
#include <qgsfield.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <variant>

namespace sicnu::agent::spatial_tools {

using sicnu::agent::AgentServices;

namespace {

/// Resolves a target (workspace entity id, asset id, canonical path, or
/// display name) to a project layer. Null when unresolved.
QgsMapLayer *resolveLayer( const QString &target )
{
  if ( target.isEmpty() || !QgsProject::instance() )
    return nullptr;
  // 1. Workspace entity id → natural key (layer uuid).
  const QString naturalKey =
    sicnu::agent::WorkspaceEntityRegistry::instance().naturalKeyFor( target );
  if ( !naturalKey.isEmpty() )
  {
    QgsMapLayer *layer = QgsProject::instance()->mapLayer( naturalKey );
    if ( layer )
      return layer;
  }
  // 2. Direct layer uuid.
  if ( QgsMapLayer *layer = QgsProject::instance()->mapLayer( target ) )
    return layer;
  // 3. Display name.
  for ( auto it = QgsProject::instance()->mapLayers().constBegin();
        it != QgsProject::instance()->mapLayers().constEnd(); ++it )
  {
    if ( it.value() && it.value()->name() == target )
      return it.value();
  }
  return nullptr;
}

/// Resolves a target to a catalog asset snapshot via AgentServices.
std::optional<sicnu::data::AssetSnapshot> resolveAsset( const QString &target )
{
  sicnu::data::DataManager *dataManager = AgentServices::instance().dataManager();
  if ( !dataManager || target.isEmpty() )
    return std::nullopt;
  for ( const auto &asset : dataManager->assets() )
  {
    if ( asset.source().canonicalSource == target || asset.displayName() == target ||
         asset.id().toString() == target )
      return asset;
  }
  return std::nullopt;
}

class WorkspaceSummaryTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:workspace_summary"; }
    std::string displayName() const override { return "Workspace summary"; }
    std::string description() const override
    {
      return "Structured WorkspaceState: project, view (CRS/extent/scale), assets, layers with "
             "stable entity ids (asset-1, layer-2 …), temporal collections, layouts, charts, "
             "models, running tasks, recent outputs, and workflow runs. Use these ids as "
             "referents in every later tool call instead of guessing names.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "workspace", "state", "summary", "entities" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      limit["description"] = "Max recent outputs to include (default 8, max 50)";
      props["recent_outputs_limit"] = limit;
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["schema_version"] = Json::Value( Json::objectValue );
      schema["properties"]["kind"] = Json::Value( Json::objectValue );
      schema["properties"]["assets"] = Json::Value( Json::objectValue );
      schema["properties"]["layers"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const int limit = input.isMember( "recent_outputs_limit" ) && input["recent_outputs_limit"].isInt()
                          ? input["recent_outputs_limit"].asInt()
                          : 8;
      return SpatialToolResult::ok( sicnu::agent::buildWorkspaceState(
        AgentServices::instance().dataManager(), AgentServices::instance().mapCanvas(), {},
        limit ) );
    }
};

class LayerSummaryTool final : public SpatialTool
{
  public:
    std::string name() const override { return "spatial:layer_summary"; }
    std::string displayName() const override { return "Layer summary"; }
    std::string description() const override
    {
      return "Bounded deep-dive on one workspace entity: resolve a workspace id (asset-3, "
             "layer-1), asset id, path, or layer name; return kind, dimensions, band roles, "
             "CRS, extent, nodata, renderer summary (raster) or feature count, geometry type, "
             "fields (vector, capped). Read-only; use before symbology or algorithm calls.";
    }
    std::vector<std::string> tags() const override
    {
      return { "spatial", "layer", "inspect", "summary", "fields" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value target( Json::objectValue );
      target["type"] = "string";
      target["description"] = "Workspace entity id, asset id, path, or layer name";
      props["target"] = target;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "target" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["kind"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString target = requireStringField( input, "target", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      Json::Value out( Json::objectValue );
      out["target"] = target.toStdString();

      // Catalog asset first (works headless), then project layer.
      if ( auto asset = resolveAsset( target ) )
      {
        out["resolved"] = "asset";
        out["entity_id"] =
          sicnu::agent::WorkspaceEntityRegistry::instance()
            .idFor( QStringLiteral( "asset" ), asset->source().canonicalSource )
            .toStdString();
        out["asset_id"] = asset->id().toString().toStdString();
        out["revision"] = static_cast<Json::UInt64>( asset->revision().value() );
        out["name"] = asset->displayName().toStdString();
        out["path"] = asset->source().canonicalSource.toStdString();
        const auto &structure = asset->structure();
        if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &structure ) )
        {
          out["kind"] = "raster";
          out["width"] = raster->width;
          out["height"] = raster->height;
          out["band_count"] = raster->bandCount;
          if ( raster->extent.valid )
          {
            Json::Value ext( Json::objectValue );
            ext["xmin"] = raster->extent.minimumX;
            ext["ymin"] = raster->extent.minimumY;
            ext["xmax"] = raster->extent.maximumX;
            ext["ymax"] = raster->extent.maximumY;
            out["extent"] = ext;
          }
          Json::Value bands( Json::arrayValue );
          int i = 0;
          for ( const auto &band : raster->bands )
          {
            Json::Value b( Json::objectValue );
            b["index"] = band.number > 0 ? band.number : ++i;
            b["role"] = band.role == sicnu::data::BandRole::Unknown
                          ? ""
                          : sicnu::data::bandRoleToString( band.role ).toStdString();
            if ( band.noDataValue )
              b["nodata"] = *band.noDataValue;
            bands.append( b );
          }
          out["bands"] = bands;
        }
        else if ( const auto *vector = std::get_if<sicnu::data::VectorStructure>( &structure ) )
        {
          out["kind"] = "vector";
          out["layer_count"] = vector->layerCount;
          Json::Value layers( Json::arrayValue );
          int capped = 0;
          for ( const auto &layer : vector->layers )
          {
            if ( capped++ >= 16 )
              break;
            Json::Value l( Json::objectValue );
            l["name"] = layer.name.toStdString();
            l["feature_count"] = static_cast<Json::Int64>( layer.featureCount );
            l["geometry_type"] = layer.geometryType.toStdString();
            layers.append( l );
          }
          out["layers"] = layers;
        }
        return SpatialToolResult::ok( out );
      }

      if ( QgsMapLayer *layer = resolveLayer( target ) )
      {
        out["resolved"] = "layer";
        out["entity_id"] =
          sicnu::agent::WorkspaceEntityRegistry::instance()
            .idFor( QStringLiteral( "layer" ), layer->uuid() )
            .toStdString();
        out["name"] = layer->name().toStdString();
        out["crs"] = layer->crs().authid().toStdString();
        out["valid"] = layer->isValid();
        const QgsRectangle extent = layer->extent();
        if ( !extent.isEmpty() && !extent.isNull() )
        {
          Json::Value ext( Json::objectValue );
          ext["xmin"] = extent.xMinimum();
          ext["ymin"] = extent.yMinimum();
          ext["xmax"] = extent.xMaximum();
          ext["ymax"] = extent.yMaximum();
          out["extent"] = ext;
        }
        if ( auto *raster = qobject_cast<QgsRasterLayer *>( layer ) )
        {
          out["kind"] = "raster";
          out["width"] = raster->width();
          out["height"] = raster->height();
          out["band_count"] = raster->bandCount();
        }
        else if ( auto *vector = qobject_cast<QgsVectorLayer *>( layer ) )
        {
          out["kind"] = "vector";
          out["feature_count"] = static_cast<Json::Int64>( vector->featureCount() );
          out["geometry_type"] = vector->geometryTypeString().toStdString();
          out["selected_features"] = vector->selectedFeatureCount();
          Json::Value fields( Json::arrayValue );
          const int fieldLimit = std::min( vector->fields().count(), 64 );
          for ( int i = 0; i < fieldLimit; ++i )
          {
            Json::Value f( Json::objectValue );
            f["name"] = vector->fields().at( i ).name().toStdString();
            f["type"] = vector->fields().at( i ).typeName().toStdString();
            fields.append( f );
          }
          out["fields"] = fields;
          out["fields_truncated"] = vector->fields().count() > 64;
        }
        return SpatialToolResult::ok( out );
      }

      return SpatialToolResult::failure( "Cannot resolve target: " + target.toStdString(),
                                         "NOT_FOUND", "validation", false );
    }
};

} // namespace

void registerWorkspaceTools()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<WorkspaceSummaryTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<LayerSummaryTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::spatial_tools
