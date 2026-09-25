// src/lab_pack/lab_pack.cpp — sicnu.lab-pack/1 loader + verifier (Qt-free).
#include "lab_pack.h"

#include "grader/grader_sha256.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <utility>

namespace sicnu::labpack
{

namespace
{

constexpr const char *kSchemaId = "sicnu.lab-pack/1";

bool isValidSha256Hex( const std::string &s )
{
  if ( s.size() != 64 )
    return false;
  for ( const char c : s )
  {
    if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) )
      return false;
  }
  return true;
}

/// requireString twin: must exist, be a string, and be non-empty.
bool requireString( const Json::Value &object, const char *key, std::string *out )
{
  const Json::Value value = object[key];
  if ( !value.isString() || value.asString().empty() )
    return false;
  *out = value.asString();
  return true;
}

std::string contextError( const std::string &context, const std::string &detail )
{
  return context + ": " + detail;
}

/// Streaming sha256 in bounded chunks. Returns an empty string when the file
/// cannot be opened (caller reports the typed issue).
std::string fileSha256( const std::filesystem::path &path, std::int64_t *bytesOut )
{
  std::ifstream file( path, std::ios::binary );
  if ( !file.is_open() )
    return std::string();
  grader::Sha256 hash;
  std::int64_t total = 0;
  char buffer[65536];
  while ( file.good() )
  {
    file.read( buffer, sizeof( buffer ) );
    const std::streamsize got = file.gcount();
    if ( got > 0 )
    {
      hash.update( buffer, static_cast<std::size_t>( got ) );
      total += got;
    }
  }
  if ( file.bad() )
    return std::string();
  *bytesOut = total;
  return hash.hexDigest();
}

/// Splits on '/' keeping empty components (the agent parser's split twin):
/// component equality is what the lexical `..` check needs.
bool hasDotDotComponent( const std::string &path )
{
  std::size_t start = 0;
  while ( start <= path.size() )
  {
    const std::size_t slash = path.find( '/', start );
    const std::string component =
      slash == std::string::npos ? path.substr( start ) : path.substr( start, slash - start );
    if ( component == ".." )
      return true;
    if ( slash == std::string::npos )
      break;
    start = slash + 1;
  }
  return false;
}

std::string u8( const std::filesystem::path &path )
{
  return utf8FromPath( path );
}

} // namespace

std::filesystem::path pathFromUtf8( const std::string &utf8 )
{
  const std::u8string u8bytes( reinterpret_cast<const char8_t *>( utf8.data() ), utf8.size() );
  return std::filesystem::path( u8bytes );
}

std::string utf8FromPath( const std::filesystem::path &path )
{
  const std::u8string u8bytes = path.generic_u8string();
  return std::string( reinterpret_cast<const char *>( u8bytes.data() ), u8bytes.size() );
}

Provenance provenanceFromString( const std::string &provenance )
{
  if ( provenance == "committed-fixture" )
    return Provenance::CommittedFixture;
  if ( provenance == "generated-tmp" )
    return Provenance::GeneratedTmp;
  return Provenance::GeneratedSamples;
}

std::string provenanceToString( Provenance provenance )
{
  switch ( provenance )
  {
    case Provenance::CommittedFixture:
      return "committed-fixture";
    case Provenance::GeneratedTmp:
      return "generated-tmp";
    case Provenance::GeneratedSamples:
      return "generated-samples";
  }
  return "generated-samples";
}

Json::Value PackInput::toJson() const
{
  Json::Value json( Json::objectValue );
  json["path"] = path;
  json["role"] = role;
  json["provenance"] = provenanceToString( provenance );
  json["sha256"] = sha256.empty() ? Json::Value( Json::nullValue ) : Json::Value( sha256 );
  json["declared_bytes"] = static_cast<Json::Int64>( declaredBytes );
  if ( !sensorTruth.empty() )
    json["sensor_truth"] = sensorTruth;
  if ( !notes.empty() )
    json["notes"] = notes;
  return json;
}

