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

// Stream chunk for the single-pass bounded-memory artifact digest.
constexpr std::streamsize kDigestChunkBytes = 64 * 1024;

struct ArtifactDigest
{
  std::string hex;             // empty = unreadable / no bytes: nothing to bind
  unsigned long long bytes = 0; // SAME observation window as hex
};

ArtifactDigest digestArtifactStreamed( const std::string &artifactPath )
{
  // swarm-review LT2-4 keeps its full-coverage promise: every byte enters
  // the digest. #1953 / R33-T04 WP13 re-shapes HOW: one open, one streaming
  // pass — the old form materialized the whole file in memory (bad_alloc on
  // large artifacts) and makeVerdictStamp then took the size from a SECOND,
  // unchecked open whose failed tellg() cast -1 into a huge unsigned.
  if ( artifactPath.empty() )
    return {};
  std::ifstream file( artifactPath, std::ios::binary );
  if ( !file.is_open() )
    return {};
  sicnu::lab::LabSha256 hash;
  unsigned long long total = 0;
  char buffer[ kDigestChunkBytes ];
  while ( file )
  {
    file.read( buffer, kDigestChunkBytes );
    const std::streamsize got = file.gcount();
    if ( got > 0 )
    {
      hash.update( buffer, static_cast<std::size_t>( got ) );
      total += static_cast<unsigned long long>( got );
    }
  }
  if ( file.bad() || total == 0 )
    return {}; // unreadable, or no bytes at all: nothing to bind
  return { hash.finalizeHex(), total };
}

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
  return digestArtifactStreamed( artifactPath ).hex;
}

std::string makeVerdictStamp( const std::string &summaryDigest,
                              const std::string &artifactPath )
{
  if ( summaryDigest.size() != 64 )
    return {};
  const ArtifactDigest digested = digestArtifactStreamed( artifactPath );
  if ( digested.hex.empty() )
    return {};
  // Size and digest observed in the SAME pass: a vanished/unreadable file
  // yields an empty digest (invalid stamp), never a fabricated size.
  return "v1:" + summaryDigest + ":" + std::to_string( digested.bytes ) + ":" + digested.hex;
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
