/***************************************************************************
  geospatial/probe/probe.cpp
  Remote Sensing I/O Foundation 5.0 — unified probe / sniff / open contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/probe/probe.h"

#include "geospatial/cog/cog_validator.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <cpl_conv.h>
#include <gdal.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace sicnu::geo
{

namespace
{

void note( Json::Value &diagnostics, const char *stage, const std::string &message )
{
  Json::Value entry;
  entry["stage"] = stage;
  entry["note"] = message;
  diagnostics.append( entry );
}

bool headStartsWith( const std::vector<char> &head, const char *magic, std::size_t offset = 0 )
{
  const std::size_t length = std::strlen( magic );
  if ( head.size() < offset + length )
    return false;
  return std::memcmp( head.data() + offset, magic, length ) == 0;
}

} // namespace

const char *probeStageName( ProbeStage stage )
{
  switch ( stage )
  {
    case ProbeStage::Uri: return "uri";
    case ProbeStage::Signature: return "signature";
    case ProbeStage::GdalDriver: return "gdal_driver";
    case ProbeStage::ProductAdapter: return "product_adapter";
    case ProbeStage::Metadata: return "metadata";
    case ProbeStage::None: break;
  }
  return "none";
}

const char *fileSignatureName( FileSignature signature )
{
  switch ( signature )
  {
    case FileSignature::Tiff: return "tiff";
    case FileSignature::BigTiff: return "bigtiff";
    case FileSignature::Zip: return "zip";
    case FileSignature::Gpkg: return "gpkg_sqlite";
    case FileSignature::Hdf5: return "hdf5";
    case FileSignature::Hdf4: return "hdf4";
    case FileSignature::Netcdf: return "netcdf_classic";
    case FileSignature::Json: return "json";
    case FileSignature::Xml: return "xml";
    case FileSignature::PlainText: return "text";
    case FileSignature::Unknown: break;
  }
  return "unknown";
}

FileSignature classifySignature( const std::vector<char> &head )
{
  // TIFF little/big endian; BigTIFF uses version 43 ("+" / 0x2b).
  if ( headStartsWith( head, "II*\0" ) || headStartsWith( head, "MM\0*" ) )
    return FileSignature::Tiff;
  if ( headStartsWith( head, "II+\0" ) || headStartsWith( head, "MM\0+" ) )
    return FileSignature::BigTiff;
  if ( headStartsWith( head, "PK\x03\x04" ) || headStartsWith( head, "PK\x05\x06" ) )
    return FileSignature::Zip;
  if ( headStartsWith( head, "SQLite format 3" ) )
    return FileSignature::Gpkg;
  if ( headStartsWith( head, "\x89HDF\r\n\x1a\n" ) )
    return FileSignature::Hdf5;
  if ( head.size() >= 4 && static_cast<unsigned char>( head[0] ) == 0x0e &&
       static_cast<unsigned char>( head[1] ) == 0x03 &&
       static_cast<unsigned char>( head[2] ) == 0x13 &&
       static_cast<unsigned char>( head[3] ) == 0x01 )
    return FileSignature::Hdf4;
  if ( headStartsWith( head, "CDF\x01" ) || headStartsWith( head, "CDF\x02" ) )
    return FileSignature::Netcdf;

  // Text-ish families: scan the head for a BOM or printable dominance.
  bool printable = !head.empty();
  for ( const char c : head )
  {
    const unsigned char byte = static_cast<unsigned char>( c );
    if ( byte != '\t' && byte != '\n' && byte != '\r' && ( byte < 0x20 || byte == 0x7f ) )
    {
      printable = false;
      break;
    }
  }
  if ( !printable )
    return FileSignature::Unknown;

  const std::string text( head.begin(), head.end() );
  const auto firstNonSpace = text.find_first_not_of( " \t\r\n" );
  if ( firstNonSpace != std::string::npos )
  {
    if ( text[firstNonSpace] == '{' || text[firstNonSpace] == '[' )
      return FileSignature::Json;
    if ( text[firstNonSpace] == '<' )
      return FileSignature::Xml;
  }
  return FileSignature::PlainText;
}

Json::Value FormatDescriptor::toJson() const
{
  Json::Value json;
  json["profileId"] = profileId;
  json["displayName"] = displayName;
  json["driver"] = driverName;
  json["certification"] =
    certification == Certification::Certified ? "certified"
    : certification == Certification::Accessible ? "accessible" : "unsupported";
  json["driverAvailable"] = driverAvailable;
  return json;
}

Json::Value ProductDescriptor::toJson() const
{
  Json::Value json;
  json["kind"] = productKindName( kind );
  json["family"] = family;
  return json;
}

Json::Value ProbeResult::toJson() const
{
  Json::Value json;
  json["path"] = path;
  json["displayPath"] = displayPath;
  json["resourceKind"] = resourceKindName( resourceKind );
  json["decidedBy"] = probeStageName( decidedBy );
  json["signature"] = fileSignatureName( signature );
  json["format"] = format.toJson();
  json["product"] = product.toJson();
  json["isCog"] = isCog;
  json["diagnostics"] = diagnostics;
  return json;
}

ProbeResult probeResource( const std::string &path, const ProbeOptions &options )
{
  ProbeResult result;
  ensureGdalRegistered();

  // ---- Stage 1: URI classification ---------------------------------------
  const ResourceUri uri = ResourceUri::parse( path );
  result.path = path;
  result.displayPath = uri.display();
  result.resourceKind = uri.kind;
  if ( uri.kind == ResourceKind::Invalid )
  {
    throw GeoError( ErrorCode::NotFound, "resource does not exist: " + uri.display(),
                    uri.parseReason.empty() ? Json::Value() : Json::Value( uri.parseReason ) );
  }
  result.decidedBy = ProbeStage::Uri;
  note( result.diagnostics, probeStageName( ProbeStage::Uri ),
        std::string( "classified as " ) + resourceKindName( uri.kind ) );

  // ---- Stage 2: signature sniff (local payload, bounded) ------------------
  std::vector<char> head;
  if ( options.includeSignature && uri.isLocalPayload() )
  {
    const std::string payload = uri.embeddedLocalPath().empty() ? uri.canonical()
                                                                : uri.embeddedLocalPath();
    std::ifstream in( payload, std::ios::binary );
    if ( in )
    {
      head.resize( static_cast<std::size_t>( options.maxSignatureBytes ) );
      in.read( head.data(), head.size() );
      head.resize( static_cast<std::size_t>( in.gcount() ) );
      result.signature = classifySignature( head );
      note( result.diagnostics, probeStageName( ProbeStage::Signature ),
            std::string( "signature " ) + fileSignatureName( result.signature ) );
    }
  }

  // ---- Stage 3: GDAL identify/open ---------------------------------------
  // A specific product adapter (Landsat MTL / S2 SAFE / S1 SAFE / MODIS)
  // claims sidecar files (.txt/.xml) and directories (.SAFE) that no GDAL
  // driver can identify — those skip the GDAL stage entirely; "GDAL cannot
  // identify" must never mask a valid product answer.
  const bool gdalCandidate = uri.kind != ResourceKind::StacAsset &&
                             uri.kind != ResourceKind::InMemory &&
                             uri.kind != ResourceKind::VirtualDataset;
  ProductKind claimedKind = ProductKind::Unknown;
  if ( options.includeProduct )
    claimedKind = detectProductKind( path );
  const bool specificProductClaim =
    claimedKind != ProductKind::Unknown && claimedKind != ProductKind::GenericRaster;

  if ( gdalCandidate && !specificProductClaim )
  {
    const std::string gdalPath = uri.canonical();
    QuietCplErrors quiet;
    GDALDriverH driver = nullptr;
    // Identify is metadata-only; open confirms the driver can really serve it.
    driver = GDALIdentifyDriverEx( gdalPath.c_str(), GDAL_OF_RASTER | GDAL_OF_VECTOR, nullptr, nullptr );
    if ( driver == nullptr && ( uri.kind == ResourceKind::VsiRemote || uri.kind == ResourceKind::RemoteHttp ) )
    {
      // Identification of remote resources can require network round trips
      // that Identify skips for some drivers; fall back to a read-only open
      // (still metadata-only — no pixel access).
      GDALDatasetH dataset = GDALOpenEx( gdalPath.c_str(), GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr );
      if ( dataset )
      {
        driver = GDALGetDatasetDriver( dataset );
        GDALClose( dataset );
      }
    }

    if ( driver != nullptr )
    {
      result.format.driverName = GDALGetDriverShortName( driver );
      result.format.displayName = GDALGetDriverLongName( driver ) ? GDALGetDriverLongName( driver ) : "";
      result.decidedBy = ProbeStage::GdalDriver;
      note( result.diagnostics, probeStageName( ProbeStage::GdalDriver ),
            "driver " + result.format.driverName );

      // Profile lookup: by path extension first, then by driver membership.
      const FormatRegistry &registry = FormatRegistry::instance();
      const FormatProfile *profile = registry.profileForPath( gdalPath );
      bool matchedByDriver = false;
      if ( profile )
      {
        for ( const std::string &name : profile->driverNames )
        {
          if ( name == result.format.driverName )
          {
            matchedByDriver = true;
            break;
          }
        }
      }
      if ( !profile || !matchedByDriver )
      {
        // Fall back: first profile whose driver list contains this driver.
        for ( const FormatProfile &candidate : registry.profiles() )
        {
          for ( const std::string &name : candidate.driverNames )
          {
            if ( name == result.format.driverName )
            {
              profile = &candidate;
              matchedByDriver = true;
              break;
            }
          }
          if ( matchedByDriver )
            break;
        }
      }
      if ( profile )
      {
        result.format.profileId = profile->id;
        result.format.displayName = profile->displayName;
        result.format.certification = profile->certification;
        result.format.driverAvailable = registry.driverAvailable( *profile );
      }
      else
      {
        result.format.driverAvailable = true;
      }

      // Structural COG verdict: the read-only "COG" driver only accepts
      // conformant Cloud Optimized GeoTIFFs — never a name check.
      if ( result.format.driverName == "COG" )
      {
        result.isCog = true;
      }
      else if ( result.format.driverName == "GTiff" )
      {
        try
        {
          const CogValidationReport report = validateCog( gdalPath );
          result.isCog = report.isCog;
        }
        catch ( const GeoError &error )
        {
          // A file the validator cannot open (truncated/corrupt) is simply
          // not a COG; the corruption itself is reported by the caller's
          // error path or stays a probe note.
          result.isCog = false;
          note( result.diagnostics, probeStageName( ProbeStage::GdalDriver ),
                std::string( "COG validation skipped: " ) + error.what() );
        }
      }

      // Identify matches on signature alone; confirm the driver can actually
      // serve the dataset before calling it a usable format (a truncated TIFF
      // carries the magic but no servable IFD → CorruptData). Remote sources
      // skip the confirmation open: a transient network failure must not be
      // reported as corruption.
      if ( !uri.isRemote() && uri.isLocalPayload() )
      {
        GDALDatasetH confirm = GDALOpenEx( gdalPath.c_str(), GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr );
        if ( confirm == nullptr )
          throw GeoError( ErrorCode::CorruptData,
                          "signature " + std::string( fileSignatureName( result.signature ) ) +
                            " present but the dataset cannot be opened: " + uri.display() );
        GDALClose( confirm );
      }
    }
    else if ( result.signature == FileSignature::Unknown && !uri.isLocalPayload() )
    {
      throw GeoError( ErrorCode::OpenFailed, "GDAL cannot identify the resource: " + uri.display() );
    }
    else if ( result.signature != FileSignature::Unknown )
    {
      // A known signature family that GDAL refuses → corrupt or truncated.
      throw GeoError( ErrorCode::CorruptData,
                      "signature " + std::string( fileSignatureName( result.signature ) ) +
                        " present but GDAL cannot open: " + uri.display() );
    }
    else
    {
      note( result.diagnostics, probeStageName( ProbeStage::GdalDriver ),
            "no GDAL driver claims this resource" );
    }
  }

  // ---- Stage 4: product adapter ------------------------------------------
  if ( options.includeProduct )
  {
    const ProductKind kind = claimedKind;
    result.product.kind = kind;
    result.product.family = productKindDisplayName( kind );
    if ( kind != ProductKind::Unknown && kind != ProductKind::GenericRaster )
    {
      // A specific adapter claimed the path — that outranks the format-only
      // decision; a GenericRaster fallback is reported but never "decides".
      result.decidedBy = ProbeStage::ProductAdapter;
      note( result.diagnostics, probeStageName( ProbeStage::ProductAdapter ),
            "product " + result.product.family );
    }
  }

  // ---- Stage 5: (optional) lazy metadata ---------------------------------
  if ( options.includeMetadata && gdalCandidate && !result.format.driverName.empty() )
  {
    try
    {
      note( result.diagnostics, probeStageName( ProbeStage::Metadata ),
            "metadata inspected lazily" );
    }
    catch ( const GeoError &error )
    {
      note( result.diagnostics, probeStageName( ProbeStage::Metadata ),
            std::string( "metadata inspection skipped: " ) + error.what() );
    }
  }

  return result;
}

} // namespace sicnu::geo