PackLoadResult PackVerifier::loadFromBytes( const std::string &bytes,
                                            const std::string &errorContext )
{
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string parseErrors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  if ( !reader->parse( bytes.data(), bytes.data() + bytes.size(), &root, &parseErrors ) )
    return PackLoadResult::failure( "lab.pack_schema",
                                    contextError( errorContext, parseErrors ) );

  if ( !root.isObject() || !root["schema_version"].isString()
       || root["schema_version"].asString() != kSchemaId )
    return PackLoadResult::failure(
      "lab.pack_schema",
      contextError( errorContext,
                    std::string( "schema_version must be the string \"" ) + kSchemaId + "\"" ) );

  PackDocument pack;
  std::string error;
  if ( !requireString( root, "lab_id", &error ) )
    return PackLoadResult::failure(
      "lab.pack_field", contextError( errorContext, "missing or non-string field \"lab_id\"" ) );
  pack.labId = error;
  if ( !requireString( root, "pack_version", &error ) )
    return PackLoadResult::failure(
      "lab.pack_field",
      contextError( errorContext, "missing or non-string field \"pack_version\"" ) );
  pack.packVersion = error;
  if ( !requireString( root, "license", &error ) )
    return PackLoadResult::failure(
      "lab.pack_field", contextError( errorContext, "missing or non-string field \"license\"" ) );
  pack.license = error;

  if ( root.isMember( "generator" ) )
  {
    if ( !root["generator"].isString() )
      return PackLoadResult::failure(
        "lab.pack_field", contextError( errorContext, "generator must be a string" ) );
    pack.generator = root["generator"].asString();
  }
  if ( root.isMember( "notes" ) )
  {
    if ( !root["notes"].isString() )
      return PackLoadResult::failure(
        "lab.pack_field", contextError( errorContext, "notes must be a string" ) );
    pack.notes = root["notes"].asString();
  }
  if ( root.isMember( "declared_offline_bytes" ) && root["declared_offline_bytes"].isInt64() )
    pack.declaredOfflineBytes = root["declared_offline_bytes"].asInt64();

  const Json::Value inputs = root["inputs"];
  if ( !inputs.isArray() || inputs.empty() )
    return PackLoadResult::failure(
      "lab.pack_field",
      contextError( errorContext, "\"inputs\" must be a non-empty array" ) );

  std::vector<std::string> seenPaths;
  for ( const Json::Value &entry : inputs )
  {
    if ( !entry.isObject() )
      return PackLoadResult::failure(
        "lab.pack_input", contextError( errorContext, "input entries must be objects" ) );

    PackInput input;
    if ( !requireString( entry, "path", &error ) )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext, "missing or non-string field \"path\"" ) );
    input.path = error;
    if ( input.path.find( '\\' ) != std::string::npos )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext,
                      "input paths must use forward slashes (" + input.path + ")" ) );
    if ( std::find( seenPaths.begin(), seenPaths.end(), input.path ) != seenPaths.end() )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext, "duplicate input path " + input.path ) );
    seenPaths.push_back( input.path );

    if ( !requireString( entry, "role", &error ) )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext, "missing or non-string field \"role\"" ) );
    input.role = error;

    if ( !entry["provenance"].isString() )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext, "provenance must be a string (" + input.path + ")" ) );
    const std::string provenance = entry["provenance"].asString();
    input.provenance = provenanceFromString( provenance );
    if ( provenanceToString( input.provenance ) != provenance )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext,
                      "unknown provenance \"" + provenance + "\" (" + input.path + ")" ) );

    if ( entry.isMember( "sha256" ) && entry["sha256"].isString() )
      input.sha256 = entry["sha256"].asString();
    if ( !input.sha256.empty() && !isValidSha256Hex( input.sha256 ) )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext,
                      "sha256 must be 64 lowercase hex chars (" + input.path + ")" ) );
    if ( input.provenance == Provenance::CommittedFixture && input.sha256.empty() )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext,
                      "committed-fixture input requires sha256 (" + input.path + ")" ) );

    if ( entry.isMember( "bytes" ) && entry["bytes"].isInt64() )
      input.declaredBytes = entry["bytes"].asInt64();
    if ( input.provenance == Provenance::CommittedFixture && input.declaredBytes < 0 )
      return PackLoadResult::failure(
        "lab.pack_input",
        contextError( errorContext,
                      "committed-fixture input requires bytes (" + input.path + ")" ) );

    if ( entry.isMember( "sensor_truth" ) && entry["sensor_truth"].isString() )
      input.sensorTruth = entry["sensor_truth"].asString();
    if ( entry.isMember( "notes" ) && entry["notes"].isString() )
      input.notes = entry["notes"].asString();

    pack.inputs.push_back( input );
  }

  return PackLoadResult::success( std::move( pack ) );
}

