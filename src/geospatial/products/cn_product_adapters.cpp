/***************************************************************************
  geospatial/products/cn_product_adapters.cpp
  Remote Sensing I/O Foundation 5.0 — ProductAdapter implementations for the
  Chinese satellite families (GF-1/2/6 PMS/WFV, ZY-3 TLC/NAD/FWD/BWD,
  HJ-1A/1B CCD), ADR 0146.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One adapter per family. accept() is a cheap filename/shape claim (never
  opens files beyond a bounded directory listing for product directories);
  enumerate() locates the CRESDA sidecar XML, parses the declared metadata,
  maps the declared band inventory onto ADR 0065 roles through the
  data-driven band-role tables, and issues a completeness verdict.

  Fail-closed: a missing/unparseable sidecar or a missing band-role table is
  a structured verdict (Invalid + notes), never a guessed record and never a
  silent GenericRaster degradation for recognized CN names.
 ***************************************************************************/

#include "geospatial/products/cn_product_adapters.h"

#include "geospatial/products/cn_product_metadata.h"

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace sicnu::geo
{

namespace
{

std::string upperAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
  return text;
}

std::string baseNameOf( const std::string &path )
{
  const std::size_t slash = path.find_last_of( "/\\" );
  return slash == std::string::npos ? path : path.substr( slash + 1 );
}

bool isDirectoryLocal( const std::string &path )
{
  std::error_code ec;
  return std::filesystem::is_directory( std::filesystem::u8path( path ), ec );
}

std::string parentOfLocal( const std::string &path )
{
  const std::size_t slash = path.find_last_of( "/\\" );
  return slash == std::string::npos ? std::string() : path.substr( 0, slash );
}

/// Shared enumeration body for the three CN families. @p kind and
/// @p adapterId label the verdict; @p familyPattern selects the family in
/// cnIdentifyProduct (either the direct filename pattern or, for product
/// directories, any *.xml sibling carrying the family prefix).
ProductAssets enumerateCnProduct( const std::string &path, ProductKind kind,
                                  const std::string &adapterId, const char *familyPattern )
{
  ProductAssets result;
  result.kind = kind;
  result.adapterId = adapterId;
  result.notes["family"] = adapterId;

  CnProductIdentity identity = cnIdentifyProduct( path );
  if ( !identity.supported && isDirectoryLocal( path ) )
  {
    // Product directory: the family name lives on the sidecar/image files.
    // Bounded listing; the first matching name claims the directory.
    std::error_code ec;
    for ( std::filesystem::directory_iterator it( std::filesystem::u8path( path ), ec ), end;
          !ec && it != end; it.increment( ec ) )
    {
      const std::u8string u8 = it->path().generic_u8string();
      const std::string entry( reinterpret_cast<const char *>( u8.data() ), u8.size() );
      const CnProductIdentity entryIdentity = cnIdentifyProduct( entry );
      if ( entryIdentity.supported && entryIdentity.kindName == familyPattern )
      {
        identity = entryIdentity;
        break;
      }
    }
  }

  if ( !identity.supported )
  {
    result.completeness = ProductCompleteness::Invalid;
    result.notes["reason"] = identity.reason.empty()
                               ? Json::Value( "path does not name a supported CN product" )
                               : Json::Value( identity.reason );
    return result;
  }
  result.notes["satellite"] = identity.satellite;
  result.notes["sensor_mode"] = identity.sensorMode;

  ProductMetadata metadata;
  try
  {
    metadata = readCnProductMetadata( path, identity );
  }
  catch ( const GeoError &error )
  {
    result.completeness = ProductCompleteness::Invalid;
    result.notes["metadata"] = error.what();
    return result;
  }
  result.metadata = metadata;
  result.productId = metadata.productId.empty() ? baseNameOf( path ) : metadata.productId;

  const std::string sensorKey = cnSensorKey( identity, metadata );
  result.notes["sensor_key"] = sensorKey;
  CnBandRoleTable table;
  try
  {
    table = cnBandRoleTable( sensorKey );
  }
  catch ( const GeoError &error )
  {
    // Fail-closed (DECISIONS D-04): without the band-role table the adapter
    // cannot promise role semantics, so the product is not enumerated.
    result.completeness = ProductCompleteness::Invalid;
    result.notes["band_roles"] = error.what();
    return result;
  }

  // Measurement assets: one per declared band (multi-band TIFF → one asset
  // per band index, mirroring the Sentinel-2 per-band enumeration). The
  // image is paired with the sidecar by stem so PMS directories never mix
  // the MSS/PAN pair by directory-order accident.
  const std::string sidecarPath = cnLocateSidecarXml( path );
  const std::string tiffPath = cnLocateImageTiff( path, sidecarPath );
  if ( tiffPath.empty() )
    result.missingConstituents.push_back( "measurement TIFF beside the sidecar" );

  if ( metadata.declaredBandIds.empty() )
  {
    // No declared inventory: carry the image as one role-less asset and say
    // so — the verdict degrades, nothing is invented.
    if ( !tiffPath.empty() )
    {
      ProductAsset asset;
      asset.path = tiffPath;
      asset.role = "measurement";
      asset.nativeBandName = baseNameOf( tiffPath );
      result.assets.push_back( std::move( asset ) );
    }
    result.notes["bands"] = "sidecar declares no BandID inventory";
  }
  else
  {
    for ( std::size_t i = 0; i < metadata.declaredBandIds.size(); ++i )
    {
      ProductAsset asset;
      asset.path = tiffPath;
      asset.role = "measurement";
      asset.nativeBandName = metadata.declaredBandIds[i];
      const CnBandSpec *spec = nullptr;
      for ( const CnBandSpec &candidate : table.bands )
      {
        if ( upperAscii( candidate.band ) == upperAscii( asset.nativeBandName ) )
        {
          spec = &candidate;
          break;
        }
      }
      if ( spec )
      {
        asset.bandRole = spec->role;
        if ( spec->hasWavelength )
        {
          asset.hasWavelength = true;
          asset.wavelengthNm = spec->wavelengthNm;
        }
        if ( !spec->roleReason.empty() )
          result.notes[std::string( "role_" ) + asset.nativeBandName] = spec->roleReason;
      }
      else
      {
        result.notes[std::string( "role_" ) + asset.nativeBandName] =
          "band not present in band-role table " + sensorKey;
      }
      result.assets.push_back( std::move( asset ) );
    }
  }

  ProductAsset sidecar;
  sidecar.path = metadata.extra.empty() ? std::string()
                                         : [&] {
                                           for ( const auto &entry : metadata.extra )
                                             if ( entry.first == "parsed_sidecar" )
                                               return entry.second;
                                           return std::string();
                                         }();
  if ( !sidecar.path.empty() )
  {
    // Resolve the sidecar path relative to the input for the report.
    if ( isDirectoryLocal( path ) )
    {
      if ( !path.empty() && path.back() != '/' )
        sidecar.path = path + "/" + sidecar.path;
      else
        sidecar.path = path + sidecar.path;
    }
    else if ( const std::string parent = parentOfLocal( path ); !parent.empty() )
    {
      sidecar.path = parent + "/" + sidecar.path;
    }
    sidecar.role = "metadata";
    sidecar.nativeBandName = baseNameOf( sidecar.path );
    result.assets.push_back( std::move( sidecar ) );
  }

  if ( result.missingConstituents.empty() )
    result.completeness = ProductCompleteness::Complete;
  else
    result.completeness = ProductCompleteness::PartialReadable;
  return result;
}

/// The GF-1/2/6 family: GF{1,2,6}_{PMS,WFV} naming (files and directories).
class GaofenAdapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "gaofen_product"; }
    ProductKind kind() const override { return ProductKind::GaofenProduct; }

    bool accepts( const std::string &path ) const override
    {
      const CnProductIdentity identity = cnIdentifyProduct( path );
      if ( identity.supported && identity.kindName == id() )
        return true;
      if ( !isDirectoryLocal( path ) )
        return false;
      std::error_code ec;
      int visited = 0;
      for ( std::filesystem::directory_iterator it( std::filesystem::u8path( path ), ec ), end;
            !ec && it != end && visited < 512; it.increment( ec ) )
      {
        ++visited;
        const std::u8string u8 = it->path().generic_u8string();
        const std::string entry( reinterpret_cast<const char *>( u8.data() ), u8.size() );
        const CnProductIdentity entryIdentity = cnIdentifyProduct( entry );
        if ( entryIdentity.supported && entryIdentity.kindName == id() )
          return true;
      }
      return false;
    }

    ProductAssets enumerate( const std::string &path ) const override
    {
      return enumerateCnProduct( path, kind(), id(), "gaofen_product" );
    }
};

