/***************************************************************************
  geospatial/products/product_registry.cpp
  Remote Sensing I/O Foundation 5.0 — product adapter registry, constituent
  asset enumeration and completeness verdicts (ADR 0137).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/products/product_registry.h"

#include "geospatial/gdal_guard.h"

#include <cpl_conv.h>
#include <gdal.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace sicnu::geo
{

namespace fs = std::filesystem;

namespace
{

std::string baseNameOf( const std::string &path )
{
  const std::string normalized = path;
  const std::size_t slash = normalized.find_last_of( "/\\" );
  return slash == std::string::npos ? normalized : normalized.substr( slash + 1 );
}

std::string parentOf( const std::string &path )
{
  const std::size_t slash = path.find_last_of( "/\\" );
  return slash == std::string::npos ? std::string() : path.substr( 0, slash );
}

std::string joinPath( const std::string &base, const std::string &name )
{
  if ( base.empty() )
    return name;
  const char last = base.back();
  if ( last == '/' || last == '\\' )
    return base + name;
  return base + "/" + name;
}

bool existsLocal( const std::string &path )
{
  std::error_code ec;
  return fs::exists( fs::u8path( path ), ec );
}

bool isDirectoryLocal( const std::string &path )
{
  std::error_code ec;
  return fs::is_directory( fs::u8path( path ), ec );
}

/// Bounded single-level directory listing (product directories are small;
/// the bound keeps a pathological listing from ballooning the report).
/// Paths are returned as UTF-8 (the foundation's path encoding contract).
std::vector<std::string> listDirectory( const std::string &dir, int maxEntries = 4096 )
{
  std::vector<std::string> names;
  std::error_code ec;
  fs::directory_iterator it( fs::u8path( dir ), ec );
  if ( ec )
    return names;
  for ( fs::directory_iterator end; it != end && static_cast<int>( names.size() ) < maxEntries; it.increment( ec ) )
  {
    if ( ec )
      break;
    const std::u8string u8 = it->path().generic_u8string();
    names.emplace_back( reinterpret_cast<const char *>( u8.data() ), u8.size() );
  }
  return names;
}

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

std::string trimText( const std::string &text )
{
  std::size_t begin = 0;
  std::size_t end = text.size();
  while ( begin < end && std::isspace( static_cast<unsigned char>( text[begin] ) ) )
    ++begin;
  while ( end > begin && std::isspace( static_cast<unsigned char>( text[end - 1] ) ) )
    --end;
  return text.substr( begin, end - begin );
}

bool readFileInto( const std::string &path, std::string &out )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in.is_open() )
    return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

/// Sentinel-2 band id from an image file name. L2A names carry a trailing
/// resolution token ("T33UUU_..._B02_10m.jp2") — the band id is the token
/// before it; L1C names end with the band id ("..._B02.jp2").
std::string s2BandIdFromName( const std::string &fileName )
{
  const std::string stem = baseNameOf( fileName );
  const std::size_t dot = stem.rfind( '.' );
  const std::string core = dot == std::string::npos ? stem : stem.substr( 0, dot );
  std::size_t lastUnderscore = core.rfind( '_' );
  if ( lastUnderscore == std::string::npos )
    return core;
  std::string token = core.substr( lastUnderscore + 1 );
  if ( token == "10m" || token == "20m" || token == "60m" )
  {
    const std::size_t previous = core.rfind( '_', lastUnderscore - 1 );
    if ( previous == std::string::npos )
      return token;
    return core.substr( previous + 1, lastUnderscore - previous - 1 );
  }
  return token;
}

/// Declared Sentinel-2 resolution by band id (L1C flat layout convention);
/// L2A folder-derived values take precedence when present.
bool s2BandIdResolution( const std::string &bandId, double &resolution )
{
  static const struct
  {
    const char *id;
    double meters;
  } kResolutions[] = {
    { "B01", 60 }, { "B09", 60 }, { "B10", 60 },
    { "B02", 10 }, { "B03", 10 }, { "B04", 10 }, { "B08", 10 },
    { "B05", 20 }, { "B06", 20 }, { "B07", 20 }, { "B8A", 20 },
    { "B11", 20 }, { "B12", 20 }, { "SCL", 20 }, { "AOT", 20 }, { "WVP", 10 },
  };
  for ( const auto &entry : kResolutions )
  {
    if ( bandId == entry.id )
    {
      resolution = entry.meters;
      return true;
    }
  }
  return false;
}

bool s2IsMaskId( const std::string &bandId )
{
  return bandId.rfind( "MSK", 0 ) == 0 || bandId == "SCL" || bandId == "AOT" || bandId == "WVP";
}

bool s2IsVisualId( const std::string &bandId )
{
  return bandId == "TCI";
}

std::string upperOf( const std::string &text )
{
  std::string out = text;
  std::transform( out.begin(), out.end(), out.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
  return out;
}

/// Landsat Collection band-role classification by file name.
ProductAsset landsatAssetFromName( const std::string &path, const std::string &sensorId )
{
  ProductAsset asset;
  asset.path = path;
  const std::string name = baseNameOf( path );
  const std::string upper = upperOf( name );
  const std::size_t dot = upper.rfind( '.' );
  const std::string stem = dot == std::string::npos ? upper : upper.substr( 0, dot );

  if ( upper.find( ".TIF" ) != std::string::npos || upper.find( ".TIFF" ) != std::string::npos )
  {
    // QA masks: QA_PIXEL, QA_RADSAT, MSK_CLD/PSN/OCI...
    if ( upper.find( "QA_" ) != std::string::npos || upper.find( "MSK_" ) != std::string::npos ||
         upper.find( "_BQA" ) != std::string::npos )
    {
      asset.role = "mask";
      asset.nativeBandName = stem;
      asset.bandRole = productLandsatBandRole( sensorId, "QA" );
      return asset;
    }
    // Surface temperature (Collection 2: *_ST_B10.TIF) — the thermal band.
    if ( upper.find( "_ST_B" ) != std::string::npos )
    {
      asset.role = "measurement";
      asset.nativeBandName = stem;
      asset.bandRole = productLandsatBandRole( sensorId, "B10" );
      return asset;
    }
    // Optical/pan bands: *_B1.._B11
    const std::size_t bandUnderscore = stem.rfind( "_B" );
    if ( bandUnderscore != std::string::npos )
    {
      const std::string bandName = stem.substr( bandUnderscore + 1 );
      asset.role = "measurement";
      asset.nativeBandName = bandName;
      asset.bandRole = productLandsatBandRole( sensorId, bandName );
      return asset;
    }
  }

  if ( upper.find( ".JPG" ) != std::string::npos || upper.find( ".JPEG" ) != std::string::npos )
  {
    asset.role = "browse";
    asset.nativeBandName = stem;
    return asset;
  }

  asset.role = "metadata";
  asset.nativeBandName = stem;
  return asset;
}

} // namespace

// ---------------------------------------------------------------------------
// JSON plumbing
// ---------------------------------------------------------------------------

Json::Value ProductAsset::toJson() const
{
  Json::Value json;
  json["path"] = path;
  json["role"] = role;
  json["band"] = nativeBandName;
  json["band_role"] = bandRole;
  if ( hasWavelength )
    json["wavelength_nm"] = wavelengthNm;
  if ( hasResolution )
    json["resolution_m"] = resolutionMeters;
  return json;
}

const char *productCompletenessName( ProductCompleteness completeness )
{
  switch ( completeness )
  {
    case ProductCompleteness::Complete: return "complete";
    case ProductCompleteness::PartialReadable: return "partial_readable";
    case ProductCompleteness::Invalid: return "invalid";
    case ProductCompleteness::UnsupportedVersion: return "unsupported_version";
  }
  return "unknown";
}

Json::Value ProductAssets::toJson() const
{
  Json::Value json;
  json["kind"] = productKindName( kind );
  json["adapter"] = adapterId;
  json["product_id"] = productId;
  json["completeness"] = productCompletenessName( completeness );
  Json::Value missing( Json::arrayValue );
  for ( const std::string &name : missingConstituents )
    missing.append( name );
  json["missing"] = missing;
  Json::Value assetList( Json::arrayValue );
  for ( const ProductAsset &asset : assets )
    assetList.append( asset.toJson() );
  json["assets"] = assetList;
  json["metadata"] = metadata.toJson();
  json["notes"] = notes;
  return json;
}

// ---------------------------------------------------------------------------
// Landsat adapter
// ---------------------------------------------------------------------------

namespace
{

class LandsatAdapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "landsat_mtl"; }
    ProductKind kind() const override { return ProductKind::LandsatMtl; }

    bool accepts( const std::string &path ) const override
    {
      const std::string upper = upperOf( path );
      if ( upper.find( "_MTL.TXT" ) != std::string::npos || upper.find( "_MTL.XML" ) != std::string::npos )
        return true;
      // Directory form: a scene dir with exactly one *_MTL.txt child.
      if ( isDirectoryLocal( path ) )
      {
        for ( const std::string &entry : listDirectory( path, 64 ) )
        {
          if ( upperOf( baseNameOf( entry ) ).find( "_MTL.TXT" ) != std::string::npos )
            return true;
        }
      }
      return false;
    }

    ProductAssets enumerate( const std::string &path ) const override
    {
      ProductAssets result;
      result.kind = ProductKind::LandsatMtl;
      result.adapterId = id();

      // Locate the MTL.
      std::string mtlPath = isDirectoryLocal( path ) ? std::string() : path;
      std::string root = isDirectoryLocal( path ) ? path : parentOf( path );
      if ( mtlPath.empty() )
      {
        for ( const std::string &entry : listDirectory( root, 64 ) )
        {
          if ( upperOf( baseNameOf( entry ) ).find( "_MTL.TXT" ) != std::string::npos )
          {
            mtlPath = entry;
            break;
          }
        }
      }
      if ( mtlPath.empty() || !existsLocal( mtlPath ) )
      {
        result.completeness = ProductCompleteness::Invalid;
        result.missingConstituents.push_back( "*_MTL.txt" );
        result.notes["reason"] = "MTL metadata file not found";
        return result;
      }

      // parseLandsatMtlKeys opens and parses the MTL itself; an empty map
      // means unreadable or unparseable (both are Invalid, never guessed).
      const std::map<std::string, std::string> keys = parseLandsatMtlKeys( mtlPath );
      if ( keys.empty() )
      {
        result.completeness = ProductCompleteness::Invalid;
        result.missingConstituents.push_back( baseNameOf( mtlPath ) + " (unparseable)" );
        return result;
      }
      const std::string sensorId =
        keys.count( "SENSOR_ID" ) ? keys.at( "SENSOR_ID" ) : std::string();

      // Version honesty: a declared collection we do not understand is
      // reported as UnsupportedVersion, never silently parsed as v1/v2.
      const auto collection = keys.find( "COLLECTION_NUMBER" );
      if ( collection != keys.end() )
      {
        const std::string value = trimText( collection->second );
        if ( value != "1" && value != "2" && value != "01" && value != "02" )
        {
          result.completeness = ProductCompleteness::UnsupportedVersion;
          result.notes["collection_number"] = value;
          return result;
        }
      }

      result.metadata = readProductMetadata( mtlPath, ProductKind::LandsatMtl );
      result.productId = result.metadata.productId.empty() ? baseNameOf( root ) : result.metadata.productId;

      // MTL-declared constituents (FILE_NAME_*): the product's own inventory.
      std::vector<std::string> missing;
      for ( const auto &entry : keys )
      {
        if ( entry.first.rfind( "FILE_NAME_", 0 ) != 0 )
          continue;
        const std::string fileName = trimText( entry.second );
        if ( fileName.empty() )
          continue;
        const std::string fullPath = joinPath( root, fileName );
        if ( !existsLocal( fullPath ) )
        {
          missing.push_back( fileName );
          continue;
        }
        ProductAsset asset = landsatAssetFromName( fullPath, sensorId );
        if ( asset.role == "measurement" || asset.role == "mask" )
        {
          double wavelength = 0.0;
          if ( productBandWavelengthNm( ProductKind::LandsatMtl, asset.nativeBandName, wavelength ) )
          {
            asset.hasWavelength = true;
            asset.wavelengthNm = wavelength;
          }
        }
        result.assets.push_back( std::move( asset ) );
      }
      result.assets.push_back( [&] {
        ProductAsset mtl;
        mtl.path = mtlPath;
        mtl.role = "metadata";
        mtl.nativeBandName = baseNameOf( mtlPath );
        return mtl;
      }() );

      result.completeness = missing.empty() ? ProductCompleteness::Complete
                                            : ProductCompleteness::PartialReadable;
      result.missingConstituents = std::move( missing );
      return result;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Sentinel-2 adapter
// ---------------------------------------------------------------------------

namespace
{

class Sentinel2Adapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "sentinel2_safe"; }
    ProductKind kind() const override { return ProductKind::Sentinel2Safe; }

    bool accepts( const std::string &path ) const override
    {
      const std::string upper = upperOf( path );
      if ( upper.find( "MTD_MSIL1C" ) != std::string::npos || upper.find( "MTD_MSIL2A" ) != std::string::npos )
        return true;
      // A manifest.safe alone is claimed by whichever SAR/optic family the
      // scene id names — a bare "MANIFEST.SAFE" must not claim S1 products.
      if ( upper.find( "MANIFEST.SAFE" ) != std::string::npos )
        return upper.find( "S1" ) == std::string::npos && upper.find( "S3" ) == std::string::npos;
      if ( upper.find( ".SAFE" ) != std::string::npos && upper.find( "MSI" ) != std::string::npos )
        return true;
      if ( isDirectoryLocal( path ) && isDirectoryLocal( joinPath( path, "GRANULE" ) ) )
        return true;
      return false;
    }

    ProductAssets enumerate( const std::string &path ) const override
    {
      ProductAssets result;
      result.kind = ProductKind::Sentinel2Safe;
      result.adapterId = id();

      std::string root = path;
      if ( !isDirectoryLocal( root ) )
      {
        // MTD/granule-file input: climb to the .SAFE root (manifest lives
        // there), bounded — never a fixed two-parent jump.
        std::string candidate = parentOf( path );
        for ( int hops = 0; hops < 4 && !candidate.empty(); ++hops )
        {
          if ( upperOf( baseNameOf( candidate ) ).find( ".SAFE" ) != std::string::npos ||
               isDirectoryLocal( joinPath( candidate, "GRANULE" ) ) )
          {
            root = candidate;
            break;
          }
          candidate = parentOf( candidate );
        }
      }

      const std::string granuleRoot = joinPath( root, "GRANULE" );
      const bool hasManifest = existsLocal( joinPath( root, "manifest.safe" ) );
      const bool hasGranules = isDirectoryLocal( granuleRoot );

      // Product-level metadata (declared values only).
      try
      {
        result.metadata = readProductMetadata( path, ProductKind::Sentinel2Safe );
      }
      catch ( const GeoError &error )
      {
        result.notes["metadata"] = error.what();
      }
      result.productId = baseNameOf( root );

      // The product-level MTD is a core constituent: a SAFE whose MTD is
      // missing/unreadable is PartialReadable even when granules look fine.
      bool hasMtd = false;
      for ( const char *mtdName : { "MTD_MSIL1C.xml", "MTD_MSIL2A.xml" } )
      {
        if ( existsLocal( joinPath( root, mtdName ) ) )
        {
          hasMtd = true;
          break;
        }
      }

      int measurementCount = 0;
      if ( hasGranules )
      {
        // Bounded granule enumeration (multi-tile products stay bounded).
        const std::vector<std::string> granules = listDirectory( granuleRoot, 64 );
        int visited = 0;
        for ( const std::string &granule : granules )
        {
          if ( !isDirectoryLocal( granule ) )
            continue;
          if ( ++visited > 16 )
          {
            result.notes["granules"] = "enumeration bounded at 16 granules";
            break;
          }
          const std::string imgData = joinPath( granule, "IMG_DATA" );
          if ( !isDirectoryLocal( imgData ) )
            continue;

          // L2A groups by resolution folder (R10m/R20m/R60m); L1C is flat.
          for ( const std::string &imageEntry : listDirectory( imgData ) )
          {
            std::string imageDir = imageEntry;
            double folderResolution = 0.0;
            const std::string entryName = upperOf( baseNameOf( imageEntry ) );
            // "R10m"/"R20m"/"R60m" groups declare the grid authoritatively.
            const bool isResolutionDir = entryName.rfind( "R10M", 0 ) == 0 || entryName.rfind( "R20M", 0 ) == 0 ||
                                         entryName.rfind( "R60M", 0 ) == 0;
            if ( isResolutionDir )
            {
              folderResolution = entryName[1] == '1' ? 10.0 : entryName[1] == '2' ? 20.0 : 60.0;
            }
            if ( isResolutionDir && isDirectoryLocal( imageDir ) )
            {
              for ( const std::string &bandFile : listDirectory( imageDir ) )
                addMeasurement( result, bandFile, folderResolution, measurementCount );
            }
            else if ( !isResolutionDir && existsLocal( imageEntry ) )
            {
              addMeasurement( result, imageEntry, folderResolution, measurementCount );
            }
          }

          // Granule-level masks/QI directory (non-core).
          const std::string qiData = joinPath( granule, "QI_DATA" );
          if ( isDirectoryLocal( qiData ) )
          {
            for ( const std::string &qiFile : listDirectory( qiData, 256 ) )
            {
              ProductAsset asset;
              asset.path = qiFile;
              asset.role = "mask";
              asset.nativeBandName = baseNameOf( qiFile );
              result.assets.push_back( std::move( asset ) );
            }
          }
        }
      }

      if ( !hasManifest || measurementCount == 0 || !hasMtd )
      {
        result.completeness = measurementCount == 0 && !hasManifest
                                ? ProductCompleteness::Invalid
                                : ProductCompleteness::PartialReadable;
        if ( !hasManifest )
          result.missingConstituents.push_back( "manifest.safe" );
        if ( !hasMtd )
          result.missingConstituents.push_back( "MTD_MSIL1C/2A.xml" );
        if ( measurementCount == 0 )
        {
          result.missingConstituents.push_back( "GRANULE/*/IMG_DATA measurements" );
          if ( !hasManifest )
            result.completeness = ProductCompleteness::Invalid;
        }
      }
      else
      {
        result.completeness = ProductCompleteness::Complete;
        // Core band sanity: an L1C/L2A granule without B02 (blue) is a
        // partial product even when some measurements exist.
        bool hasBlue = false;
        for ( const ProductAsset &asset : result.assets )
        {
          if ( asset.role == "measurement" && asset.nativeBandName == "B02" )
            hasBlue = true;
        }
        if ( !hasBlue )
        {
          result.completeness = ProductCompleteness::PartialReadable;
          result.missingConstituents.push_back( "B02 (core blue band)" );
        }
      }
      return result;
    }

  private:
    static void addMeasurement( ProductAssets &result, const std::string &bandFile,
                                double folderResolution, int &measurementCount )
    {
      if ( !existsLocal( bandFile ) )
        return;
      const std::string bandId = s2BandIdFromName( bandFile );
      ProductAsset asset;
      asset.path = bandFile;
      asset.nativeBandName = bandId;
      asset.bandRole = productBandRole( ProductKind::Sentinel2Safe, bandId );
      if ( s2IsMaskId( bandId ) )
        asset.role = "mask";
      else if ( s2IsVisualId( bandId ) )
        asset.role = "browse";
      else
      {
        asset.role = "measurement";
        ++measurementCount;
        double wavelength = 0.0;
        if ( productBandWavelengthNm( ProductKind::Sentinel2Safe, bandId, wavelength ) )
        {
          asset.hasWavelength = true;
          asset.wavelengthNm = wavelength;
        }
      }
      if ( folderResolution > 0.0 || s2BandIdResolution( bandId, folderResolution ) )
      {
        asset.hasResolution = true;
        asset.resolutionMeters = folderResolution;
      }
      result.assets.push_back( std::move( asset ) );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Sentinel-1 adapter
// ---------------------------------------------------------------------------

namespace
{

class Sentinel1Adapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "sentinel1_safe"; }
    ProductKind kind() const override { return ProductKind::Sentinel1Safe; }

    bool accepts( const std::string &path ) const override
    {
      const std::string upper = upperOf( path );
      if ( upper.find( "MANIFEST.SAFE" ) != std::string::npos )
        return upper.find( "S1" ) != std::string::npos || upper.find( "S3" ) != std::string::npos;
      if ( upper.find( ".SAFE" ) != std::string::npos && upper.find( "S1" ) != std::string::npos )
        return true;
      return isDirectoryLocal( path ) && existsLocal( joinPath( path, "manifest.safe" ) ) &&
             upperOf( baseNameOf( path ) ).find( "S1" ) == 0;
    }

    ProductAssets enumerate( const std::string &path ) const override
    {
      ProductAssets result;
      result.kind = ProductKind::Sentinel1Safe;
      result.adapterId = id();

      std::string root = path;
      if ( !isDirectoryLocal( root ) )
        root = parentOf( parentOf( root ) );

      const std::string manifest = joinPath( root, "manifest.safe" );
      if ( !existsLocal( manifest ) )
      {
        result.completeness = ProductCompleteness::Invalid;
        result.missingConstituents.push_back( "manifest.safe" );
        return result;
      }

      try
      {
        result.metadata = readProductMetadata( manifest, ProductKind::Sentinel1Safe );
      }
      catch ( const GeoError &error )
      {
        result.notes["metadata"] = error.what();
      }
      result.productId = baseNameOf( root );

      ProductAsset manifestAsset;
      manifestAsset.path = manifest;
      manifestAsset.role = "metadata";
      manifestAsset.nativeBandName = "manifest.safe";
      result.assets.push_back( std::move( manifestAsset ) );

      int measurementCount = 0;
      int annotationCount = 0;
      const std::string measurementDir = joinPath( root, "measurement" );
      if ( isDirectoryLocal( measurementDir ) )
      {
        for ( const std::string &entry : listDirectory( measurementDir ) )
        {
          const std::string name = baseNameOf( entry );
          const std::string lower = lowerAscii( name );
          if ( lower.find( ".tif" ) == std::string::npos )
            continue;
          ProductAsset asset;
          asset.path = entry;
          asset.role = "measurement";
          // Polarization from the file name: ...-vv-....tiff → "vv".
          std::vector<std::string> parts;
          std::string current;
          for ( const char c : name )
          {
            if ( c == '-' || c == '.' )
            {
              parts.push_back( current );
              current.clear();
            }
            else
              current.push_back( c );
          }
          parts.push_back( current );
          for ( const std::string &part : parts )
          {
            if ( part == "vv" || part == "vh" || part == "hh" || part == "hv" )
            {
              asset.nativeBandName = part;
              asset.bandRole = productBandRole( ProductKind::Sentinel1Safe, part );
            }
          }
          ++measurementCount;
          result.assets.push_back( std::move( asset ) );
        }
      }

      const std::string annotationDir = joinPath( root, "annotation" );
      if ( isDirectoryLocal( annotationDir ) )
      {
        for ( const std::string &entry : listDirectory( annotationDir, 256 ) )
        {
          if ( isDirectoryLocal( entry ) )
            continue; // "calibration" and friends are enumerated separately
          ProductAsset asset;
          asset.path = entry;
          asset.role = "annotation";
          asset.nativeBandName = baseNameOf( entry );
          ++annotationCount;
          result.assets.push_back( std::move( asset ) );
        }
      }
      // Calibration / noise references live under annotation/calibration.
      const std::string calibrationDir = joinPath( annotationDir, "calibration" );
      if ( isDirectoryLocal( calibrationDir ) )
      {
        for ( const std::string &entry : listDirectory( calibrationDir, 256 ) )
        {
          ProductAsset asset;
          asset.path = entry;
          asset.role = "metadata"; // calibration reference (Track A consumes)
          asset.nativeBandName = baseNameOf( entry );
          result.assets.push_back( std::move( asset ) );
        }
      }

      if ( measurementCount == 0 )
      {
        result.completeness = ProductCompleteness::Invalid;
        result.missingConstituents.push_back( "measurement/*.tiff" );
      }
      else if ( annotationCount == 0 )
      {
        result.completeness = ProductCompleteness::PartialReadable;
        result.missingConstituents.push_back( "annotation/*.xml" );
      }
      else
      {
        result.completeness = ProductCompleteness::Complete;
      }
      return result;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// MODIS adapter
// ---------------------------------------------------------------------------

namespace
{

class ModisAdapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "modis_container"; }
    ProductKind kind() const override { return ProductKind::ModisContainer; }

    bool accepts( const std::string &path ) const override
    {
      const std::string upper = upperOf( path );
      if ( upper.find( ".HDF" ) != std::string::npos || upper.find( ".H5" ) != std::string::npos ||
           upper.find( ".HE5" ) != std::string::npos )
        return true;
      const std::string base = upperOf( baseNameOf( path ) );
      return base.rfind( "MOD", 0 ) == 0 || base.rfind( "MYD", 0 ) == 0;
    }

    ProductAssets enumerate( const std::string &path ) const override
    {
      ProductAssets result;
      result.kind = ProductKind::ModisContainer;
      result.adapterId = id();
      result.productId = baseNameOf( path );

      if ( !existsLocal( path ) )
      {
        result.completeness = ProductCompleteness::Invalid;
        result.missingConstituents.push_back( baseNameOf( path ) );
        return result;
      }

      try
      {
        result.metadata = readProductMetadata( path, ProductKind::ModisContainer );
      }
      catch ( const GeoError &error )
      {
        result.notes["metadata"] = error.what();
      }

      // Enumerate subdatasets through GDAL (metadata-only open, bounded).
      ensureGdalRegistered();
      QuietCplErrors quiet;
      GDALDatasetH dataset =
        GDALOpenEx( path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr );
      if ( dataset == nullptr )
      {
        result.completeness = ProductCompleteness::Invalid;
        result.missingConstituents.push_back( baseNameOf( path ) + " (GDAL cannot open)" );
        return result;
      }
      GdalDatasetGuard guard( dataset );

      int subdatasetCount = 0;
      if ( char **metadataList = GDALGetMetadata( dataset, "SUBDATASETS" ) )
      {
        for ( char **entry = metadataList; *entry; ++entry )
        {
          const std::string item = *entry;
          const std::size_t eq = item.find( '=' );
          if ( eq == std::string::npos )
            continue;
          const std::string key = item.substr( 0, eq );
          if ( key.find( "_NAME" ) == std::string::npos )
            continue;
          if ( ++subdatasetCount > 64 )
          {
            result.notes["subdatasets"] = "enumeration bounded at 64";
            break;
          }
          ProductAsset asset;
          asset.path = item.substr( eq + 1 );
          asset.role = "measurement";
          asset.nativeBandName = key.substr( 0, key.find( "_NAME" ) );
          result.assets.push_back( std::move( asset ) );
        }
      }
      result.completeness = subdatasetCount > 0 ? ProductCompleteness::Complete
                                                : ProductCompleteness::PartialReadable;
      if ( subdatasetCount == 0 )
        result.missingConstituents.push_back( "SUBDATASETS (no variables exposed)" );
      return result;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Generic raster adapter (the never-say-unusable fallback, ADR 0137)
// ---------------------------------------------------------------------------

namespace
{

class GenericRasterAdapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "generic_raster"; }
    ProductKind kind() const override { return ProductKind::GenericRaster; }

    bool accepts( const std::string & ) const override { return true; }

    ProductAssets enumerate( const std::string &path ) const override
    {
      ProductAssets result;
      result.kind = ProductKind::GenericRaster;
      result.adapterId = id();
      result.productId = baseNameOf( path );

      ensureGdalRegistered();
      QuietCplErrors quiet;
      GDALDriverH driver = GDALIdentifyDriverEx( path.c_str(), GDAL_OF_RASTER | GDAL_OF_VECTOR, nullptr, nullptr );
      if ( driver == nullptr )
      {
        result.completeness = ProductCompleteness::Invalid;
        result.missingConstituents.push_back( "GDAL-identifiable dataset" );
        return result;
      }
      ProductAsset asset;
      asset.path = path;
      asset.role = "measurement";
      asset.nativeBandName = baseNameOf( path );
      result.assets.push_back( std::move( asset ) );
      result.completeness = ProductCompleteness::Complete;
      result.notes["driver"] = GDALGetDriverShortName( driver );
      return result;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

ProductAdapterRegistry::ProductAdapterRegistry()
{
  mAdapters.push_back( std::make_unique<LandsatAdapter>() );
  mAdapters.push_back( std::make_unique<Sentinel2Adapter>() );
  mAdapters.push_back( std::make_unique<Sentinel1Adapter>() );
  mAdapters.push_back( std::make_unique<ModisAdapter>() );
  mAdapters.push_back( std::make_unique<GenericRasterAdapter>() );
}

ProductAdapterRegistry &ProductAdapterRegistry::instance()
{
  static ProductAdapterRegistry registry;
  return registry;
}

ProductAdapter *ProductAdapterRegistry::adapterFor( const std::string &path ) const
{
  // Specific adapters were registered before the GenericRaster fallback, so
  // first-match ordering realizes the specificity rule.
  for ( const std::unique_ptr<ProductAdapter> &adapter : mAdapters )
  {
    if ( adapter->accepts( path ) )
      return adapter.get();
  }
  return nullptr;
}

ProductAssets ProductAdapterRegistry::describe( const std::string &path ) const
{
  ProductAdapter *adapter = adapterFor( path );
  if ( adapter == nullptr )
  {
    Json::Value details;
    details["path"] = path;
    throw GeoError( ErrorCode::UnsupportedProduct, "no product adapter accepts this path", details );
  }
  return adapter->enumerate( path );
}

} // namespace sicnu::geo
