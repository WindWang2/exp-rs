/***************************************************************************
  geospatial/products/product_adapters.cpp
  Geospatial I/O Foundation 4.0 — RS product metadata adapters.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Landsat MTL and the Sentinel XML documents are parsed with a small expat
  scanner over a whitelist of tags (no Qt, no regex guesswork). Everything is
  read-only; sidecar files never influence CRS resolution beyond carrying the
  product's *declared* projection text.
 ***************************************************************************/

#include "geospatial/products/product_adapters.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>
#include <cstdio>
#include <cstring>
#include <expat.h>
#include <fstream>
#include <map>
#include <sstream>

namespace sicnu::geo
{
namespace
{

std::string trim( const std::string &text )
{
  std::size_t begin = 0;
  std::size_t end = text.size();
  while ( begin < end && std::isspace( static_cast<unsigned char>( text[begin] ) ) )
    ++begin;
  while ( end > begin && std::isspace( static_cast<unsigned char>( text[end - 1] ) ) )
    --end;
  return text.substr( begin, end - begin );
}

std::string upperAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
  return text;
}

namespace fs = std::filesystem;

bool fileExistsLocal( const std::string &path )
{
  std::error_code ec;
  const fs::file_status status = fs::status( fs::u8path( path ), ec );
  return !ec && fs::exists( status );
}

bool readFileText( const std::string &path, std::string &out )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in.is_open() )
    return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

// ─── Landsat MTL ─────────────────────────────────────────────────────────────

/// MTL is "KEY = value" lines, possibly quoted, grouped into GROUP blocks.
/// Collection-2 nesting (LANDSAT_PRODUCT_ID inside LEVEL1_PROCESSING_RECORD)
/// is flat enough that a last-wins key map captures the relevant fields.
std::map<std::string, std::string> parseMtlKeyValues( const std::string &text )
{
  std::map<std::string, std::string> values;
  std::istringstream stream( text );
  std::string line;
  while ( std::getline( stream, line ) )
  {
    const std::size_t eq = line.find( '=' );
    if ( eq == std::string::npos )
      continue;
    const std::string key = trim( line.substr( 0, eq ) );
    std::string value = trim( line.substr( eq + 1 ) );
    if ( value.size() >= 2 && value.front() == '"' && value.back() == '"' )
      value = value.substr( 1, value.size() - 2 );
    if ( !key.empty() && !value.empty() )
      values[upperAscii( key )] = value;
  }
  return values;
}

std::pair<double, bool> mtlDouble( const std::map<std::string, std::string> &values, const char *key )
{
  const auto it = values.find( upperAscii( key ) );
  if ( it == values.end() )
    return { 0.0, false };
  try
  {
    return { std::stod( it->second ), true };
  }
  catch ( const std::exception & )
  {
    return { 0.0, false };
  }
}