/// The ZY-3 family: ZY3_{TLC,NAD,FWD,BWD} naming.
class Zy3Adapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "zy3_product"; }
    ProductKind kind() const override { return ProductKind::Zy3Product; }

    bool accepts( const std::string &path ) const override
    {
      const CnProductIdentity identity = cnIdentifyProduct( path );
      if ( identity.supported && identity.kindName == id() )
        return true;
      if ( !isDirectoryLocal( path ) )
        return false;
      std::error_code ec;
      int visited = 0;
      for ( std::filesystem::directory_iterator it( std::filesystem::u8path( path ), ec ), end;
            !ec && it != end && visited < 512; it.increment( ec ) )
      {
        ++visited;
        const std::u8string u8 = it->path().generic_u8string();
        const std::string entry( reinterpret_cast<const char *>( u8.data() ), u8.size() );
        const CnProductIdentity entryIdentity = cnIdentifyProduct( entry );
        if ( entryIdentity.supported && entryIdentity.kindName == id() )
          return true;
      }
      return false;
    }

    ProductAssets enumerate( const std::string &path ) const override
    {
      return enumerateCnProduct( path, kind(), id(), "zy3_product" );
    }
};

/// The HJ-1A/1B CCD family: HJ1A-CCD*/HJ1B-CCD* naming.
class HjAdapter final : public ProductAdapter
{
  public:
    std::string id() const override { return "hj_ccd_product"; }
    ProductKind kind() const override { return ProductKind::HjCcdProduct; }

