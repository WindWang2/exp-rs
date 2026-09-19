/***************************************************************************
  geospatial/io/finalize_manifest.cpp
  Geospatial I/O, COG & Interchange 11.0 — finalize manifest (provenance
  sidecar) for atomically published datasets.
 ***************************************************************************/

#include "geospatial/io/finalize_manifest.h"

#include "geospatial/io/param_guard.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/sha256.h"
#include "geospatial/util/time_normalization.h"

#include <chrono>
#include <fstream>
#include <json/reader.h>
#include <vector>

namespace fs = std::filesystem;

namespace sicnu::geo::io
{

const char *const kFinalizeManifestSuffix = ".sicnu-manifest.json";

namespace
{

/// Manifest schema version. Bump on any breaking field change; readers
/// refuse foreign versions instead of guessing (InvalidMetadata).
constexpr int kManifestSchemaVersion = 1;

/// A manifest is a small JSON sidecar; anything bigger is a planted file,
/// not provenance (memory-DoS guard for the read path).
constexpr std::uintmax_t kMaxManifestBytes = 16ull * 1024ull * 1024ull;

constexpr std::size_t kDigestChunkBytes = 1024 * 1024;

std::int64_t nowEpochNanos()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>( now ).count();
}

Json::Value creationOptionsJson( const std::vector<std::string> &options )
{
  Json::Value array( Json::arrayValue );
  for ( const std::string &option : options )
    array.append( option );
  return array;
}

bool parseManifestDocument( const std::string &text, Json::Value &out, std::string &error )
{
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  return reader->parse( text.data(), text.data() + text.size(), &out, &error );
}

} // namespace

std::string datasetSha256Hex( const std::string &path )
{
  std::ifstream file( path, std::ios::binary );
  if ( !file )
    throw GeoError( ErrorCode::IoError, "digest: cannot open " + path );
  Sha256 digest;
  std::vector<char> chunk( kDigestChunkBytes );
  while ( file )
  {
    file.read( chunk.data(), static_cast<std::streamsize>( chunk.size() ) );
    const std::streamsize got = file.gcount();
    if ( got > 0 )
      digest.update( chunk.data(), static_cast<std::size_t>( got ) );
  }
  if ( file.bad() )
    throw GeoError( ErrorCode::IoError, "digest: read failed for " + path );
  return toHex( digest.finalize() );
}

std::string finalizeManifestPath( const std::string &mainPath )
{
  return mainPath + kFinalizeManifestSuffix;
}

Json::Value buildFinalizeManifest( const std::string &mainPath, const FinalizeManifestFields &fields )
{
  const std::string digest = datasetSha256Hex( mainPath );
  const std::uintmax_t size = atomic_fs::fileSize( mainPath );

  Json::Value manifest;
  manifest["schema_version"] = kManifestSchemaVersion;
  manifest["producer"] = fields.producer;
  manifest["driver"] = fields.driver;
  // display form: manifests travel with the data; a credential-bearing raw
  // path must never be embedded.
  manifest["path_display"] = ResourceUri::parse( mainPath ).display();
  Json::Value shape;
  shape["width"] = fields.width;
  shape["height"] = fields.height;
  shape["band_count"] = fields.bandCount;
  shape["dtype"] = fields.dtype;
  manifest["shape"] = shape;
  manifest["crs"] = fields.crsAuthid;
  manifest["creation_options"] = creationOptionsJson( fields.creationOptions );
  manifest["dataset_sha256"] = digest;
  manifest["dataset_bytes"] = static_cast<Json::UInt64>( size );
  manifest["finalized_utc"] = instantToUtcString( nowEpochNanos() );
  return manifest;
}

void writeFinalizeManifest( const std::string &mainPath, const Json::Value &manifest )
{
  const std::string manifestPath = finalizeManifestPath( mainPath );
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string text = Json::writeString( builder, manifest );
  atomic_fs::writeFileAtomic( manifestPath, [ & ]( const std::string &stagedPath ) {
    std::ofstream file( stagedPath, std::ios::binary | std::ios::trunc );
    if ( !file )
      throw GeoError( ErrorCode::IoError, "manifest: cannot create " + stagedPath );
    file.write( text.data(), static_cast<std::streamsize>( text.size() ) );
    file.flush();
    if ( !file )
      throw GeoError( ErrorCode::IoError, "manifest: write failed for " + stagedPath );
  } );
}

