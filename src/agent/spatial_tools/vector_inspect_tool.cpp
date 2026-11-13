// src/agent/spatial_tools/vector_inspect_tool.cpp
#include "vector_inspect_tool.h"

#include <ogr_api.h>
#include <ogrsf_frmts.h>

#include <QFileInfo>

#include "qgsdatasourceresolver.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

namespace sicnu::agent::spatial_tools {

namespace {

// asInt() throws outside int. In-range fractions still truncate toward zero.
bool jsonIntIfFits( const Json::Value &value, int &out )
{
  if ( value.isInt() )
  {
    out = value.asInt();
    return true;
  }
  if ( !value.isNumeric() )
    return false;
  const double number = value.asDouble();
  if ( !std::isfinite( number ) ||
       number < static_cast<double>( std::numeric_limits<int>::min() ) ||
       number > static_cast<double>( std::numeric_limits<int>::max() ) )
    return false;
  out = static_cast<int>( number );
  return true;
}

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
  // Cheap count only (#701): the default bForce=TRUE fully parses the file
  // (GeoJSON: every feature) inline on the serialized MCP main loop — the
  // same bounded-cost violation the extent path fixed in #644.
  const GIntBig featureCount = layer->GetFeatureCount( /*bForce=*/FALSE );
  if ( featureCount >= 0 )
    out["featureCount"] = static_cast<Json::Int64>( featureCount );
  else
    out["featureCount"] = "unknown";

  // Prefer the driver-stored extent: GetExtent(force=true) scans every
  // feature on the calling (MCP) thread for drivers without a stored extent
  // (CSV, GeoJSON, ...), which stalls the tool's bounded-cost contract (#644).
  // swarm-review AG-C-5: that force=true fallback was itself an unbounded
  // full-file scan — replace it with a capped manual scan. A layer that
  // outlives the cap reports extent="unknown" with the reason, the same
  // honest contract featureCount already follows.
  constexpr GIntBig kExtentScanCap = 100000;
  OGREnvelope extent;
  bool haveExtent = layer->GetExtent( &extent, /*force=*/false ) == OGRERR_NONE;
  bool extentCapped = false;
  if ( !haveExtent )
  {
    OGREnvelope scanned;
    bool any = false;
    GIntBig scannedFeatures = 0;
    layer->ResetReading();
    while ( OGRFeature *feature = layer->GetNextFeature() )
    {
      if ( const OGRGeometry *geom = feature->GetGeometryRef() )
      {
        OGREnvelope geomEnvelope;
        geom->getEnvelope( &geomEnvelope );
        if ( !any )
        {
          scanned = geomEnvelope;
          any = true;
        }
        else
        {
          scanned.Merge( geomEnvelope );
        }
      }
      OGRFeature::DestroyFeature( feature );
      if ( ++scannedFeatures >= kExtentScanCap )
        break;
    }
    if ( any )
    {
      // "cap hit" vs "layer exhausted": one more read decides. Consuming the
      // probe feature is safe — the sampling loop below resets reading anyway.
      if ( OGRFeature *probe = layer->GetNextFeature() )
      {
        OGRFeature::DestroyFeature( probe );
        extentCapped = true;
      }
      else
      {
        extent = scanned;
        haveExtent = true;
      }
    }
  }
  if ( haveExtent )
  {
    Json::Value ext( Json::objectValue );
    ext["minX"] = extent.MinX;
    ext["minY"] = extent.MinY;
    ext["maxX"] = extent.MaxX;
    ext["maxY"] = extent.MaxY;
    out["extent"] = ext;
  }
  else if ( extentCapped )
  {
    out["extent"] = "unknown";
    out["extent_reason"] = "extent scan capped at " + std::to_string( kExtentScanCap ) +
                           " features; use a format with a stored extent for the full bbox";
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
          // Embed as a JSON object, not an escaped string — assigning the
          // raw text would double-encode (every quote/backslash escaped) and
          // cost ~2x bytes per feature. Raw string only as parse fallback.
          Json::Value geomVal;
          std::string parseErr;
          const std::unique_ptr<Json::CharReader> reader(
            Json::CharReaderBuilder{}.newCharReader() );
          if ( reader->parse( geojson, geojson + std::strlen( geojson ), &geomVal, &parseErr ) )
            f["geometry"] = std::move( geomVal );
          else
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
  maxFeatures["description"] = "Sample this many features per layer with attributes/geometry "
                                "(default 0 = schema only; capped at 50)";
  maxFeatures["default"] = 0;
  maxFeatures["maximum"] = 50;
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
  // Hard cap (#620): max_features drives per-layer full-attribute sampling
  // whose result is serialized into ONE stdio line - an unbounded value
  // (e.g. 1e6 over many layers) exhausted memory and response size on the
  // main thread.
  constexpr int kMaxFeaturesCap = 50;
  // 0 means schema only (no feature sampling), which is already bounded.
  int maxFeatures = 0;
  if ( input.isMember( "max_features" ) )
    jsonIntIfFits( input["max_features"], maxFeatures );
  if ( maxFeatures > kMaxFeaturesCap )
    maxFeatures = kMaxFeaturesCap;
  if ( maxFeatures < 0 )
    maxFeatures = 0;

  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( QString::fromStdString( path ) )
       && !QFileInfo::exists( QgsDataSourceResolver::localPath( QString::fromStdString( path ) ) ) )
    return SpatialToolResult::failure( "Vector file not found: " + path, "local_file_not_found", "io", false );

  // file:// inputs are opened through their local mapping (#1467).
  const std::string openPath = QgsDataSourceResolver::localPath( QString::fromStdString( path ) ).toStdString();
  GDALDatasetUniquePtr ds( GDALDataset::Open( openPath.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY ) );
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