    bool accepts( const std::string &path ) const override
    {
      const CnProductIdentity identity = cnIdentifyProduct( path );
      if ( identity.supported && identity.kindName == id() )
        return true;
      if ( !isDirectoryLocal( path ) )
        return false;
      std::error_code ec;
      int visited = 0;
      for ( std::filesystem::directory_iterator it( std::filesystem::u8path( path ), ec ), end;
            !ec && it != end && visited < 512; it.increment( ec ) )
      {
        ++visited;
        const std::u8string u8 = it->path().generic_u8string();
        const std::string entry( reinterpret_cast<const char *>( u8.data() ), u8.size() );
        const CnProductIdentity entryIdentity = cnIdentifyProduct( entry );
        if ( entryIdentity.supported && entryIdentity.kindName == id() )
          return true;
      }
      return false;
    }

    ProductAssets enumerate( const std::string &path ) const override
    {
      return enumerateCnProduct( path, kind(), id(), "hj_ccd_product" );
    }
};

} // namespace

std::unique_ptr<ProductAdapter> makeGaofenAdapter()
{
  return std::make_unique<GaofenAdapter>();
}

std::unique_ptr<ProductAdapter> makeZy3Adapter()
{
  return std::make_unique<Zy3Adapter>();
}

std::unique_ptr<ProductAdapter> makeHjAdapter()
{
  return std::make_unique<HjAdapter>();
}

} // namespace sicnu::geo
