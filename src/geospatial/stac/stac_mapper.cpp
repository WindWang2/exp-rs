/***************************************************************************
  geospatial/stac/stac_mapper.cpp
  Geospatial I/O Foundation 4.0 — STAC interoperability.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/stac/stac_mapper.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace sicnu::geo
{
namespace
{

std::string requireString( const Json::Value &object, const char *key, const char *context )
{
  if ( !object.isMember( key ) || !object[key].isString() || object[key].asString().empty() )
  {
    Json::Value details;
    details["field"] = key;
    details["context"] = context;
    throw GeoError( ErrorCode::InvalidArgument, std::string( "STAC item is missing required field: " ) + key,
                    details );
  }
  return object[key].asString();
}

std::string optionalString( const Json::Value &object, const char *key )
{
  if ( object.isMember( key ) && object[key].isString() )
    return object[key].asString();
  return std::string();
}

double optionalDouble( const Json::Value &object, const char *key, bool &present )
{
  present = object.isMember( key ) && object[key].isNumeric();
  return present ? object[key].asDouble() : 0.0;
}

} // namespace

Json::Value StacAsset::toJson() const
{
  Json::Value json;
  json["href"] = href;
  if ( !title.empty() )
    json["title"] = title;
  if ( !mediaType.empty() )
    json["type"] = mediaType;
  if ( !roles.empty() )
  {
    Json::Value roleList( Json::arrayValue );
    for ( const std::string &role : roles )
      roleList.append( role );
    json["roles"] = roleList;
  }
  return json;
}

StacItem StacItem::parse( const Json::Value &item )
{
  if ( !item.isObject() )
    throw GeoError( ErrorCode::InvalidArgument, "STAC item must be a JSON object" );
  if ( item.get( "type", "" ).asString() != "Feature" )
  {
    Json::Value details;
    details["type"] = item.get( "type", "" ).asString();
    throw GeoError( ErrorCode::InvalidArgument, "STAC item requires type=Feature", details );
  }

  StacItem parsed;
  parsed.id = requireString( item, "id", "stac_item" );
  parsed.stacVersion = optionalString( item, "stac_version" );

  const Json::Value &properties = item["properties"];
  if ( !properties.isObject() )
    throw GeoError( ErrorCode::InvalidArgument, "STAC item requires a properties object" );

  parsed.datetime = optionalString( properties, "datetime" );
  parsed.startDatetime = optionalString( properties, "start_datetime" );
  parsed.endDatetime = optionalString( properties, "end_datetime" );
  if ( parsed.datetime.empty() && ( parsed.startDatetime.empty() || parsed.endDatetime.empty() ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "STAC item requires properties.datetime or start_datetime/end_datetime" );

  parsed.platform = optionalString( properties, "platform" );
  parsed.constellation = optionalString( properties, "constellation" );
  parsed.processingLevel = optionalString( properties, "s2:processing_level" );
  if ( parsed.processingLevel.empty() )
    parsed.processingLevel = optionalString( properties, "processing:level" );
  parsed.modality = optionalString( properties, "modality" );
  parsed.hasCloudCover = false;
  parsed.cloudCover = optionalDouble( properties, "eo:cloud_cover", parsed.hasCloudCover );
  parsed.hasGsd = false;
  parsed.gsd = optionalDouble( properties, "gsd", parsed.hasGsd );
  if ( properties.isMember( "sar:polarizations" ) && properties["sar:polarizations"].isArray() )
  {
    for ( const Json::Value &pol : properties["sar:polarizations"] )
      parsed.polarizations.push_back( pol.asString() );
  }
  if ( properties.isMember( "proj:epsg" ) && properties["proj:epsg"].isIntegral() )
    parsed.epsg = "EPSG:" + std::to_string( properties["proj:epsg"].asInt() );
  else if ( properties.isMember( "proj:epsg" ) && properties["proj:epsg"].isString() )
    parsed.epsg = properties["proj:epsg"].asString();
  if ( properties.isMember( "instruments" ) && properties["instruments"].isArray() )
  {
    for ( const Json::Value &instrument : properties["instruments"] )
      parsed.instruments.push_back( instrument.asString() );
  }

  if ( item.isMember( "bbox" ) && item["bbox"].isArray() )
  {
    for ( const Json::Value &value : item["bbox"] )
      parsed.bbox.push_back( value.asDouble() );
  }
  parsed.geometry = item.get( "geometry", Json::Value() );

  if ( item.isMember( "assets" ) && item["assets"].isObject() )
  {
    for ( const std::string &key : item["assets"].getMemberNames() )
    {
      const Json::Value &assetJson = item["assets"][key];
      if ( !assetJson.isObject() )
        continue;
      StacAsset asset;
      asset.href = requireString( assetJson, "href", ( "assets." + key ).c_str() );
      asset.title = optionalString( assetJson, "title" );
      asset.mediaType = optionalString( assetJson, "type" );
      if ( assetJson.isMember( "roles" ) && assetJson["roles"].isArray() )
      {
        for ( const Json::Value &role : assetJson["roles"] )
          asset.roles.push_back( role.asString() );
      }
      parsed.assets[key] = asset;
    }
  }
  if ( parsed.assets.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "STAC item carries no assets" );

  parsed.raw = item;
  return parsed;
}

StacItem StacItem::parseText( const std::string &jsonText )
{
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  std::istringstream stream( jsonText );
  std::string errors;
  if ( !Json::parseFromStream( builder, stream, &parsed, &errors ) )
    throw GeoError( ErrorCode::InvalidArgument, "STAC item is not valid JSON: " + errors );
  return parse( parsed );
}

Json::Value StacItem::toJson() const
{
  Json::Value item;
  item["type"] = "Feature";
  item["stac_version"] = stacVersion.empty() ? "1.0.0" : stacVersion;
  item["id"] = id;
  Json::Value extensions( Json::arrayValue );
  if ( !epsg.empty() )
    extensions.append( "https://stac-extensions.github.io/projection/v1.1.0/schema.json" );
  if ( hasCloudCover || hasGsd || !polarizations.empty() )
    extensions.append( "https://stac-extensions.github.io/eo/v1.1.0/schema.json" );
  if ( !polarizations.empty() )
    extensions.append( "https://stac-extensions.github.io/sar/v1.0.0/schema.json" );
  item["stac_extensions"] = extensions;

  if ( !bbox.empty() )
  {
    Json::Value bboxJson( Json::arrayValue );
    for ( const double value : bbox )
      bboxJson.append( value );
    item["bbox"] = bboxJson;
  }
  item["geometry"] = geometry.isNull() ? Json::Value() : geometry;

  Json::Value properties( Json::objectValue );
  if ( !datetime.empty() )
    properties["datetime"] = datetime;
  if ( !startDatetime.empty() )
    properties["start_datetime"] = startDatetime;
  if ( !endDatetime.empty() )
    properties["end_datetime"] = endDatetime;
  if ( !platform.empty() )
    properties["platform"] = platform;
  if ( !constellation.empty() )
    properties["constellation"] = constellation;
  if ( !processingLevel.empty() )
    properties["s2:processing_level"] = processingLevel;
  if ( !modality.empty() )
    properties["modality"] = modality;
  if ( hasCloudCover )
    properties["eo:cloud_cover"] = cloudCover;
  if ( hasGsd )
    properties["gsd"] = gsd;
  if ( !epsg.empty() && epsg.rfind( "EPSG:", 0 ) == 0 )
    properties["proj:epsg"] = std::atoi( epsg.c_str() + 5 );
  if ( !polarizations.empty() )
  {
    Json::Value pols( Json::arrayValue );
    for ( const std::string &pol : polarizations )
      pols.append( pol );
    properties["sar:polarizations"] = pols;
  }
  if ( !instruments.empty() )
  {
    Json::Value inst( Json::arrayValue );
    for ( const std::string &instrument : instruments )
      inst.append( instrument );
    properties["instruments"] = inst;
  }
  if ( properties["datetime"].isNull() && properties["start_datetime"].isNull() )
    properties["datetime"] = datetime; // keep the field present (may still be empty → caller validated)
  item["properties"] = properties;

  Json::Value assetsJson( Json::objectValue );
  for ( const auto &entry : assets )
    assetsJson[entry.first] = entry.second.toJson();
  item["assets"] = assetsJson;
  return item;
}

RasterMetadata stacItemToCanonical( const StacItem &item )
{
  RasterMetadata meta;
  meta.productId = item.id;
  meta.platform = item.platform;
  meta.sensor = item.constellation.empty()
                  ? ( item.instruments.empty() ? std::string() : item.instruments.front() )
                  : item.constellation;
  meta.processingLevel = item.processingLevel;
  meta.acquisitionTime = item.datetime.empty() ? item.startDatetime : item.datetime;
  meta.hasCloudCover = item.hasCloudCover;
  meta.cloudCover = item.cloudCover;
  meta.hasGsd = item.hasGsd;
  meta.gsd = item.gsd;

  // SAR / modality vocabulary rides the generic metadata domain.
  if ( !item.polarizations.empty() )
  {
    std::string joined;
    for ( const std::string &pol : item.polarizations )
    {
      if ( !joined.empty() )
        joined += ",";
      joined += pol;
    }
    meta.metadata["sar:polarizations"] = joined;
  }
  if ( !item.modality.empty() )
    meta.metadata["modality"] = item.modality;

  // Declared CRS from the projection extension — carried as declared, not
  // resolved (no dataset opened); resolveDatasetCrs() handles it later.
  if ( !item.epsg.empty() )
  {
    meta.crs.valid = true;
    meta.crs.authid = item.epsg;
  }

  if ( item.bbox.size() >= 4 )
  {
    meta.metadata["stac:bbox"] = std::to_string( item.bbox[0] ) + "," + std::to_string( item.bbox[1] ) + ","
                                  + std::to_string( item.bbox[2] ) + "," + std::to_string( item.bbox[3] );
  }

  // Primary asset: first with role "data", else the first declared.
  std::string primaryHref;
  std::string primaryType;
  for ( const auto &entry : item.assets )
  {
    const bool dataRole = std::find( entry.second.roles.begin(), entry.second.roles.end(), "data" )
                          != entry.second.roles.end();
    if ( primaryHref.empty() )
    {
      primaryHref = entry.second.href;
      primaryType = entry.second.mediaType;
    }
    if ( dataRole )
    {
      primaryHref = entry.second.href;
      primaryType = entry.second.mediaType;
      break;
    }
  }
  meta.path = primaryHref;
  if ( primaryType == "image/tiff" || primaryType == "image/tiff; application=geotiff" || primaryType == "image/vnd.stac.geotiff" )
    meta.driver = "GTiff";
  else if ( primaryType == "image/jp2" )
    meta.driver = "JP2OpenJPEG";
  if ( !primaryType.empty() )
    meta.metadata["stac:primary_asset_type"] = primaryType;

  return meta;
}

Json::Value canonicalToStacItem( const RasterMetadata &metadata, const std::string &assetHref,
                                 const std::string &itemId )
{
  const std::string datetime = metadata.acquisitionTime;
  if ( datetime.empty() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "canonicalToStacItem: STAC requires an acquisition time; canonical metadata carries none" );

  StacItem item;
  item.id = itemId.empty() ? ( metadata.productId.empty() ? "exp-rs-item" : metadata.productId ) : itemId;
  item.datetime = datetime;
  item.platform = metadata.platform;
  item.constellation = metadata.sensor;
  item.processingLevel = metadata.processingLevel;
  if ( metadata.hasCloudCover )
  {
    item.hasCloudCover = true;
    item.cloudCover = metadata.cloudCover;
  }
  if ( metadata.hasGsd )
  {
    item.hasGsd = true;
    item.gsd = metadata.gsd;
  }
  if ( metadata.crs.valid && metadata.crs.authid.rfind( "EPSG:", 0 ) == 0 )
    item.epsg = metadata.crs.authid;

  // bbox + geometry from the canonical extent (traditional GIS order).
  if ( metadata.hasExtent )
  {
    item.bbox = { metadata.minX, metadata.minY, metadata.maxX, metadata.maxY };
    Json::Value coordinates( Json::arrayValue );
    Json::Value ring( Json::arrayValue );
    ring.append( Json::Value( Json::arrayValue ) );
    ring[0].append( metadata.minX );
    ring[0].append( metadata.minY );
    ring.append( Json::Value( Json::arrayValue ) );
    ring[1].append( metadata.minX );
    ring[1].append( metadata.maxY );
    ring.append( Json::Value( Json::arrayValue ) );
    ring[2].append( metadata.maxX );
    ring[2].append( metadata.maxY );
    ring.append( Json::Value( Json::arrayValue ) );
    ring[3].append( metadata.maxX );
    ring[3].append( metadata.minY );
    ring.append( Json::Value( Json::arrayValue ) );
    ring[4].append( metadata.minX );
    ring[4].append( metadata.minY );
    coordinates.append( ring );
    item.geometry["type"] = "Polygon";
    item.geometry["coordinates"] = coordinates;
  }
  else if ( metadata.hasGsd )
  {
    item.hasGsd = true;
  }

  // eo:bands are carried separately and merged after model serialization.
  Json::Value eoBands( Json::arrayValue );
  for ( const BandInfo &band : metadata.bands )
  {
    Json::Value eoBand( Json::objectValue );
    eoBand["name"] = band.description.empty()
                       ? ( band.role.empty() ? "b" + std::to_string( band.index ) : band.role )
                       : band.description;
    if ( band.hasWavelength )
      eoBand["center_wavelength"] = band.wavelengthNm / 1000.0; // µm per eo extension
    if ( band.hasFwhm )
      eoBand["full_width_half_max"] = band.fwhmNm / 1000.0;
    eoBands.append( eoBand );
  }

  StacAsset asset;
  asset.href = assetHref;
  if ( metadata.driver == "GTiff" || metadata.driver == "COG" )
    asset.mediaType = "image/tiff; application=geotiff";
  asset.roles.emplace_back( "data" );
  item.assets["data"] = asset;

  // Serialize through the model for vocabulary consistency, then merge the
  // eo:bands derived from canonical band metadata.
  Json::Value itemJson = item.toJson();
  if ( eoBands.size() > 0 )
    itemJson["properties"]["eo:bands"] = eoBands;
  return itemJson;
}

} // namespace sicnu::geo