ProductMetadata readLandsatMtl( const std::string &path )
{
  std::string text;
  if ( !readFileText( path, text ) )
    throw GeoError( ErrorCode::OpenFailed, "Cannot read Landsat MTL: " + path );

  const std::map<std::string, std::string> values = parseMtlKeyValues( text );
  ProductMetadata product;
  product.productId = values.count( "LANDSAT_PRODUCT_ID" ) ? values.at( "LANDSAT_PRODUCT_ID" ) : std::string();
  product.platform = values.count( "SPACECRAFT_ID" ) ? values.at( "SPACECRAFT_ID" ) : std::string();
  product.sensor = values.count( "SENSOR_ID" ) ? values.at( "SENSOR_ID" ) : std::string();
  product.processingLevel = values.count( "COLLECTION_NUMBER" ) ? ( "C" + values.at( "COLLECTION_NUMBER" ) )
                                                                : std::string();
  const auto dateIt = values.find( "DATE_ACQUIRED" );
  const auto timeIt = values.find( "SCENE_CENTER_TIME" );
  if ( dateIt != values.end() )
  {
    product.acquisitionTime = dateIt->second;
    if ( timeIt != values.end() )
      product.acquisitionTime += "T" + timeIt->second;
  }
  product.hasCloudCover = values.count( "CLOUD_COVER" ) > 0;
  if ( product.hasCloudCover )
    product.cloudCover = mtlDouble( values, "CLOUD_COVER" ).first;
  product.modality = "optical";
  product.radiometricState = "digital_number";
  if ( values.count( "MAP_PROJECTION" ) )
  {
    product.crsHint = values.at( "MAP_PROJECTION" );
    if ( values.count( "UTM_ZONE" ) )
      product.crsHint += " zone " + values.at( "UTM_ZONE" );
  }

  // Radiometric state, from the MTL's own group structure: Collection-2
  // Level-2 MTLs carry REFLECTANCE_MULT_BAND_1 too (inside the
  // LEVEL2_SURFACE_REFLECTANCE group), so its presence alone cannot
  // distinguish TOA from surface reflectance.
  const bool hasLevel2Group = values.count( "LEVEL2_SURFACE_REFLECTANCE" ) > 0 ||
                              values.count( "FILE_NAME_SURFACE_REFLECTANCE" ) > 0 ||
                              values.count( "FILE_NAME_ST_B10" ) > 0;
  const bool l2Product = values.count( "COLLECTION_CATEGORY" ) &&
                         values.at( "COLLECTION_CATEGORY" ).find( "L2" ) != std::string::npos;
  if ( hasLevel2Group || l2Product )
    product.radiometricState = "surface_reflectance";
  else if ( values.count( "REFLECTANCE_MULT_BAND_1" ) )
    product.radiometricState = "toa_reflectance";

  // Declared rescaling constants are REPORTED, never applied (ADR 0137):
  // carry the band-1 reflectance/radiance rescaling factors as declared
  // values so calibration consumers can apply them deliberately.
  static const char *const kRadiometricKeys[] = {
    "REFLECTANCE_MULT_BAND_1", "REFLECTANCE_ADD_BAND_1",
    "RADIANCE_MULT_BAND_1", "RADIANCE_ADD_BAND_1",
    "TEMPERATURE_MULT_BAND_ST_B10", "TEMPERATURE_ADD_BAND_ST_B10",
    "SUN_ELEVATION", "EARTH_SUN_DISTANCE",
  };
  for ( const char *key : kRadiometricKeys )
  {
    const auto it = values.find( key );
    if ( it != values.end() && product.extra.size() < 16 )
      product.extra.emplace_back( key, it->second );
  }
  return product;
}

// ─── XML sidecars (Sentinel-2 / Sentinel-1) ─────────────────────────────────

struct XmlScan
{
  std::map<std::string, std::string> values; // lowercased tag → first char data
  std::vector<std::string> listValues;       // repeated tags (polarizations) in order
  std::vector<std::string> watched;
  std::string current;
  std::ostringstream buffer;
};

void xmlStart( void *user, const XML_Char *element, const XML_Char ** )
{
  auto *scan = static_cast<XmlScan *>( user );
  std::string lower;
  for ( const char *p = element; p && *p; ++p )
    lower.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( *p ) ) ) );
  scan->current = lower;
  scan->buffer.str( std::string() );
  scan->buffer.clear();
}

void xmlEnd( void *user, const XML_Char * )
{
  auto *scan = static_cast<XmlScan *>( user );
  const std::string text = trim( scan->buffer.str() );
  if ( !scan->current.empty() && !text.empty() )
  {
    if ( std::find( scan->watched.begin(), scan->watched.end(), scan->current ) != scan->watched.end() )
    {
      if ( !scan->values.count( scan->current ) )
        scan->values[scan->current] = text;
      else
        scan->listValues.push_back( text );
    }
  }
  scan->current.clear();
}

void xmlChars( void *user, const XML_Char *data, int length )
{
  auto *scan = static_cast<XmlScan *>( user );
  scan->buffer.write( data, length );
}