PackLoadResult PackVerifier::load( const std::filesystem::path &path )
{
  std::ifstream file( path, std::ios::binary );
  if ( !file.is_open() )
    return PackLoadResult::failure( "lab.pack_unreadable",
                                    "cannot open " + u8( path ) );
  std::string text( ( std::istreambuf_iterator<char>( file ) ),
                    std::istreambuf_iterator<char>() );
  if ( file.bad() )
    return PackLoadResult::failure( "lab.pack_unreadable",
                                    "cannot read " + u8( path ) );
  return loadFromBytes( text, u8( path ) );
}

Json::Value PackVerification::toJson() const
{
  Json::Value json( Json::objectValue );
  json["overall"] = overall;
  json["verified_bytes"] = static_cast<Json::Int64>( verifiedBytes );
  Json::Value issueArray( Json::arrayValue );
  for ( const Json::Value &issue : issues )
    issueArray.append( issue );
  json["issues"] = issueArray;
  return json;
}

PackVerification PackVerifier::verify( const PackDocument &pack,
                                       const std::filesystem::path &root )
{
  PackVerification verification;
  bool failed = false;
  bool degraded = false;

  std::error_code ec;
  const std::filesystem::path canonicalRoot = std::filesystem::canonical( root, ec );
  const std::string canonicalRootUtf8 = ec ? std::string() : u8( canonicalRoot );
  for ( const PackInput &input : pack.inputs )
  {
    // #1186: confine input paths under the pack root — absolute / ../ paths
    // used to yield an existence+size+sha256 oracle outside the pack.
    if ( std::filesystem::path( pathFromUtf8( input.path ) ).is_absolute()
         || hasDotDotComponent( input.path ) )
    {
      Json::Value issue( Json::objectValue );
      issue["code"] = "lab.pack_input_outside_root";
      issue["path"] = input.path;
      issue["detail"] = "input path must be pack-relative without '..'";
      verification.issues.push_back( issue );
      failed = true;
      continue;
    }
    const std::filesystem::path absolute = root / pathFromUtf8( input.path );
    if ( !canonicalRootUtf8.empty() )
    {
      std::error_code canonicalEc;
      const std::filesystem::path canonical = std::filesystem::canonical( absolute, canonicalEc );
      if ( !canonicalEc )
      {
        const std::string canonicalUtf8 = u8( canonical );
        if ( canonicalUtf8 != canonicalRootUtf8
             && canonicalUtf8.rfind( canonicalRootUtf8 + "/", 0 ) != 0 )
        {
          Json::Value issue( Json::objectValue );
          issue["code"] = "lab.pack_input_outside_root";
          issue["path"] = input.path;
          issue["detail"] = "resolved input escapes the pack root";
          verification.issues.push_back( issue );
          failed = true;
          continue;
        }
      }
    }

    std::error_code statEc;
    if ( !std::filesystem::is_regular_file( absolute, statEc ) || statEc )
    {
      Json::Value issue( Json::objectValue );
      issue["code"] = "lab.pack_input_missing";
      issue["path"] = input.path;
      issue["provenance"] = provenanceToString( input.provenance );
      if ( input.provenance == Provenance::CommittedFixture )
      {
        issue["detail"] = "committed fixture is absent from this deployment";
        failed = true;
      }
      else
      {
        issue["detail"] = "regenerable input absent — run the pack generator";
        degraded = true;
      }
      verification.issues.push_back( issue );
      continue;
    }

    if ( input.provenance == Provenance::CommittedFixture )
    {
      std::int64_t actualBytes = -1;
      const std::string actualHash = fileSha256( absolute, &actualBytes );
      if ( actualHash.empty() )
      {
        Json::Value issue( Json::objectValue );
        issue["code"] = "lab.pack_unreadable";
        issue["path"] = input.path;
        issue["detail"] = "file exists but cannot be read";
        verification.issues.push_back( issue );
        failed = true;
        continue;
      }
      if ( actualBytes != input.declaredBytes )
      {
        Json::Value issue( Json::objectValue );
        issue["code"] = "lab.pack_size_mismatch";
        issue["path"] = input.path;
        issue["declared_bytes"] = static_cast<Json::Int64>( input.declaredBytes );
        issue["actual_bytes"] = static_cast<Json::Int64>( actualBytes );
        verification.issues.push_back( issue );
        failed = true;
        continue;
      }
      if ( actualHash != input.sha256 )
      {
        Json::Value issue( Json::objectValue );
        issue["code"] = "lab.pack_checksum_mismatch";
        issue["path"] = input.path;
        issue["declared_sha256"] = input.sha256;
        issue["actual_sha256"] = actualHash;
        verification.issues.push_back( issue );
        failed = true;
        continue;
      }
      verification.verifiedBytes += actualBytes;
      continue;
    }

    // Regenerable inputs: presence verified; declared size is informative.
    std::error_code sizeEc;
    const std::int64_t actualBytes =
      static_cast<std::int64_t>( std::filesystem::file_size( absolute, sizeEc ) );
    if ( input.declaredBytes >= 0 && !sizeEc && actualBytes != input.declaredBytes )
    {
      Json::Value issue( Json::objectValue );
      issue["code"] = "lab.pack_regenerated_size_drift";
      issue["path"] = input.path;
      issue["declared_bytes"] = static_cast<Json::Int64>( input.declaredBytes );
      issue["actual_bytes"] = static_cast<Json::Int64>( actualBytes );
      issue["detail"] = "informative: regenerated by a different toolchain";
      verification.issues.push_back( issue );
      degraded = true;
    }
    if ( !sizeEc )
      verification.verifiedBytes += actualBytes;
  }

  verification.overall = failed ? "failed" : degraded ? "degraded" : "verified";
  return verification;
}

