/***************************************************************************
  geospatial/formats/format_profiles.cpp
  Geospatial I/O Foundation 4.0 — certified format profile registry.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/formats/format_profiles.h"

#include "geospatial/gdal_guard.h"

#include <gdal.h>

#include <algorithm>
#include <cctype>

namespace sicnu::geo
{
namespace
{

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

std::vector<FormatProfile> buildDeclaredProfiles()
{
  std::vector<FormatProfile> profiles;

  // ─── Raster ──────────────────────────────────────────────────────────────
  {
    FormatProfile p;
    p.id = "GeoTIFF";
    p.displayName = "GeoTIFF / BigTIFF";
    p.family = FormatFamily::Raster;
    p.driverNames = { "GTiff" };
    p.extensions = { "tif", "tiff" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.remoteCapable = true;
    p.preservesCrs = true;
    p.preservesNoData = true;
    p.preservesScaleOffset = true;
    p.preservesBandMetadata = true;
    p.notes = "Round-trip certified incl. nodata/scale/offset/roles/wavelengths; "
              "BigTIFF auto-policy via BIGTIFF creation option in COG presets.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "COG";
    p.displayName = "Cloud Optimized GeoTIFF";
    p.family = FormatFamily::Raster;
    p.driverNames = { "COG", "GTiff" };
    p.extensions = { "tif", "tiff" };
    p.certification = Certification::Certified;
    p.supportsRead = true;   // reads as any GeoTIFF
    p.supportsWrite = true;  // through the COG driver CreateCopy (io:make_cog)
    p.supportsStreaming = true;
    p.remoteCapable = true;
    p.preservesCrs = true;
    p.preservesNoData = true;
    p.preservesScaleOffset = true;
    p.preservesBandMetadata = true;
    p.notes = "Write path goes through the COG driver + validator + safe presets; "
              "categorical products default to lossless.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "VRT";
    p.displayName = "GDAL Virtual Raster";
    p.family = FormatFamily::Raster;
    p.driverNames = { "VRT" };
    p.extensions = { "vrt" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.remoteCapable = true;
    p.preservesCrs = true;
    p.preservesNoData = true;
    p.notes = "Certified for simple source-list VRTs (translate/mosaic recipes); "
              "pixel functions are Accessible-only.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "NetCDF";
    p.displayName = "NetCDF (CF conventions)";
    p.family = FormatFamily::Multidim;
    p.driverNames = { "netCDF" };
    p.extensions = { "nc" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = false; // conversion OUT of NetCDF is certified; CF writing stays Accessible
    p.supportsStreaming = true;
    p.remoteCapable = true;
    p.preservesCrs = true;
    p.preservesNoData = true;
    p.preservesScaleOffset = true;
    p.notes = "Variable/time/level semantics via the multidim contract; slice→GeoTIFF round-trip certified. "
              "Degrades to Accessible when the driver or MDArray API is missing.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "HDF";
    p.displayName = "HDF4 / HDF5 (subdataset view)";
    p.family = FormatFamily::Multidim;
    p.driverNames = { "HDF5", "HDF4", "HDF5Image", "HDF4Image" };
    p.extensions = { "h5", "hdf", "hdf5", "hdf4" };
    p.certification = Certification::Accessible;
    p.supportsRead = true;
    p.supportsWrite = false;
    p.supportsStreaming = true;
    p.notes = "Read via GDAL subdatasets; MODIS import is certified separately through product adapters.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "PNG-JPEG";
    p.displayName = "PNG / JPEG (visualization)";
    p.family = FormatFamily::Raster;
    p.driverNames = { "PNG", "JPEG" };
    p.extensions = { "png", "jpg", "jpeg" };
    p.certification = Certification::Accessible;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = false;
    p.notes = "Visualization only: no CRS, no nodata fidelity, lossy (JPEG). Never for scientific products.";
    profiles.push_back( p );
  }

  // ─── Vector ──────────────────────────────────────────────────────────────
  {
    FormatProfile p;
    p.id = "GeoPackage";
    p.displayName = "GeoPackage";
    p.family = FormatFamily::Vector;
    p.driverNames = { "GPKG" };
    p.extensions = { "gpkg" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.remoteCapable = false;
    p.preservesCrs = true;
    p.preservesAttributes = true;
    p.notes = "Preferred vector container: single file, atomic publish is a single rename.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "GeoJSON";
    p.displayName = "GeoJSON / GeoJSONSeq";
    p.family = FormatFamily::Vector;
    p.driverNames = { "GeoJSON", "GeoJSONSeq" };
    p.extensions = { "geojson", "json" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.remoteCapable = true;
    p.preservesCrs = true;
    p.preservesAttributes = true;
    p.notes = "Round-trip certified against GeoPackage (RFC 7946 WGS84 semantics).";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "Shapefile";
    p.displayName = "ESRI Shapefile";
    p.family = FormatFamily::Vector;
    p.driverNames = { "ESRI Shapefile" };
    p.extensions = { "shp" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.notes = "Multi-file group published atomically as a group (sidecars first, .shp last). "
              "Field-name truncation and 2 GB limits documented in the migration guide.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "FlatGeobuf";
    p.displayName = "FlatGeobuf";
    p.family = FormatFamily::Vector;
    p.driverNames = { "FlatGeobuf" };
    p.extensions = { "fgb" };
    p.certification = Certification::Accessible;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.notes = "Certification pending streaming-filter round-trip coverage on this stack.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "CSV-XY";
    p.displayName = "CSV (XY points)";
    p.family = FormatFamily::Vector;
    p.driverNames = { "CSV" };
    p.extensions = { "csv" };
    p.certification = Certification::Accessible;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.notes = "XY geometry requires explicit X_POSSIBLE_NAMES/Y_POSSIBLE_NAMES open options; CRS never guessed.";
    profiles.push_back( p );
  }

  // ─── Container / cloud ───────────────────────────────────────────────────
  {
    FormatProfile p;
    p.id = "STAC";
    p.displayName = "SpatioTemporal Asset Catalog";
    p.family = FormatFamily::Container;
    p.driverNames = {};
    p.extensions = { "json" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;  // canonical metadata → STAC-compatible Item JSON
    p.supportsStreaming = false;
    p.remoteCapable = true;
    p.notes = "Item/Collection parse → CanonicalMetadata and back; asset hrefs ride /vsicurl/ when remote.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "Zarr";
    p.displayName = "Zarr (multidimensional)";
    p.family = FormatFamily::Multidim;
    p.driverNames = { "Zarr" };
    p.extensions = { "zarr", "zarr.json" };
    p.certification = Certification::Accessible;
    p.supportsRead = true;
    p.supportsWrite = false;
    p.supportsStreaming = true;
    p.remoteCapable = true;
    p.notes = "Offered through the multidim contract when the driver is present; certification pending.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "Remote-HTTP";
    p.displayName = "Remote datasets (/vsicurl/, /vsis3/, /vsigs/, /vsiaz/)";
    p.family = FormatFamily::Container;
    p.driverNames = { "VSICURL", "VSIS3" };
    p.extensions = {};
    p.certification = Certification::Accessible;
    p.supportsRead = true;
    p.supportsWrite = false;
    p.supportsStreaming = true;
    p.remoteCapable = true;
    p.notes = "Range-read capable raster formats (GeoTIFF/COG/NetCDF) inherit remote support; "
              "credentials stay in GDAL config, never in metadata.";
    profiles.push_back( p );
  }

  return profiles;
}

} // namespace

Json::Value FormatProfile::toJson( bool driverAvailable ) const
{
  Json::Value json;
  json["id"] = id;
  json["display_name"] = displayName;
  json["family"] = family == FormatFamily::Raster ? "raster"
                   : family == FormatFamily::Vector ? "vector"
                   : family == FormatFamily::Multidim ? "multidimensional"
                                                      : "container";
  json["extensions"] = [ & ] {
    Json::Value list( Json::arrayValue );
    for ( const std::string &extension : extensions )
      list.append( extension );
    return list;
  }();
  json["certification"] = certification == Certification::Certified ? "certified"
                          : certification == Certification::Accessible ? "accessible"
                                                                       : "unsupported";
  if ( !driverAvailable )
    json["certification"] = "unavailable_in_build";
  json["capabilities"] = [ & ] {
    Json::Value caps;
    caps["read"] = supportsRead;
    caps["write"] = supportsWrite;
    caps["streaming"] = supportsStreaming;
    caps["remote"] = remoteCapable;
    caps["crs_preserved"] = preservesCrs;
    caps["nodata_preserved"] = preservesNoData;
    caps["scale_offset_preserved"] = preservesScaleOffset;
    caps["band_metadata_preserved"] = preservesBandMetadata;
    caps["attributes_preserved"] = preservesAttributes;
    return caps;
  }();
  json["drivers"] = [ & ] {
    Json::Value list( Json::arrayValue );
    for ( const std::string &driver : driverNames )
      list.append( driver );
    return list;
  }();
  json["notes"] = notes;
  return json;
}

FormatRegistry::FormatRegistry()
  : mProfiles( buildDeclaredProfiles() )
{
}

const FormatRegistry &FormatRegistry::instance()
{
  static const FormatRegistry registry;
  return registry;
}

std::vector<FormatProfile> FormatRegistry::profiles() const
{
  return mProfiles;
}

const FormatProfile *FormatRegistry::find( const std::string &id ) const
{
  for ( const FormatProfile &profile : mProfiles )
  {
    if ( profile.id == id )
      return &profile;
  }
  return nullptr;
}

const FormatProfile *FormatRegistry::profileForPath( const std::string &path ) const
{
  const std::size_t dot = path.rfind( '.' );
  if ( dot == std::string::npos )
    return nullptr;
  const std::string extension = lowerAscii( path.substr( dot + 1 ) );
  for ( const FormatProfile &profile : mProfiles )
  {
    if ( std::find( profile.extensions.begin(), profile.extensions.end(), extension ) != profile.extensions.end() )
      return &profile;
  }
  return nullptr;
}

bool FormatRegistry::driverAvailable( const FormatProfile &profile ) const
{
  if ( profile.driverNames.empty() )
    return true; // non-GDAL container (STAC)
  ensureGdalRegistered();
  for ( const std::string &driverName : profile.driverNames )
  {
    if ( GDALGetDriverByName( driverName.c_str() ) )
      return true;
  }
  return false;
}

Json::Value FormatRegistry::supportMatrixJson() const
{
  Json::Value json;
  json["format_version"] = 1;
  json["note"] = "certified = round-trip tested in this repository; "
                 "accessible = GDAL may open it but ExpRS makes no fidelity claim; "
                 "unavailable_in_build = no backing driver in this process.";
  json["generated_by"] = "sicnu_geospatial FormatRegistry";
  Json::Value list( Json::arrayValue );
  for ( const FormatProfile &profile : mProfiles )
    list.append( profile.toJson( driverAvailable( profile ) ) );
  json["formats"] = list;
  return json;
}

} // namespace sicnu::geo
