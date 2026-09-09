/***************************************************************************
  geospatial/remote/probe_remote.cpp
  Remote Sensing I/O Foundation 5.0 — bounded remote reachability probe.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Implementation notes (proven against GDAL 3.10+/curl 8.x on this stack):
  * CPLHTTPFetch delivers response header pairs in papszHeaders as
    "Name=value"; the HTTP status code may be absent (nStatus == 0) even on
    success, so reachability is derived from the whole answer, not nStatus.
  * The probe range is declared through the HEADERS option ("Range: ...")
    and the fetch is double-bounded with MAX_FILE_SIZE so even a range-ignoring
    origin cannot push more than maxProbeBytes into this process.
  * Timeout strings arrive through CPL error state ("timed out").
 ***************************************************************************/

#include "geospatial/remote/probe_remote.h"

#include "geospatial/util/resource_uri.h"

#include <cpl_conv.h>
#include <cpl_http.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace sicnu::geo
{

namespace
{

bool headerIs( const char *headerLine, const char *name, std::string &value )
{
  if ( headerLine == nullptr )
    return false;
  const char *equals = std::strchr( headerLine, '=' );
  if ( equals == nullptr )
    return false;
  std::string key( headerLine, static_cast<std::size_t>( equals - headerLine ) );
  std::transform( key.begin(), key.end(), key.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  if ( key != name )
    return false;
  value = equals + 1;
  while ( !value.empty() && ( value.front() == ' ' || value.front() == '\t' ) )
    value.erase( value.begin() );
  while ( !value.empty() && ( value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ) )
    value.pop_back();
  return true;
}

bool containsIgnoreCase( const std::string &haystack, const char *needle )
{
  if ( haystack.empty() )
    return false;
  std::string lowered = haystack;
  std::transform( lowered.begin(), lowered.end(), lowered.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return lowered.find( needle ) != std::string::npos;
}

} // namespace

Json::Value RemoteProbeResult::toJson() const
{
  Json::Value json;
  json["reachable"] = reachable;
  json["http_status"] = httpStatus;
  json["accepts_ranges"] = acceptsRanges;
  json["has_size"] = hasSize;
  if ( hasSize )
    json["size_bytes"] = static_cast<Json::UInt64>( sizeBytes );
  json["content_type"] = contentType;
  json["effective_url"] = effectiveUrl;
  return json;
}

RemoteProbeResult probeRemote( const std::string &url, const RemoteProbeOptions &options )
{
  const ResourceUri uri = ResourceUri::parse( url );
  const bool remoteSpelling =
    uri.kind == ResourceKind::RemoteHttp ||
    ( uri.kind == ResourceKind::VsiRemote && !uri.remoteUrl().empty() );
  if ( !remoteSpelling )
  {
    Json::Value details;
    details["url"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "probeRemote: not a remote http(s) resource", details );
  }
  const std::string target =
    uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();

  // Option strings are built once, then the char* array — c_str() pointers
  // into a growing container dangle; assemble the array after the strings.
  const std::uintmax_t probeBytes =
    options.maxProbeBytes == 0 ? 1 : std::min<std::uintmax_t>( options.maxProbeBytes, 1024 * 1024 );
  std::vector<std::string> optionStrings;
  optionStrings.reserve( 5 );
  optionStrings.push_back( "TIMEOUT=" + std::to_string( options.timeoutSeconds ) );
  optionStrings.push_back( "CONNECTTIMEOUT=" + std::to_string( options.connectTimeoutSeconds ) );
  optionStrings.push_back( "RETRIES=" + std::to_string( options.maxRetries ) );
  optionStrings.push_back( "MAX_FILE_SIZE=" + std::to_string( probeBytes ) );
  optionStrings.push_back( "HEADERS=Range: bytes=0-" + std::to_string( probeBytes - 1 ) );
  std::vector<const char *> optionKeys;
  optionKeys.reserve( optionStrings.size() + 1 );
  for ( const std::string &option : optionStrings )
    optionKeys.push_back( option.c_str() );
  optionKeys.push_back( nullptr );

  CPLHTTPResult *result = CPLHTTPFetch( target.c_str(), optionKeys.data() );
  if ( result == nullptr )
    throw GeoError( ErrorCode::NetworkError, "probeRemote: request could not be dispatched: " + uri.display() );

  RemoteProbeResult probe;
  probe.httpStatus = result->nStatus;
  // effectiveUrl stays empty: this GDAL build's CPLHTTPResult does not
  // expose the post-redirect URL; the field is reserved for transports that
  // do. Redaction is applied by consumers via ResourceUri::display().

  // Response headers: subset only, never request credentials.
  const char *lastError = result->pszErrBuf != nullptr ? result->pszErrBuf : CPLGetLastErrorMsg();
  bool timedOut = false;
  if ( lastError != nullptr && containsIgnoreCase( lastError, "timed out" ) )
    timedOut = true;

  bool declaredLength = false;
  std::uintmax_t declaredLengthBytes = 0;
  bool rangeRewritten = false; // origin answered with Content-Range
  if ( result->papszHeaders != nullptr )
  {
    for ( char **header = result->papszHeaders; *header; ++header )
    {
      std::string value;
      if ( headerIs( *header, "accept-ranges", value ) )
        probe.acceptsRanges = containsIgnoreCase( value, "bytes" );
      else if ( headerIs( *header, "content-range", value ) )
      {
        rangeRewritten = true;
        probe.acceptsRanges = true; // a Content-Range answer is the strongest proof
        // "bytes 0-1023/4195070" → full size after '/'
        const std::size_t slash = value.rfind( '/' );
        if ( slash != std::string::npos )
        {
          try
          {
            declaredLengthBytes = std::stoull( value.substr( slash + 1 ) );
            declaredLength = true;
          }
          catch ( const std::exception & )
          {
          }
        }
      }
      else if ( headerIs( *header, "content-length", value ) && !rangeRewritten )
      {
        // Content-Range carries the authoritative full size; a plain
        // Content-Length on a ranged answer would only be the slice size.
        try
        {
          declaredLengthBytes = std::stoull( value );
          declaredLength = true;
        }
        catch ( const std::exception & )
        {
        }
      }
      else if ( headerIs( *header, "content-type", value ) )
        probe.contentType = value;
    }
  }

  const bool fetched = result->pabyData != nullptr && result->nDataLen > 0;
  CPLHTTPDestroyResult( result );

  if ( timedOut )
    throw GeoError( ErrorCode::Timeout, "probeRemote: request exceeded its time budget: " + uri.display() );

  // Reachability: a body, an explicit status, or a declared length.
  probe.reachable = fetched || probe.httpStatus > 0 || declaredLength;
  if ( !probe.reachable )
    throw GeoError( ErrorCode::NetworkError, "probeRemote: no usable answer from origin: " + uri.display() );
  // 404/410 must be tested BEFORE the generic >=400 branch, or the
  // documented NotFound mapping is unreachable.
  if ( probe.httpStatus == 404 || probe.httpStatus == 410 )
    throw GeoError( ErrorCode::NotFound, "probeRemote: remote resource is gone: " + uri.display() );
  if ( probe.httpStatus >= 400 )
  {
    Json::Value details;
    details["status"] = probe.httpStatus;
    throw GeoError( ErrorCode::NetworkError, "probeRemote: remote origin reported an error: " + uri.display(), details );
  }
  if ( !fetched && declaredLength && declaredLengthBytes == 0 )
  {
    // No status code exposed by this GDAL build, but an empty declared
    // length with no body is an origin error (5xx) or an empty object —
    // never a usable asset.
    throw GeoError( ErrorCode::NetworkError, "probeRemote: remote origin reported an error: " + uri.display() );
  }

  // Range support comes from a rewritten Content-Range (a real 206) or the
  // explicit Accept-Ranges header — already parsed above. A truncated 200
  // from a range-ignoring origin must NOT be read as range support (the
  // dropped heuristic "fetched <= probeBytes" would bless exactly that
  // origin).
  if ( declaredLength )
  {
    probe.hasSize = true;
    probe.sizeBytes = declaredLengthBytes;
  }

  return probe;
}

} // namespace sicnu::geo