XmlScan scanXml( const std::string &path, std::vector<std::string> watchedTags )
{
  std::string text;
  if ( !readFileText( path, text ) )
    throw GeoError( ErrorCode::OpenFailed, "Cannot read product metadata: " + path );

  XmlScan scan;
  scan.watched = std::move( watchedTags );
  XML_Parser parser = XML_ParserCreate( nullptr );
  if ( !parser )
    throw GeoError( ErrorCode::OpenFailed, "Cannot allocate XML parser for " + path );
  XML_SetUserData( parser, &scan );
  XML_SetElementHandler( parser, &xmlStart, &xmlEnd );
  XML_SetCharacterDataHandler( parser, &xmlChars );
  if ( XML_Parse( parser, text.data(), static_cast<int>( text.size() ), 1 ) == XML_STATUS_ERROR )
  {
    const XML_LChar *error = XML_ErrorString( XML_GetErrorCode( parser ) );
    XML_ParserFree( parser );
    throw GeoError( ErrorCode::InvalidArgument,
                    std::string( "Malformed product XML: " ) + ( error ? reinterpret_cast<const char *>( error ) : "parse error" ) );
  }
  XML_ParserFree( parser );
  return scan;
}

std::string scanValue( const XmlScan &scan, const char *tag )
{
  const auto it = scan.values.find( tag );
  return it == scan.values.end() ? std::string() : it->second;
}

bool looksLikeSentinel2Directory( const std::string &path )
{
  const std::string name = upperAscii( path );
  return name.find( ".SAFE" ) != std::string::npos && name.find( "S2" ) != std::string::npos;
}

ProductMetadata readSentinel2( const std::string &path )
{
  // Product-level MTD or the .SAFE directory: locate MTD_MSIL1C/2A.xml beside
  // or inside the given path.
  std::string base = path;
  std::string mtdPath = path;
  if ( looksLikeSentinel2Directory( path ) )
  {
    // Replace the granule part: look for MTD_*.xml in the .SAFE root.
    const std::size_t safe = base.rfind( ".SAFE" );
    const std::string root = safe == std::string::npos ? base : base.substr( 0, safe + 5 );
    mtdPath = root + "/MTD_MSIL1C.xml";
    if ( !fileExistsLocal( mtdPath ) )
    {
      mtdPath = root + "/MTD_MSIL2A.xml";
      if ( !fileExistsLocal( mtdPath ) )
        throw GeoError( ErrorCode::OpenFailed, "Sentinel-2 product metadata not found under " + root );
    }
  }

  ProductMetadata product;
  // Platform from the directory/zip name (S2A_..._T... → SENTINEL-2A).
  const std::string upper = upperAscii( base );
  // Guard the fabrication path: this reader must never stamp optical
  // semantics onto an unknown sidecar. If no S2 product metadata can be
  // located, refuse — the caller sees a structured error instead of an
  // invented record.
  if ( !looksLikeSentinel2Directory( path ) &&
       upper.find( "MTD_MSIL1C" ) == std::string::npos &&
       upper.find( "MTD_MSIL2A" ) == std::string::npos )
  {
    throw GeoError( ErrorCode::UnsupportedProduct,
                    "Sentinel-2 adapter requires the .SAFE directory or its MTD_MSIL*.xml" );
  }
  if ( upper.find( "S2A_" ) != std::string::npos )
    product.platform = "SENTINEL-2A";
  else if ( upper.find( "S2B_" ) != std::string::npos )
    product.platform = "SENTINEL-2B";
  else if ( upper.find( "S2C_" ) != std::string::npos )
    product.platform = "SENTINEL-2C";

  const bool isL2A = upper.find( "MSIL2A" ) != std::string::npos || upper.find( "_L2A_" ) != std::string::npos
                      || upper.find( "MTD_MSIL2A" ) != std::string::npos;
  // Filename-derived values are HINTS; the MTD XML overrides them when it
  // declares the product type and quantification (authoritative sidecar
  // wins — the foundation's no-fabrication contract).
  product.processingLevel = isL2A ? "Level-2A" : "Level-1C";
  product.radiometricState = isL2A ? "surface_reflectance" : "toa_reflectance";
  product.numericScale = 10000.0;
  product.modality = "optical";
  product.sensor = "MSI";

  if ( fileExistsLocal( mtdPath ) )
  {
    const XmlScan scan = scanXml( mtdPath,
                                  { "product_start_time", "cloud_coverage_assessment", "productive_centre",
                                    "sensing_time", "cloudy_pixel_percentage", "product_type",
                                    "quantification_values_max", "boa_quantification" } );
    const std::string productType = scanValue( scan, "product_type" );
    if ( !productType.empty() )
    {
      // e.g. "S2MSI2A" → Level-2A surface reflectance
      if ( productType.find( "2A" ) != std::string::npos )
      {
        product.processingLevel = "Level-2A";
        product.radiometricState = "surface_reflectance";
      }
      else if ( productType.find( "1C" ) != std::string::npos )
      {
        product.processingLevel = "Level-1C";
        product.radiometricState = "toa_reflectance";
      }
    }
    const std::string quantification = scanValue( scan, "boa_quantification" );
    const std::string quantificationFallback = scanValue( scan, "quantification_values_max" );
    const std::string &declared = !quantification.empty() ? quantification : quantificationFallback;
    if ( !declared.empty() )
    {
      try
      {
        product.numericScale = std::stod( declared );
      }
      catch ( const std::exception & ) { /* unparseable declared value stays default-hinted */ }
    }
    std::string startTime = scanValue( scan, "product_start_time" );
    if ( startTime.empty() )
      startTime = scanValue( scan, "sensing_time" );
    product.acquisitionTime = startTime;
    const std::string cloud = scanValue( scan, "cloud_coverage_assessment" );
    if ( !cloud.empty() )
    {
      try
      {
        product.cloudCover = std::stod( cloud );
        product.hasCloudCover = true;
      }
      catch ( const std::exception & ) { /* declared value unparseable: leave unset */ }
    }
    const std::string cloudy = scanValue( scan, "cloudy_pixel_percentage" );
    if ( !product.hasCloudCover && !cloudy.empty() )
    {
      try
      {
        product.cloudCover = std::stod( cloudy );
        product.hasCloudCover = true;
      }
      catch ( const std::exception & ) { /* ignore */ }
    }
  }
  return product;
}

