// src/agent/spatial_tools/vector_inspect_tool.cpp
#include "vector_inspect_tool.h"

#include <ogr_api.h>
#include <ogrsf_frmts.h>

#include <QFileInfo>

#include "qgsdatasourceresolver.h"

#include <cstring>
#include <string>

namespace sicnu::agent::spatial_tools {

namespace {

std::string fieldTypeName( OGRFieldType type )
{
  switch ( type )
  {
    case OFTInteger: return "integer";
    case OFTInteger64: return "integer64";
    case OFTReal: return "real";
    case OFTString: return "string";
    case OFTDate: return "date";
    case OFTTime: return "time";
    case OFTDateTime: return "datetime";
    case OFTBinary: return "binary";
    default: return "other";
  }
}

Json::Value describeLayer( OGRLayer *layer, int maxFeatures )
{
  Json::Value out( Json::objectValue );
  out["name"] = layer->GetName();

  out["geometryType"] = OGRGeometryTypeToName( layer->GetGeomType() );
  const GIntBig featureCount = layer->GetFeatureCount();
  if ( featureCount >= 0 )
    out["featureCount"] = static_cast<Json::Int64>( featureCount );
  else
    out["featureCount"] = "unknown";

  OGREnvelope extent;
  if ( layer->GetExtent( &extent, /*force=*/true ) == OGRERR_NONE )
  {
    Json::Value ext( Json::objectValue );
    ext["minX"] = extent.MinX;
    ext["minY"] = extent.MinY;
    ext["maxX"] = extent.MaxX;
    ext["maxY"] = extent.MaxY;
    out["extent"] = ext;
  }

  if ( const OGRSpatialReference *srs = layer->GetSpatialRef() )
  {
    const char *authid = srs->GetAuthorityName( nullptr );
    const char *code = srs->GetAuthorityCode( nullptr );
    if ( authid && code )
      out["crs"] = std::string( authid ) + ":" + code;
    else
      out["crs"] = srs->GetName() ? srs->GetName() : "";
  }

  Json::Value fields( Json::arrayValue );
  const OGRFeatureDefn *defn = layer->GetLayerDefn();
  for ( int i = 0; i < defn->GetFieldCount(); ++i )
  {
    const OGRFieldDefn *field = defn->GetFieldDefn( i );
    Json::Value f( Json::objectValue );
    f["name"] = field->GetNameRef();
    f["type"] = fieldTypeName( field->GetType() );
    f["width"] = field->GetWidth();
    fields.append( f );
  }
  out["fields"] = fields;

  if ( maxFeatures > 0 )
  {
    Json::Value features( Json::arrayValue );
    layer->ResetReading();
    while ( OGRFeature *feature = layer->GetNextFeature() )
    {
      Json::Value f( Json::objectValue );
      f["fid"] = static_cast<Json::Int64>( feature->GetFID() );

      Json::Value attrs( Json::objectValue );
      for ( int i = 0; i < feature->GetFieldCount(); ++i )
      {
        const char *name = feature->GetFieldDefnRef( i )->GetNameRef();
        if ( !feature->IsFieldSetAndNotNull( i ) )
          continue;
        switch ( feature->GetFieldDefnRef( i )->GetType() )
        {
          case OFTInteger:
            attrs[name] = feature->GetFieldAsInteger( i );
            break;
          case OFTInteger64:
            attrs[name] = static_cast<Json::Int64>( feature->GetFieldAsInteger64( i ) );
            break;
          case OFTReal:
            attrs[name] = feature->GetFieldAsDouble( i );
            break;
          default:
            attrs[name] = feature->GetFieldAsString( i );
            break;
        }
      }
      f["attributes"] = attrs;

      if ( const OGRGeometry *geom = feature->GetGeometryRef() )
      {
        // exportToJson(options) RETURNS the GeoJSON string (CPLFree after use).
        char *geojson = geom->exportToJson();
        if ( geojson )
        {
          f["geometryJson"] = geojson;
          CPLFree( geojson );
        }
      }
      features.append( f );
      OGRFeature::DestroyFeature( feature );
      if ( features.size() >= static_cast<Json::ArrayIndex>( maxFeatures ) )
        break;
    }
    out["sampleFeatures"] = features;
  }

  return out;
}

} // namespace

std::string VectorInspectTool::description() const
{
  return "Inspect a vector dataset (Shapefile, GeoPackage, GeoJSON, any "
         "OGR-readable source): layers, geometry types, feature counts, "
         "extents, CRS, field schemas, and optional sampled features with "
         "attributes and GeoJSON geometries. Use this to understand vector "
         "inputs before geometry operations, spatial queries, or exports.";
}

std::vector<std::string> VectorInspectTool::tags() const
{
  return { "spatial", "vector", "metadata", "inspection", "layers", "fields", "features", "geojson" };
}

Json::Value VectorInspectTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value path( Json::objectValue );
  path["type"] = "string";
  path["description"] = "Path or OGR connection string of the vector dataset";
  props["path"] = path;

  Json::Value layer( Json::objectValue );
  layer["type"] = "string";
  layer["description"] = "Layer name to inspect (default: first layer)";
  props["layer"] = layer;

  Json::Value maxFeatures( Json::objectValue );
  maxFeatures["type"] = "integer";
  maxFeatures["description"] = "Sample this many features per layer with attributes/geometry (default 0 = schema only)";
  maxFeatures["default"] = 0;
  props["max_features"] = maxFeatures;

  schema["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "path" );
  schema["required"] = required;
  return schema;
}

Json::Value VectorInspectTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value driver( Json::objectValue );
  driver["type"] = "string";
  props["driver"] = driver;

  Json::Value layers( Json::objectValue );
  layers["type"] = "array";
  props["layers"] = layers;

  schema["properties"] = props;
  return schema;
}

SpatialToolResult VectorInspectTool::execute( const Json::Value &input )
{
  const std::string path = input.isMember( "path" ) ? input["path"].asString() : std::string();
  if ( path.empty() )
    return SpatialToolResult::failure( "Missing required parameter: path" );

  const std::string layerName = input.isMember( "layer" ) ? input["layer"].asString() : std::string();
  const int maxFeatures = input.isMember( "max_features" ) && input["max_features"].isNumeric()
                              ? input["max_features"].asInt()
                              : 0;

<  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( QString::fromStdString( path ) ) && !QFileInfo::exists( QString::fromStdString( path ) ) )
    return SpatialToolResult::failure( "Vector file not found: " + path, "local_file_not_found", "io", false );

  GDALDatasetUniquePtr ds( GDALDataset::Open( path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY ) );
  if ( !ds )
    return SpatialToolResult::failure( "OGR could not open vector dataset: " + path, "provider_open_failed", "io", false );

  Json::Value out( Json::objectValue );
  out["path"] = path;
  out["driver"] = ds->GetDriverName();
  out["layerCount"] = ds->GetLayerCount();

  Json::Value layers( Json::arrayValue );
  if ( !layerName.empty() )
  {
    OGRLayer *layer = ds->GetLayerByName( layerName.c_str() );
    if ( !layer )
      return SpatialToolResult::failure( "Layer not found: " + layerName );
    layers.append( describeLayer( layer, maxFeatures ) );
  }
  else
  {
    for ( int i = 0; i < ds->GetLayerCount(); ++i )
    {
      OGRLayer *layer = ds->GetLayer( i );
      if ( layer )
        layers.append( describeLayer( layer, maxFeatures ) );
    }
  }
  out["layers"] = layers;

  return SpatialToolResult::ok( out );
}

} // namespace sicnu::agent::spatial_tools
