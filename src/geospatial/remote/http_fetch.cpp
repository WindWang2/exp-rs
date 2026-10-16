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
  * A budget abort CPLHTTPFetch records as a hard CPLError(CE_Failure)
    ("Maximum file size exceeded") in the process-global CPL error state,
    which survives the fetch. A handled condition must not leave that state
    dirty: GDALOpenEx's per-driver loop returns null while a non-warning
    error is pending, so the leftover error would abort the very open a
    /vsirangecache/ handle is being served for (GDAL 3.8 lane — see
    fetchImpl's sizeGuard branch).
  * CPLHTTPFetch follows redirects UNCONDITIONALLY in this GDAL (3.12.4
    hardcodes CURLOPT_FOLLOWLOCATION=1; no papszOptions key or config
    option disables it — checked against the pinned GDAL source). A
    first-hop egress check made before httpFetch therefore does NOT cover
    redirect targets; closing that gap needs a transport-level change, not
    an option string here (#1456, open).
 ***************************************************************************/

#include "geospatial/remote/http_fetch.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/util/resource_uri.h"

#include <cpl_conv.h>
#include <cpl_http.h>
#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <mutex>
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

/// Shared URL admission of both transports: remote-spelling check, then the
/// offline gate, then the canonical request target. One choke point, so the
/// guarded path can never drift from the CPL path on what is fetchable.
struct RemoteTarget
{
  ResourceUri uri;
  std::string target;
};

RemoteTarget resolveRemoteTarget( const std::string &url )
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
  if ( offline::enabled() )
    throw GeoError( ErrorCode::NetworkError, offline::refusalMessage( uri.display() ) );
  return RemoteTarget{ uri, uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl() };
}

/// Shared bounded fetch. `throwHttpErrors` selects between the strict wrapper
/// (typed errors for HTTP >= 400) and the raw variant used by revalidation.
HttpFetchResult fetchImpl( const std::string &url, const HttpFetchOptions &options,
                           bool throwHttpErrors )
{
  // Offline gate (goal D7): typed refusal before any request is dispatched.
  // This is the single HTTP choke point of the layer (ADR 0139), so STAC,
  // the remote validator and every future caller are covered here.
  const RemoteTarget remote = resolveRemoteTarget( url );
  const ResourceUri &uri = remote.uri;
  const std::string &target = remote.target;

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
  // Truncation must be decided from the PRE-resize size: clamping first
  // made `body.size() > budget` always false and left `truncated` dead.
  // Exactly-budget bodies are treated as truncated (header contract —
  // conservative, truthful); a guard abort always means a cut answer.
  const bool bodyWasOverBudget = fetch.body.size() >= budget;
  if ( fetch.body.size() > budget )
    fetch.body.resize( static_cast<std::size_t>( budget ) );
  fetch.truncated = fetch.sizeGuardHit || bodyWasOverBudget;

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
    // The budget abort is a HANDLED condition here — but CPLHTTPFetch still
    // recorded it as a hard CPLError(CE_Failure, "Maximum file size
    // exceeded") in the process-global CPL error state, and that state is not
    // ours to leave dirty. GDAL's GDALOpenEx driver loop aborts the whole
    // open as soon as a non-warning error is pending when a driver's Open()
    // returns null ("if (CPLGetLastErrorNo() != 0 && CPLGetLastErrorType() >
    // CE_Warning) return nullptr"): on GDAL 3.8 (ubuntu-latest ships 3.8.4 +
    // libcurl 8.5) the leftover error trips that check inside the very open
    // our /vsirangecache/ handler is serving — the range-ignoring origin
    // whose probe answer exceeds the 1 KiB probe budget makes GDALOpenEx
    // return null with "Maximum file size exceeded" as the last CPL error,
    // and RasterReader::open surfaces it as "Cannot open raster". Verified
    // against GDAL 3.8.4: clearing the state here makes the same open
    // succeed; GDAL 3.13 tolerates the stale error, which is why the suite
    // only ever went red on the 3.8 lane. Clear it where the condition is
    // fully interpreted, so no caller can inherit it.
    CPLErrorReset();
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

// --- redirect-controlling transport (#1456) ------------------------------
//
// CPLHTTPFetch follows redirects unconditionally in the pinned GDAL
// (CURLOPT_FOLLOWLOCATION is hardcoded; no papszOptions key or config option
// disables it), and the caller never sees the intermediate hops — a
// first-hop egress check therefore says nothing about where the transfer
// ends up. When a caller declares a per-hop policy (HttpFetchOptions::
// redirectGuard) the fetch runs here instead: libcurl directly, auto-follow
// OFF, every Location hop resolved locally, policy-checked BEFORE dispatch,
// and bounded by kMaxRedirectHops. Unguarded callers keep the CPL transport
// above, byte for byte.

/// libcurl global state: CPLHTTPFetch initializes libcurl globally already,
/// but this layer must not depend on a GDAL call having happened first.
std::once_flag &curlGlobalOnce()
{
  static std::once_flag flag;
  return flag;
}

struct GuardedTransfer
{
  std::uintmax_t budget = 0;
  std::uintmax_t totalBytes = 0;  ///< every body byte the origin sent
  bool overBudget = false;        ///< the write callback aborted the transfer
  std::vector<unsigned char> body;
};

size_t guardedBodySink( char *ptr, size_t size, size_t nmemb, void *userdata )
{
  GuardedTransfer *transfer = static_cast<GuardedTransfer *>( userdata );
  const std::size_t chunk = size * nmemb;
  transfer->totalBytes += chunk;
  const std::uintmax_t kept = transfer->body.size();
  if ( kept + chunk > transfer->budget )
  {
    // Same contract as CPL MAX_FILE_SIZE: abort the transfer but KEEP the
    // in-budget prefix of this chunk (curl delivers large chunks; dropping
    // the whole chunk would lose bytes CPL's guard preserves), and report
    // it as a budget abort (not a transport failure).
    const std::uintmax_t keep =
      transfer->budget > kept ? transfer->budget - kept : 0;
    const std::size_t prefix = static_cast<std::size_t>(
      keep < chunk ? keep : chunk );
    transfer->body.insert( transfer->body.end(), ptr, ptr + prefix );
    transfer->overBudget = true;
    return 0;
  }
  transfer->body.insert( transfer->body.end(), ptr, ptr + chunk );
  return chunk;
}

/// Header collection state: the map plus the name of the most recently
/// RECEIVED header (an obs-fold continuation appends there — rbegin() would
/// be the alphabetically-largest key, not the latest arrival).
struct GuardedHeaders
{
  std::map<std::string, std::string> map;
  std::string lastName;
};

size_t guardedHeaderSink( char *buffer, size_t size, size_t nmemb, void *userdata )
{
  auto *headers = static_cast<GuardedHeaders *>( userdata );
  std::string line( buffer, size * nmemb );
  while ( !line.empty() && ( line.back() == '\r' || line.back() == '\n' ) )
    line.pop_back();
  if ( line.empty() )
    return size * nmemb;
  if ( line[0] == ' ' || line[0] == '\t' )
  {
    // obs-fold continuation: append to the most recently received header.
    const auto it = headers->map.find( headers->lastName );
    if ( it != headers->map.end() )
    {
      if ( !it->second.empty() )
        it->second += " ";
      it->second += line.substr( 1 );
    }
    return size * nmemb;
  }
  const std::size_t colon = line.find( ':' );
  if ( colon == std::string::npos || colon == 0 )
    return size * nmemb; // status line ("HTTP/1.1 302 Found") or garbage
  std::string name = toLower( line.substr( 0, colon ) );
  std::string value = line.substr( colon + 1 );
  while ( !value.empty() && ( value.front() == ' ' || value.front() == '\t' ) )
    value.erase( value.begin() );
  headers->lastName = name;
  headers->map.emplace( name, value ); // first occurrence wins, as with CPL
  return size * nmemb;
}

/// Scheme of an absolute URL, lowercased; "" when the string carries none.
std::string urlSchemeOf( const std::string &url )
{
  const std::size_t colon = url.find( ':' );
  if ( colon == std::string::npos || colon == 0 )
    return std::string();
  for ( std::size_t i = 0; i < colon; ++i )
  {
    const char c = url[i];
    const bool schemeChar = std::isalnum( static_cast<unsigned char>( c ) ) || c == '+' ||
                            c == '-' || c == '.';
    if ( !schemeChar )
      return std::string();
  }
  return toLower( url.substr( 0, colon ) );
}

/// Authority ([userinfo@]host[:port]) of an absolute URL; "" when absent.
std::string urlAuthorityOf( const std::string &url )
{
  const std::size_t authorityAt = url.find( "://" );
  if ( authorityAt == std::string::npos )
    return std::string();
  const std::size_t authorityEnd = url.find_first_of( "/?#", authorityAt + 3 );
  return url.substr( authorityAt + 3, authorityEnd == std::string::npos
                                       ? std::string::npos
                                       : authorityEnd - authorityAt - 3 );
}

/// Bare host of an absolute URL: authority minus userinfo, brackets and port.
std::string urlHostOf( const std::string &url )
{
  std::string authority = urlAuthorityOf( url );
  const std::size_t at = authority.rfind( '@' );
  if ( at != std::string::npos )
    authority = authority.substr( at + 1 );
  if ( !authority.empty() && authority.front() == '[' )
  {
    const std::size_t close = authority.find( ']' );
    return ( close == std::string::npos ) ? authority.substr( 1 )
                                          : authority.substr( 1, close - 1 );
  }
  const std::size_t colon = authority.rfind( ':' );
  return ( colon == std::string::npos ) ? authority : authority.substr( 0, colon );
}

/// Port the transport will actually connect to: the explicit authority port,
/// else the scheme default. CURLOPT_RESOLVE entries are keyed by host+port,
/// so a mismatched port would be silently ignored by libcurl (#1485).
int urlPortOf( const std::string &url )
{
  const int fallback = ( urlSchemeOf( url ) == "https" ) ? 443 : 80;
  std::string authority = urlAuthorityOf( url );
  const std::size_t at = authority.rfind( '@' );
  if ( at != std::string::npos )
    authority = authority.substr( at + 1 );
  std::size_t colon = std::string::npos;
  if ( !authority.empty() && authority.front() == '[' )
  {
    const std::size_t close = authority.find( ']' );
    if ( close == std::string::npos )
      return fallback;
    colon = authority.find( ':', close );
  }
  else
  {
    colon = authority.rfind( ':' );
  }
  if ( colon == std::string::npos )
    return fallback;
  const std::string digits = authority.substr( colon + 1 );
  if ( digits.empty() || digits.find_first_not_of( "0123456789" ) != std::string::npos )
    return fallback;
  const long value = std::strtol( digits.c_str(), nullptr, 10 );
  if ( value <= 0 || value > 65535 )
    return fallback;
  return static_cast<int>( value );
}

/// RFC 3986 §5-style resolution of a Location reference against the URL the
/// response came from — absolute, network-path ("//host/…"), absolute-path
/// and same-directory relative forms, with a LEXICALLY bounded "./" ".."
/// normalization (no filesystem access; the hop still faces the guard and
/// the http(s) admission below).
std::string resolveRedirectTarget( const std::string &base, const std::string &location )
{
  const std::string trimmed = location.substr( 0, location.find( '#' ) );
  if ( trimmed.empty() )
    return base;
  if ( !urlSchemeOf( trimmed ).empty() )
    return trimmed; // absolute
  const std::string scheme = urlSchemeOf( base );
  const std::size_t authorityAt = base.find( "://" );
  if ( authorityAt == std::string::npos )
    return trimmed; // defensive: malformed base, let admission refuse it
  const std::size_t authorityEnd = base.find_first_of( "/?#", authorityAt + 3 );
  const std::string authority =
    base.substr( authorityAt + 3, authorityEnd == std::string::npos ? std::string::npos
                                                                      : authorityEnd - authorityAt - 3 );
  if ( !trimmed.empty() && trimmed.compare( 0, 2, "//" ) == 0 )
    return scheme + ":" + trimmed; // network-path
  const std::size_t baseQuery = base.find_first_of( "?#" );
  const std::string basePath =
    base.substr( authorityEnd == std::string::npos ? base.size() : authorityEnd,
                 baseQuery == std::string::npos ? std::string::npos : baseQuery - ( authorityEnd == std::string::npos ? base.size() : authorityEnd ) );
  // The reference splits into path and query BEFORE resolution: RFC 3986
  // §5.2.4 removes dot segments from the PATH only, and a query carrying
  // '/'-or-'.' text (signed-URL signatures do) must never be rewritten.
  const std::size_t locQueryAt = trimmed.find( '?' );
  const std::string locPath = locQueryAt == std::string::npos ? trimmed : trimmed.substr( 0, locQueryAt );
  const std::string locQuery = locQueryAt == std::string::npos ? std::string() : trimmed.substr( locQueryAt );
  std::string path;
  if ( !locPath.empty() && locPath[0] == '/' )
    path = locPath; // absolute-path
  else if ( locPath.empty() && !locQuery.empty() )
    return scheme + "://" + authority + ( basePath.empty() ? "/" : basePath ) + locQuery;
  else
  {
    // same-directory relative: everything after the last '/'
    const std::size_t lastSlash = basePath.rfind( '/' );
    path = basePath.substr( 0, lastSlash == std::string::npos ? 0 : lastSlash + 1 ) + locPath;
  }
  // Lexical dot-segment removal (RFC 3986 §5.2.4), bounded.
  std::vector<std::string> segments;
  std::size_t cursor = 0;
  int guard = 0;
  while ( cursor <= path.size() && guard++ < 512 )
  {
    std::size_t next = path.find( '/', cursor );
    if ( next == std::string::npos )
      next = path.size();
    const std::string segment = path.substr( cursor, next - cursor );
    if ( segment == ".." )
    {
      if ( !segments.empty() )
        segments.pop_back();
    }
    else if ( segment != "." )
      segments.push_back( segment );
    if ( next == path.size() )
      break;
    cursor = next + 1;
  }
  std::string normalized;
  for ( const std::string &segment : segments )
    normalized += ( normalized.empty() ? "" : "/" ) + segment;
  if ( normalized.empty() || normalized[0] != '/' )
    normalized = "/" + normalized;
  return scheme + "://" + authority + normalized + locQuery;
}

/// The redirect-controlling transport. Mirrors fetchImpl's contract (typed
/// errors, budgets, truncation semantics, display URL) hop for hop.
HttpFetchResult guardedFetchImpl( const std::string &url, const HttpFetchOptions &options,
                                  bool throwHttpErrors )
{
  const RemoteTarget remote = resolveRemoteTarget( url );
  const ResourceUri &uri = remote.uri;

  std::call_once( curlGlobalOnce(), [] { curl_global_init( CURL_GLOBAL_DEFAULT ); } );

  const std::uintmax_t budget =
    options.maxResponseBytes == 0 ? kDefaultHttpMaxResponseBytes : options.maxResponseBytes;
  std::string current = remote.target;
  std::string currentAuthority = urlAuthorityOf( current );
  std::vector<std::string> hopChain;
  // Method semantics mirror libcurl's auto-follow: 303 (and the historical
  // 301/302 after a body-carrying request) degrades to GET; 307/308 replay
  // the method and the body verbatim. The body travels as per-hop state so
  // no hop ever re-sends what a redirect downgraded away.
  std::string postBodyThisHop = options.postBody;
  // The wall-clock budget is WHOLE-REQUEST (the header contract of the CPL
  // TIMEOUT): every hop and every transient retry draws from one deadline
  // instead of re-arming a full budget per dispatch.
  const auto deadline =
    std::chrono::steady_clock::now() +
    std::chrono::seconds( options.timeoutSeconds > 0 ? options.timeoutSeconds : 0 );
  for ( int hop = 0;; ++hop )
  {
    if ( offline::enabled() )
      throw GeoError( ErrorCode::NetworkError, offline::refusalMessage( current ) );

    // Policy sees EVERY hop — including the first, so the predicate is the
    // single source of truth (the caller's own first-hop check, when it has
    // one, fires earlier with the same verdict).
    const std::string refusal = options.redirectGuard ? options.redirectGuard( current ) : std::string();
    if ( !refusal.empty() )
    {
      Json::Value details;
      details["url"] = uri.display();
      details["hop"] = hop;
      Json::Value chain( Json::arrayValue );
      for ( const std::string &hopUrl : hopChain )
        chain.append( hopUrl );
      details["hop_chain"] = chain;
      std::ostringstream message;
      message << "httpFetch: egress policy refused the request";
      if ( hop > 0 )
        message << " at redirect hop " << hop;
      message << ": " << refusal;
      throw GeoError( ErrorCode::PermissionDenied, message.str(), details );
    }
    // Transport-level admission independent of policy: this layer speaks
    // http(s) only; a Location naming another scheme is refused even when a
    // caller-supplied guard would allow it.
    const std::string scheme = urlSchemeOf( current );
    if ( scheme != "http" && scheme != "https" )
      throw GeoError( ErrorCode::NetworkError,
                      "httpFetch: redirect target scheme must be http or https (got '" +
                        ( scheme.empty() ? std::string( "none" ) : scheme ) + "')" );

    CURL *handle = curl_easy_init();
    if ( handle == nullptr )
      throw GeoError( ErrorCode::NetworkError, "httpFetch: transport could not be initialized" );
    GuardedTransfer transfer;
    transfer.budget = budget;
    GuardedHeaders headers;
    struct curl_slist *requestHeaders = nullptr;
    // libcurl's auto-follow strips Authorization on cross-host redirects;
    // the manual hop loop must do the same or a redirected request would
    // hand the caller's bearer token to whatever host the origin names.
    // Compared against the PREVIOUS dispatch's authority, hop by hop.
    const std::string hopAuthority = urlAuthorityOf( current );
    const bool dropAuthorization = !currentAuthority.empty() && hopAuthority != currentAuthority;
    currentAuthority = hopAuthority;
    for ( const std::string &header : options.headers )
    {
      if ( header.empty() )
        continue;
      if ( dropAuthorization )
      {
        const std::size_t colon = header.find( ':' );
        if ( colon != std::string::npos &&
             toLower( header.substr( 0, colon ) ) == "authorization" )
          continue;
      }
      requestHeaders = curl_slist_append( requestHeaders, header.c_str() );
    }
    if ( !options.range.empty() )
      requestHeaders =
        curl_slist_append( requestHeaders, ( "Range: " + options.range ).c_str() );
    curl_easy_setopt( handle, CURLOPT_URL, current.c_str() );
    curl_easy_setopt( handle, CURLOPT_FOLLOWLOCATION, 0L );
    // #1485: pin this hop to the addresses the egress policy just accepted.
    // Without it libcurl performs its OWN resolution at connect time, so a
    // short-TTL record (or an attacker-controlled resolver) can answer the
    // policy check with a public address and the connect with
    // 169.254.169.254 — the TOCTOU window between isPrivateHost() and the
    // socket. Resolution failure fails CLOSED: a host that cannot be proven
    // public is not fetched. An IP literal target has nothing to resolve.
    struct curl_slist *resolveEntries = nullptr;
    if ( options.pinResolvedAddresses )
    {
      std::vector<std::string> pins;
      std::string pinError;
      const auto pinStatus =
        offline::resolveForPinning( urlHostOf( current ), urlPortOf( current ), pins, pinError );
      if ( pinStatus == offline::HostPinStatus::PrivateAddress )
      {
        curl_slist_free_all( requestHeaders );
        curl_easy_cleanup( handle );
        throw GeoError( ErrorCode::PermissionDenied,
                        "httpFetch: egress policy refused the request: " + pinError );
      }
      if ( pinStatus == offline::HostPinStatus::Unresolved )
      {
        curl_slist_free_all( requestHeaders );
        curl_easy_cleanup( handle );
        throw GeoError( ErrorCode::NetworkError, "httpFetch: " + pinError );
      }
      for ( const std::string &entry : pins )
        resolveEntries = curl_slist_append( resolveEntries, entry.c_str() );
      if ( resolveEntries != nullptr )
        curl_easy_setopt( handle, CURLOPT_RESOLVE, resolveEntries );
    }
    curl_easy_setopt( handle, CURLOPT_NOSIGNAL, 1L );
    curl_easy_setopt( handle, CURLOPT_USERAGENT, "SICNU-Geo-HTTP/7.0" );
    curl_easy_setopt( handle, CURLOPT_HTTPHEADER, requestHeaders );
    // Same proxy/CA posture as the CPL transport this guards against
    // divergence from: CPLHTTPFetch honors the GDAL_HTTP_* options the
    // application configures globally, and an egress-guarded fetch that
    // silently bypassed the (often enforcement-point) proxy would be the
    // opposite of hardening.
    if ( const char *proxy = CPLGetConfigOption( "GDAL_HTTP_PROXY", nullptr );
         proxy != nullptr && *proxy != '\0' )
      curl_easy_setopt( handle, CURLOPT_PROXY, proxy );
    if ( const char *proxyAuth = CPLGetConfigOption( "GDAL_HTTP_PROXYUSERPWD", nullptr );
         proxyAuth != nullptr && *proxyAuth != '\0' )
      curl_easy_setopt( handle, CURLOPT_PROXYUSERPWD, proxyAuth );
    if ( const char *caInfo = CPLGetConfigOption( "GDAL_HTTP_CAINFO", nullptr );
         caInfo != nullptr && *caInfo != '\0' )
      curl_easy_setopt( handle, CURLOPT_CAINFO, caInfo );
    if ( const char *caPath = CPLGetConfigOption( "GDAL_HTTP_CAPATH", nullptr );
         caPath != nullptr && *caPath != '\0' )
      curl_easy_setopt( handle, CURLOPT_CAPATH, caPath );
    if ( options.timeoutSeconds > 0 )
    {
      const long remaining = static_cast<long>(
        std::chrono::duration_cast<std::chrono::seconds>( deadline -
                                                          std::chrono::steady_clock::now() )
          .count() );
      if ( remaining <= 0 )
      {
        curl_slist_free_all( requestHeaders );
        curl_easy_cleanup( handle );
        curl_slist_free_all( resolveList );
        throw GeoError( ErrorCode::Timeout,
                        "httpFetch: request exceeded its time budget: " + uri.display() );
      }
      curl_easy_setopt( handle, CURLOPT_TIMEOUT,
                        remaining < static_cast<long>( options.timeoutSeconds )
                          ? remaining
                          : static_cast<long>( options.timeoutSeconds ) );
    }
    if ( options.connectTimeoutSeconds > 0 )
      curl_easy_setopt( handle, CURLOPT_CONNECTTIMEOUT,
                        static_cast<long>( options.connectTimeoutSeconds ) );
    if ( !postBodyThisHop.empty() )
    {
      curl_easy_setopt( handle, CURLOPT_POSTFIELDSIZE,
                        static_cast<long>( postBodyThisHop.size() ) );
      curl_easy_setopt( handle, CURLOPT_COPYPOSTFIELDS, postBodyThisHop.c_str() );
    }
    curl_easy_setopt( handle, CURLOPT_WRITEFUNCTION, &guardedBodySink );
    curl_easy_setopt( handle, CURLOPT_WRITEDATA, &transfer );
    curl_easy_setopt( handle, CURLOPT_HEADERFUNCTION, &guardedHeaderSink );
    curl_easy_setopt( handle, CURLOPT_HEADERDATA, &headers );

    CURLcode code = curl_easy_perform( handle );
    // Bounded retry of TRANSIENT transport failures only (resolve/connect/
    // recv). A timed-out request already consumed the caller's whole
    // wall-clock budget — retrying it would silently double the budget, so
    // timeouts are final here.
    int attemptsLeft = options.maxRetries > 0 ? options.maxRetries : 0;
    bool deadlineExhausted = false;
    while ( code != CURLE_OK && attemptsLeft > 0 && !transfer.overBudget &&
            ( code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT ||
              code == CURLE_RECV_ERROR || code == CURLE_SEND_ERROR ) )
    {
      // A retry draws from the SAME whole-request deadline: clamping each
      // attempt to the remaining time (and stopping once none is left)
      // keeps the tail attempt from outliving the contract.
      if ( options.timeoutSeconds > 0 )
      {
        const long retryRemaining = static_cast<long>(
          std::chrono::duration_cast<std::chrono::seconds>( deadline -
                                                            std::chrono::steady_clock::now() )
            .count() );
        if ( retryRemaining <= 0 )
        {
          deadlineExhausted = true;
          break;
        }
        curl_easy_setopt( handle, CURLOPT_TIMEOUT,
                          retryRemaining < static_cast<long>( options.timeoutSeconds )
                            ? retryRemaining
                            : static_cast<long>( options.timeoutSeconds ) );
      }
      --attemptsLeft;
      transfer = GuardedTransfer{};
      transfer.budget = budget;
      headers = GuardedHeaders{}; // fresh map AND fresh last-name for the fold
      code = curl_easy_perform( handle );
    }
    long status = 0;
    curl_easy_getinfo( handle, CURLINFO_RESPONSE_CODE, &status );
    std::string location;
    const auto locationIt = headers.map.find( "location" );
    if ( locationIt != headers.map.end() )
      location = locationIt->second;
    curl_slist_free_all( requestHeaders );
    curl_slist_free_all( resolveEntries );
    curl_easy_cleanup( handle );
    curl_slist_free_all( resolveList );

    if ( transfer.overBudget )
    {
      HttpFetchResult fetch;
      fetch.displayUrl = uri.display();
      fetch.httpStatus = static_cast<int>( status );
      fetch.reachedServer = true; // an answer was streaming when the budget hit
      fetch.headers = std::move( headers.map );
      fetch.body = std::move( transfer.body );
      fetch.sizeGuardHit = true;
      fetch.truncated = true;
      return fetch; // same HANDLED-condition doctrine as the CPL size guard
    }
    if ( code == CURLE_OPERATION_TIMEDOUT || deadlineExhausted )
      throw GeoError( ErrorCode::Timeout,
                      "httpFetch: request exceeded its time budget: " + uri.display() );
    if ( code != CURLE_OK )
      throw GeoError( ErrorCode::NetworkError,
                      std::string( "httpFetch: transport failure: " ) + curl_easy_strerror( code ) );

    const bool redirectStatus =
      status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if ( redirectStatus && !location.empty() )
    {
      if ( hop >= kMaxRedirectHops )
      {
        Json::Value details;
        details["hops"] = hop + 1;
        throw GeoError( ErrorCode::NetworkError,
                        "httpFetch: origin exceeded the redirect budget (" +
                          std::to_string( kMaxRedirectHops ) + " hops)" );
      }
      current = resolveRedirectTarget( current, location );
      hopChain.push_back( current );
      if ( status == 303 || ( ( status == 301 || status == 302 ) && !postBodyThisHop.empty() ) )
        postBodyThisHop.clear();
      continue;
    }
    HttpFetchResult fetch;
    fetch.displayUrl = uri.display();
    fetch.httpStatus = static_cast<int>( status );
    fetch.reachedServer = status > 0 || !headers.map.empty() || !transfer.body.empty();
    fetch.headers = std::move( headers.map );
    fetch.body = std::move( transfer.body );
    // Exactly-budget bodies are reported truncated (header contract,
    // conservative and truthful — same as the CPL path).
    fetch.truncated = transfer.totalBytes >= budget && budget > 0;
    if ( throwHttpErrors )
    {
      if ( fetch.httpStatus == 404 || fetch.httpStatus == 410 )
        throw GeoError( ErrorCode::NotFound,
                        "httpFetch: remote resource is gone: " + uri.display() );
      if ( fetch.httpStatus >= 400 )
      {
        Json::Value details;
        details["status"] = fetch.httpStatus;
        throw GeoError( ErrorCode::NetworkError,
                        "httpFetch: remote origin reported an error: " + uri.display(), details );
      }
    }
    return fetch;
  }
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
  if ( options.redirectGuard )
    return guardedFetchImpl( url, options, true );
  return fetchImpl( url, options, true );
}

HttpFetchResult httpFetchStatus( const std::string &url, const HttpFetchOptions &options )
{
  if ( options.redirectGuard )
    return guardedFetchImpl( url, options, false );
  return fetchImpl( url, options, false );
}

Json::Value httpFetchJson( const std::string &url, const HttpFetchOptions &options )
{
  HttpFetchResult fetch = options.redirectGuard ? guardedFetchImpl( url, options, true )
                                                : fetchImpl( url, options, true );
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
  // #1155: the body is remote-origin (STAC/HTTP) content — a hostile or
  // broken origin can nest deeply enough to overflow the reader stack
  // (SIGSEGV) instead of failing cleanly. STAC documents are shallow; bound
  // the recursion and catch jsoncpp's limit exception as a parse failure.
  builder[ "stackLimit" ] = 64;
  std::string parseErrors;
  std::istringstream stream( text );
  bool parsedOk = false;
  try
  {
    parsedOk = Json::parseFromStream( builder, stream, &parsed, &parseErrors );
  }
  catch ( const Json::Exception & )
  {
    parsedOk = false;
  }
  if ( !parsedOk )
  {
    Json::Value details;
    details["parse_error"] = parseErrors.substr( 0, 512 );
    throw GeoError( ErrorCode::InvalidMetadata,
                    "httpFetchJson: response is not valid JSON: " + fetch.displayUrl, details );
  }
  return parsed;
}

} // namespace sicnu::geo