ProductMetadata readSentinel1( const std::string &path )
{
  // manifest.safe inside the .SAFE directory (or the manifest itself).
  std::string manifestPath = path;
  std::error_code ec;
  const bool isDirectory = fs::is_directory( fs::u8path( path ), ec );
  if ( !ec && isDirectory )
  {
    const fs::path manifest = fs::u8path( path ) / "manifest.safe";
    if ( !fs::exists( manifest ) )
      throw GeoError( ErrorCode::OpenFailed, "Sentinel-1 manifest.safe not found in " + path );
    const std::u8string u8 = manifest.u8string();
    manifestPath = std::string( u8.begin(), u8.end() );
  }
  const XmlScan scan = scanXml( manifestPath,
                                { "safe:platform", "safe:familyname", "s1sarl1:mission_pol_id",
                                  "s1sarl1:polarisation", "safe:pass", "s1sarl1:instrumentmode",
                                  "s1sarl1:producttype", "safe:starttime", "safe:stoptime",
                                  "orbitdirection", "s1sarl1:orbitdirection" } );

  ProductMetadata product;
  product.platform = scanValue( scan, "safe:platform" );
  if ( product.platform.empty() )
  {
    const std::string upper = upperAscii( path );
    if ( upper.find( "S1A_" ) != std::string::npos )
      product.platform = "SENTINEL-1A";
    else if ( upper.find( "S1B_" ) != std::string::npos )
      product.platform = "SENTINEL-1B";
  }
  product.sensor = scanValue( scan, "safe:familyname" ).empty() ? "SAR" : scanValue( scan, "safe:familyname" );
  product.instrumentMode = scanValue( scan, "s1sarl1:instrumentmode" );
  product.processingLevel = scanValue( scan, "s1sarl1:producttype" );
  product.acquisitionTime = scanValue( scan, "safe:starttime" );
  product.orbitDirection = upperAscii( scanValue( scan, "s1sarl1:orbitdirection" ) );
  if ( product.orbitDirection.empty() )
    product.orbitDirection = upperAscii( scanValue( scan, "orbitdirection" ) );
  if ( product.orbitDirection.empty() )
    product.orbitDirection = upperAscii( scanValue( scan, "safe:pass" ) );

  // Polarizations: the first occurrence lands in values, repeats in
  // listValues; collect both in declaration order.
  std::vector<std::string> candidates;
  candidates.push_back( scanValue( scan, "s1sarl1:polarisation" ) );
  for ( const std::string &value : scan.listValues )
    candidates.push_back( value );
  for ( const std::string &value : candidates )
  {
    if ( !value.empty() && value.size() <= 4
         && ( value.find( 'H' ) != std::string::npos || value.find( 'V' ) != std::string::npos ) )
      product.polarizations.push_back( value );
  }
  product.modality = "sar";
  product.radiometricState = "digital_number";
  return product;
}

