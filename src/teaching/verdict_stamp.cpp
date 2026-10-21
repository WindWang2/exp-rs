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

// Same window shape as the batch checkpoint stamp (#1520/#1525): head,
// evenly-spaced interior windows and tail, bounded read regardless of the
// artifact size. Files up to ~8 windows are fully covered; larger files
// are fingerprinted at fixed stride.
constexpr std::streamsize kWindowBytes = 64 * 1024;
constexpr int kMiddleWindows = 6;

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

  std::string fingerprint;
  fingerprint.reserve( static_cast<std::size_t>( kWindowBytes ) * ( kMiddleWindows + 2 ) );

  char buffer[ kWindowBytes ];
  auto readWindowAt = [ &file, &fingerprint, &buffer ]( std::streamoff offset ) {
    if ( !file.seekg( offset, std::ios::beg ) )
      return false;
    file.read( buffer, kWindowBytes );
    const std::streamsize got = file.gcount();
    if ( got <= 0 )
      return false;
    fingerprint.append( buffer, static_cast<std::size_t>( got ) );
    return true;
  };

  if ( !readWindowAt( 0 ) )
    return {};
  const auto wideSize = static_cast<unsigned long long>( size );
  if ( wideSize > 2 * static_cast<unsigned long long>( kWindowBytes ) )
  {
    const unsigned long long interior =
      wideSize - 2 * static_cast<unsigned long long>( kWindowBytes );
    const unsigned long long stride = interior / ( kMiddleWindows + 1 );
    for ( int i = 1; i <= kMiddleWindows; ++i )
    {
      if ( !readWindowAt( static_cast<std::streamoff>(
             static_cast<unsigned long long>( kWindowBytes ) + i * stride ) ) )
        return {};
    }
  }
  if ( wideSize > static_cast<unsigned long long>( kWindowBytes ) )
  {
    if ( !readWindowAt(
           static_cast<std::streamoff>( wideSize - static_cast<unsigned long long>( kWindowBytes ) ) ) )
      return {};
  }
  return sicnu::lab::sha256Hex( fingerprint );
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
  Json::Value doc;
  std::string errors;
  std::istringstream input( summaryJson );
  if ( !Json::parseFromStream( reader, input, &doc, &errors ) )
    return {};
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  writer["commentStyle"] = "None";
  return sicnu::lab::sha256Hex( Json::writeString( writer, doc ) );
}

} // namespace sicnu::teaching
