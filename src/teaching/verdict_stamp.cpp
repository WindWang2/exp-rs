#include "teaching/verdict_stamp.h"

#include "lab/sha256.h"

#include <json/json.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sicnu::teaching
{
namespace
{

// Stream chunk for the one-shot full-content digest below.
constexpr std::streamsize kDigestChunkBytes = 64 * 1024;

} // namespace

VerdictStamp parseVerdictStamp( const std::string &stamp )
{
  VerdictStamp parsed;
  if ( stamp.rfind( "v1:", 0 ) != 0 )
    return parsed;
  const std::string body = stamp.substr( 3 );
  const auto first = body.find( ':' );
  if ( first == std::string::npos )
    return parsed;
  const auto second = body.find( ':', first + 1 );
  if ( second == std::string::npos )
    return parsed;
  parsed.summaryDigest = body.substr( 0, first );
  const std::string sizeText = body.substr( first + 1, second - first - 1 );
  parsed.contentDigest = body.substr( second + 1 );
  if ( parsed.summaryDigest.size() != 64 || parsed.contentDigest.size() != 64 )
  {
    return VerdictStamp{};
  }
  if ( sizeText.empty()
       || sizeText.find_first_not_of( "0123456789" ) != std::string::npos )
  {
    return VerdictStamp{};
  }
  try
  {
    parsed.size = std::stoull( sizeText );
  }
  catch ( ... )
  {
    return VerdictStamp{};
  }
  parsed.parsed = true;
  return parsed;
}

std::string verdictArtifactContentDigest( const std::string &artifactPath )
{
  if ( artifactPath.empty() )
    return {};
  std::ifstream file( artifactPath, std::ios::binary );
  if ( !file.is_open() )
    return {};
  file.seekg( 0, std::ios::end );
  const std::streamoff size = file.tellg();
  if ( size < 0 )
    return {};
  file.seekg( 0, std::ios::beg );

  // swarm-review LT2-4: the old fingerprint sampled head, evenly-spaced
  // interior and tail 64 KiB windows, so on a >~512 KiB artifact most bytes
  // never entered the digest and a rewrite confined between the windows did
  // not trip drift — the tamper-detection promise (R18-E T102) covered a
  // fraction of the file. The artifact is a local, one-time-read input, so
  // the digest now covers EVERY byte: stream the whole file once and hash
  // it in full.
  std::string bytes;
  bytes.reserve( static_cast<std::size_t>( size ) );
  char buffer[ kDigestChunkBytes ];
  while ( file )
  {
    file.read( buffer, kDigestChunkBytes );
    const std::streamsize got = file.gcount();
    if ( got > 0 )
      bytes.append( buffer, static_cast<std::size_t>( got ) );
  }
  if ( file.bad() || bytes.empty() )
    return {}; // unreadable, or no bytes at all: nothing to bind
  return sicnu::lab::sha256Hex( bytes );
}

std::string makeVerdictStamp( const std::string &summaryDigest,
                              const std::string &artifactPath )
{
  if ( summaryDigest.size() != 64 )
    return {};
  const std::string contentDigest = verdictArtifactContentDigest( artifactPath );
  if ( contentDigest.empty() )
    return {};
  std::ifstream file( artifactPath, std::ios::binary );
  file.seekg( 0, std::ios::end );
  const auto size = static_cast<unsigned long long>( file.tellg() );
  return "v1:" + summaryDigest + ":" + std::to_string( size ) + ":" + contentDigest;
}

std::string verdictSummaryDigest( const std::string &summaryJson )
{
  // Digest the parsed document's compact serialization: whitespace-only
  // edits must not void a verdict that did not change.
  Json::CharReaderBuilder reader;
  // #1607 residual (R20-05): pinned stackLimit + typed catch — a verdict
  // summary is untrusted persisted text, and jsoncpp THROWS once the nesting
  // limit is exceeded; the refusal stays an empty digest (the stamp then
  // fails its own equality check), never a crash through the stamp path.
  reader["stackLimit"] = 256;
  Json::Value doc;
  std::string errors;
  std::istringstream input( summaryJson );
  try
  {
    if ( !Json::parseFromStream( reader, input, &doc, &errors ) )
      return {};
  }
  catch ( const std::exception & )
  {
    return {};
  }
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  writer["commentStyle"] = "None";
  return sicnu::lab::sha256Hex( Json::writeString( writer, doc ) );
}

} // namespace sicnu::teaching