ProductMetadata readModis( const std::string &path )
{
  // MODIS HDF containers: structure comes from GDAL subdatasets; the adapter
  // carries the well-known sinusoidal/level facts only when declared.
  ProductMetadata product;
  product.modality = "optical";
  product.sensor = "MODIS";
  const std::string upper = upperAscii( path );
  // MOD/MYD prefixes identify the satellite (Terra/Aqua); the processing
  // level is NOT declared by the file name and stays absent unless a real
  // sidecar declares it.
  if ( upper.find( "MYD" ) != std::string::npos )
    product.platform = "AQUA";
  else if ( upper.find( "MOD" ) != std::string::npos )
    product.platform = "TERRA";
  return product;
}

struct BandRoleMapping
{
  const char *name;
  const char *role;
  double wavelengthNm;
};

// Roles use the canonical lowercase vocabulary of src/data/band_role.h
// ("coastal", "blue", ..., "qa", "scene_classification"); SAR polarizations
// extend the same string vocabulary ("vv"/"vh"/"hh"/"hv") rather than
// inventing a sensor-specific enum (ADR 0137).
const BandRoleMapping kSentinel2Bands[] = {
  { "B01", "coastal", 443.0 }, { "B02", "blue", 490.0 }, { "B03", "green", 560.0 },
  { "B04", "red", 665.0 }, { "B05", "red_edge", 705.0 }, { "B06", "red_edge", 740.0 },
  { "B07", "red_edge", 783.0 }, { "B08", "nir", 842.0 }, { "B8A", "narrow_nir", 865.0 },
  { "B09", "", 945.0 }, { "B10", "cirrus", 1375.0 }, { "B11", "swir1", 1610.0 },
  { "B12", "swir2", 2190.0 }, { "SCL", "scene_classification", 0.0 },
  { "VV", "vv", 0.0 }, { "VH", "vh", 0.0 }, { "HH", "hh", 0.0 }, { "HV", "hv", 0.0 },
};

const BandRoleMapping kLandsatBands[] = {
  { "B1", "coastal", 443.0 }, { "B2", "blue", 482.0 }, { "B3", "green", 561.0 },
  { "B4", "red", 655.0 }, { "B5", "nir", 865.0 }, { "B6", "swir1", 1610.0 },
  { "B7", "swir2", 2200.0 }, { "B8", "panchromatic", 590.0 }, { "B9", "cirrus", 1375.0 },
  { "B10", "thermal", 10895.0 }, { "B11", "thermal", 12005.0 }, { "QA", "qa", 0.0 },
};

// Landsat 4-7 (TM / ETM+): B1 blue (not coastal), B6 thermal, B8 pan (ETM+
// only). Used when the MTL SENSOR_ID says TM/ETM — an OLI table would
// mislabel every legacy band.
const BandRoleMapping kLandsatLegacyBands[] = {
  { "B1", "blue", 485.0 }, { "B2", "green", 560.0 }, { "B3", "red", 660.0 },
  { "B4", "nir", 835.0 }, { "B5", "swir1", 1650.0 }, { "B6", "thermal", 11450.0 },
  { "B7", "swir2", 2215.0 }, { "B8", "panchromatic", 710.0 }, { "QA", "qa", 0.0 },
};

template <std::size_t N>
const BandRoleMapping *findBand( const BandRoleMapping ( &table )[N], const std::string &name )
{
  const std::string upper = upperAscii( name );
  for ( const BandRoleMapping &entry : table )
  {
    if ( upper == entry.name )
      return &entry;
  }
  return nullptr;
}

} // namespace

std::map<std::string, std::string> parseLandsatMtlKeys( const std::string &mtlPath )
{
  std::string text;
  if ( !readFileText( mtlPath, text ) )
    return {};
  return parseMtlKeyValues( text );
}