Json::Value readFinalizeManifest( const std::string &mainPath )
{
  const std::string manifestPath = finalizeManifestPath( mainPath );
  if ( !atomic_fs::fileExists( manifestPath ) )
    throw GeoError( ErrorCode::NotFound, "finalize manifest missing for " + mainPath );
  if ( atomic_fs::fileSize( manifestPath ) > kMaxManifestBytes )
    throw GeoError( ErrorCode::InvalidMetadata, "finalize manifest exceeds the size cap; refusing to read" );
  std::ifstream file( manifestPath, std::ios::binary );
  if ( !file )
    throw GeoError( ErrorCode::IoError, "manifest: cannot open " + manifestPath );
  std::string text( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
  Json::Value manifest;
  std::string error;
  if ( !parseManifestDocument( text, manifest, error ) )
  {
    Json::Value details;
    details["parse_error"] = error;
    throw GeoError( ErrorCode::InvalidMetadata, "finalize manifest is not valid JSON", details );
  }
  if ( !manifest.isObject() || !manifest["schema_version"].isInt()
       || manifest["schema_version"].asInt() != kManifestSchemaVersion )
  {
    Json::Value details;
    if ( manifest["schema_version"].isInt() )
      details["found_version"] = manifest["schema_version"].asInt();
    details["expected_version"] = kManifestSchemaVersion;
    throw GeoError( ErrorCode::InvalidMetadata, "finalize manifest schema version mismatch", details );
  }
  return manifest;
}

Json::Value ManifestVerifyReport::toJson() const
{
  Json::Value json;
  json["verified"] = verified;
  json["digest_matched"] = digestMatched;
  json["manifest_present"] = manifestPresent;
  json["dataset_sha256"] = datasetSha256;
  json["path_display"] = displayPath;
  Json::Value issueArray( Json::arrayValue );
  for ( const ManifestIssue &issue : issues )
  {
    Json::Value entry;
    entry["code"] = issue.code;
    entry["message"] = issue.message;
    issueArray.append( entry );
  }
  json["issues"] = issueArray;
  return json;
}

ManifestVerifyReport verifyDataset( const std::string &mainPath, bool allowMissingManifest )
{
  ManifestVerifyReport report;
  report.displayPath = ResourceUri::parse( mainPath ).display();
  auto addIssue = [ & ]( const std::string &code, const std::string &message ) {
    report.issues.push_back( ManifestIssue{ code, message } );
  };

  if ( !atomic_fs::fileExists( mainPath ) )
  {
    addIssue( "main_file_missing", "dataset main file does not exist" );
    return report;
  }

  Json::Value manifest;
  try
  {
    manifest = readFinalizeManifest( mainPath );
    report.manifestPresent = true;
  }
  catch ( const GeoError &error )
  {
    if ( error.code() == ErrorCode::NotFound && allowMissingManifest )
    {
      addIssue( "manifest_missing_allowed", "finalize manifest absent (legacy tolerance requested by caller)" );
      return report; // verified stays false — absence is reported, never green-washed
    }
    if ( error.code() == ErrorCode::NotFound )
      addIssue( "manifest_missing", "finalize manifest absent; integrity is unverifiable" );
    else
      addIssue( "manifest_invalid", error.what() );
  }
  catch ( const std::exception & )
  {
    // Foreign-typed JSON values (jsoncpp LogicError etc.) are a malformed
    // manifest from the caller's point of view — never an escaping throw.
    addIssue( "manifest_invalid", "finalize manifest is malformed (foreign JSON types)" );
  }

  // Foreign-typed fields are a malformed manifest: every read below is
  // type-guarded, so a hostile document can never throw outside this
  // function's catch coverage.
  const std::string declaredDigest = manifest["dataset_sha256"].isString()
                                       ? manifest["dataset_sha256"].asString()
                                       : std::string();
  if ( report.manifestPresent && declaredDigest.empty() )
    addIssue( "manifest_invalid", "manifest carries no dataset_sha256; integrity is unverifiable" );
  if ( !declaredDigest.empty() )
  {
    try
    {
      report.datasetSha256 = datasetSha256Hex( mainPath );
      report.digestMatched = report.datasetSha256 == declaredDigest;
      if ( !report.digestMatched )
        addIssue( "digest_mismatch", "recomputed digest differs from the manifest declaration" );
    }
    catch ( const GeoError &error )
    {
      addIssue( "digest_mismatch", error.what() );
    }
  }

  // Shape re-check: only when the manifest declares a raster shape AND the
  // file still inspects as a raster. Vector/other kinds skip (declared 0s).
  const Json::Value &declaredShape = manifest["shape"];
  const int declaredWidth =
    declaredShape.isObject() && declaredShape["width"].isInt() ? declaredShape["width"].asInt() : 0;
  const int declaredHeight =
    declaredShape.isObject() && declaredShape["height"].isInt() ? declaredShape["height"].asInt() : 0;
  const int declaredBands = declaredShape.isObject() && declaredShape["band_count"].isInt()
                              ? declaredShape["band_count"].asInt()
                              : 0;
  if ( declaredWidth > 0 && declaredHeight > 0 )
  {
    try
    {
      const RasterMetadata meta = inspectRaster( mainPath );
      if ( meta.width != declaredWidth || meta.height != declaredHeight || meta.bandCount != declaredBands )
        addIssue( "shape_mismatch", "dataset shape differs from the manifest declaration" );
    }
    catch ( const GeoError & )
    {
      // Not raster-inspectable (vector/multidim/etc.): digest remains the
      // integrity gate; the shape verdict is advisory-only by design.
      addIssue( "inspect_unavailable", "dataset not raster-inspectable; shape re-check skipped" );
    }
  }

  report.verified = report.issues.empty();
  return report;
}

} // namespace sicnu::geo::io