std::vector<PackDocument> PackVerifier::loadPacksFromDir(
  const std::filesystem::path &dir, std::vector<PackLoadResult> *problems )
{
  std::vector<PackDocument> packs;
  std::vector<std::filesystem::path> entries;
  std::error_code ec;
  for ( std::filesystem::directory_iterator it( dir, ec ), end; !ec && it != end; it.increment( ec ) )
  {
    const std::filesystem::path entry = it->path();
    std::error_code statEc;
    if ( !std::filesystem::is_regular_file( entry, statEc ) || statEc )
      continue;
    const std::string name = u8( entry );
    if ( name.empty() || name.front() == '.' )
      continue; // hidden files never match the *.pack.json glob
    if ( name.size() <= 10 || name.rfind( ".pack.json" ) != name.size() - 10 )
      continue;
    entries.push_back( entry );
  }
  std::sort( entries.begin(), entries.end(), []( const std::filesystem::path &a,
                                                 const std::filesystem::path &b ) {
    return utf8FromPath( a ) < utf8FromPath( b );
  } );
  for ( const std::filesystem::path &entry : entries )
  {
    PackLoadResult result = PackVerifier::load( entry );
    if ( result.ok )
      packs.push_back( std::move( result.pack ) );
    else if ( problems )
      problems->push_back( std::move( result ) );
  }
  return packs;
}

} // namespace sicnu::labpack