Json::Value ProductMetadata::toJson() const
{
  Json::Value json;
  json["product_id"] = productId;
  json["sensor"] = sensor;
  json["platform"] = platform;
  json["processing_level"] = processingLevel;
  json["acquisition_time"] = acquisitionTime;
  json["radiometric_state"] = radiometricState;
  if ( numericScale != 0.0 )
    json["numeric_scale"] = numericScale;
  json["has_cloud_cover"] = hasCloudCover;
  if ( hasCloudCover )
    json["cloud_cover"] = cloudCover;
  json["has_resolution"] = hasResolution;
  if ( hasResolution )
    json["resolution_m"] = resolutionMeters;
  json["modality"] = modality;
  if ( !polarizations.empty() )
  {
    Json::Value pols( Json::arrayValue );
    for ( const std::string &pol : polarizations )
      pols.append( pol );
    json["polarizations"] = pols;
  }
  json["orbit_direction"] = orbitDirection;
  json["instrument_mode"] = instrumentMode;
  json["crs_hint"] = crsHint;
  if ( !extra.empty() )
  {
    Json::Value extras( Json::objectValue );
    for ( const auto &entry : extra )
      extras[entry.first] = entry.second;
    json["declared"] = extras; // verbatim sidecar values, reported never applied
  }
  return json;
}

ProductKind detectProductKind( const std::string &path )
{
  const std::string upper = upperAscii( path );
  if ( upper.find( "MTL.TXT" ) != std::string::npos || upper.find( "_MTL" ) != std::string::npos )
    return ProductKind::LandsatMtl;
  if ( upper.find( "S1" ) != std::string::npos && upper.find( ".SAFE" ) != std::string::npos )
    return ProductKind::Sentinel1Safe;
  if ( upper.find( "MANIFEST.SAFE" ) != std::string::npos )
    return ProductKind::Sentinel1Safe;
  if ( upper.find( "MTD_MSIL1C" ) != std::string::npos || upper.find( "MTD_MSIL2A" ) != std::string::npos )
    return ProductKind::Sentinel2Safe;
  if ( looksLikeSentinel2Directory( path ) )
    return ProductKind::Sentinel2Safe;
  if ( upper.find( "HDF" ) != std::string::npos || upper.find( "MOD0" ) != std::string::npos
       || upper.find( "MYD0" ) != std::string::npos )
    return ProductKind::ModisContainer;
  return ProductKind::GenericRaster;
}

const char *productKindName( ProductKind kind )
{
  switch ( kind )
  {
    case ProductKind::LandsatMtl: return "landsat_mtl";
    case ProductKind::Sentinel2Safe: return "sentinel2_safe";
    case ProductKind::Sentinel1Safe: return "sentinel1_safe";
    case ProductKind::ModisContainer: return "modis_container";
    case ProductKind::GenericRaster: return "generic_raster";
    case ProductKind::Unknown: break;
  }
  return "unknown";
}

std::string productKindDisplayName( ProductKind kind )
{
  switch ( kind )
  {
    case ProductKind::LandsatMtl: return "Landsat MTL scene";
    case ProductKind::Sentinel2Safe: return "Sentinel-2 SAFE product";
    case ProductKind::Sentinel1Safe: return "Sentinel-1 SAFE product";
    case ProductKind::ModisContainer: return "MODIS HDF container";
    case ProductKind::GenericRaster: return "Generic raster";
    case ProductKind::Unknown: break;
  }
  return "Unknown";
}

ProductMetadata readProductMetadata( const std::string &path, ProductKind kind )
{
  switch ( kind )
  {
    case ProductKind::LandsatMtl:
      return readLandsatMtl( path );
    case ProductKind::Sentinel2Safe:
      return readSentinel2( path );
    case ProductKind::Sentinel1Safe:
      return readSentinel1( path );
    case ProductKind::ModisContainer:
      return readModis( path );
    case ProductKind::GenericRaster:
    case ProductKind::Unknown:
      break;
  }
  Json::Value details;
  details["path"] = path;
  throw GeoError( ErrorCode::InvalidArgument, "No product adapter for this kind of source", details );
}

