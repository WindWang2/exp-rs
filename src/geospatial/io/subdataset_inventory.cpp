/***************************************************************************
  geospatial/io/subdataset_inventory.cpp
  Geospatial I/O, COG & Interchange 11.0 — HDF/NetCDF/VRT subdataset
  inventory, safe-URI accounting and selection projection.
 ***************************************************************************/

#include "geospatial/io/subdataset_inventory.h"

#include "geospatial/io/param_guard.h"
#include "geospatial/util/resource_uri.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cpl_error.h>

#include <cstdlib>

namespace sicnu::geo::io
{

namespace
{

void ensureGdalRegistered()
{
  static bool registered = [] {
    GDALAllRegister();
    return true;
  }();
  ( void )registered;
}

Json::Value entryToJson( const SubdatasetEntry &entry )
{
  Json::Value json;
  json["index"] = entry.index;
  json["name"] = entry.name; // GDAL SDS names carry no credentials (local payload path)
  json["description"] = entry.description;
  json["display"] = entry.display;
  json["kind"] = entry.kind;
  if ( !entry.embeddedLocalPath.empty() )
    json["embedded_local_path"] = entry.embeddedLocalPath;
  return json;
}

} // namespace

Json::Value SubdatasetEntry::toJson() const
{
  return entryToJson( *this );
}

Json::Value SubdatasetInventory::toJson() const
{
  Json::Value json;
  json["source"] = source;
  json["source_display"] = sourceDisplay;
  json["count"] = static_cast<Json::ArrayIndex>( entries.size() );
  json["truncated"] = truncated;
  Json::Value array( Json::arrayValue );
  for ( const SubdatasetEntry &entry : entries )
    array.append( entryToJson( entry ) );
  json["entries"] = array;
  return json;
}

SubdatasetInventory inventorySubdatasets( const std::string &source, int maxEntries )
{
  ensureGdalRegistered();
  if ( maxEntries < 1 || maxEntries > kMaxSubdatasetEntries )
  {
    Json::Value details;
    details["max_entries"] = maxEntries;
    details["cap"] = kMaxSubdatasetEntries;
    throw GeoError( ErrorCode::InvalidArgument, "subdataset inventory cap outside bounds", details );
  }

  CPLErrorStateBackuper errorBackuper( CPLQuietErrorHandler );
  GDALDatasetH handle = GDALOpenEx( source.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER, nullptr, nullptr, nullptr );
  if ( !handle )
  {
    const char *lastError = CPLGetLastErrorMsg();
    Json::Value details;
    details["display"] = ResourceUri::parse( source ).display();
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::OpenFailed, "subdataset inventory: cannot open source", details );
  }
  CSLConstList subdatasets = GDALGetMetadata( handle, "SUBDATASETS" );
  SubdatasetInventory inventory;
  inventory.source = source;
  inventory.sourceDisplay = ResourceUri::parse( source ).display();
  if ( !subdatasets )
  {
    GDALClose( handle );
    Json::Value details;
    details["display"] = inventory.sourceDisplay;
    throw GeoError( ErrorCode::InvalidArgument, "source carries no SUBDATASETS domain", details );
  }

  // Count declared entries first so truncation is honest.
  int declared = 0;
  for ( const char *const *item = subdatasets; *item; ++item )
  {
    if ( std::string( *item ).rfind( "SUBDATASET_", 0 ) == 0
         && std::string( *item ).find( "_NAME=" ) != std::string::npos )
      ++declared;
  }

  int seen = 0;
  for ( const char *const *item = subdatasets; *item; ++item )
  {
    const std::string text = *item;
    if ( text.rfind( "SUBDATASET_", 0 ) != 0 )
      continue;
    const std::size_t nameAt = text.find( "_NAME=" );
    if ( nameAt == std::string::npos )
      continue;
    ++seen;
    if ( seen > maxEntries )
      break;
    SubdatasetEntry entry;
    entry.index = seen;
    entry.name = text.substr( nameAt + 6 );
    // Descriptions ride the sibling "_DESC=" lines (second pass below).
    const ResourceUri uri = ResourceUri::parse( entry.name );
    entry.kind = resourceKindName( uri.kind );
    entry.display = uri.display();
    entry.embeddedLocalPath = uri.embeddedLocalPath();
    inventory.entries.push_back( entry );
  }

  // Second pass for descriptions (GDAL lists NAME/DESC as separate lines of
  // the same index).
  for ( const char *const *item = subdatasets; *item; ++item )
  {
    const std::string text = *item;
    if ( text.rfind( "SUBDATASET_", 0 ) != 0 )
      continue;
    const std::size_t descAt = text.find( "_DESC=" );
    if ( descAt == std::string::npos )
      continue;
    const int index = std::atoi( text.substr( 11 ).c_str() ); // "SUBDATASET_<n>_DESC=..."
    if ( index >= 1 && index <= static_cast<int>( inventory.entries.size() ) )
      inventory.entries[static_cast<std::size_t>( index ) - 1].description = text.substr( descAt + 6 );
  }

  inventory.truncated = declared > maxEntries;
  GDALClose( handle );
  return inventory;
}

RasterMetadata inspectSubdataset( const std::string &subdatasetName )
{
  if ( subdatasetName.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "subdataset selector is empty" );
  const ResourceUri uri = ResourceUri::parse( subdatasetName );
  // Trust boundary: only real subdataset selectors are projected. Plain local
  // paths belong to inspectRaster; refusing here prevents the inventory and
  // the projection from drifting into two overlapping surfaces.
  if ( uri.kind != ResourceKind::Subdataset )
  {
    Json::Value details;
    details["kind"] = resourceKindName( uri.kind );
    details["display"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "not a subdataset selector; inspect plain sources with inspectRaster", details );
  }
  return inspectRaster( subdatasetName );
}

} // namespace sicnu::geo::io
