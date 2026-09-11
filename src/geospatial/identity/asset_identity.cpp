/***************************************************************************
  geospatial/identity/asset_identity.cpp — unified asset identity (M1).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/identity/asset_identity.h"

#include "geospatial/remote/remote_identity_token.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace sicnu::geo
{

namespace
{

/// Canonical absolute spelling of a local path (identity, not display).
/// Best effort: a path that cannot be canonicalized (deep link loops,
/// permission walls) still normalizes lexically — the SIZE/MTime/Inode facts
/// carried alongside remain the change-detection basis.
std::string canonicalLocalPath( const std::string &path )
{
  std::error_code ec;
  fs::path canonical = fs::weakly_canonical( fs::u8path( path ), ec );
  if ( ec )
  {
    canonical = fs::u8path( path ).lexically_normal();
    if ( canonical.is_relative() )
      return canonical.string();
  }
#if defined( _WIN32 )
  const std::wstring wide = canonical.wstring();
  if ( wide.size() >= 2 && wide[1] == L':' )
  {
    // Stable lowercase drive letter.
    std::wstring lowered = wide;
    lowered[0] = static_cast<wchar_t>( std::tolower( wide[0] ) );
    return std::string( lowered.begin(), lowered.end() );
  }
  return std::string( wide.begin(), wide.end() );
#else
  return canonical.string();
#endif
}

struct LocalFacts
{
  bool readable = false;
  std::uintmax_t size = 0;
  std::uintmax_t mtimeTicks = 0;
  std::uintmax_t inode = 0;
};

LocalFacts localFacts( const fs::path &path )
{
  LocalFacts facts;
  std::error_code ec;
  const fs::file_status status = fs::status( path, ec );
  if ( ec || !fs::is_regular_file( status ) )
    return facts;
  facts.size = fs::file_size( path, ec );
  if ( ec )
    return facts;
  const fs::file_time_type mtime = fs::last_write_time( path, ec );
  if ( ec )
    return facts;
  facts.mtimeTicks = static_cast<std::uintmax_t>( mtime.time_since_epoch().count() );
#ifndef _WIN32
  struct stat st;
  if ( ::stat( path.c_str(), &st ) == 0 )
    facts.inode = static_cast<std::uintmax_t>( st.st_ino );
  else
    return facts;
#endif
  facts.readable = true;
  return facts;
}

/// Content hash of the first `budget` bytes (or the whole file when smaller).
/// Returns false when the file cannot be read (unprovable, never a guess).
bool hashFilePrefix( const fs::path &path, std::uint64_t budget, std::string &hexOut,
                     std::uintmax_t &hashedOut )
{
  // .string() first: fs::path::c_str() is wchar_t* on Windows.
  std::FILE *file = std::fopen( path.string().c_str(), "rb" );
  if ( !file )
    return false;
  Sha256 hash;
  unsigned char buffer[64 * 1024];
  std::uint64_t remaining = budget;
  bool anyRead = false;
  while ( remaining > 0 )
  {
    const std::size_t chunk = static_cast<std::size_t>( std::min<std::uint64_t>( remaining, sizeof( buffer ) ) );
    const std::size_t got = std::fread( buffer, 1, chunk, file );
    if ( got == 0 )
      break;
    hash.update( buffer, got );
    remaining -= got;
    hashedOut += got;
    anyRead = true;
    if ( got < chunk )
      break; // EOF
  }
  const bool ok = anyRead && std::ferror( file ) == 0;
  std::fclose( file );
  if ( !ok )
    return false;
  hexOut = toHex( hash.finalize() );
  return true;
}

} // namespace

Json::Value LocalIdentityToken::toJson() const
{
  Json::Value json;
  json["token"] = token;
  json["strength"] = strength;
  json["canonical_path"] = canonicalPath;
  json["size_bytes"] = static_cast<Json::UInt64>( sizeBytes );
  json["hashed_bytes"] = static_cast<Json::UInt64>( hashedBytes );
  json["provable"] = provable();
  return json;
}

LocalIdentityToken localIdentityToken( const std::string &path, const LocalIdentityOptions &options )
{
  LocalIdentityToken result;
  if ( path.empty() )
    return result; // unprovable

  const LocalFacts facts = localFacts( fs::u8path( path ) );
  if ( !facts.readable )
    return result; // fail-closed: missing/unreadable ⇒ no identity claim

  result.canonicalPath = canonicalLocalPath( path );
  result.sizeBytes = facts.size;

  std::string contentHex;
  const std::uint64_t budget = options.hashBytes;
  if ( budget > 0 )
  {
    std::uintmax_t hashed = 0;
    if ( !hashFilePrefix( fs::u8path( path ), budget, contentHex, hashed ) )
      return result; // read failure ⇒ unprovable
    result.hashedBytes = hashed;
  }

  // Field-tagged basis: the canonical path is itself hashed so arbitrary
  // path bytes (including newlines) can never forge field boundaries — the
  // basis stays injective.
  const std::string contentField =
    std::string( "content=" ) + ( contentHex.empty() ? "unhashed" : contentHex ) + "\n";
  Sha256 basis;
  basis.update( "li1-basis:v1\n" );
  basis.update( "path=" + sha256Hex( result.canonicalPath ) + "\n" );
  basis.update( "size=" + std::to_string( facts.size ) + "\n" );
  basis.update( "mtime=" + std::to_string( facts.mtimeTicks ) + "\n" );
#ifndef _WIN32
  basis.update( "inode=" + std::to_string( facts.inode ) + "\n" );
#endif
  basis.update( contentField );

  result.token = std::string( "li1:v1:" ) + toHex( basis.finalize() );
  result.strength = contentHex.empty() ? "metadata" : "content";
  return result;
}

namespace
{

AssetIdentity fromLocal( const LocalIdentityToken &local, const std::string &kind )
{
  AssetIdentity identity;
  identity.token = local.token;
  identity.strength = local.strength;
  identity.resourceKind = kind;
  return identity;
}

/// Wraps the 8.0 remote token (fail-closed ETag basis) with honest strength
/// naming: a token exists ONLY on a strong ETag, so presence ⇒ "etag".
AssetIdentity fromRemoteToken( const std::string &token, const std::string &kind )
{
  AssetIdentity identity;
  identity.token = token;
  identity.strength = token.empty() ? std::string() : std::string( "etag" );
  identity.resourceKind = kind;
  return identity;
}

/// Subdataset identity = container identity + the canonical selector. Two
/// variables of one store are two identities; a mutated container invalidates
/// every subdataset identity derived from it.
AssetIdentity qualifiedIdentity( const AssetIdentity &container, const std::string &qualifier,
                                 const std::string &kind )
{
  AssetIdentity identity;
  identity.resourceKind = kind;
  if ( !container.provable() || qualifier.empty() )
    return identity; // unprovable container ⇒ unprovable subdataset
  Sha256 basis;
  basis.update( std::string( "subdataset-basis:v1\n" ) );
  basis.update( container.token + "\n" );
  basis.update( "selector=" + sha256Hex( qualifier ) + "\n" );
  identity.token = std::string( "sd1:v1:" ) + toHex( basis.finalize() );
  identity.strength = container.strength;
  return identity;
}

} // namespace

Json::Value AssetIdentity::toJson() const
{
  Json::Value json;
  json["token"] = token;
  json["strength"] = strength;
  json["resource_kind"] = resourceKind;
  json["provable"] = provable();
  return json;
}

AssetIdentity assetIdentityToken( const std::string &resource, const AssetIdentityOptions &options )
{
  const ResourceUri uri = ResourceUri::parse( resource );

  // Plain local files take the strong local path; the canonical spelling is
  // the identity key. Subdatasets are deliberately NOT routed here (their
  // local payload is a CONTAINER — the selector must join the basis below).
  if ( uri.kind == ResourceKind::LocalFile )
    return fromLocal( localIdentityToken( resource, options.local ), resourceKindName( uri.kind ) );

  switch ( uri.kind )
  {
    case ResourceKind::VsiVirtual:
    {
      // A local container accessed through a virtual handler (zip/tar):
      // identity of the container bytes.
      const std::string localPayload = uri.embeddedLocalPath();
      if ( localPayload.empty() )
        break;
      return fromLocal( localIdentityToken( localPayload, options.local ), resourceKindName( uri.kind ) );
    }
    case ResourceKind::RemoteHttp:
    case ResourceKind::VsiRemote:
    {
      RemoteIdentityTokenOptions remote;
      remote.timeoutSeconds = options.timeoutSeconds;
      remote.connectTimeoutSeconds = options.connectTimeoutSeconds;
      remote.maxRetries = options.maxRetries;
      remote.probeBytes = options.probeBytes;
      const std::string url = uri.kind == ResourceKind::VsiRemote ? uri.remoteUrl() : resource;
      return fromRemoteToken( remoteIdentityToken( url, remote ), resourceKindName( uri.kind ) );
    }
    case ResourceKind::Subdataset:
    {
      // Container: local payload when wrapped locally, else the remote URL.
      AssetIdentity container;
      const std::string localPayload = uri.embeddedLocalPath();
      if ( !localPayload.empty() )
        container = fromLocal( localIdentityToken( localPayload, options.local ), "local_file" );
      else
      {
        const ResourceUri payload = ResourceUri::parse( uri.remoteUrl() );
        if ( payload.kind == ResourceKind::RemoteHttp || payload.kind == ResourceKind::VsiRemote )
        {
          RemoteIdentityTokenOptions remote;
          remote.timeoutSeconds = options.timeoutSeconds;
          remote.connectTimeoutSeconds = options.connectTimeoutSeconds;
          remote.maxRetries = options.maxRetries;
          remote.probeBytes = options.probeBytes;
          container = fromRemoteToken(
            remoteIdentityToken( payload.kind == ResourceKind::VsiRemote ? payload.remoteUrl() : payload.raw,
                                 remote ),
            resourceKindName( payload.kind ) );
        }
      }
      return qualifiedIdentity( container, uri.canonical(), resourceKindName( uri.kind ) );
    }
    default:
      break; // directories, in-memory, stac references, invalid: unprovable
  }

  AssetIdentity unprovable;
  unprovable.resourceKind = uri.kind == ResourceKind::Invalid ? std::string( "invalid" ) : resourceKindName( uri.kind );
  return unprovable;
}

} // namespace sicnu::geo
