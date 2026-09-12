/***************************************************************************
  geospatial/formats/format_profiles.cpp
  Geospatial I/O Foundation 4.0 — certified format profile registry.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/formats/format_profiles.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/util/resource_uri.h"

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

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
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.notes = "Certified: GPKG <-> FlatGeobuf round-trip with filtered streaming reads "
              "(tests/test_io_roundtrip_matrix.cpp); driver presence resolved at runtime.";
    profiles.push_back( p );
  }
  {
    FormatProfile p;
    p.id = "GeoParquet";
    p.displayName = "GeoParquet";
    p.family = FormatFamily::Vector;
    // The GDAL Parquet driver appears only in builds compiled with Arrow
    // support — capability queries must degrade truthfully elsewhere. The
    // 8.0 round-trip certification (fields, nulls, empty-vs-null, polygon +
    // null geometry, projected CRS, atomic staged publish) is proven by
    // tests/test_io_vector_interop.cpp on stacks where the driver can
    // actually create datasets.
    p.driverNames = { "Parquet" };
    p.extensions = { "parquet", "geoparquet" };
    p.certification = Certification::Certified;
    p.supportsRead = true;
    p.supportsWrite = true;
    p.supportsStreaming = true;
    p.notes = "Certified round-trip where the Parquet driver is create-capable "
              "(verified on the current Linux CI stack); driver-gated elsewhere — capability "
              "queries answer unavailable, never a claimed fidelity. Layer name "
              "on read is the file stem (GDAL Parquet convention).";
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

// ---------------------------------------------------------------------------
// Dataset-level capability resolution (5.0)
// ---------------------------------------------------------------------------

DataCapability operator|( DataCapability a, DataCapability b )
{
  return static_cast<DataCapability>( static_cast<std::uint64_t>( a ) |
                                      static_cast<std::uint64_t>( b ) );
}

DataCapability &operator|=( DataCapability &value, DataCapability flag )
{
  value = value | flag;
  return value;
}

bool hasCapability( DataCapability value, DataCapability flag )
{
  return ( static_cast<std::uint64_t>( value ) & static_cast<std::uint64_t>( flag ) ) != 0;
}

std::vector<std::string> capabilityNames( DataCapability caps )
{
  static const struct
  {
    DataCapability flag;
    const char *name;
  } kNames[] = {
    { DataCapability::Read, "read" },
    { DataCapability::Write, "write" },
    { DataCapability::Update, "update" },
    { DataCapability::WindowRead, "window_read" },
    { DataCapability::BlockRead, "block_read" },
    { DataCapability::RandomAccess, "random_access" },
    { DataCapability::Multiband, "multiband" },
    { DataCapability::Multidim, "multidim" },
    { DataCapability::Subdataset, "subdataset" },
    { DataCapability::Georeferencing, "georeferencing" },
    { DataCapability::Crs, "crs" },
    { DataCapability::NoData, "nodata" },
    { DataCapability::Mask, "mask" },
    { DataCapability::Overviews, "overviews" },
    { DataCapability::Metadata, "metadata" },
    { DataCapability::RemoteRange, "remote_range" },
    { DataCapability::Streaming, "streaming" },
    { DataCapability::Vector, "vector" },
    { DataCapability::Attributes, "attributes" },
    { DataCapability::Transactions, "transactions" },
  };
  std::vector<std::string> names;
  for ( const auto &entry : kNames )
  {
    if ( hasCapability( caps, entry.flag ) )
      names.push_back( entry.name );
  }
  return names;
}

Json::Value ResolvedCapabilities::toJson() const
{
  Json::Value json;
  Json::Value list( Json::arrayValue );
  for ( const std::string &name : capabilityNames( caps ) )
    list.append( name );
  json["capabilities"] = list;
  json["profile_id"] = profileId;
  json["driver"] = driver;
  json["remote"] = remote;
  json["is_cog"] = isCog;
  json["notes"] = notes;
  return json;
}

ResolvedCapabilities resolveDatasetCapabilities( const std::string &path )
{
  ensureGdalRegistered();
  const ResourceUri uri = ResourceUri::parse( path );
  if ( uri.kind == ResourceKind::Invalid )
    throw GeoError( ErrorCode::NotFound, "resource does not exist: " + uri.display() );

  ResolvedCapabilities resolved;
  resolved.remote = uri.isRemote();
  const FormatRegistry &registry = FormatRegistry::instance();
  if ( const FormatProfile *profile = registry.profileForPath( uri.canonical() ) )
    resolved.profileId = profile->id;

  QuietCplErrors quiet;
  const std::string gdalPath = uri.canonical();
  GDALDatasetH dataset =
    GDALOpenEx( gdalPath.c_str(), GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr );
  if ( dataset == nullptr )
    throw GeoError( ErrorCode::OpenFailed, "dataset could not be opened: " + uri.display() );
  GdalDatasetGuard guard( dataset );

  if ( GDALDriverH driver = GDALGetDatasetDriver( dataset ) )
    resolved.driver = GDALGetDriverShortName( driver );

  if ( resolved.driver == "COG" )
    resolved.isCog = true;

  const int bandCount = GDALGetRasterCount( dataset );
  const bool isRaster = bandCount > 0 || GDALGetRasterXSize( dataset ) > 0;

  if ( isRaster )
  {
    resolved.caps |= DataCapability::Read | DataCapability::WindowRead | DataCapability::BlockRead |
                     DataCapability::RandomAccess;
    if ( bandCount > 1 )
      resolved.caps |= DataCapability::Multiband;

    // Metadata domains: SUBDATASETS is its own capability; any other custom
    // domain beyond the structural defaults marks rich metadata.
    if ( char **domainList = GDALGetMetadataDomainList( dataset ) )
    {
      for ( char **entry = domainList; *entry; ++entry )
      {
        if ( std::strcmp( *entry, "SUBDATASETS" ) == 0 )
          resolved.caps |= DataCapability::Subdataset;
        else if ( std::strcmp( *entry, "IMAGE_STRUCTURE" ) != 0 &&
                  std::strcmp( *entry, "DERIVED_SUBDATASETS" ) != 0 )
          resolved.caps |= DataCapability::Metadata;
      }
      CSLDestroy( domainList );
    }

    // Georeferencing + CRS.
    double geotransform[6] = { 0, 1, 0, 0, 0, 1 };
    const bool hasGeotransform = GDALGetGeoTransform( dataset, geotransform ) == CE_None;
    const bool hasGcps = GDALGetGCPCount( dataset ) > 0;
    if ( hasGeotransform || hasGcps )
      resolved.caps |= DataCapability::Georeferencing;
    const std::string wkt = GDALGetProjectionRef( dataset ) ? GDALGetProjectionRef( dataset ) : "";
    if ( !wkt.empty() )
      resolved.caps |= DataCapability::Crs;

    // Per-band: NoData / mask presence.
    bool anyNoData = false;
    bool anyMask = false;
    for ( int bandNumber = 1; bandNumber <= bandCount; ++bandNumber )
    {
      GDALRasterBandH band = GDALGetRasterBand( dataset, bandNumber );
      if ( band == nullptr )
        continue;
      int hasNoData = 0;
      GDALGetRasterNoDataValue( band, &hasNoData );
      if ( hasNoData )
        anyNoData = true;
      const int maskFlags = GDALGetMaskFlags( band );
      if ( maskFlags & ( GMF_PER_DATASET | GMF_ALPHA ) )
        anyMask = true;
    }
    if ( anyNoData )
      resolved.caps |= DataCapability::NoData;
    if ( anyMask )
      resolved.caps |= DataCapability::Mask;

    GDALRasterBandH firstBand = GDALGetRasterBand( dataset, 1 );
    if ( firstBand != nullptr )
    {
      const int overviewCount = GDALGetOverviewCount( firstBand );
      if ( overviewCount > 0 )
        resolved.caps |= DataCapability::Overviews;
      else
        resolved.notes["overviews"] = "none present";
    }

    // Multidim API present?
    if ( GDALDatasetGetRootGroup( dataset ) != nullptr )
      resolved.caps |= DataCapability::Multidim;

    // Remote range: remote sources through range-capable drivers inherit the
    // remote support; full-file drivers (e.g. remote shapefiles) do not.
    if ( resolved.remote && ( resolved.driver == "GTiff" || resolved.driver == "COG" ||
                              resolved.driver == "netCDF" || resolved.driver == "HDF5" ) )
      resolved.caps |= DataCapability::RemoteRange;
    resolved.caps |= DataCapability::Streaming;
  }

  const int layerCount = GDALDatasetGetLayerCount( dataset );
  if ( layerCount > 0 )
  {
    resolved.caps |= DataCapability::Vector | DataCapability::Read;
    OGRLayerH layer = GDALDatasetGetLayer( dataset, 0 );
    if ( layer != nullptr )
    {
      if ( OGR_L_GetLayerDefn( layer ) != nullptr )
        resolved.caps |= DataCapability::Attributes;
      if ( OGR_L_TestCapability( layer, OLCSequentialWrite ) )
        resolved.caps |= DataCapability::Write;
      if ( OGR_L_TestCapability( layer, OLCTransactions ) )
        resolved.caps |= DataCapability::Transactions;
      if ( OGR_L_TestCapability( layer, OLCRandomRead ) )
        resolved.caps |= DataCapability::RandomAccess;
    }
    if ( resolved.remote )
      resolved.notes["remote_vector"] = "remote vector access is stream-only; not range-verified";
  }

  // Multidim-only stores (no raster bands, no layers, but a root group).
  if ( !isRaster && layerCount == 0 && GDALDatasetGetRootGroup( dataset ) != nullptr )
  {
    resolved.caps |= DataCapability::Read | DataCapability::Multidim | DataCapability::Streaming;
    if ( resolved.remote )
      resolved.caps |= DataCapability::RemoteRange;
  }

  if ( resolved.profileId.empty() )
    resolved.notes["profile"] = "no certified profile matches this extension";

  return resolved;
}

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
  // Capability claims answer false when the backing driver is absent —
  // a declared capability the running GDAL cannot exercise must never be
  // advertised (the certification downgrade above is not enough on its own).
  json["capabilities"] = [ & ] {
    const bool live = driverAvailable;
    Json::Value caps;
    caps["read"] = live && supportsRead;
    caps["write"] = live && supportsWrite;
    caps["streaming"] = live && supportsStreaming;
    caps["remote"] = live && remoteCapable;
    caps["crs_preserved"] = live && preservesCrs;
    caps["nodata_preserved"] = live && preservesNoData;
    caps["scale_offset_preserved"] = live && preservesScaleOffset;
    caps["band_metadata_preserved"] = live && preservesBandMetadata;
    caps["attributes_preserved"] = live && preservesAttributes;
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

const std::vector<FormatProfile> &FormatRegistry::profiles() const
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
