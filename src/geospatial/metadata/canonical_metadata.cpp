/***************************************************************************
  geospatial/metadata/canonical_metadata.cpp
  Geospatial I/O Foundation 4.0 — canonical metadata model implementation.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/metadata/canonical_metadata.h"

#include "geospatial/gdal_guard.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>

namespace sicnu::geo
{
namespace
{

constexpr int kMaxMetadataItemsFallback = 128;
constexpr int kMaxSubdatasetEntriesFallback = 128; // entries incl. name+desc pairs

std::string gdalDataTypeName( GDALDataType type )
{
  const char *name = GDALGetDataTypeName( type );
  return name ? std::string( name ) : std::string( "Unknown" );
}

void captureBoundedMetadata( GDALMajorObjectH object, std::map<std::string, std::string> &out, int maxItems )
{
  CSLConstList metadata = GDALGetMetadata( object, nullptr );
  if ( !metadata )
    return;
  int captured = 0;
  for ( CSLConstList entry = metadata; *entry; ++entry )
  {
    if ( captured >= maxItems )
      break;
    const std::string line( *entry );
    const std::size_t eq = line.find( '=' );
    if ( eq == std::string::npos )
      continue;
    out[line.substr( 0, eq )] = line.substr( eq + 1 );
    ++captured;
  }
}

bool parseDouble( const std::string &text, double &out )
{
  try
  {
    std::size_t consumed = 0;
    const double value = std::stod( text, &consumed );
    if ( consumed == 0 )
      return false;
    out = value;
    return true;
  }
  catch ( const std::exception & )
  {
    return false;
  }
}

std::string productMetadataItem( const std::map<std::string, std::string> &metadata,
                                 std::initializer_list<const char *> keys )
{
  for ( const char *key : keys )
  {
    const auto it = metadata.find( key );
    if ( it != metadata.end() && !it->second.empty() )
      return it->second;
  }
  return std::string();
}

GDALDatasetH openReadOnlyOrThrow( const std::string &path, unsigned int openFlags, const char *context )
{
  QuietCplErrors quiet;
  ensureGdalRegistered();
  GDALDatasetH handle = GDALOpenEx( path.c_str(), GDAL_OF_READONLY | openFlags, nullptr, nullptr, nullptr );
  if ( !handle )
  {
    Json::Value details;
    details["path"] = path;
    const char *lastError = CPLGetLastErrorMsg();
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::OpenFailed, std::string( context ) + ": cannot open dataset: " + path, details );
  }
  return handle;
}

CrsInfo readCrsInfo( OGRSpatialReferenceH srs )
{
  CrsInfo info;
  if ( !srs )
    return info;
  // OGRSpatialReferenceH is void* (or a typed alias depending on the GDAL
  // version) — reinterpret_cast is the only form valid for both.
  info.valid = !reinterpret_cast< OGRSpatialReference * >( srs )->IsEmpty();
  if ( !info.valid )
    return info;

  char *wkt = nullptr;
  if ( OSRExportToWkt( srs, &wkt ) == OGRERR_NONE && wkt )
  {
    info.wkt = wkt;
    CPLFree( wkt );
  }
  const char *authority = OSRGetAuthorityName( srs, nullptr );
  const char *code = OSRGetAuthorityCode( srs, nullptr );
  if ( authority && code )
    info.authid = std::string( authority ) + ":" + code;
  info.isGeographic = OSRIsGeographic( srs ) == TRUE;
  info.isProjected = OSRIsProjected( srs ) == TRUE;
  const double epoch = OSRGetCoordinateEpoch( srs );
  if ( epoch > 0.0 )
  {
    info.hasCoordinateEpoch = true;
    info.coordinateEpoch = epoch;
  }
  return info;
}

} // namespace

// ─── CrsInfo ─────────────────────────────────────────────────────────────────

Json::Value CrsInfo::toJson() const
{
  Json::Value json;
  json["valid"] = valid;
  json["wkt"] = wkt;
  json["authid"] = authid;
  json["is_geographic"] = isGeographic;
  json["is_projected"] = isProjected;
  json["has_coordinate_epoch"] = hasCoordinateEpoch;
  if ( hasCoordinateEpoch )
    json["coordinate_epoch"] = coordinateEpoch;
  return json;
}

CrsInfo CrsInfo::fromJson( const Json::Value &json )
{
  CrsInfo info;
  info.valid = json.get( "valid", false ).asBool();
  info.wkt = json.get( "wkt", "" ).asString();
  info.authid = json.get( "authid", "" ).asString();
  info.isGeographic = json.get( "is_geographic", false ).asBool();
  info.isProjected = json.get( "is_projected", false ).asBool();
  info.hasCoordinateEpoch = json.get( "has_coordinate_epoch", false ).asBool();
  if ( info.hasCoordinateEpoch && json.isMember( "coordinate_epoch" ) )
    info.coordinateEpoch = json["coordinate_epoch"].asDouble();
  return info;
}

// ─── BandInfo ────────────────────────────────────────────────────────────────

Json::Value BandInfo::toJson() const
{
  Json::Value json;
  json["index"] = index;
  json["dtype"] = dtype;
  json["description"] = description;
  json["has_nodata"] = hasNoData;
  if ( hasNoData )
  {
    if ( noDataIsNaN )
      json["nodata"] = "nan";
    else
      json["nodata"] = noDataValue;
  }
  json["has_scale"] = hasScale;
  if ( hasScale )
    json["scale"] = scale;
  json["has_offset"] = hasOffset;
  if ( hasOffset )
    json["offset"] = offset;
  json["unit"] = unit;
  json["role"] = role;
  json["has_wavelength"] = hasWavelength;
  if ( hasWavelength )
    json["wavelength_nm"] = wavelengthNm;
  json["has_fwhm"] = hasFwhm;
  if ( hasFwhm )
    json["fwhm_nm"] = fwhmNm;
  json["color_interpretation"] = colorInterpretation;
  json["has_color_table"] = hasColorTable;
  if ( hasColorTable )
    json["color_table_entries"] = colorTableEntryCount;
  json["is_mask_band"] = isMaskBand;
  if ( !metadata.empty() )
  {
    Json::Value meta( Json::objectValue );
    for ( const auto &item : metadata )
      meta[item.first] = item.second;
    json["metadata"] = meta;
  }
  return json;
}

BandInfo BandInfo::fromJson( const Json::Value &json )
{
  BandInfo band;
  band.index = json.get( "index", 0 ).asInt();
  band.dtype = json.get( "dtype", "" ).asString();
  band.description = json.get( "description", "" ).asString();
  band.hasNoData = json.get( "has_nodata", false ).asBool();
  if ( band.hasNoData && json.isMember( "nodata" ) )
  {
    const Json::Value nodata = json["nodata"];
    if ( nodata.isString() && nodata.asString() == "nan" )
    {
      band.noDataIsNaN = true;
      band.noDataValue = std::numeric_limits<double>::quiet_NaN();
    }
    else if ( nodata.isNumeric() )
    {
      band.noDataValue = nodata.asDouble();
    }
  }
  band.hasScale = json.get( "has_scale", false ).asBool();
  if ( band.hasScale && json.isMember( "scale" ) )
    band.scale = json["scale"].asDouble();
  band.hasOffset = json.get( "has_offset", false ).asBool();
  if ( band.hasOffset && json.isMember( "offset" ) )
    band.offset = json["offset"].asDouble();
  band.unit = json.get( "unit", "" ).asString();
  band.role = json.get( "role", "" ).asString();
  band.hasWavelength = json.get( "has_wavelength", false ).asBool();
  if ( band.hasWavelength && json.isMember( "wavelength_nm" ) )
    band.wavelengthNm = json["wavelength_nm"].asDouble();
  band.hasFwhm = json.get( "has_fwhm", false ).asBool();
  if ( band.hasFwhm && json.isMember( "fwhm_nm" ) )
    band.fwhmNm = json["fwhm_nm"].asDouble();
  band.colorInterpretation = json.get( "color_interpretation", "" ).asString();
  band.hasColorTable = json.get( "has_color_table", false ).asBool();
  if ( band.hasColorTable && json.isMember( "color_table_entries" ) )
    band.colorTableEntryCount = json["color_table_entries"].asInt();
  band.isMaskBand = json.get( "is_mask_band", false ).asBool();
  if ( json.isMember( "metadata" ) )
  {
    for ( const auto &key : json["metadata"].getMemberNames() )
      band.metadata[key] = json["metadata"][key].asString();
  }
  return band;
}

// ─── RasterMetadata JSON ─────────────────────────────────────────────────────

Json::Value RasterMetadata::toJson() const
{
  Json::Value json;
  json["format_version"] = 1;
  json["kind"] = "raster";
  json["path"] = path;
  json["driver"] = driver;
  json["driver_long_name"] = driverLongName;
  json["width"] = width;
  json["height"] = height;
  json["band_count"] = bandCount;
  json["crs"] = crs.toJson();
  json["has_geotransform"] = hasGeotransform;
  if ( hasGeotransform )
  {
    Json::Value gt( Json::arrayValue );
    for ( const double value : geotransform )
      gt.append( value );
    json["geotransform"] = gt;
    json["has_extent"] = hasExtent;
    if ( hasExtent )
    {
      Json::Value extent( Json::arrayValue );
      extent.append( minX );
      extent.append( minY );
      extent.append( maxX );
      extent.append( maxY );
      json["extent"] = extent;
    }
    json["resolution_x"] = resolutionX;
    json["resolution_y"] = resolutionY;
  }
  Json::Value bandsJson( Json::arrayValue );
  for ( const BandInfo &band : bands )
    bandsJson.append( band.toJson() );
  json["bands"] = bandsJson;
  json["overview_count"] = overviewCount;
  json["compression"] = compression;
  json["interleave"] = interleave;
  if ( !subdatasets.empty() )
  {
    Json::Value subs( Json::arrayValue );
    for ( const std::string &sub : subdatasets )
      subs.append( sub );
    json["subdatasets"] = subs;
  }
  json["has_gcps"] = hasGcps;
  if ( hasGcps )
    json["gcp_count"] = gcpCount;
  json["has_rpc"] = hasRpc;
  json["sensor"] = sensor;
  json["platform"] = platform;
  json["product_id"] = productId;
  json["processing_level"] = processingLevel;
  json["acquisition_time"] = acquisitionTime;
  json["radiometric_state"] = radiometricState;
  if ( numericScale != 0.0 )
    json["numeric_scale"] = numericScale;
  json["has_cloud_cover"] = hasCloudCover;
  if ( hasCloudCover )
    json["cloud_cover"] = cloudCover;
  json["has_gsd"] = hasGsd;
  if ( hasGsd )
    json["gsd"] = gsd;
  if ( !metadata.empty() )
  {
    Json::Value meta( Json::objectValue );
    for ( const auto &item : metadata )
      meta[item.first] = item.second;
    json["metadata"] = meta;
  }
  return json;
}

RasterMetadata RasterMetadata::fromJson( const Json::Value &json )
{
  RasterMetadata meta;
  meta.path = json.get( "path", "" ).asString();
  meta.driver = json.get( "driver", "" ).asString();
  meta.driverLongName = json.get( "driver_long_name", "" ).asString();
  meta.width = json.get( "width", 0 ).asInt();
  meta.height = json.get( "height", 0 ).asInt();
  meta.bandCount = json.get( "band_count", 0 ).asInt();
  if ( json.isMember( "crs" ) )
    meta.crs = CrsInfo::fromJson( json["crs"] );
  meta.hasGeotransform = json.get( "has_geotransform", false ).asBool();
  if ( meta.hasGeotransform && json.isMember( "geotransform" ) )
  {
    const Json::Value &gt = json["geotransform"];
    for ( Json::ArrayIndex i = 0; i < gt.size() && i < 6; ++i )
      meta.geotransform[i] = gt[i].asDouble();
  }
  meta.hasExtent = json.get( "has_extent", false ).asBool();
  if ( meta.hasExtent && json.isMember( "extent" ) && json["extent"].size() == 4 )
  {
    meta.minX = json["extent"][0].asDouble();
    meta.minY = json["extent"][1].asDouble();
    meta.maxX = json["extent"][2].asDouble();
    meta.maxY = json["extent"][3].asDouble();
  }
  if ( json.isMember( "resolution_x" ) )
    meta.resolutionX = json.get( "resolution_x", 0.0 ).asDouble();
  if ( json.isMember( "resolution_y" ) )
    meta.resolutionY = json.get( "resolution_y", 0.0 ).asDouble();
  if ( json.isMember( "bands" ) )
  {
    for ( const Json::Value &bandJson : json["bands"] )
      meta.bands.push_back( BandInfo::fromJson( bandJson ) );
  }
  meta.overviewCount = json.get( "overview_count", -1 ).asInt();
  meta.compression = json.get( "compression", "" ).asString();
  meta.interleave = json.get( "interleave", "" ).asString();
  if ( json.isMember( "subdatasets" ) )
  {
    for ( const Json::Value &sub : json["subdatasets"] )
      meta.subdatasets.push_back( sub.asString() );
  }
  meta.hasGcps = json.get( "has_gcps", false ).asBool();
  if ( meta.hasGcps && json.isMember( "gcp_count" ) )
    meta.gcpCount = json["gcp_count"].asInt();
  meta.hasRpc = json.get( "has_rpc", false ).asBool();
  meta.sensor = json.get( "sensor", "" ).asString();
  meta.platform = json.get( "platform", "" ).asString();
  meta.productId = json.get( "product_id", "" ).asString();
  meta.processingLevel = json.get( "processing_level", "" ).asString();
  meta.acquisitionTime = json.get( "acquisition_time", "" ).asString();
  meta.radiometricState = json.get( "radiometric_state", "" ).asString();
  if ( json.isMember( "numeric_scale" ) )
    meta.numericScale = json["numeric_scale"].asDouble();
  meta.hasCloudCover = json.get( "has_cloud_cover", false ).asBool();
  if ( meta.hasCloudCover && json.isMember( "cloud_cover" ) )
    meta.cloudCover = json["cloud_cover"].asDouble();
  meta.hasGsd = json.get( "has_gsd", false ).asBool();
  if ( meta.hasGsd && json.isMember( "gsd" ) )
    meta.gsd = json["gsd"].asDouble();
  if ( json.isMember( "metadata" ) )
  {
    for ( const auto &key : json["metadata"].getMemberNames() )
      meta.metadata[key] = json["metadata"][key].asString();
  }
  return meta;
}

RasterMetadata rasterMetadataFromJsonText( const std::string &jsonText, Json::Value *errors )
{
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  std::istringstream stream( jsonText );
  std::string parseErrors;
  if ( !Json::parseFromStream( builder, stream, &parsed, &parseErrors ) )
  {
    if ( errors )
      *errors = Json::Value( parseErrors );
    return RasterMetadata{};
  }
  if ( errors )
    *errors = Json::Value();
  return RasterMetadata::fromJson( parsed );
}

// ─── Vector JSON ─────────────────────────────────────────────────────────────

Json::Value FieldInfo::toJson() const
{
  Json::Value json;
  json["name"] = name;
  json["type"] = typeName;
  json["width"] = width;
  json["precision"] = precision;
  return json;
}

FieldInfo FieldInfo::fromJson( const Json::Value &json )
{
  FieldInfo field;
  field.name = json.get( "name", "" ).asString();
  field.typeName = json.get( "type", "" ).asString();
  field.width = json.get( "width", 0 ).asInt();
  field.precision = json.get( "precision", 0 ).asInt();
  return field;
}

Json::Value VectorLayerInfo::toJson() const
{
  Json::Value json;
  json["name"] = name;
  json["geometry_type"] = geometryTypeName;
  json["feature_count"] = static_cast<Json::Int64>( featureCount );
  json["feature_count_exact"] = featureCountExact;
  json["crs"] = crs.toJson();
  Json::Value fieldsJson( Json::arrayValue );
  for ( const FieldInfo &field : fields )
    fieldsJson.append( field.toJson() );
  json["fields"] = fieldsJson;
  json["has_extent"] = hasExtent;
  json["extent_exact"] = extentExact;
  if ( hasExtent )
  {
    Json::Value extent( Json::arrayValue );
    extent.append( minX );
    extent.append( minY );
    extent.append( maxX );
    extent.append( maxY );
    json["extent"] = extent;
  }
  json["encoding"] = encoding;
  json["supports_fast_spatial_filter"] = supportsFastSpatialFilter;
  json["supports_sequential_write"] = supportsSequentialWrite;
  json["supports_random_write"] = supportsRandomWrite;
  return json;
}

VectorLayerInfo VectorLayerInfo::fromJson( const Json::Value &json )
{
  VectorLayerInfo layer;
  layer.name = json.get( "name", "" ).asString();
  layer.geometryTypeName = json.get( "geometry_type", "" ).asString();
  layer.featureCount = json.get( "feature_count", static_cast<Json::Int64>( -1 ) ).asInt64();
  layer.featureCountExact = json.get( "feature_count_exact", false ).asBool();
  if ( json.isMember( "crs" ) )
    layer.crs = CrsInfo::fromJson( json["crs"] );
  if ( json.isMember( "fields" ) )
  {
    for ( const Json::Value &fieldJson : json["fields"] )
      layer.fields.push_back( FieldInfo::fromJson( fieldJson ) );
  }
  layer.hasExtent = json.get( "has_extent", false ).asBool();
  layer.extentExact = json.get( "extent_exact", false ).asBool();
  if ( layer.hasExtent && json.isMember( "extent" ) && json["extent"].size() == 4 )
  {
    layer.minX = json["extent"][0].asDouble();
    layer.minY = json["extent"][1].asDouble();
    layer.maxX = json["extent"][2].asDouble();
    layer.maxY = json["extent"][3].asDouble();
  }
  layer.encoding = json.get( "encoding", "" ).asString();
  layer.supportsFastSpatialFilter = json.get( "supports_fast_spatial_filter", false ).asBool();
  layer.supportsSequentialWrite = json.get( "supports_sequential_write", false ).asBool();
  layer.supportsRandomWrite = json.get( "supports_random_write", false ).asBool();
  return layer;
}

Json::Value VectorMetadata::toJson() const
{
  Json::Value json;
  json["format_version"] = 1;
  json["kind"] = "vector";
  json["path"] = path;
  json["driver"] = driver;
  json["driver_long_name"] = driverLongName;
  Json::Value layersJson( Json::arrayValue );
  for ( const VectorLayerInfo &layer : layers )
    layersJson.append( layer.toJson() );
  json["layers"] = layersJson;
  return json;
}

VectorMetadata VectorMetadata::fromJson( const Json::Value &json )
{
  VectorMetadata meta;
  meta.path = json.get( "path", "" ).asString();
  meta.driver = json.get( "driver", "" ).asString();
  meta.driverLongName = json.get( "driver_long_name", "" ).asString();
  if ( json.isMember( "layers" ) )
  {
    for ( const Json::Value &layerJson : json["layers"] )
      meta.layers.push_back( VectorLayerInfo::fromJson( layerJson ) );
  }
  return meta;
}

// ─── Multidim JSON ───────────────────────────────────────────────────────────

Json::Value DimensionInfo::toJson() const
{
  Json::Value json;
  json["name"] = name;
  json["size"] = static_cast<Json::Int64>( size );
  json["type"] = type;
  json["direction"] = direction;
  json["unit"] = unit;
  if ( hasValues )
  {
    json["has_values"] = true;
    json["values_bounded"] = valuesBounded;
    Json::Value axis( Json::arrayValue );
    for ( const double value : values )
      axis.append( value );
    json["values"] = axis;
  }
  if ( hasStringValues )
  {
    json["has_string_values"] = true;
    json["string_values_bounded"] = stringValuesBounded;
    Json::Value axis( Json::arrayValue );
    for ( const std::string &value : stringValues )
      axis.append( value );
    json["string_values"] = axis;
  }
  return json;
}

DimensionInfo DimensionInfo::fromJson( const Json::Value &json )
{
  DimensionInfo dim;
  dim.name = json.get( "name", "" ).asString();
  dim.size = json.get( "size", static_cast<Json::Int64>( 0 ) ).asInt64();
  dim.type = json.get( "type", "" ).asString();
  dim.direction = json.get( "direction", "" ).asString();
  dim.unit = json.get( "unit", "" ).asString();
  dim.hasValues = json.get( "has_values", false ).asBool();
  dim.valuesBounded = json.get( "values_bounded", false ).asBool();
  if ( dim.hasValues && json["values"].isArray() )
  {
    for ( const Json::Value &value : json["values"] )
      dim.values.push_back( value.asDouble() );
  }
  dim.hasStringValues = json.get( "has_string_values", false ).asBool();
  dim.stringValuesBounded = json.get( "string_values_bounded", false ).asBool();
  if ( dim.hasStringValues && json["string_values"].isArray() )
  {
    for ( const Json::Value &value : json["string_values"] )
      dim.stringValues.push_back( value.asString() );
  }
  return dim;
}

Json::Value VariableInfo::toJson() const
{
  Json::Value json;
  json["name"] = name;
  json["dtype"] = dtype;
  Json::Value dims( Json::arrayValue );
  for ( const std::string &dim : dimensionNames )
    dims.append( dim );
  json["dimensions"] = dims;
  json["unit"] = unit;
  json["has_nodata"] = hasNoData;
  if ( hasNoData )
  {
    if ( noDataIsNaN )
      json["nodata"] = "nan";
    else
      json["nodata"] = noDataValue;
  }
  json["has_scale"] = hasScale;
  if ( hasScale )
    json["scale"] = scale;
  json["has_offset"] = hasOffset;
  if ( hasOffset )
    json["offset"] = offset;
  if ( !attributes.empty() )
  {
    Json::Value attrs( Json::objectValue );
    for ( const auto &item : attributes )
      attrs[item.first] = item.second;
    json["attributes"] = attrs;
  }
  Json::Value blocks( Json::arrayValue );
  for ( const std::int64_t b : blockShape )
    blocks.append( static_cast<Json::Int64>( b ) );
  json["block_shape"] = blocks;
  return json;
}

VariableInfo VariableInfo::fromJson( const Json::Value &json )
{
  VariableInfo variable;
  variable.name = json.get( "name", "" ).asString();
  variable.dtype = json.get( "dtype", "" ).asString();
  if ( json.isMember( "dimensions" ) )
  {
    for ( const Json::Value &dim : json["dimensions"] )
      variable.dimensionNames.push_back( dim.asString() );
  }
  variable.unit = json.get( "unit", "" ).asString();
  variable.hasNoData = json.get( "has_nodata", false ).asBool();
  if ( variable.hasNoData && json.isMember( "nodata" ) )
  {
    const Json::Value nodata = json["nodata"];
    if ( nodata.isString() && nodata.asString() == "nan" )
    {
      variable.noDataIsNaN = true;
      variable.noDataValue = std::numeric_limits<double>::quiet_NaN();
    }
    else if ( nodata.isNumeric() )
    {
      variable.noDataValue = nodata.asDouble();
    }
  }
  variable.hasScale = json.get( "has_scale", false ).asBool();
  if ( variable.hasScale && json.isMember( "scale" ) )
    variable.scale = json["scale"].asDouble();
  variable.hasOffset = json.get( "has_offset", false ).asBool();
  if ( variable.hasOffset && json.isMember( "offset" ) )
    variable.offset = json["offset"].asDouble();
  if ( json.isMember( "attributes" ) )
  {
    for ( const auto &key : json["attributes"].getMemberNames() )
      variable.attributes[key] = json["attributes"][key].asString();
  }
  if ( json.isMember( "block_shape" ) && json["block_shape"].isArray() )
  {
    for ( const Json::Value &b : json["block_shape"] )
      variable.blockShape.push_back( b.asInt64() );
  }
  return variable;
}

Json::Value MultidimMetadata::toJson() const
{
  Json::Value json;
  json["format_version"] = 1;
  json["kind"] = "multidimensional";
  json["path"] = path;
  json["driver"] = driver;
  json["crs"] = crs.toJson();
  Json::Value dimsJson( Json::arrayValue );
  for ( const DimensionInfo &dim : dimensions )
    dimsJson.append( dim.toJson() );
  json["dimensions"] = dimsJson;
  Json::Value varsJson( Json::arrayValue );
  for ( const VariableInfo &variable : variables )
    varsJson.append( variable.toJson() );
  json["variables"] = varsJson;
  return json;
}

MultidimMetadata MultidimMetadata::fromJson( const Json::Value &json )
{
  MultidimMetadata meta;
  meta.path = json.get( "path", "" ).asString();
  meta.driver = json.get( "driver", "" ).asString();
  if ( json.isMember( "crs" ) )
    meta.crs = CrsInfo::fromJson( json["crs"] );
  if ( json.isMember( "dimensions" ) )
  {
    for ( const Json::Value &dimJson : json["dimensions"] )
      meta.dimensions.push_back( DimensionInfo::fromJson( dimJson ) );
  }
  if ( json.isMember( "variables" ) )
  {
    for ( const Json::Value &variableJson : json["variables"] )
      meta.variables.push_back( VariableInfo::fromJson( variableJson ) );
  }
  return meta;
}

// ─── Inspection ──────────────────────────────────────────────────────────────

RasterMetadata inspectRaster( const std::string &path, const InspectOptions &options )
{
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "inspectRaster: empty path" );

  GdalDatasetGuard guard( openReadOnlyOrThrow( path, GDAL_OF_RASTER, "inspectRaster" ) );
  GDALDatasetH handle = guard.get();

  RasterMetadata meta;
  meta.path = path;
  GDALDriverH driver = GDALGetDatasetDriver( handle );
  meta.driver = GDALGetDriverShortName( driver );
  meta.driverLongName = GDALGetDriverLongName( driver );
  meta.width = GDALGetRasterXSize( handle );
  meta.height = GDALGetRasterYSize( handle );
  meta.bandCount = GDALGetRasterCount( handle );

  meta.crs = readCrsInfo( GDALGetSpatialRef( handle ) );

  double gt[6] = { 0, 1, 0, 0, 0, 1 };
  if ( GDALGetGeoTransform( handle, gt ) == CE_None )
  {
    meta.hasGeotransform = true;
    std::copy( gt, gt + 6, meta.geotransform.begin() );
    const double det = gt[1] * gt[5] - gt[2] * gt[4];
    if ( std::fabs( det ) > std::numeric_limits<double>::epsilon() )
    {
      meta.hasExtent = true;
      // Corners in traditional GIS map coordinates (GDAL convention).
      const double x0 = gt[0];
      const double y0 = gt[3];
      const double x1 = gt[0] + meta.width * gt[1] + meta.height * gt[2];
      const double y1 = gt[3] + meta.width * gt[4] + meta.height * gt[5];
      meta.minX = std::min( x0, x1 );
      meta.maxX = std::max( x0, x1 );
      meta.minY = std::min( y0, y1 );
      meta.maxY = std::max( y0, y1 );
      meta.resolutionX = std::sqrt( gt[1] * gt[1] + gt[4] * gt[4] );
      meta.resolutionY = std::sqrt( gt[2] * gt[2] + gt[5] * gt[5] );
    }
  }

  const int maxMetadataItems = options.maxMetadataItems > 0 ? options.maxMetadataItems : kMaxMetadataItemsFallback;
  captureBoundedMetadata( handle, meta.metadata, maxMetadataItems );

  if ( meta.bandCount > 0 )
  {
    GDALRasterBandH firstBand = GDALGetRasterBand( handle, 1 );
    meta.overviewCount = firstBand ? GDALGetOverviewCount( firstBand ) : -1;
  }

  if ( const char *compression = GDALGetMetadataItem( handle, "COMPRESSION", "IMAGE_STRUCTURE" ) )
    meta.compression = compression;
  if ( const char *interleave = GDALGetMetadataItem( handle, "INTERLEAVE", "IMAGE_STRUCTURE" ) )
    meta.interleave = interleave;

  CSLConstList subdatasets = GDALGetMetadata( handle, "SUBDATASETS" );
  if ( subdatasets )
  {
    const int maxEntries = options.maxSubdatasets > 0 ? options.maxSubdatasets * 2 : kMaxSubdatasetEntriesFallback;
    int captured = 0;
    for ( CSLConstList entry = subdatasets; *entry; ++entry )
    {
      if ( captured >= maxEntries )
        break;
      meta.subdatasets.emplace_back( *entry );
      ++captured;
    }
  }

  meta.gcpCount = GDALGetGCPCount( handle );
  meta.hasGcps = meta.gcpCount > 0;
  if ( !meta.hasGcps )
  {
    // RPCs live as RPC* keys in the default domain (GTiff/PAM) — presence of
    // the coefficient blocks is the marker; contents are not copied here.
    if ( meta.metadata.count( "LINE_NUM_COEFF" ) || meta.metadata.count( "SAMP_NUM_COEFF" ) )
      meta.hasRpc = true;
  }

  for ( int index = 1; index <= meta.bandCount; ++index )
  {
    GDALRasterBandH band = GDALGetRasterBand( handle, index );
    if ( !band )
      continue;
    BandInfo info;
    info.index = index;
    info.dtype = gdalDataTypeName( GDALGetRasterDataType( band ) );
    if ( const char *description = GDALGetDescription( band ) )
      info.description = description;

    int hasNoData = 0;
    const double noData = GDALGetRasterNoDataValue( band, &hasNoData );
    info.hasNoData = hasNoData != 0;
    info.noDataValue = noData;
    info.noDataIsNaN = info.hasNoData && std::isnan( noData );

    int hasScale = 0;
    const double scale = GDALGetRasterScale( band, &hasScale );
    info.hasScale = hasScale != 0;
    info.scale = hasScale ? scale : 1.0;
    int hasOffset = 0;
    const double offset = GDALGetRasterOffset( band, &hasOffset );
    info.hasOffset = hasOffset != 0;
    info.offset = hasOffset ? offset : 0.0;

    if ( const char *unit = GDALGetRasterUnitType( band ) )
      info.unit = unit;

    info.colorInterpretation = GDALGetColorInterpretationName( GDALGetRasterColorInterpretation( band ) );
    GDALColorTableH colorTable = GDALGetRasterColorTable( band );
    info.hasColorTable = colorTable != nullptr;
    if ( info.hasColorTable )
      info.colorTableEntryCount = GDALGetColorEntryCount( colorTable );

    info.isMaskBand = ( GDALGetMaskFlags( band ) & GMF_ALPHA ) != 0;

    captureBoundedMetadata( band, info.metadata, maxMetadataItems );

    // Band role: platform vocabulary rides in SICNU_BAND_ROLE (the
    // src/data/band_role.h carrier); wavelength/FWHM in the import stamps.
    const auto roleIt = info.metadata.find( "SICNU_BAND_ROLE" );
    if ( roleIt != info.metadata.end() )
      info.role = roleIt->second;
    double wavelength = 0.0;
    if ( !info.hasWavelength )
    {
      const auto wavelengthIt = info.metadata.find( "WAVELENGTH" );
      if ( wavelengthIt != info.metadata.end() && parseDouble( wavelengthIt->second, wavelength ) )
      {
        info.wavelengthNm = wavelength;
        info.hasWavelength = true;
      }
    }
    const auto fwhmIt = info.metadata.find( "FWHM" );
    if ( !info.hasFwhm && fwhmIt != info.metadata.end() && parseDouble( fwhmIt->second, info.fwhmNm ) )
      info.hasFwhm = true;

    meta.bands.push_back( std::move( info ) );
  }

  // Product semantics from SICNU_* stamps (ADR 0114, product import) with
  // conservative standard-key fallbacks. Absence stays absence — no guessing.
  meta.sensor = productMetadataItem( meta.metadata, { "SICNU_SENSOR", "SENSOR", "Sensor" } );
  meta.platform = productMetadataItem( meta.metadata, { "SICNU_PRODUCT_SPACECRAFT", "SPACECRAFT", "PLATFORM", "Satellite" } );
  meta.productId = productMetadataItem( meta.metadata, { "SICNU_PRODUCT_ID", "PRODUCT_ID", "PRODUCTIDENTIFIER" } );
  meta.processingLevel = productMetadataItem( meta.metadata, { "SICNU_PROCESSING_LEVEL", "PROCESSING_LEVEL", "ProcessingLevel" } );
  meta.acquisitionTime = productMetadataItem( meta.metadata, { "SICNU_ACQUISITION_DATE", "ACQUISITION_DATE", "TIMESTAMP", "ACQ_TIME" } );
  meta.radiometricState = productMetadataItem( meta.metadata, { "SICNU_RADIOMETRIC_STATE" } );
  const std::string numericScale = productMetadataItem( meta.metadata, { "SICNU_NUMERIC_SCALE" } );
  if ( !numericScale.empty() )
  {
    double parsedScale = 0.0;
    if ( parseDouble( numericScale, parsedScale ) )
      meta.numericScale = parsedScale;
  }

  return meta;
}

VectorMetadata inspectVector( const std::string &path, const InspectOptions &options )
{
  (void)options;
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "inspectVector: empty path" );

  GdalDatasetGuard guard( openReadOnlyOrThrow( path, GDAL_OF_VECTOR, "inspectVector" ) );
  GDALDatasetH handle = guard.get();

  VectorMetadata meta;
  meta.path = path;
  GDALDriverH driver = GDALGetDatasetDriver( handle );
  meta.driver = GDALGetDriverShortName( driver );
  meta.driverLongName = GDALGetDriverLongName( driver );

  const int layerCount = GDALDatasetGetLayerCount( handle );
  for ( int index = 0; index < layerCount; ++index )
  {
    OGRLayerH layer = GDALDatasetGetLayer( handle, index );
    if ( !layer )
      continue;
    VectorLayerInfo info;
    if ( const char *name = OGR_L_GetName( layer ) )
      info.name = name;

    info.crs = readCrsInfo( OGR_L_GetSpatialRef( layer ) );

    if ( OGRFeatureDefnH defn = OGR_L_GetLayerDefn( layer ) )
    {
      info.geometryTypeName = OGRGeometryTypeToName( OGR_FD_GetGeomType( defn ) );
      const int fieldCount = OGR_FD_GetFieldCount( defn );
      for ( int fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex )
      {
        OGRFieldDefnH fieldDefn = OGR_FD_GetFieldDefn( defn, fieldIndex );
        if ( !fieldDefn )
          continue;
        FieldInfo field;
        if ( const char *name = OGR_Fld_GetNameRef( fieldDefn ) )
          field.name = name;
        field.typeName = OGR_GetFieldTypeName( OGR_Fld_GetType( fieldDefn ) );
        field.width = OGR_Fld_GetWidth( fieldDefn );
        field.precision = OGR_Fld_GetPrecision( fieldDefn );
        info.fields.push_back( field );
      }
    }

    // Cheap count only (bForce=FALSE): dataset listing must not trigger a
    // full scan. featureCountExact records whether the driver could answer.
    info.featureCount = OGR_L_GetFeatureCount( layer, 0 );
    info.featureCountExact = info.featureCount >= 0;

    OGREnvelope envelope;
    envelope.MinX = envelope.MinY = envelope.MaxX = envelope.MaxY = 0.0;
    if ( OGR_L_GetExtent( layer, &envelope, 0 ) == OGRERR_NONE )
    {
      info.hasExtent = true;
      info.extentExact = true;
      info.minX = envelope.MinX;
      info.minY = envelope.MinY;
      info.maxX = envelope.MaxX;
      info.maxY = envelope.MaxY;
    }

    info.supportsFastSpatialFilter = OGR_L_TestCapability( layer, OLCFastSpatialFilter ) != 0;
    info.supportsSequentialWrite = OGR_L_TestCapability( layer, OLCSequentialWrite ) != 0;
    info.supportsRandomWrite = OGR_L_TestCapability( layer, OLCRandomWrite ) != 0;

    meta.layers.push_back( std::move( info ) );
  }
  return meta;
}

MultidimMetadata inspectMultidim( const std::string &path, const InspectOptions &options )
{
  (void)options;
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "inspectMultidim: empty path" );

  GdalDatasetGuard guard( openReadOnlyOrThrow( path, GDAL_OF_MULTIDIM_RASTER, "inspectMultidim" ) );
  GDALDatasetH handle = guard.get();

  MultidimMetadata meta;
  meta.path = path;
  GDALDriverH driver = GDALGetDatasetDriver( handle );
  meta.driver = GDALGetDriverShortName( driver );

  GDALGroupH rootGroup = GDALDatasetGetRootGroup( handle );
  if ( !rootGroup )
    throw GeoError( ErrorCode::Unsupported, "Dataset exposes no multidimensional API: " + path );

  // Root-group dimensions.
  size_t dimCount = 0;
  GDALDimensionH *dimensions = GDALGroupGetDimensions( rootGroup, &dimCount, nullptr );
  if ( dimensions )
  {
    for ( size_t i = 0; i < dimCount; ++i )
    {
      GDALDimensionH dim = dimensions[i];
      if ( !dim )
        continue;
      DimensionInfo info;
      if ( const char *name = GDALDimensionGetName( dim ) )
        info.name = name;
      info.size = static_cast<std::int64_t>( GDALDimensionGetSize( dim ) );
      if ( const char *type = GDALDimensionGetType( dim ) )
        info.type = type;
      if ( const char *direction = GDALDimensionGetDirection( dim ) )
        info.direction = direction;
      // Unit + coordinate axis values of the indexing variable, when one
      // exists (netCDF CF convention). Axis values are read as doubles,
      // hard-bounded at kMaxAxisValues (a truncated capture flags
      // valuesBounded so callers never mistake a partial axis for complete).
      if ( GDALMDArrayH indexingVariable = GDALMDArrayH( GDALDimensionGetIndexingVariable( dim ) ) )
      {
        if ( GDALAttributeH unitAttribute = GDALMDArrayGetAttribute( indexingVariable, "units" ) )
        {
          if ( const char *unitText = GDALAttributeReadAsString( unitAttribute ) )
            info.unit = unitText;
          GDALAttributeRelease( unitAttribute );
        }
        GDALExtendedDataTypeH axisType = GDALMDArrayGetDataType( indexingVariable );
        const GDALExtendedDataTypeClass axisClass = GDALExtendedDataTypeGetClass( axisType );
        const bool numericAxis = axisClass == GEDTC_NUMERIC &&
                                 GDALExtendedDataTypeGetNumericDataType( axisType ) != GDT_Unknown;
        GDALExtendedDataTypeRelease( axisType );
        if ( numericAxis && info.size > 0 )
        {
          const GUInt64 axisCount = static_cast<GUInt64>(
            std::min<std::int64_t>( info.size, static_cast<std::int64_t>( DimensionInfo::kMaxAxisValues ) ) );
          std::vector<double> axisValues( static_cast<std::size_t>( axisCount ), 0.0 );
          const GUInt64 axisStart = 0;
          const std::size_t axisCountSize = static_cast<std::size_t>( axisCount );
          const GInt64 axisStep = 1;
          const GPtrDiff_t axisStride = 1;
          GDALExtendedDataTypeH bufferType = GDALExtendedDataTypeCreate( GDT_Float64 );
          QuietCplErrors quietAxis;
          if ( GDALMDArrayRead( indexingVariable, &axisStart, &axisCountSize, &axisStep, &axisStride,
                                bufferType, axisValues.data(), axisValues.data(),
                                axisValues.size() * sizeof( double ) ) )
          {
            info.hasValues = true;
            info.valuesBounded = static_cast<std::int64_t>( axisCount ) < info.size;
            info.values = std::move( axisValues );
          }
          GDALExtendedDataTypeRelease( bufferType );
        }
        else if ( axisClass == GEDTC_STRING && info.size > 0 )
        {
          // 8.0: string coordinate axes (CF datetime strings, categorical
          // labels). Read element-by-element with a string buffer type and
          // copy immediately into std::string storage; each element is
          // CPL-allocated by the driver and released right after the copy.
          const GUInt64 axisCount = static_cast<GUInt64>(
            std::min<std::int64_t>( info.size, static_cast<std::int64_t>( DimensionInfo::kMaxAxisValues ) ) );
          GDALExtendedDataTypeH stringType = GDALExtendedDataTypeCreateString( 0 );
          QuietCplErrors quietAxis;
          const GInt64 oneStep = 1;
          GPtrDiff_t oneStride = 1;
          const std::size_t oneCount = 1;
          for ( GUInt64 i = 0; i < axisCount && stringType != nullptr; ++i )
          {
            char *element = nullptr;
            if ( GDALMDArrayRead( indexingVariable, &i, &oneCount, &oneStep, &oneStride,
                                  stringType, &element, &element, sizeof( char * ) ) )
            {
              info.stringValues.push_back( element ? element : "" );
              CPLFree( element );
            }
            else
            {
              break; // first failed element truncates the axis capture
            }
          }
          if ( stringType != nullptr )
            GDALExtendedDataTypeRelease( stringType );
          if ( !info.stringValues.empty() )
          {
            info.hasStringValues = true;
            info.stringValuesBounded =
              static_cast<std::int64_t>( info.stringValues.size() ) < info.size;
          }
        }
        GDALMDArrayRelease( indexingVariable );
      }
      meta.dimensions.push_back( std::move( info ) );
      GDALDimensionRelease( dim );
    }
    CPLFree( dimensions );
  }

  // Root-group variables.
  char **variableNames = GDALGroupGetMDArrayNames( rootGroup, nullptr );
  if ( variableNames )
  {
    for ( char **name = variableNames; *name; ++name )
    {
      GDALMDArrayH array = GDALGroupOpenMDArray( rootGroup, *name, nullptr );
      if ( !array )
        continue;
      VariableInfo info;
      info.name = *name;

      GDALExtendedDataTypeH dataType = GDALMDArrayGetDataType( array );
      if ( GDALExtendedDataTypeGetClass( dataType ) == GEDTC_NUMERIC )
        info.dtype = gdalDataTypeName( GDALExtendedDataTypeGetNumericDataType( dataType ) );
      else
        info.dtype = GDALExtendedDataTypeGetClass( dataType ) == GEDTC_STRING ? "String" : "Compound";
      GDALExtendedDataTypeRelease( dataType );

      size_t arrayDimCount = 0;
      GDALDimensionH *arrayDims = GDALMDArrayGetDimensions( array, &arrayDimCount );
      if ( arrayDims )
      {
        for ( size_t d = 0; d < arrayDimCount; ++d )
        {
          if ( arrayDims[d] )
          {
            if ( const char *dimName = GDALDimensionGetName( arrayDims[d] ) )
              info.dimensionNames.emplace_back( dimName );
            GDALDimensionRelease( arrayDims[d] );
          }
        }
        CPLFree( arrayDims );
      }

      // Chunk shape for chunk-aware read planning (absent storages answer 0).
      size_t blockCount = 0;
      GUInt64 *blockSizes = GDALMDArrayGetBlockSize( array, &blockCount );
      if ( blockSizes )
      {
        for ( size_t b = 0; b < blockCount; ++b )
          info.blockShape.push_back( static_cast<std::int64_t>( blockSizes[b] ) );
        CPLFree( blockSizes );
      }

      if ( GDALAttributeH unitAttribute = GDALMDArrayGetAttribute( array, "units" ) )
      {
        if ( const char *unitText = GDALAttributeReadAsString( unitAttribute ) )
          info.unit = unitText;
        GDALAttributeRelease( unitAttribute );
      }

      int hasNoData = 0;
      const double noData = GDALMDArrayGetNoDataValueAsDouble( array, &hasNoData );
      info.hasNoData = hasNoData != 0;
      info.noDataValue = noData;
      info.noDataIsNaN = info.hasNoData && std::isnan( noData );

      int hasScale = 0;
      const double scale = GDALMDArrayGetScale( array, &hasScale );
      info.hasScale = hasScale != 0;
      info.scale = hasScale ? scale : 1.0;
      int hasOffset = 0;
      const double offset = GDALMDArrayGetOffset( array, &hasOffset );
      info.hasOffset = hasOffset != 0;
      info.offset = hasOffset ? offset : 0.0;

      meta.variables.push_back( std::move( info ) );
      GDALMDArrayRelease( array );
    }
    CSLDestroy( variableNames );
  }

  GDALGroupRelease( rootGroup );
  return meta;
}

Json::Value inspectAny( const std::string &path, const InspectOptions &options )
{
  // Raster first (the common case; multidim stores usually also expose a
  // subdataset raster view), vector second, multidim-only stores last.
  try
  {
    return inspectRaster( path, options ).toJson();
  }
  catch ( const GeoError &rasterError )
  {
    if ( rasterError.code() != ErrorCode::OpenFailed )
      throw;
    try
    {
      // An empty layer list is still a valid answer for vector containers.
      return inspectVector( path, options ).toJson();
    }
    catch ( const GeoError &vectorError )
    {
      if ( vectorError.code() != ErrorCode::OpenFailed )
        throw;
      MultidimMetadata multidim = inspectMultidim( path, options );
      if ( multidim.isNull() )
      {
        Json::Value details;
        details["raster"] = rasterError.toJson();
        details["vector"] = vectorError.toJson();
        throw GeoError( ErrorCode::OpenFailed, "Cannot open dataset as raster, vector or multidimensional: " + path, details );
      }
      return multidim.toJson();
    }
  }
}

} // namespace sicnu::geo
