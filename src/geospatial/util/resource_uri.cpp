/***************************************************************************
  geospatial/util/resource_uri.cpp
  Remote Sensing I/O Foundation 5.0 — resource URI classification & identity.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/util/resource_uri.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <vector>

namespace sicnu::geo
{

namespace
{

struct VsiPrefixEntry
{
  const char *prefix;
  bool network;
};

/// GDAL VSI virtual-file prefixes the foundation recognizes. Network families
/// classify as VsiRemote, local/memory containers as VsiVirtual.
const std::array<VsiPrefixEntry, 15> &vsiPrefixes()
{
  static const std::array<VsiPrefixEntry, 15> kPrefixes{ {
    { "/vsicurl/", true },
    { "/vsicurl_streaming/", true },
    { "/vsis3/", true },
    { "/vsis3_streaming/", true },
    { "/vsigs/", true },
    { "/vsigs_streaming/", true },
    { "/vsiaz/", true },
    { "/vsiaz_streaming/", true },
    { "/vsiadls/", true },
    { "/vsihdfs/", true },
    { "/vsioss/", true },
    { "/vsiswift/", true },
    { "/vsizip/", false },
    { "/vsitar/", false },
    { "/vsigzip/", false },
  } };
  return kPrefixes;
}

bool startsWithIgnoreCase( const std::string &text, const std::string &prefix )
{
  if ( text.size() < prefix.size() )
    return false;
  for ( std::size_t i = 0; i < prefix.size(); ++i )
  {
    if ( std::tolower( static_cast<unsigned char>( text[i] ) ) !=
         std::tolower( static_cast<unsigned char>( prefix[i] ) ) )
      return false;
  }
  return true;
}

std::string toLower( const std::string &text )
{
  std::string out = text;
  std::transform( out.begin(), out.end(), out.begin(), []( unsigned char c ) {
    return static_cast<char>( std::tolower( c ) );
  } );
  return out;
}

/// Windows drive-letter path: "C:\..." or "C:/..." (single letter + colon).
bool hasWindowsDriveLetter( const std::string &path )
{
  return path.size() >= 2 && path[1] == ':' &&
         std::isalpha( static_cast<unsigned char>( path[0] ) ) != 0;
}

/// UNC: \\server\share or //server/share (but not \\?\ or \\.\)
bool hasUncPrefix( const std::string &path )
{
  return ( path.rfind( "\\\\", 0 ) == 0 || path.rfind( "//", 0 ) == 0 ) &&
         ( path.size() < 3 || ( path[2] != '?' && path[2] != '.' ) );
}

bool fileExistsUtf8( const std::string &path )
{
  std::error_code ec;
  std::filesystem::path native;
  try
  {
    native = std::filesystem::u8path( path );
  }
  catch ( ... )
  {
    // Not representable as UTF-8 (parse() fuzz, Verification 7.0): the
    // documented contract is that classification NEVER throws — treat the
    // input as not-an-existing-file and let the classifier answer by shape.
    return false;
  }
  return std::filesystem::exists( native, ec );
}

bool isDirectoryUtf8( const std::string &path )
{
  std::error_code ec;
  std::filesystem::path native;
  try
  {
    native = std::filesystem::u8path( path );
  }
  catch ( ... )
  {
    return false; // same totality rule as fileExistsUtf8
  }
  return std::filesystem::is_directory( native, ec );
}

/// GDAL subdataset selector: "DRIVER:"path":selector" — driver is a word with
/// at least one uppercase letter (NETCDF, HDF5, ZARR...), path is quoted or
/// unquoted (unquoted keeps drive letters intact).
struct SubdatasetParts
{
  std::string driver;
  std::string payloadPath;
  std::string selector;
  bool valid = false;
};

SubdatasetParts parseSubdataset( const std::string &text )
{
  SubdatasetParts parts;
  std::size_t i = 0;
  while ( i < text.size() &&
          ( std::isalnum( static_cast<unsigned char>( text[i] ) ) || text[i] == '_' ) )
    ++i;
  if ( i == 0 || i >= text.size() || text[i] != ':' )
    return parts;
  parts.driver = text.substr( 0, i );

  // Reject URL schemes ("http", "stac", "memory") and Windows drive letters
  // ("C:"): a subdataset driver name is ≥3 chars and always carries an
  // uppercase letter (NETCDF, HDF5, HDF4, ZARR, KEA, ...).
  bool hasUppercase = false;
  for ( const char c : parts.driver )
    hasUppercase = hasUppercase || std::isupper( static_cast<unsigned char>( c ) ) != 0;
  if ( !hasUppercase || parts.driver.size() < 3 )
    return parts;

  ++i; // past ':'
  if ( i < text.size() && text[i] == '"' )
  {
    const std::size_t closing = text.find( '"', i + 1 );
    if ( closing == std::string::npos )
      return parts;
    parts.payloadPath = text.substr( i + 1, closing - i - 1 );
    if ( closing + 1 < text.size() && text[closing + 1] == ':' )
      parts.selector = text.substr( closing + 2 );
    else if ( closing + 1 != text.size() )
      return parts; // trailing garbage after the quoted path
  }
  else
  {
    // Unquoted: a ':' followed by '\\' or '/' is a Windows drive separator,
    // not the selector boundary.
    std::size_t colon = text.find( ':', i );
    while ( colon != std::string::npos && colon + 1 < text.size() &&
            ( text[colon + 1] == '\\' || text[colon + 1] == '/' ) )
      colon = text.find( ':', colon + 1 );
    if ( colon == std::string::npos )
      parts.payloadPath = text.substr( i );
    else
    {
      parts.payloadPath = text.substr( i, colon - i );
      parts.selector = text.substr( colon + 1 );
    }
  }
  parts.valid = true;
  return parts;
}

int hexValue( char c )
{
  if ( c >= '0' && c <= '9' )
    return c - '0';
  if ( c >= 'a' && c <= 'f' )
    return c - 'a' + 10;
  if ( c >= 'A' && c <= 'F' )
    return c - 'A' + 10;
  return -1;
}

std::string normalizeSeparators( std::string text )
{
  std::replace( text.begin(), text.end(), '\\', '/' );
  // Collapse duplicate slashes; keep a leading "//" (UNC spelled forward) and
  // the "//" of URL schemes ("https://host") — a '/' directly after ':' is
  // scheme syntax, not a doubled separator.
  std::string out;
  out.reserve( text.size() );
  for ( std::size_t i = 0; i < text.size(); ++i )
  {
    if ( text[i] == '/' && !out.empty() && out.back() == '/' )
    {
      const bool schemeColon = out.size() >= 2 && out[out.size() - 2] == ':';
      if ( out.size() == 1 || schemeColon )
        out.push_back( '/' );
      continue;
    }
    out.push_back( text[i] );
  }
  return out;
}

std::string stripTrailingSlash( std::string text )
{
  while ( text.size() > 1 && ( text.back() == '/' ) &&
          !( text.size() == 2 && text[1] == '/' ) ) // keep UNC "//server//"? no: keep "//" root only
    text.pop_back();
  return text;
}

/// Masks credential-shaped values in a query string ("a=1;token=SECRET;b=2").
std::string redactQuery( const std::string &query )
{
  std::string out;
  std::size_t start = 0;
  while ( start <= query.size() )
  {
    const std::size_t end = query.find( '&', start );
    const std::string pair =
      query.substr( start, end == std::string::npos ? std::string::npos : end - start );
    const std::size_t eq = pair.find( '=' );
    std::string rendered = pair;
    if ( eq != std::string::npos && isCredentialQueryKey( pair.substr( 0, eq ) ) )
      rendered = pair.substr( 0, eq + 1 ) + "***";
    if ( !out.empty() )
      out.push_back( '&' );
    out += rendered;
    if ( end == std::string::npos )
      break;
    start = end + 1;
  }
  return out;
}

/// Masks credentials in an authority block's userinfo: "user:password" →
/// "user:***", and a token-only userinfo (no colon, e.g.
/// "https://TOKEN@host" — #776) is itself the secret → "***".
std::string redactUserinfo( const std::string &userinfo )
{
  if ( userinfo.empty() )
    return std::string();
  const std::size_t colon = userinfo.find( ':' );
  if ( colon == std::string::npos )
    return "***";
  return userinfo.substr( 0, colon + 1 ) + "***";
}

/// Uniform display-time credential pass (#776): masks credential-shaped
/// query pairs anywhere in the text and userinfo blocks in any embedded
/// authority ("...://user:pass@host/…", "...://token@host/…"). Covers
/// schemes the structured parser never sees (s3://, gs://, az://,
/// postgres://, ftp://, /vsis3/…?X-Amz-Signature=… payloads). Idempotent
/// over strings the structured branches already redacted.
std::string redactEmbeddedCredentials( const std::string &text )
{
  std::string out = text;
  const std::size_t schemeMarker = out.find( "://" );
  if ( schemeMarker != std::string::npos )
  {
    const std::size_t authorityStart = schemeMarker + 3;
    const std::size_t authorityEnd = out.find_first_of( "/?#", authorityStart );
    const std::size_t authorityLength =
      authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart;
    const std::string authority = out.substr( authorityStart, authorityLength );
    const std::size_t at = authority.rfind( '@' );
    if ( at != std::string::npos && at > 0 )
      out.replace( authorityStart, at, redactUserinfo( authority.substr( 0, at ) ) );
  }
  const std::size_t question = out.find( '?' );
  if ( question != std::string::npos )
  {
    const std::size_t hash = out.find( '#', question );
    const std::string query = out.substr(
      question + 1, hash == std::string::npos ? std::string::npos : hash - question - 1 );
    out.replace( question + 1, query.size(), redactQuery( query ) );
  }
  return out;
}

std::vector<std::string> splitSegments( const std::string &path )
{
  std::vector<std::string> segments;
  std::size_t start = 0;
  while ( start <= path.size() )
  {
    const std::size_t slash = path.find( '/', start );
    const std::string segment =
      path.substr( start, slash == std::string::npos ? std::string::npos : slash - start );
    if ( !segment.empty() )
      segments.push_back( segment );
    if ( slash == std::string::npos )
      break;
    start = slash + 1;
  }
  return segments;
}

} // namespace

const char *resourceKindName( ResourceKind kind )
{
  switch ( kind )
  {
    case ResourceKind::LocalFile: return "local_file";
    case ResourceKind::LocalDirectory: return "local_directory";
    case ResourceKind::DirectoryProduct: return "directory_product";
    case ResourceKind::RemoteHttp: return "remote_http";
    case ResourceKind::VsiRemote: return "vsi_remote";
    case ResourceKind::VsiVirtual: return "vsi_virtual";
    case ResourceKind::Subdataset: return "subdataset";
    case ResourceKind::StacAsset: return "stac_asset";
    case ResourceKind::InMemory: return "in_memory";
    case ResourceKind::Invalid: break;
  }
  return "invalid";
}

bool percentDecode( const std::string &text, std::string &out )
{
  out.clear();
  out.reserve( text.size() );
  bool clean = true;
  for ( std::size_t i = 0; i < text.size(); ++i )
  {
    if ( text[i] != '%' )
    {
      out.push_back( text[i] );
      continue;
    }
    if ( i + 2 < text.size() )
    {
      const int hi = hexValue( text[i + 1] );
      const int lo = hexValue( text[i + 2] );
      if ( hi >= 0 && lo >= 0 )
      {
        out.push_back( static_cast<char>( hi * 16 + lo ) );
        i += 2;
        continue;
      }
    }
    clean = false;
    out.push_back( '%' ); // malformed escape: kept verbatim
  }
  return clean;
}

bool isCredentialQueryKey( const std::string &keyName )
{
  static const char *const kCredentialKeys[] = {
    "x-amz-signature", "x-amz-credential", "x-amz-security-token", "x-goog-signature",
    "googleaccessid", "signature", "sig", "token", "access_token", "apikey", "api_key",
    "key", "sas", "sharedaccesssignature", "password", "passwd", "secret",
    // #810: common denylist gaps — signed-URL and OAuth families.
    "auth", "authorization", "bearer", "access_key", "accesskey", "aws_access_key_id",
    "aws_secret_access_key", "client_secret", "refresh_token", "id_token",
    "session_token", "sessiontoken", "credentials", "jwt", "hmac", "sha",
  };
  const std::string key = toLower( keyName );
  for ( const char *candidate : kCredentialKeys )
  {
    if ( key == candidate )
      return true;
  }
  return false;
}

ResourceUri ResourceUri::parse( const std::string &raw )
{
  ResourceUri uri;
  uri.raw = raw;

  if ( raw.empty() )
  {
    uri.kind = ResourceKind::Invalid;
    uri.parseReason = "empty source string";
    return uri;
  }

  // --- STAC ---
  if ( startsWithIgnoreCase( raw, "stac://" ) )
  {
    uri.kind = ResourceKind::StacAsset;
    uri.scheme = "stac";
    uri.path = raw.substr( 7 );
    return uri;
  }

  // --- memory ---
  if ( startsWithIgnoreCase( raw, "memory://" ) || startsWithIgnoreCase( raw, "/vsimem/" ) )
  {
    uri.kind = ResourceKind::InMemory;
    uri.vsiPrefix = "/vsimem";
    uri.path = startsWithIgnoreCase( raw, "memory://" ) ? raw.substr( 9 ) : raw;
    return uri;
  }

  // --- network / local VSI ---
  for ( const VsiPrefixEntry &entry : vsiPrefixes() )
  {
    if ( !startsWithIgnoreCase( raw, entry.prefix ) )
      continue;
    const std::size_t prefixLength = std::char_traits<char>::length( entry.prefix );
    uri.vsiPrefix = std::string( entry.prefix, prefixLength - 1 ); // without trailing '/'
    uri.path = raw.substr( prefixLength - 1 );                     // keeps one leading '/'
    uri.kind = entry.network ? ResourceKind::VsiRemote : ResourceKind::VsiVirtual;
    return uri;
  }

  // --- http(s) ---
  if ( startsWithIgnoreCase( raw, "http://" ) || startsWithIgnoreCase( raw, "https://" ) )
  {
    const std::size_t schemeEnd = raw.find( "://" );
    uri.scheme = toLower( raw.substr( 0, schemeEnd ) );
    std::string rest = raw.substr( schemeEnd + 3 );
    const std::size_t hash = rest.find( '#' );
    if ( hash != std::string::npos )
    {
      uri.fragment = rest.substr( hash + 1 );
      rest = rest.substr( 0, hash );
    }
    const std::size_t question = rest.find( '?' );
    if ( question != std::string::npos )
    {
      uri.query = rest.substr( question + 1 );
      rest = rest.substr( 0, question );
    }
    const std::size_t slash = rest.find( '/' );
    std::string authority = slash == std::string::npos ? rest : rest.substr( 0, slash );
    uri.path = slash == std::string::npos ? std::string( "/" ) : rest.substr( slash );
    const std::size_t at = authority.rfind( '@' );
    if ( at != std::string::npos )
    {
      uri.userinfo = authority.substr( 0, at );
      authority = authority.substr( at + 1 );
    }
    uri.host = authority;
    if ( uri.host.empty() )
    {
      uri.kind = ResourceKind::Invalid;
      uri.parseReason = "http(s) URL without host";
      return uri;
    }
    uri.kind = ResourceKind::RemoteHttp;
    return uri;
  }

  // --- subdataset selectors ("NETCDF:\"f.nc\":var") ---
  const SubdatasetParts subdataset = parseSubdataset( raw );
  if ( subdataset.valid )
  {
    uri.kind = ResourceKind::Subdataset;
    uri.scheme = subdataset.driver;
    uri.path = subdataset.payloadPath;
    uri.fragment = subdataset.selector;
    return uri;
  }

  // --- virtual dataset marker ---
  if ( startsWithIgnoreCase( raw, "vrt://" ) || startsWithIgnoreCase( raw, "virtual://" ) )
  {
    uri.kind = ResourceKind::VirtualDataset;
    uri.path = raw;
    return uri;
  }

  // --- local filesystem (long path, UNC, drive, plain, relative) ---
  if ( startsWithIgnoreCase( raw, "\\\\?\\" ) )
  {
    uri.path = raw;
    uri.kind = isDirectoryUtf8( raw ) ? ResourceKind::LocalDirectory : ResourceKind::LocalFile;
    return uri;
  }

  if ( hasUncPrefix( raw ) )
  {
    uri.path = raw;
    uri.kind = isDirectoryUtf8( raw ) ? ResourceKind::LocalDirectory : ResourceKind::LocalFile;
    return uri;
  }

  if ( hasWindowsDriveLetter( raw ) || raw[0] == '/' || raw[0] == '\\' )
  {
    uri.path = raw;
    if ( isDirectoryUtf8( raw ) )
      uri.kind = looksLikeDirectoryProduct( raw ) ? ResourceKind::DirectoryProduct
                                                  : ResourceKind::LocalDirectory;
    else
      uri.kind = fileExistsUtf8( raw ) ? ResourceKind::LocalFile : ResourceKind::Invalid;
    if ( uri.kind == ResourceKind::Invalid )
      uri.parseReason = "local path does not exist";
    return uri;
  }

  // Relative path: existing inputs classify; non-existing are invalid
  // (callers that mean project-relative resolution use resolveAgainst).
  if ( fileExistsUtf8( raw ) || isDirectoryUtf8( raw ) )
  {
    uri.path = raw;
    if ( isDirectoryUtf8( raw ) )
      uri.kind = looksLikeDirectoryProduct( raw ) ? ResourceKind::DirectoryProduct
                                                  : ResourceKind::LocalDirectory;
    else
      uri.kind = ResourceKind::LocalFile;
    return uri;
  }

  uri.kind = ResourceKind::Invalid;
  uri.parseReason = "not a URI, URL, VSI handle or existing local path";
  return uri;
}

bool ResourceUri::isRemote() const
{
  return kind == ResourceKind::RemoteHttp || kind == ResourceKind::VsiRemote;
}

bool ResourceUri::isLocalPayload() const
{
  switch ( kind )
  {
    case ResourceKind::LocalFile:
    case ResourceKind::LocalDirectory:
    case ResourceKind::DirectoryProduct:
      return true;
    case ResourceKind::VsiVirtual:
    case ResourceKind::Subdataset:
      return !embeddedLocalPath().empty();
    default:
      return false;
  }
}

std::string ResourceUri::canonical() const
{
  switch ( kind )
  {
    case ResourceKind::LocalFile:
    case ResourceKind::LocalDirectory:
    case ResourceKind::DirectoryProduct:
    case ResourceKind::Invalid:
      return stripTrailingSlash( normalizeSeparators( raw ) );
    case ResourceKind::RemoteHttp:
    {
      std::string out =
        scheme + "://" + ( userinfo.empty() ? std::string() : userinfo + "@" ) + host;
      const std::string normalizedPath = normalizeSeparators( path );
      out += normalizedPath.empty() ? std::string( "/" ) : normalizedPath;
      if ( !query.empty() )
        out += "?" + query;
      if ( !fragment.empty() )
        out += "#" + fragment;
      return out;
    }
    case ResourceKind::VsiRemote:
    case ResourceKind::VsiVirtual:
      return vsiPrefix + stripTrailingSlash( normalizeSeparators( path ) );
    case ResourceKind::InMemory:
      return raw;
    case ResourceKind::Subdataset:
      return scheme + ":\"" + stripTrailingSlash( normalizeSeparators( path ) ) + "\":" + fragment;
    case ResourceKind::StacAsset:
      return scheme + "://" + path;
    case ResourceKind::VirtualDataset:
      return raw;
  }
  return raw;
}

std::string ResourceUri::display() const
{
  if ( kind == ResourceKind::VsiRemote )
  {
    // A VSI payload may carry credentials in an embedded URL
    // ("/vsicurl/https://user:pass@host/…") — redact it like RemoteHttp.
    const std::string inner = remoteUrl();
    if ( startsWithIgnoreCase( inner, "http://" ) || startsWithIgnoreCase( inner, "https://" ) )
    {
      const ResourceUri embedded = ResourceUri::parse( inner );
      return vsiPrefix + "/" + embedded.display();
    }
  }
  if ( kind != ResourceKind::RemoteHttp )
    // #776: every other kind goes through the same uniform credential pass
    // before display — s3://, gs://, az://, postgres://, ftp:// payloads and
    // VSI handles carrying signed-URL queries never render raw secrets.
    return redactEmbeddedCredentials( canonical() );

  std::string out =
    scheme + "://" + ( userinfo.empty() ? std::string() : redactUserinfo( userinfo ) + "@" ) + host;
  std::string decoded;
  percentDecode( path, decoded );
  out += decoded.empty() ? std::string( "/" ) : decoded;
  if ( !query.empty() )
    out += "?" + redactQuery( query );
  if ( !fragment.empty() )
    out += "#" + fragment;
  // Token-only userinfo was the RemoteHttp gap (#776): redactUserinfo masks
  // it, and the uniform pass keeps non-standard spellings covered too.
  return redactEmbeddedCredentials( out );
}

std::string ResourceUri::remoteUrl() const
{
  if ( kind == ResourceKind::VsiRemote )
  {
    // path keeps one leading '/' after the prefix strip
    // ("/vsicurl/https://…" → "/https://…"); a leading slash followed by the
    // URL scheme collapses back to the URL. Other spellings (e.g. S3
    // "bucket/key") keep their VSI-internal form.
    if ( path.size() > 6 && path[0] == '/' &&
         ( startsWithIgnoreCase( path.substr( 1 ), "http:/" ) ||
           startsWithIgnoreCase( path.substr( 1 ), "https:/" ) ) )
      return path.substr( 1 );
    return path;
  }
  if ( kind == ResourceKind::RemoteHttp )
    return raw;
  return std::string();
}

std::string ResourceUri::embeddedLocalPath() const
{
  if ( kind == ResourceKind::Subdataset )
    return path;
  if ( kind == ResourceKind::VsiVirtual && path.size() >= 2 )
    return path.substr( 1 ); // drop the leading '/' kept by the prefix strip
  return std::string();
}

bool ResourceUri::looksLikeDirectoryProduct( const std::string &path )
{
  if ( !isDirectoryUtf8( path ) )
    return false;

  const std::string name = stripTrailingSlash( path );
  const std::size_t lastSlash = name.find_last_of( "/\\" );
  const std::string base = lastSlash == std::string::npos ? name : name.substr( lastSlash + 1 );
  const std::string lowerBase = toLower( base );

  // Container suffixes
  static const char *const kProductSuffixes[] = { ".safe", ".grp", ".sen3", ".n1" };
  for ( const char *suffix : kProductSuffixes )
  {
    const std::size_t length = std::char_traits<char>::length( suffix );
    if ( lowerBase.size() >= length &&
         lowerBase.compare( lowerBase.size() - length, length, suffix ) == 0 )
      return true;
  }

  // Marker children (targeted existence checks — never a directory scan)
  const std::string norm = stripTrailingSlash( normalizeSeparators( path ) );
  static const char *const kMarkers[] = {
    "GRANULE", "IMG_DATA", "manifest.safe", "MTD_MSIL1C.xml", "MTD_MSIL2A.xml",
    "MTD_TL.xml", "MTD_DS.xml", "DATA",
  };
  for ( const char *marker : kMarkers )
  {
    if ( fileExistsUtf8( norm + "/" + marker ) )
      return true;
  }

  // Scene-directory naming heuristics (Landsat LC08/LT05/LE07/LO08/L1T,
  // MODIS/MOD13/MYD11 style prefixes) — naming is a hint, not authoritative.
  static const char *const kNameHints[] = {
    "lc0", "lt0", "le0", "lo0", "l1t", "mod", "myd", "mod1", "myd1",
  };
  for ( const char *hint : kNameHints )
  {
    const std::size_t length = std::char_traits<char>::length( hint );
    if ( lowerBase.size() >= length && lowerBase.compare( 0, length, hint ) == 0 )
      return true;
  }
  return false;
}

ResourceUri ResourceUri::resolveAgainst( const std::string &baseDirectory, const std::string &reference )
{
  ResourceUri direct = parse( reference );
  // Anchored = not relative: explicit schemes, VSI handles, or path shapes
  // rooted outside the base (drive letter, slash/UNC prefix). An Invalid
  // parse alone does NOT mean anchored — a relative reference that does not
  // exist yet is exactly the input resolveAgainst exists to join.
  bool referenceAnchored = false;
  switch ( direct.kind )
  {
    case ResourceKind::RemoteHttp:
    case ResourceKind::VsiRemote:
    case ResourceKind::VsiVirtual:
    case ResourceKind::Subdataset:
    case ResourceKind::StacAsset:
    case ResourceKind::InMemory:
    case ResourceKind::VirtualDataset:
      referenceAnchored = true;
      break;
    case ResourceKind::LocalFile:
    case ResourceKind::LocalDirectory:
    case ResourceKind::DirectoryProduct:
      // An existing path is anchored only when spelled absolutely; a
      // relative spelling that exists resolves against the CWD, not base —
      // treat it as anchored only with an absolute shape (checked below).
      referenceAnchored = false;
      break;
    case ResourceKind::Invalid:
      referenceAnchored = false;
      break;
  }
  referenceAnchored = referenceAnchored || hasWindowsDriveLetter( reference ) ||
                      hasUncPrefix( reference ) ||
                      ( !reference.empty() && ( reference[0] == '/' || reference[0] == '\\' ) );
  if ( referenceAnchored )
    return direct;

  // Join and lexically resolve '.'/'..' against the base; refuse any ".."
  // that would climb above the base directory.
  const std::string base = stripTrailingSlash( normalizeSeparators( baseDirectory ) );
  std::vector<std::string> segments = splitSegments( base );
  const std::size_t baseDepth = segments.size();

  bool escaped = false;
  for ( const std::string &segment : splitSegments( normalizeSeparators( reference ) ) )
  {
    if ( segment == ".." )
    {
      if ( segments.size() <= baseDepth || segments.empty() )
      {
        escaped = true;
        break;
      }
      segments.pop_back();
    }
    else
    {
      segments.push_back( segment );
    }
  }
  if ( escaped )
  {
    ResourceUri refused;
    refused.raw = reference;
    refused.kind = ResourceKind::Invalid;
    refused.parseReason = "reference escapes the base directory ('..' traversal)";
    return refused;
  }

  const bool leadingSlash = !base.empty() && base[0] == '/';
  const std::string drivePrefix =
    !segments.empty() && segments.front().size() == 2 && segments.front()[1] == ':'
      ? segments.front() + "/"
      : std::string();
  std::string resolved;
  for ( std::size_t i = drivePrefix.empty() ? 0 : 1; i < segments.size(); ++i )
  {
    resolved += segments[i];
    if ( i + 1 < segments.size() )
      resolved += "/";
  }
  const std::string joinedPath = drivePrefix.empty()
    ? ( ( leadingSlash ? "/" : "" ) + resolved )
    : drivePrefix + resolved;

  // Lexical result: classification is by SHAPE, not by existence — a resolved
  // path may be an export target that does not exist yet.
  ResourceUri out;
  out.raw = joinedPath;
  out.path = joinedPath;
  out.kind = ResourceKind::LocalFile;
  return out;
}

std::string ResourceUri::toWindowsLongPath( const std::string &localPath )
{
#if defined( _WIN32 )
  if ( localPath.rfind( "\\\\", 0 ) == 0 )
    return localPath; // UNC or already-extended: untouched
  const std::string normalized = normalizeSeparators( localPath );
  if ( normalized.size() < 2 || normalized[1] != ':' )
    return localPath;
  std::string out = "\\\\?\\" + normalized;
  std::replace( out.begin() + 4, out.end(), '/', '\\' );
  return out;
#else
  ( void ) localPath;
  return std::string();
#endif
}

} // namespace sicnu::geo
