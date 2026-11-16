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

// #1953 (verdict stamp): digest AND size from ONE open + ONE read pass, so
// the two observations can never disagree (the old code re-opened the file
// for the size and an unchecked tellg() failure wrapped -1 to
// 18446744073709551615). Memory stays bounded at kDigestChunkBytes for any
// artifact size — the old whole-file buffer was a bad_alloc on multi-GB
// inputs. Empty artifact / unreadable / mid-read failure → false (nothing
// to bind), preserving the v1 empty-digest contract.
bool verdictArtifactDigestAndSize( const std::string &artifactPath, std::string &digestOut,
                                   unsigned long long &sizeOut )
{
  digestOut.clear();
  sizeOut = 0;
  if ( artifactPath.empty() )
    return false;
  std::ifstream file( artifactPath, std::ios::binary );
  if ( !file.is_open() )
    return false;

  // swarm-review LT2-4: the old fingerprint sampled head, evenly-spaced
  // interior and tail 64 KiB windows, so on a >~512 KiB artifact most bytes
  // never entered the digest and a rewrite confined between the windows did
  // not trip drift — the tamper-detection promise (R18-E T102) covers EVERY
  // byte: stream the whole file once and hash it in full (bounded memory).
  sicnu::lab::Sha256Stream digest;
  unsigned long long total = 0;
  char buffer[ kDigestChunkBytes ];
  while ( file )
  {
    file.read( buffer, kDigestChunkBytes );
    const std::streamsize got = file.gcount();
    if ( got > 0 )
    {
      digest.update( buffer, static_cast<std::size_t>( got ) );
      total += static_cast<unsigned long long>( got );
    }
  }
  if ( file.bad() || total == 0 )
    return false; // unreadable, or no bytes at all: nothing to bind
  digestOut = digest.finalizeHex();
  sizeOut = total;
  return true;
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
  std::string digest;
  unsigned long long size = 0;
  if ( !verdictArtifactDigestAndSize( artifactPath, digest, size ) )
    return {};
  return digest;
}

std::string makeVerdictStamp( const std::string &summaryDigest,
                              const std::string &artifactPath )
{
  if ( summaryDigest.size() != 64 )
    return {};
  // #1953: digest and size come from the SAME single pass — the old second
  // open re-observed the file (TOCTOU) and its unchecked tellg() failure
  // wrapped -1 into a huge unsigned size bound to a valid digest.
  std::string contentDigest;
  unsigned long long size = 0;
  if ( !verdictArtifactDigestAndSize( artifactPath, contentDigest, size ) )
    return {};
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
