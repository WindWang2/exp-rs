/***************************************************************************
  geospatial/remote/http_fetch.cpp — bounded CPL HTTP fetch implementation.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Proven CPL behavior this code relies on (GDAL 3.10+/curl 8.x):
  * CPLHTTPFetch returns nullptr for dispatch/connect failures and records
    timeout / transfer-cut evidence in CPL error state (probe_remote 5.0
    established this against the same vendored build).
  * Response headers arrive in papszHeaders as "Name=value" lines.
  * nStatus can be 0 even on a usable answer; reachability and status are
    therefore derived from the whole answer, never from nStatus alone.
  * MAX_FILE_SIZE aborts the transfer once the budget is exceeded (partial
    bytes are preserved); a body of exactly maxResponseBytes is treated as
    truncated (conservative, truthful). This layer additionally slices the
    body to the budget — CPL's enforcement turned out to be unreliable for
    plain 200 answers in the vendored build.
 ***************************************************************************/

#include "geospatial/remote/http_fetch.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/util/resource_uri.h"

#include <cpl_conv.h>
#include <cpl_http.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <sstream>

namespace sicnu::geo
{

namespace
{

std::string toLower( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

bool containsIgnoreCase( const std::string &haystack, const char *needle )
{
  return !haystack.empty() && toLower( haystack ).find( needle ) != std::string::npos;
}

/// Parses one "Name=value" CPL header line into the (already lowercased) map.
void recordHeader( std::map<std::string, std::string> &headers, const char *headerLine )
{
  if ( headerLine == nullptr )
    return;
  const char *equals = std::strchr( headerLine, '=' );
  if ( equals == nullptr )
    return;
  std::string name( headerLine, static_cast<std::size_t>( equals - headerLine ) );
  std::string value = equals + 1;
  while ( !value.empty() && ( value.front() == ' ' || value.front() == '\t' ) )
    value.erase( value.begin() );
  while ( !value.empty() && ( value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ) )
    value.pop_back();
  const std::string lower = toLower( name );
  // First occurrence wins: with duplicate headers the first carries the
  // origin's declared value in practice, and stability beats cleverness.
  headers.emplace( lower, value );
}

bool declaredContentTypeIsJson( const HttpFetchResult &result )
{
  const std::string type = toLower( result.headerValue( "content-type" ) );
  if ( type.empty() )
    return true; // undeclared: attempt the parse, failures stay typed
  return type.find( "json" ) != std::string::npos;
}

/// Captures CPL error text emitted during a fetch. The default handler
/// prints AND clears state, so transport evidence ("Failed to connect…",
/// "Operation timed out…", "HTTP error code : 404") only survives if a
/// scoped handler records it. Runs on the fetching thread → thread_local.
class CapturingCplErrors
{
  public:
    CapturingCplErrors() { CPLPushErrorHandler( &CapturingCplErrors::handler ); }
    ~CapturingCplErrors() { CPLPopErrorHandler(); }
    CapturingCplErrors( const CapturingCplErrors & ) = delete;
    CapturingCplErrors &operator=( const CapturingCplErrors & ) = delete;

    static void clear() { t_lastMessage.clear(); }
    static const std::string &lastMessage() { return t_lastMessage; }

  private:
    static void CPL_STDCALL handler( CPLErr, CPLErrorNum, const char *message )
    {
      t_lastMessage = message != nullptr ? message : "";
    }
    static thread_local std::string t_lastMessage;
};

thread_local std::string CapturingCplErrors::t_lastMessage;

bool contentTypeMatchesAcceptList( const HttpFetchResult &result,
                                   const std::vector<std::string> &acceptContentTypes )
{
  if ( acceptContentTypes.empty() )
    return true;
  const std::string type = toLower( result.headerValue( "content-type" ) );
  for ( const std::string &accepted : acceptContentTypes )
  {
    if ( !accepted.empty() && type.find( toLower( accepted ) ) != std::string::npos )
      return true;
  }
  return false;
}

/// Shared bounded fetch. `throwHttpErrors` selects between the strict wrapper
/// (typed errors for HTTP >= 400) and the raw variant used by revalidation.
HttpFetchResult fetchImpl( const std::string &url, const HttpFetchOptions &options,
                           bool throwHttpErrors )
{
  const ResourceUri uri = ResourceUri::parse( url );
  const bool remoteSpelling =
    uri.kind == ResourceKind::RemoteHttp ||
    ( uri.kind == ResourceKind::VsiRemote && !uri.remoteUrl().empty() );
  if ( !remoteSpelling )
  {
    Json::Value details;
    details["url"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "httpFetch: not a remote http(s) resource", details );
  }
  const std::string target =
    uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();

  ensureGdalRegistered();

  // Option strings are built before the char* array: c_str() pointers into a
  // growing container would dangle (same trap probe_remote documented).
  std::vector<std::string> optionStrings;
  optionStrings.reserve( options.headers.size() + 5 );
  optionStrings.push_back( "TIMEOUT=" + std::to_string( options.timeoutSeconds ) );
  optionStrings.push_back( "CONNECTTIMEOUT=" + std::to_string( options.connectTimeoutSeconds ) );
  optionStrings.push_back( "RETRIES=" + std::to_string( options.maxRetries ) );
  optionStrings.push_back(
    "MAX_FILE_SIZE=" + std::to_string( options.maxResponseBytes == 0 ? kDefaultHttpMaxResponseBytes
                                                                : options.maxResponseBytes ) );
  // CPL exposes exactly ONE HEADERS option; individual header lines are
  // carried inside it, separated by CRLF (CPL splits them for curl).
  {
    std::string combined;
    for ( const std::string &header : options.headers )
    {
      if ( header.empty() )
        continue;
      if ( !combined.empty() )
        combined += "\r\n";
      combined += header;
    }
    if ( !options.range.empty() )
    {
      if ( !combined.empty() )
        combined += "\r\n";
      combined += "Range: " + options.range;
    }
    if ( !combined.empty() )
      optionStrings.push_back( "HEADERS=" + combined );
  }
  if ( !options.postBody.empty() )
    optionStrings.push_back( "POSTFIELDS=" + options.postBody );
  std::vector<const char *> optionKeys;
  optionKeys.reserve( optionStrings.size() + 1 );
  for ( const std::string &option : optionStrings )
    optionKeys.push_back( option.c_str() );
  optionKeys.push_back( nullptr );

  // CPL error state is thread-global and this build clears it internally
  // before a caller can read it, so the error text is captured through a
  // scoped handler instead (same thread → thread_local storage).
  CapturingCplErrors capturedErrors;
  capturedErrors.clear();
  CPLHTTPResult *result = CPLHTTPFetch( target.c_str(), optionKeys.data() );
  const std::string capturedText = capturedErrors.lastMessage();
  if ( result == nullptr )
  {
    const std::string &errorText = capturedText;
    if ( containsIgnoreCase( errorText, "timed out" ) )
      throw GeoError( ErrorCode::Timeout, "httpFetch: request exceeded its time budget: " + uri.display() );
    throw GeoError( ErrorCode::NetworkError, "httpFetch: request could not be dispatched: " +
                                               ( errorText.empty() ? uri.display() : errorText.substr( 0, 192 ) ) );
  }

  HttpFetchResult fetch;
  fetch.displayUrl = uri.display();
  fetch.httpStatus = result->nStatus;
  fetch.reachedServer = result->nStatus > 0 || result->pabyData != nullptr ||
                        result->papszHeaders != nullptr;

  // Derive the real HTTP status: this build leaves nStatus = 0 for answers
  // GDAL considers errors (the text "HTTP error code : 404" is the signal)
  // and stuffs curl transport codes (< 100) into nStatus on failures.
  if ( fetch.httpStatus == 0 )
  {
    const std::size_t at = capturedText.find( "HTTP error code : " );
    if ( at != std::string::npos )
    {
      try
      {
        fetch.httpStatus = std::stoi( capturedText.substr( at + std::strlen( "HTTP error code : " ) ) );
      }
      catch ( const std::exception & )
      {
      }
    }
  }

  std::string errorText = result->pszErrBuf != nullptr ? result->pszErrBuf : std::string();
  if ( errorText.empty() )
    errorText = capturedText;
  const bool timedOut = containsIgnoreCase( errorText, "timed out" );
  const bool sizeGuard = containsIgnoreCase( errorText, "file size" );

  if ( result->papszHeaders != nullptr )
  {
    for ( char **header = result->papszHeaders; *header; ++header )
      recordHeader( fetch.headers, *header );
  }
  if ( result->pabyData != nullptr && result->nDataLen > 0 )
  {
    const unsigned char *bytes = result->pabyData;
    fetch.body.assign( bytes, bytes + result->nDataLen );
  }
  const std::uintmax_t budget =
    options.maxResponseBytes == 0 ? kDefaultHttpMaxResponseBytes : options.maxResponseBytes;
  // CPL's MAX_FILE_SIZE enforcement is not reliable for every answer shape
  // in this build (plain 200 answers can slip through) — the budget is also
  // enforced here, at the layer that promises it.
  if ( fetch.body.size() > budget )
    fetch.body.resize( static_cast<std::size_t>( budget ) );
  fetch.truncated = fetch.body.size() >= budget;

  // This build's CPLHTTPFetch can answer a failed transport with a NON-null
  // result carrying a curl code (< 100) in nStatus, no body and no headers.
  // That shape is a typed transport error — never an empty success.
  const bool curlCodeInStatus = fetch.httpStatus != 0 && fetch.httpStatus < 100;
  if ( sizeGuard )
  {
    // A budget abort is not a transport failure: the resource answered (its
    // answer was simply bigger than this caller declared). Headers survive
    // when the origin announced them; the body is partial by design.
    fetch.sizeGuardHit = true;
    fetch.truncated = true;
  }
  else if ( !timedOut &&
       ( curlCodeInStatus ||
         ( fetch.httpStatus == 0 && fetch.body.empty() && fetch.headers.empty() &&
           !capturedText.empty() ) ) )
  {
    CPLHTTPDestroyResult( result );
    throw GeoError( ErrorCode::NetworkError,
                    "httpFetch: transport failure: " + errorText.substr( 0, 192 ) );
  }
  // No error text and literally nothing on the wire: the caller decides
  // what an entity-less answer means (a 304 shape or an empty origin).
  CPLHTTPDestroyResult( result );

  if ( timedOut )
    throw GeoError( ErrorCode::Timeout, "httpFetch: request exceeded its time budget: " + uri.display() );

  if ( fetch.httpStatus == 404 )
    std::fprintf( stderr, "DIAG404 url=%s\n", fetch.displayUrl.c_str() );
  if ( throwHttpErrors )
  {
    if ( fetch.httpStatus == 404 || fetch.httpStatus == 410 )
      throw GeoError( ErrorCode::NotFound, "httpFetch: remote resource is gone: " + uri.display() );
    if ( fetch.httpStatus >= 400 )
    {
      Json::Value details;
      details["status"] = fetch.httpStatus;
      throw GeoError( ErrorCode::NetworkError, "httpFetch: remote origin reported an error: " + uri.display(),
                      details );
    }
  }
  return fetch;
}

} // namespace

std::string HttpFetchResult::headerValue( const std::string &lowerName ) const
{
  const auto it = headers.find( lowerName );
  return it == headers.end() ? std::string() : it->second;
}

Json::Value HttpFetchResult::toJson() const
{
  Json::Value json;
  json["url"] = displayUrl;
  json["http_status"] = httpStatus;
  json["reached_server"] = reachedServer;
  json["body_bytes"] = static_cast<Json::UInt64>( body.size() );
  json["truncated"] = truncated;
  Json::Value headerJson( Json::objectValue );
  for ( const auto &entry : headers )
    headerJson[entry.first] = entry.second;
  json["headers"] = headerJson;
  return json;
}

HttpFetchResult httpFetch( const std::string &url, const HttpFetchOptions &options )
{
  return fetchImpl( url, options, true );
}

HttpFetchResult httpFetchStatus( const std::string &url, const HttpFetchOptions &options )
{
  return fetchImpl( url, options, false );
}

Json::Value httpFetchJson( const std::string &url, const HttpFetchOptions &options )
{
  HttpFetchResult fetch = fetchImpl( url, options, true );
  if ( fetch.truncated )
    throw GeoError( ErrorCode::ResourceExhausted,
                    "httpFetchJson: response exceeded the byte budget: " + fetch.displayUrl );
  if ( !contentTypeMatchesAcceptList( fetch, options.acceptContentTypes ) )
  {
    Json::Value details;
    details["content_type"] = fetch.headerValue( "content-type" );
    throw GeoError( ErrorCode::UnsupportedFormat,
                    "httpFetchJson: origin declared a non-JSON media type: " + fetch.displayUrl, details );
  }
  if ( !declaredContentTypeIsJson( fetch ) )
  {
    Json::Value details;
    details["content_type"] = fetch.headerValue( "content-type" );
    throw GeoError( ErrorCode::UnsupportedFormat,
                    "httpFetchJson: origin declared a non-JSON media type: " + fetch.displayUrl, details );
  }
  const std::string text( fetch.body.begin(), fetch.body.end() );
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  builder[ "collectComments" ] = false;
  std::string parseErrors;
  std::istringstream stream( text );
  if ( !Json::parseFromStream( builder, stream, &parsed, &parseErrors ) )
  {
    Json::Value details;
    details["parse_error"] = parseErrors.substr( 0, 512 );
    throw GeoError( ErrorCode::InvalidMetadata,
                    "httpFetchJson: response is not valid JSON: " + fetch.displayUrl, details );
  }
  return parsed;
}

} // namespace sicnu::geo