ProductMetadata readProductMetadataAuto( const std::string &path )
{
  const ProductKind kind = detectProductKind( path );
  if ( kind == ProductKind::Unknown || kind == ProductKind::GenericRaster )
  {
    Json::Value details;
    details["path"] = path;
    throw GeoError( ErrorCode::InvalidArgument, "No product adapter detected for source", details );
  }
  return readProductMetadata( path, kind );
}

void enrichWithProductMetadata( RasterMetadata &canonical, const ProductMetadata &product )
{
  // Canonical values already present win: the adapter is a fallback layer.
  if ( canonical.productId.empty() )
    canonical.productId = product.productId;
  if ( canonical.sensor.empty() )
    canonical.sensor = product.sensor;
  if ( canonical.platform.empty() )
    canonical.platform = product.platform;
  if ( canonical.processingLevel.empty() )
    canonical.processingLevel = product.processingLevel;
  if ( canonical.acquisitionTime.empty() )
    canonical.acquisitionTime = product.acquisitionTime;
  if ( canonical.radiometricState.empty() )
    canonical.radiometricState = product.radiometricState;
  if ( canonical.numericScale == 0.0 )
    canonical.numericScale = product.numericScale;
  if ( !canonical.hasCloudCover && product.hasCloudCover )
  {
    canonical.hasCloudCover = true;
    canonical.cloudCover = product.cloudCover;
  }
  if ( !canonical.hasGsd && product.hasResolution )
  {
    canonical.hasGsd = true;
    canonical.gsd = product.resolutionMeters;
  }
  if ( !product.polarizations.empty() )
  {
    std::string joined;
    for ( const std::string &pol : product.polarizations )
    {
      if ( !joined.empty() )
        joined += ",";
      joined += pol;
    }
    canonical.metadata["sar:polarizations"] = joined;
  }
  if ( !product.orbitDirection.empty() )
    canonical.metadata["sar:orbit_direction"] = product.orbitDirection;
  if ( !product.instrumentMode.empty() )
    canonical.metadata["sar:instrument_mode"] = product.instrumentMode;
  if ( !product.modality.empty() )
    canonical.metadata["modality"] = product.modality;
}

std::string productBandRole( ProductKind kind, const std::string &bandName )
{
  if ( kind == ProductKind::Sentinel2Safe )
  {
    const BandRoleMapping *mapping = findBand( kSentinel2Bands, bandName );
    return mapping ? mapping->role : std::string();
  }
  if ( kind == ProductKind::LandsatMtl )
  {
    const BandRoleMapping *mapping = findBand( kLandsatBands, bandName );
    return mapping ? mapping->role : std::string();
  }
  if ( kind == ProductKind::Sentinel1Safe )
  {
    // SAR polarizations share the canonical string vocabulary; wavelength
    // is meaningless and stays unset.
    const BandRoleMapping *mapping = findBand( kSentinel2Bands, bandName );
    return mapping && mapping->wavelengthNm <= 0.0 ? mapping->role : std::string();
  }
  return std::string();
}

std::string productLandsatBandRole( const std::string &sensorId, const std::string &bandName )
{
  const std::string upper = upperAscii( sensorId );
  const BandRoleMapping *table = kLandsatBands;
  if ( upper.find( "ETM" ) != std::string::npos || upper.find( "TM" ) != std::string::npos )
    table = kLandsatLegacyBands; // TM/ETM+ layout (an unknown sensor keeps OLI)
  const BandRoleMapping *mapping = findBand( *table, bandName );
  return mapping ? mapping->role : std::string();
}

bool productBandWavelengthNm( ProductKind kind, const std::string &bandName, double &wavelengthNm )
{
  const BandRoleMapping *mapping = nullptr;
  if ( kind == ProductKind::Sentinel2Safe )
    mapping = findBand( kSentinel2Bands, bandName );
  else if ( kind == ProductKind::LandsatMtl )
    mapping = findBand( kLandsatBands, bandName );
  if ( !mapping || mapping->wavelengthNm <= 0.0 )
    return false; // roles without a declared centre wavelength carry none
  wavelengthNm = mapping->wavelengthNm;
  return true;
}

} // namespace sicnu::geo
