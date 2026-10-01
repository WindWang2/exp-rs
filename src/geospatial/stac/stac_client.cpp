/***************************************************************************
  geospatial/stac/stac_client.cpp — STAC API client implementation.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/stac/stac_client.h"

#include "geospatial/util/sha256.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

#include "geospatial/util/resource_uri.h"
#include "geospatial/util/time_normalization.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include "platform/portable.h"

namespace sicnu::geo
{

namespace
{

std::string trimSlash( std::string url )
{
  while ( !url.empty() && url.back() == '/' )
    url.pop_back();
  return url;
}

std::string urlEncodeValue( const std::string &value )
{
  static const char *hexDigits = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve( value.size() );
  for ( const unsigned char c : value )
  {
    const bool unreserved = std::isalnum( c ) != 0 || c == '-' || c == '_' || c == '.' || c == '~';
    if ( unreserved )
      encoded += static_cast<char>( c );
    else
    {
      encoded += '%';
      encoded += hexDigits[c >> 4];
      encoded += hexDigits[c & 0xF];
    }
  }
  return encoded;
}

std::string joinEncoded( const std::vector<std::string> &values )
{
  std::string joined;
  for ( const std::string &value : values )
  {
    if ( !joined.empty() )
      joined += ",";
    joined += value;
  }
  return joined;
}

/// Appends "name=value" (both encoded) when value is non-empty.
void appendParam( std::string &query, const char *name, const std::string &value )
{
  if ( value.empty() )
    return;
  if ( !query.empty() )
    query += "&";
  query += name;
  query += "=";
  query += urlEncodeValue( value );
}

/// Extracts the rel="next" link from a STAC response document.
bool extractNextLink( const Json::Value &document, std::string &method, std::string &href,
                      Json::Value &body, bool &merge )
{
  if ( !document.isObject() || !document.isMember( "links" ) || !document["links"].isArray() )
    return false;
  for ( const Json::Value &link : document["links"] )
  {
    if ( !link.isObject() )
      continue;
    const std::string rel = link["rel"].isString() ? link["rel"].asString() : std::string();
    if ( rel != "next" )
      continue;
    href = link["href"].isString() ? link["href"].asString() : std::string();
    if ( href.empty() )
      continue;
    method = link["method"].isString() ? link["method"].asString() : "GET";
    if ( method != "POST" && method != "GET" )
      method = "GET";
    if ( link.isMember( "body" ) && link["body"].isObject() )
      body = link["body"];
    // "merge": the body is a DELTA over the original request body (STAC API
    // spec) — the caller merges before sending. Absent/false replaces it.
    // Non-bool merge is inert (asBool throws Json::LogicError on strings and
    // objects, and a hostile link must not abort the walk).
    merge = link.isMember( "merge" ) && link["merge"].isBool() && link["merge"].asBool();
    return true;
  }
  return false;
}

/// Resolves an RFC 8288 reference against the URL it was served from —
/// spec-legal servers emit relative next links ("../search?page=2").
std::string resolveReference( const std::string &baseUrl, const std::string &reference )
{
  const ResourceUri base = ResourceUri::parse( baseUrl );
  if ( base.kind != ResourceKind::RemoteHttp )
    return reference;
  const ResourceUri ref = ResourceUri::parse( reference );
  if ( ref.kind == ResourceKind::RemoteHttp )
    return reference; // already absolute
  // Strip the reference's fragment; keep its query for same-path refs.
  std::string path = reference;
  std::string query;
  const std::size_t hash = path.find( '#' );
  if ( hash != std::string::npos )
    path = path.substr( 0, hash );
  const std::size_t q = path.find( '?' );
  if ( q != std::string::npos )
  {
    query = path.substr( q );
    path = path.substr( 0, q );
  }
  std::string basePath = base.path;
  const std::size_t lastSlash = basePath.rfind( '/' );
  std::string directory = lastSlash == std::string::npos ? std::string() : basePath.substr( 0, lastSlash );
  // Explicit current-directory spellings contribute nothing.
  while ( path.rfind( "./", 0 ) == 0 )
    path = path.substr( 2 );
  if ( path == "." )
    path.clear();
  while ( path.rfind( "../", 0 ) == 0 )
  {
    path = path.substr( 3 );
    const std::size_t up = directory.rfind( '/' );
    directory = up == std::string::npos ? std::string() : directory.substr( 0, up );
  }
  // RFC 3986 §5.3 merge: a root-absolute reference replaces the base path
  // outright; otherwise it joins the base directory (never a doubled slash —
  // a root-relative self link merged against a bare origin used to yield
  // "host//path" and poison everything resolved from it).
  std::string mergedPath;
  if ( !path.empty() && path[0] == '/' )
  {
    mergedPath = path;
  }
  else
  {
    mergedPath = directory;
    if ( !path.empty() )
    {
      if ( !mergedPath.empty() && mergedPath.back() != '/' )
        mergedPath += '/';
      mergedPath += path;
    }
  }
  std::string resolved = base.scheme + "://" + base.host;
  // userinfo is deliberately DROPPED on relative resolution: resolve only
  // from credential-free bases (display stays redacted either way).
  resolved += mergedPath + query;
  return resolved;
}

bool containsIgnoreCase( const std::string &haystack, const std::string &needle )
{
  if ( haystack.size() < needle.size() )
    return false;
  for ( std::size_t i = 0; i + needle.size() <= haystack.size(); ++i )
  {
    bool matched = true;
    for ( std::size_t j = 0; j < needle.size(); ++j )
    {
      if ( std::tolower( static_cast<unsigned char>( haystack[i + j] ) ) !=
           std::tolower( static_cast<unsigned char>( needle[j] ) ) )
      {
        matched = false;
        break;
      }
    }
    if ( matched )
      return true;
  }
  return false;
}

bool endsWithIgnoreCase( const std::string &haystack, const std::string &suffix )
{
  if ( haystack.size() < suffix.size() )
    return false;
  return std::equal( suffix.begin(), suffix.end(), haystack.end() - suffix.size(),
                     [] ( char a, char b ) {
                       return std::tolower( static_cast<unsigned char>( a ) ) ==
                              std::tolower( static_cast<unsigned char>( b ) );
                     } );
}

/// SICNU_* boolean env flag with the repo's shared semantics
/// ("1"/"true"/"yes"/"on", case-insensitive, trimmed) — the Qt-free twin of
/// src/agent/env_flag.h so the policy cannot drift between surfaces.
bool sicnuEnvFlagEnabled( const char *name )
{
  const char *raw = std::getenv( name );
  if ( raw == nullptr )
    return false;
  std::string value;
  for ( const char *c = raw; *c; ++c )
  {
    if ( *c == ' ' || *c == '\t' )
      continue;
    value += static_cast<char>( std::tolower( static_cast<unsigned char>( *c ) ) );
  }
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

/// Strict dotted-quad IPv4 parse (no short forms — safer than inet_aton).
bool parseIpv4( const std::string &text, std::uint32_t &out )
{
  std::uint32_t value = 0;
  int octets = 0;
  std::size_t i = 0;
  while ( i < text.size() )
  {
    if ( octets == 4 )
      return false;
    std::uint32_t octet = 0;
    int digits = 0;
    while ( i < text.size() && std::isdigit( static_cast<unsigned char>( text[i] ) ) )
    {
      octet = octet * 10 + static_cast<unsigned char>( text[i] - '0' );
      if ( octet > 255 )
        return false;
      ++i;
      ++digits;
    }
    if ( digits == 0 )
      return false;
    value = ( value << 8 ) | octet;
    ++octets;
    if ( i < text.size() )
    {
      if ( text[i] != '.' )
        return false;
      ++i;
    }
  }
  if ( octets != 4 )
    return false;
  out = value;
  return true;
}

/// IPv6 literal parse (one optional "::" compression, optional embedded
/// IPv4 tail "::ffff:192.168.0.1") into 16 network-order bytes.
bool parseIpv6( const std::string &text, unsigned char out[16] )
{
  const std::size_t cc = text.find( "::" );
  if ( cc != std::string::npos && text.find( "::", cc + 2 ) != std::string::npos )
    return false; // at most one compression
  const std::string head = cc == std::string::npos ? text : text.substr( 0, cc );
  const std::string tail = cc == std::string::npos ? std::string() : text.substr( cc + 2 );

  std::uint16_t groups[8] = {};
  int headCount = 0;
  int tailCount = 0;

  // Parses one colon-separated section into @p groups_ from @p offset.
  // The tail section may end in a dotted-quad (two groups).
  auto parseSection = [ & ] ( const std::string &section, std::uint16_t *groups_, int &count,
                              bool allowV4Tail ) {
    if ( section.empty() )
      return true;
    std::size_t pos = 0;
    while ( true )
    {
      if ( count >= 8 )
        return false;
      const std::size_t colon = section.find( ':', pos );
      const std::string token = colon == std::string::npos ? section.substr( pos )
                                                           : section.substr( pos, colon - pos );
      if ( allowV4Tail && colon == std::string::npos && token.find( '.' ) != std::string::npos )
      {
        std::uint32_t v4 = 0;
        if ( !parseIpv4( token, v4 ) )
          return false;
        groups_[count++] = static_cast<std::uint16_t>( v4 >> 16 );
        if ( count >= 8 )
          return false;
        groups_[count++] = static_cast<std::uint16_t>( v4 & 0xFFFFu );
        return true;
      }
      if ( token.empty() || token.size() > 4 )
        return false;
      std::uint32_t group = 0;
      for ( const char c : token )
      {
        if ( !std::isxdigit( static_cast<unsigned char>( c ) ) )
          return false;
        const std::uint32_t digit = c <= '9' ? static_cast<std::uint32_t>( c - '0' )
                                             : static_cast<std::uint32_t>( ( c | 0x20 ) - 'a' + 10 );
        group = group * 16 + digit;
      }
      groups_[count++] = static_cast<std::uint16_t>( group );
      if ( colon == std::string::npos )
        return true;
      pos = colon + 1;
    }
  };

  if ( !parseSection( head, groups, headCount, false ) )
    return false;
  if ( !parseSection( tail, groups + headCount, tailCount, true ) )
    return false;

  const int total = headCount + tailCount;
  if ( cc == std::string::npos )
  {
    if ( total != 8 )
      return false; // full form must hold exactly 8 groups
  }
  else if ( total > 7 )
  {
    return false; // "::" must replace at least one group
  }
  else
  {
    // Move the tail groups to the END: the compressed run fills the middle.
    for ( int i = 0; i < tailCount; ++i )
    {
      groups[7 - i] = groups[headCount + tailCount - 1 - i];
      groups[headCount + tailCount - 1 - i] = 0;
    }
  }

  for ( int i = 0; i < 8; ++i )
  {
    out[i * 2] = static_cast<unsigned char>( groups[i] >> 8 );
    out[i * 2 + 1] = static_cast<unsigned char>( groups[i] & 0xFF );
  }
  return true;
}

/// The SSRF private-host verdict (migrated verbatim from the UI client's
/// QHostAddress-based check, now Qt-free): loopback, RFC 1918/CGNAT,
/// link-local, unique-local and 0.0.0.0(/8) are refused. Non-literal names
/// pass (DNS rebinding residual risk accepted, as before).
bool isPrivateOrLocalHost( std::string host )
{
  if ( host.empty() )
    return true;

  // Strip brackets and port (ResourceUri keeps the authority verbatim).
  if ( !host.empty() && host.front() == '[' )
  {
    const std::size_t close = host.find( ']' );
    host = close == std::string::npos ? host.substr( 1 ) : host.substr( 1, close - 1 );
  }
  else if ( host.find( ':' ) != std::string::npos && host.find( ':' ) == host.rfind( ':' ) )
  {
    host = host.substr( 0, host.find( ':' ) ); // host:port
  }

  std::string lowered;
  lowered.reserve( host.size() );
  for ( const char c : host )
    lowered += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
  if ( lowered == "localhost" || lowered.size() > 10 && lowered.compare( lowered.size() - 10, 10, ".localhost" ) == 0 )
    return true;
  if ( lowered == "metadata.google.internal" )
    return true;

  std::uint32_t v4 = 0;
  if ( parseIpv4( host, v4 ) )
  {
    // 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16 link-local,
    // 127.0.0.0/8 loopback, 0.0.0.0/8, 100.64.0.0/10 CGNAT.
    if ( ( v4 & 0xFF000000u ) == 0x0A000000u )
      return true;
    if ( ( v4 & 0xFFF00000u ) == 0xAC100000u )
      return true;
    if ( ( v4 & 0xFFFF0000u ) == 0xC0A80000u )
      return true;
    if ( ( v4 & 0xFFFF0000u ) == 0xA9FE0000u )
      return true;
    if ( ( v4 & 0xFF000000u ) == 0x7F000000u )
      return true;
    if ( ( v4 & 0xFF000000u ) == 0x00000000u )
      return true;
    if ( ( v4 & 0xFFC00000u ) == 0x64400000u )
      return true;
    return false;
  }

  unsigned char v6[16] = {};
  if ( parseIpv6( host, v6 ) )
  {
    // Loopback ::1
    bool loopback = true;
    for ( int i = 0; i < 15; ++i )
      loopback = loopback && v6[i] == 0;
    loopback = loopback && v6[15] == 1;
    if ( loopback )
      return true;
    // Link-local fe80::/10
    if ( v6[0] == 0xFE && ( v6[1] & 0xC0 ) == 0x80 )
      return true;
    // Unique local fc00::/7
    if ( ( v6[0] & 0xFE ) == 0xFC )
      return true;
    return false;
  }

  // Not a literal IP — allow by name (DNS rebinding residual risk accepted
  // for MVP; hostname "localhost" already handled above).
  return false;
}

} // namespace

void StacSearchQuery::validate() const
{
  if ( !bbox.empty() && bbox.size() != 4 && bbox.size() != 6 )
  {
    Json::Value details;
    details["size"] = static_cast<Json::UInt64>( bbox.size() );
    throw GeoError( ErrorCode::InvalidArgument, "StacSearchQuery: bbox must hold 4 or 6 values", details );
  }
  if ( !intersects.isNull() && !intersects.isObject() )
    throw GeoError( ErrorCode::InvalidArgument, "StacSearchQuery: intersects must be a GeoJSON object" );
}

StacClient::StacClient( std::string root, const StacClientOptions &options )
  : mRoot( trimSlash( std::move( root ) ) ), mOptions( options )
{
  const ResourceUri uri = ResourceUri::parse( mRoot );
  if ( uri.kind != ResourceKind::RemoteHttp )
  {
    Json::Value details;
    details["root"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: root must be a remote http(s) URL", details );
  }
  assertEgressAllowed( mRoot );
}

// --- shared href/egress safety (single home — see stac_client.h) ----------

void StacClient::assertEgressAllowed( const std::string &url ) const
{
  if ( !mOptions.blockPrivateNetworks )
    return;
  const std::string error = egressPolicyError( url );
  if ( error.empty() )
    return;
  Json::Value details;
  details["url"] = ResourceUri::parse( url ).display();
  throw GeoError( ErrorCode::PermissionDenied, error, details );
}

std::string StacClient::egressPolicyError( const std::string &url )
{
  const ResourceUri uri = ResourceUri::parse( url );
  if ( uri.kind != ResourceKind::RemoteHttp )
  {
    // Distinguish an unusable URL from a deliberate non-http scheme so the
    // refusal texts stay stable for the UI adapter (historical strings).
    const std::size_t schemeEnd = url.find( "://" );
    if ( schemeEnd == std::string::npos || schemeEnd == 0 )
      return "Invalid URL";
    std::string scheme = url.substr( 0, schemeEnd );
    for ( char &c : scheme )
      c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    if ( scheme == "http" || scheme == "https" )
      return "Invalid URL"; // http(s) spelling ResourceUri rejected (no host, ...)
    return "URL scheme must be http or https";
  }
  if ( !sicnuEnvFlagEnabled( "SICNU_STAC_ALLOW_PRIVATE" ) && isPrivateOrLocalHost( uri.host ) )
    return "Private / loopback / link-local STAC hosts are blocked "
           "(set SICNU_STAC_ALLOW_PRIVATE=1 to allow)";
  return "";
}

std::string StacClient::validateAssetHref( const std::string &href )
{
  if ( href.empty() )
    return "Empty asset href";

  // Reject GDAL VSI paths that could already encode schemes
  if ( href.front() == '/' && containsIgnoreCase( href, "/vsi" ) )
    return "Pre-formed VSI paths are not accepted as asset hrefs";

  const ResourceUri uri = ResourceUri::parse( href );
  if ( uri.kind != ResourceKind::RemoteHttp )
  {
    const std::size_t schemeEnd = href.find( "://" );
    if ( schemeEnd != std::string::npos && schemeEnd > 0 )
    {
      std::string scheme = href.substr( 0, schemeEnd );
      for ( char &c : scheme )
        c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
      if ( scheme != "http" && scheme != "https" )
        return "Asset href scheme must be http or https (got '" + scheme + "')";
    }
    return "Asset href must be an absolute http(s) URL";
  }

  // SSRF policy applies to asset hrefs too (private hosts, bad schemes,
  // pre-formed VSI paths are all rejected here).
  return egressPolicyError( href );
}

std::string StacClient::selectCogVsicurlHref( const Json::Value &stacItemFeature )
{
  if ( !stacItemFeature.isObject() )
    return {};
  const Json::Value &assets = stacItemFeature["assets"];
  if ( !assets.isObject() )
    return {};

  // Asset-key order (jsoncpp members are std::map-ordered, exactly the order
  // the UI's QJsonObject iteration produced).
  for ( const std::string &key : assets.getMemberNames() )
  {
    const Json::Value &asset = assets[key];
    if ( !asset.isObject() )
      continue;
    const std::string href = asset["href"].isString() ? asset["href"].asString() : std::string();
    const std::string type = asset["type"].isString() ? asset["type"].asString() : std::string();
    const bool cogLike = endsWithIgnoreCase( href, ".tif" ) || containsIgnoreCase( type, "image/tiff" );
    if ( !cogLike )
      continue;
    if ( !validateAssetHref( href ).empty() )
      continue; // unusable asset — the documented contract is "no USABLE COG asset"
    return "/vsicurl/" + href;
  }
  return {};
}

HttpFetchOptions StacClient::fetchOptions() const
{
  HttpFetchOptions options;
  options.timeoutSeconds = mOptions.timeoutSeconds;
  options.connectTimeoutSeconds = mOptions.connectTimeoutSeconds;
  options.maxRetries = mOptions.maxRetries;
  options.maxResponseBytes = mOptions.maxResponseBytes;
  options.acceptContentTypes = { "json" };
  return options;
}

Json::Value StacClient::fetchDocument( const std::string &method, const std::string &url,
                                       const Json::Value &body ) const
{
  assertEgressAllowed( url );
  if ( method == "POST" )
  {
    HttpFetchOptions options = fetchOptions();
    options.headers.push_back( "Content-Type: application/json" );
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    options.postBody = Json::writeString( writerBuilder, body );
    return httpFetchJson( url, options );
  }
  return httpFetchJson( url, fetchOptions() );
}

/// 9.0 M4: stamps delivery provenance onto every parsed item — the item's
/// rel="self" link when present (resolved against the delivering URL), else
/// the delivering URL itself. Relative asset hrefs resolve against this; a
/// page-less parse leaves it empty and resolution fails typed.
void stampItemProvenance( StacItem &item, const std::string &url )
{
  const Json::Value &links = item.raw[ "links" ];
  if ( links.isArray() )
  {
    for ( const Json::Value &link : links )
    {
      // Foreign-typed rel/href are inert (extractNextLink doctrine): asString
      // on an object/array throws Json::LogicError — a plain std::exception
      // the search walk never catches — so one poisoned link must never be
      // able to kill the whole page parse.
      if ( link.isObject() && link["rel"].isString() && link["rel"].asString() == "self"
           && link["href"].isString() )
      {
        item.sourceHref = resolveReference( url, link["href"].asString() );
        return;
      }
    }
  }
  item.sourceHref = url;
}

/// 9.0 M4: resolves a RELATIVE reference against a LOCAL base path with
/// lexical normalization and traversal containment — a local STAC item's
/// relative asset hrefs stay inside the item's directory tree, and any ".."
/// chain escaping the base directory is refused (never a guess).
std::string resolveLocalReference( const std::string &basePath, const std::string &reference )
{
  const fs::path base = sicnu::portable::pathFromUtf8( basePath ).parent_path();
  if ( reference.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: empty local asset href" );
  std::error_code ec;
  fs::path baseAbsolute = fs::weakly_canonical( base, ec );
  if ( ec )
    baseAbsolute = fs::absolute( base, ec );
  if ( ec )
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: item provenance path is not absolute" );
  const fs::path merged = baseAbsolute / sicnu::portable::pathFromUtf8( reference );
  const fs::path normalized = merged.lexically_normal();
  // Containment (9.0 review: separator-agnostic — path separators differ on
  // Windows, so prefix string compare breaks there): the normalized path
  // must be RELATIVE TO the base with no leading ".." components, else the
  // reference escaped the item's directory tree.
  const fs::path relative = normalized.lexically_relative( baseAbsolute );
  const bool escapes = relative.empty()
                       || ( relative.begin() != relative.end() && *relative.begin() == fs::path( ".." ) );
  if ( escapes )
  {
    Json::Value details;
    details["hint"] = "relative asset hrefs must stay within the item's directory tree";
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: asset href escapes the item directory", details );
  }
  // UTF-8 out (the value re-enters pathFromUtf8 downstream).
  return sicnu::portable::pathToUtf8( normalized );
}

/// 9.0 M4: bounded response cache key — method, URL, canonical compact body
/// (jsoncpp object members are std::map-ordered, so the written form is
/// canonical for identical queries). The URL stays unredacted IN PROCESS;
/// nothing here is logged or persisted and no display surface reads it.
std::string cacheKey( const std::string &method, const std::string &url, const Json::Value &body )
{
  Json::StreamWriterBuilder writerBuilder;
  writerBuilder["indentation"] = "";
  const std::string bodyText = body.isNull() ? std::string() : Json::writeString( writerBuilder, body );
  return method + "\n" + url + "\n" + sha256Hex( bodyText );
}

StacPage StacClient::executeSearch( const std::string &method, const std::string &url,
                                    const Json::Value &body ) const
{
  // --- bounded response cache (opt-in) -----------------------------------
  // 9.0 review: the cache mutex NEVER spans the network fetch — lookup under
  // the lock, fetch outside it, store under the lock again.
  Json::Value document;
  bool servedFromCache = false;
  std::string cacheKeyText;
  std::chrono::steady_clock::time_point fetchedAt = std::chrono::steady_clock::now();
  bool shouldStore = false;
  if ( mOptions.cacheEnabled )
  {
    cacheKeyText = cacheKey( method, url, body );
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock( mCacheMutex );
    auto it = mCache.find( cacheKeyText );
    if ( it != mCache.end() )
    {
      const bool expired = mOptions.cacheTtlSeconds > 0
                             && std::chrono::duration_cast<std::chrono::seconds>( now - it->second.storedAt ).count()
                                  >= mOptions.cacheTtlSeconds;
      if ( expired )
      {
        mCacheBytes -= it->second.bytes;
        mCache.erase( it );
        mCacheEvictions += 1;
      }
      else
      {
        document = it->second.document;
        mCacheHits += 1;
        servedFromCache = true;
      }
    }
    shouldStore = !servedFromCache;
  }
  if ( !servedFromCache )
  {
    document = fetchDocument( method, url, body ); // deliberately outside mCacheMutex
    fetchedAt = std::chrono::steady_clock::now();
    if ( mOptions.cacheEnabled )
    {
      std::lock_guard<std::mutex> lock( mCacheMutex );
      mCacheMisses += 1;
      // Store bounded: serialize once for accounting, evict LRU-ish (oldest
      // storedAt) when entry/byte caps overflow.
      Json::StreamWriterBuilder writerBuilder;
      writerBuilder["indentation"] = "";
      QueryCacheEntry entry;
      entry.document = document;
      entry.bytes = Json::writeString( writerBuilder, document ).size();
      entry.storedAt = fetchedAt;
      while ( ( mCache.size() + 1 > mOptions.cacheMaxEntries
                || mCacheBytes + entry.bytes > mOptions.cacheMaxBytes )
              && !mCache.empty() )
      {
        auto oldest = mCache.begin();
        for ( auto scan = mCache.begin(); scan != mCache.end(); ++scan )
          if ( scan->second.storedAt < oldest->second.storedAt )
            oldest = scan;
        mCacheBytes -= oldest->second.bytes;
        mCache.erase( oldest );
        mCacheEvictions += 1;
      }
      if ( mOptions.cacheMaxEntries > 0 && entry.bytes <= mOptions.cacheMaxBytes )
      {
        mCache[cacheKeyText] = std::move( entry );
        mCacheBytes += mCache[cacheKeyText].bytes;
      }
    }
  }

  StacPage page;
  page.selfMethod = method;
  page.selfBody = body;
  ( void )shouldStore;
  if ( !document.isObject() || !document.isMember( "features" ) || !document["features"].isArray() )
  {
    throw GeoError( ErrorCode::InvalidMetadata, "StacClient: search answer carries no features array" );
  }
  for ( const Json::Value &feature : document["features"] )
  {
    page.items.push_back( StacItem::parse( feature ) );
    stampItemProvenance( page.items.back(), url );
  }

  std::string nextMethod;
  std::string nextHref;
  Json::Value nextBody;
  bool nextMerge = false;
  if ( extractNextLink( document, nextMethod, nextHref, nextBody, nextMerge ) )
  {
    page.nextMethod = nextMethod;
    page.nextHref = resolveReference( url, nextHref );
    page.nextBody = nextBody;
    page.nextMerge = nextMerge;
  }
  return page;
}

std::string StacClient::buildSearchUrl( const std::string &root, const StacSearchQuery &query )
{
  std::string params;
  if ( !query.bbox.empty() )
  {
    std::vector<std::string> parts;
    for ( const double value : query.bbox )
    {
      std::ostringstream text;
      text << value;
      parts.push_back( text.str() );
    }
    appendParam( params, "bbox", joinEncoded( parts ) );
  }
  if ( !query.datetime.empty() )
    appendParam( params, "datetime", query.datetime );
  if ( !query.collections.empty() )
    appendParam( params, "collections", joinEncoded( query.collections ) );
  if ( !query.ids.empty() )
    appendParam( params, "ids", joinEncoded( query.ids ) );
  if ( query.limit > 0 )
  {
    std::ostringstream text;
    text << query.limit;
    appendParam( params, "limit", text.str() );
  }
  if ( !query.sortBy.empty() )
    appendParam( params, "sortby", query.sortBy );
  return trimSlash( root ) + "/search" + ( params.empty() ? "" : "?" + params );
}

StacRequest StacClient::buildSearchRequest( const StacSearchQuery &query ) const
{
  query.validate();

  const bool needsPost = !query.query.isNull() || !query.intersects.isNull();
  if ( needsPost )
  {
    Json::Value body( Json::objectValue );
    if ( !query.bbox.empty() )
    {
      Json::Value bboxJson( Json::arrayValue );
      for ( const double value : query.bbox )
        bboxJson.append( value );
      body["bbox"] = bboxJson;
    }
    if ( !query.intersects.isNull() )
      body["intersects"] = query.intersects;
    if ( !query.datetime.empty() )
      body["datetime"] = query.datetime;
    if ( !query.collections.empty() )
    {
      Json::Value list( Json::arrayValue );
      for ( const std::string &collection : query.collections )
        list.append( collection );
      body["collections"] = list;
    }
    if ( !query.ids.empty() )
    {
      Json::Value list( Json::arrayValue );
      for ( const std::string &id : query.ids )
        list.append( id );
      body["ids"] = list;
    }
    if ( query.limit > 0 )
      body["limit"] = query.limit;
    if ( !query.query.isNull() )
      body["query"] = query.query;
    if ( !query.sortBy.empty() )
    {
      Json::Value sort( Json::arrayValue );
      Json::Value sortField( Json::objectValue );
      sortField["field"] = query.sortBy;
      sortField["direction"] = "asc";
      sort.append( sortField );
      body["sortby"] = sort;
    }
    return StacRequest{ "POST", mRoot + "/search", body };
  }

  // GET pages record their EQUIVALENT canonical body so a POST rel=next
  // (merge:true) can merge into the original filters — the query string
  // alone cannot survive a POST continuation.
  Json::Value canonicalBody( Json::objectValue );
  if ( !query.bbox.empty() )
  {
    Json::Value bboxJson( Json::arrayValue );
    for ( const double value : query.bbox )
      bboxJson.append( value );
    canonicalBody["bbox"] = bboxJson;
  }
  if ( !query.datetime.empty() )
    canonicalBody["datetime"] = query.datetime;
  if ( !query.collections.empty() )
  {
    Json::Value list( Json::arrayValue );
    for ( const std::string &collection : query.collections )
      list.append( collection );
    canonicalBody["collections"] = list;
  }
  if ( !query.ids.empty() )
  {
    Json::Value list( Json::arrayValue );
    for ( const std::string &id : query.ids )
      list.append( id );
    canonicalBody["ids"] = list;
  }
  if ( query.limit > 0 )
    canonicalBody["limit"] = query.limit;
  return StacRequest{ "GET", buildSearchUrl( mRoot, query ), canonicalBody };
}

StacPage StacClient::search( const StacSearchQuery &query ) const
{
  const StacRequest request = buildSearchRequest( query );
  return executeSearch( request.method, request.url, request.body );
}

StacRequest StacClient::buildNextRequest( const StacPage &page ) const
{
  if ( !page.hasMore() )
    return StacRequest{};
  if ( page.nextMethod == "POST" )
  {
    // rel=next POST links: with "merge": true the continuation body is a
    // DELTA over the request that produced this page — the caller's filters
    // MUST survive pagination, so deep-merge into selfBody. Without merge,
    // the body replaces it. A POST link with no body at all is a broken
    // origin, not an unfiltered re-search.
    if ( page.nextBody.isNull() )
      throw GeoError( ErrorCode::InvalidMetadata,
                      "StacClient: rel=next POST link carries no body; refusing an unfiltered crawl" );
    Json::Value merged = page.selfBody;
    if ( page.nextMerge )
    {
      for ( const std::string &key : page.nextBody.getMemberNames() )
        merged[key] = page.nextBody[key];
    }
    else
    {
      merged = page.nextBody;
    }
    return StacRequest{ "POST", page.nextHref, merged };
  }
  return StacRequest{ "GET", page.nextHref, Json::Value() };
}

StacPage StacClient::nextPage( const StacPage &page ) const
{
  if ( !page.hasMore() )
    return StacPage{};
  const StacRequest request = buildNextRequest( page );
  return executeSearch( request.method, request.url, request.body );
}

void StacClient::requestPageDetached( const StacRequest &request, const PageCallback &onDone ) const
{
  if ( !onDone )
    return;
  if ( request.url.empty() )
  {
    onDone( StacPage{}, std::string() );
    return;
  }
  // The worker owns a PRIVATE client (root + options copied before the
  // thread starts): no lifetime coupling to the caller, no shared mutable
  // state. The bounded response cache is per-instance and is deliberately
  // NOT shared across detached fetches.
  std::shared_ptr<StacClient> worker;
  try
  {
    worker = std::make_shared<StacClient>( mRoot, mOptions );
  }
  catch ( const GeoError &error )
  {
    onDone( StacPage{}, error.what() );
    return;
  }
  std::thread(
    [ worker, request, onDone ]() {
      try
      {
        StacPage page = worker->executeSearch( request.method, request.url, request.body );
        onDone( std::move( page ), std::string() );
      }
      catch ( const GeoError &error )
      {
        onDone( StacPage{}, error.what() );
      }
      catch ( const std::exception &error )
      {
        onDone( StacPage{}, error.what() );
      }
      catch ( ... )
      {
        onDone( StacPage{}, "StacClient: unknown fetch failure" );
      }
    } )
    .detach();
}

StacSearchAllResult StacClient::searchAll( const StacSearchQuery &query ) const
{
  StacSearchAllResult result;
  // Hard bounds: maxItems caps accumulation, a page budget caps the WALK
  // itself (an origin answering empty pages forever must never spin), and
  // a revisit guard catches pagination loops by href.
  const int pageBudget = mOptions.maxItems + 16;
  std::set<std::string> visited;
  StacPage page = search( query );
  int pages = 0;
  while ( true )
  {
    for ( StacItem &item : page.items )
    {
      if ( static_cast<int>( result.items.size() ) >= mOptions.maxItems )
      {
        result.truncatedByLimit = true;
        return result;
      }
      result.items.push_back( std::move( item ) );
    }
    if ( !page.hasMore() )
      return result;
    if ( ++pages > pageBudget )
    {
      result.truncatedByLimit = true;
      return result;
    }
    if ( !visited.insert( page.nextHref ).second )
      throw GeoError( ErrorCode::InvalidMetadata,
                      "StacClient: pagination loop detected (repeated rel=next href)" );
    page = nextPage( page );
  }
}

Json::Value StacClient::collections() const
{
  const std::string url = mRoot + "/collections";
  assertEgressAllowed( url );
  return httpFetchJson( url, fetchOptions() );
}

Json::Value StacClient::collection( const std::string &collectionId ) const
{
  if ( collectionId.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: collection id must not be empty" );
  const std::string url = mRoot + "/collections/" + urlEncodeValue( collectionId );
  assertEgressAllowed( url );
  return httpFetchJson( url, fetchOptions() );
}

StacPage StacClient::collectionItems( const std::string &collectionId,
                                      const StacSearchQuery &query ) const
{
  if ( collectionId.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: collection id must not be empty" );
  query.validate();
  std::string params;
  if ( !query.datetime.empty() )
    appendParam( params, "datetime", query.datetime );
  if ( query.limit > 0 )
  {
    std::ostringstream text;
    text << query.limit;
    appendParam( params, "limit", text.str() );
  }
  if ( !query.bbox.empty() )
  {
    std::vector<std::string> parts;
    for ( const double value : query.bbox )
    {
      std::ostringstream text;
      text << value;
      parts.push_back( text.str() );
    }
    appendParam( params, "bbox", joinEncoded( parts ) );
  }
  const std::string url = mRoot + "/collections/" + urlEncodeValue( collectionId ) + "/items" +
                          ( params.empty() ? "" : "?" + params );
  return executeSearch( "GET", url, Json::Value() );
}

std::vector<StacAsset> StacClient::assetsWithRole( const StacItem &item, const std::string &role )
{
  std::vector<StacAsset> matches;
  for ( const auto &entry : item.assets )
  {
    if ( std::find( entry.second.roles.begin(), entry.second.roles.end(), role ) != entry.second.roles.end() )
      matches.push_back( entry.second );
  }
  return matches;
}

std::vector<StacAsset> StacClient::assetsOfMediaType( const StacItem &item,
                                                      const std::string &mediaTypeSubstring )
{
  std::vector<StacAsset> matches;
  for ( const auto &entry : item.assets )
  {
    if ( containsIgnoreCase( entry.second.mediaType, mediaTypeSubstring ) )
      matches.push_back( entry.second );
  }
  return matches;
}

bool StacClient::matchesCloudCover( const StacItem &item, double maxPercent )
{
  if ( !item.hasCloudCover )
    return true; // absence is not evidence
  return item.cloudCover <= maxPercent;
}

std::string StacClient::displayAssetHref( const StacAsset &asset )
{
  const ResourceUri uri = ResourceUri::parse( asset.href );
  return uri.display();
}

std::string StacClient::resolveAssetHref( const StacItem &item, const StacAsset &asset ) const
{
  if ( asset.href.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "StacClient::resolveAssetHref: asset carries no href" );
  const ResourceUri ref = ResourceUri::parse( asset.href );
  if ( ref.kind == ResourceKind::RemoteHttp )
    return asset.href; // already absolute — verbatim (fetches stay raw)
  if ( item.sourceHref.empty() )
  {
    Json::Value details;
    details["asset"] = asset.title.empty() ? asset.href : asset.title;
    details["hint"] = "items delivered without provenance cannot resolve relative hrefs";
    throw GeoError( ErrorCode::InvalidMetadata,
                    "StacClient::resolveAssetHref: relative href with no item provenance", details );
  }
  const ResourceUri base = ResourceUri::parse( item.sourceHref );
  if ( base.kind == ResourceKind::RemoteHttp )
    return resolveReference( item.sourceHref, asset.href );
  // Local provenance (parseFromFile): lexical merge inside the item directory.
  return resolveLocalReference( item.sourceHref, asset.href );
}

Json::Value StacClient::cacheStats() const
{
  std::lock_guard<std::mutex> lock( mCacheMutex );
  Json::Value json;
  json["enabled"] = mOptions.cacheEnabled;
  json["hits"] = static_cast<Json::UInt64>( mCacheHits );
  json["misses"] = static_cast<Json::UInt64>( mCacheMisses );
  json["evictions"] = static_cast<Json::UInt64>( mCacheEvictions );
  json["entries"] = static_cast<Json::UInt64>( mCache.size() );
  json["bytes"] = static_cast<Json::UInt64>( mCacheBytes );
  json["max_entries"] = static_cast<Json::UInt64>( mOptions.cacheMaxEntries );
  json["max_bytes"] = static_cast<Json::UInt64>( mOptions.cacheMaxBytes );
  json["ttl_seconds"] = mOptions.cacheTtlSeconds;
  return json;
}

std::vector<StacClient::ItemFetchResult> StacClient::fetchItemsConcurrent(
    const std::vector<std::string> &itemHrefs, int maxConcurrency ) const
{
  const int requested = maxConcurrency > 0 ? maxConcurrency : mOptions.maxConcurrency;
  const int workers = std::clamp( requested, 1, 32 );
  std::vector<ItemFetchResult> results( itemHrefs.size() );
  if ( itemHrefs.empty() )
    return results;

  // Shared work cursor: workers claim indexes atomically — no queue, no
  // locks; every result slot has exactly one writer (its claiming worker).
  std::atomic<std::size_t> next{ 0 };
  const auto run = [ & ] {
    for ( ;; )
    {
      const std::size_t index = next.fetch_add( 1 );
      if ( index >= itemHrefs.size() )
        return;
      ItemFetchResult &slot = results[index];
      slot.index = index;
      try
      {
        const Json::Value document = fetchDocument( "GET", itemHrefs[index], Json::Value() );
        if ( !document.isObject() )
          throw GeoError( ErrorCode::InvalidMetadata,
                          "StacClient: item document is not a JSON object" );
        slot.item = StacItem::parse( document );
        stampItemProvenance( slot.item, itemHrefs[index] );
        slot.ok = true;
      }
      catch ( const GeoError &error )
      {
        slot.errorText = std::string( error.what() ).substr( 0, 512 );
      }
      catch ( const std::exception &error )
      {
        slot.errorText = std::string( "item fetch failed: " ) + error.what();
      }
    }
  };
  if ( workers == 1 || itemHrefs.size() == 1 )
  {
    run();
    return results;
  }
  std::vector<std::thread> pool;
  pool.reserve( static_cast<std::size_t>( workers ) - 1 );
  for ( int i = 0; i < workers - 1; ++i )
    pool.emplace_back( run );
  run();
  for ( std::thread &worker : pool )
    worker.join();
  return results;
}

namespace
{

/// The item's effective instant: the parsed epoch-nanoseconds of the item
/// datetime (or range start when datetime is a range-only declaration), plus
/// whether an instant exists at all. Parse-failure keeps `hasInstant` false so
/// the ordering falls back to the raw-string comparison (never a guess).
struct EffectiveInstant
{
    bool hasInstant = false;
    std::int64_t epochNanos = 0;
    std::string id;
    std::size_t inputOrder = 0;
    bool hasDate = false;
};

EffectiveInstant effectiveInstantOf( const StacItem &item, std::size_t order )
{
  EffectiveInstant instant;
  instant.id = item.id;
  instant.inputOrder = order;
  const std::string &verbatim = !item.datetime.empty() ? item.datetime : item.startDatetime;
  instant.hasDate = !verbatim.empty();
  if ( !item.datetimeUtc.empty() )
  {
    // Prefer the parse-time normalized instant when present.
    const InstantParse normalized = parseIso8601Instant( item.datetimeUtc );
    if ( normalized.ok )
    {
      instant.hasInstant = true;
      instant.epochNanos = normalized.epochNanos;
    }
  }
  if ( !instant.hasInstant )
  {
    const InstantParse parsed = parseIso8601Instant( verbatim );
    if ( parsed.ok )
    {
      instant.hasInstant = true;
      instant.epochNanos = parsed.epochNanos;
    }
  }
  return instant;
}

} // namespace

StacSeries buildTemporalSeriesDetailed( const std::vector<StacItem> &items )
{
  StacSeries series;
  series.entries.reserve( items.size() );
  std::vector<EffectiveInstant> instants;
  instants.reserve( items.size() );
  for ( std::size_t i = 0; i < items.size(); ++i )
  {
    StacSeriesEntry entry;
    entry.item = items[i];
    entry.datetime = !items[i].datetime.empty() ? items[i].datetime : items[i].startDatetime;
    entry.canonical = stacItemToCanonical( items[i] );
    series.entries.push_back( std::move( entry ) );
    instants.push_back( effectiveInstantOf( items[i], i ) );
  }

  // Sort an index permutation (the instants stay addressed by ORIGINAL input
  // order — sorting the entries directly would desynchronize the two arrays
  // mid-sort). Pairwise order: undated last (input order kept); normalized
  // instants when BOTH parse (mixed offsets order correctly); raw strings for
  // unparseable-but-present datetimes; item id; input order as the final
  // deterministic tie-break.
  std::vector<std::size_t> order( series.entries.size() );
  for ( std::size_t i = 0; i < order.size(); ++i )
    order[i] = i;
  std::stable_sort(
    order.begin(), order.end(),
    [ & ] ( std::size_t aIndex, std::size_t bIndex ) {
      const EffectiveInstant &ia = instants[ aIndex ];
      const EffectiveInstant &ib = instants[ bIndex ];
      const StacSeriesEntry &a = series.entries[ aIndex ];
      const StacSeriesEntry &b = series.entries[ bIndex ];
      if ( !ia.hasDate || !ib.hasDate )
        return ia.hasDate && !ib.hasDate;
      // Class split (strict weak ordering): parseable instants ALWAYS sort
      // before unparseable datetimes — mixing the epoch ordering with the
      // raw-string ordering across classes would be intransitive (a cycle).
      if ( ia.hasInstant != ib.hasInstant )
        return ia.hasInstant;
      if ( ia.hasInstant )
      {
        if ( ia.epochNanos != ib.epochNanos )
          return ia.epochNanos < ib.epochNanos;
      }
      else if ( a.datetime != b.datetime )
      {
        return a.datetime < b.datetime; // unparseable class: raw-string order
      }
      if ( ia.id != ib.id )
        return ia.id < ib.id;
      return ia.inputOrder < ib.inputOrder;
    } );

  std::vector<StacSeriesEntry> ordered;
  ordered.reserve( series.entries.size() );
  for ( const std::size_t index : order )
    ordered.push_back( std::move( series.entries[ index ] ) );
  series.entries = std::move( ordered );

  // Duplicate accounting AFTER ordering: an entry whose instant equals an
  // earlier entry's instant is a duplicate acquisition (reported, kept).
  std::map<std::int64_t, bool> seenInstants;
  for ( std::size_t i = 0; i < series.entries.size(); ++i )
  {
    // order[] maps sorted position -> original index; recompute the original
    // index to reach the matching instant.
    const std::size_t originalIndex = order[ i ];
    const EffectiveInstant &instant = instants[ originalIndex ];
    if ( !instant.hasInstant )
      continue;
    const auto inserted = seenInstants.emplace( instant.epochNanos, false );
    if ( inserted.first->second )
      series.duplicateEntryIndices.push_back( i );
    else
      inserted.first->second = true; // first occurrence — not a duplicate
  }
  return series;
}

std::vector<StacSeriesEntry> buildTemporalSeries( const std::vector<StacItem> &items )
{
  return buildTemporalSeriesDetailed( items ).entries;
}

} // namespace sicnu::geo
