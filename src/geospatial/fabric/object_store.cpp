/***************************************************************************
  geospatial/fabric/object_store.cpp — object storage profile seam.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/object_store.h"

#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/remote/remote_identity_token.h"
#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/remote/vsi_object_identity.h"
#include "geospatial/util/sha256.h"

#include <cpl_conv.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <vector>

namespace sicnu::geo
{

namespace
{

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

struct ProfileTable
{
  std::mutex mutex;
  std::vector<ObjectStoreProfile> profiles;
};

ProfileTable &profileTable()
{
  static ProfileTable table;
  static std::once_flag initialized;
  std::call_once( initialized, [] {
    // Built-ins (DECISIONS D-1002). Order matters only for lookups of
    // prefixes vs schemes — they never collide.
    table.profiles.push_back( { "s3", "/vsis3/", "aws", true } );
    table.profiles.push_back( { "s3a", "/vsis3/", "s3-compatible", true } );
    table.profiles.push_back( { "s3c", "/vsis3/", "s3-compatible", true } );
    table.profiles.push_back( { "gs", "/vsigs/", "gcs", true } );
    table.profiles.push_back( { "az", "/vsiaz/", "azure", false } );
  } );
  return table;
}

/// Restores exactly the keys this window set (prior value back, or the key
/// removed when nothing was set before us).
void installConfigKey( std::vector<FabricInstalledConfigKey> &installed, const char *key, const std::string &value )
{
  FabricInstalledConfigKey state;
  state.key = key;
  const char *prior = CPLGetConfigOption( key, nullptr );
  state.hadPrior = prior != nullptr;
  if ( state.hadPrior )
    state.priorValue = prior;
  CPLSetConfigOption( key, value.c_str() );
  installed.push_back( std::move( state ) );
}

const char *kS3Anonymous = "AWS_NO_SIGN_REQUEST";   ///< modern spelling (pre-3.x used AWS_NO_SIGNREQUEST)
const char *kS3Endpoint = "AWS_S3_ENDPOINT";
const char *kS3Https = "AWS_HTTPS";
const char *kS3VirtualHosting = "AWS_VIRTUAL_HOSTING";

/// Diagnostic count of open credential windows (RAII balance evidence).
std::atomic<int> gActiveCredentialWindows { 0 };

/// Redacted single-line form for error messages (never raw identity bytes).
std::string displayForm( const std::string &rawUri )
{
  return ResourceUri::parse( rawUri ).display();
}

} // namespace

Json::Value ObjectStoreProfile::toJson() const
{
  Json::Value json;
  json["scheme"] = scheme;
  json["vsiPrefix"] = vsiPrefix;
  json["provider"] = provider;
  json["anonymousSupported"] = anonymousSupported;
  return json;
}

std::vector<ObjectStoreProfile> objectStoreProfiles()
{
  ProfileTable &table = profileTable();
  std::lock_guard<std::mutex> lock( table.mutex );
  return table.profiles;
}

ObjectStoreProfile findObjectStoreProfile( const std::string &scheme )
{
  const std::string wanted = lowerAscii( scheme );
  ProfileTable &table = profileTable();
  std::lock_guard<std::mutex> lock( table.mutex );
  for ( const ObjectStoreProfile &profile : table.profiles )
    if ( profile.scheme == wanted )
      return profile;   // copy under the lock — safe against reallocation
  return ObjectStoreProfile {};
}

void registerObjectStoreProfile( const ObjectStoreProfile &profile )
{
  if ( profile.scheme.empty() || profile.vsiPrefix.empty() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "object store profile needs a scheme and a VSI prefix" );
  if ( profile.vsiPrefix.front() != '/' || profile.vsiPrefix.back() != '/' )
    throw GeoError( ErrorCode::InvalidArgument,
                    "object store VSI prefix must have leading and trailing slashes: " +
                      profile.vsiPrefix );
  ProfileTable &table = profileTable();
  std::lock_guard<std::mutex> lock( table.mutex );
  for ( const ObjectStoreProfile &existing : table.profiles )
    if ( existing.scheme == lowerAscii( profile.scheme ) )
      throw GeoError( ErrorCode::InvalidArgument,
                      "object store scheme already registered: " + existing.scheme );
  ObjectStoreProfile copy = profile;
  copy.scheme = lowerAscii( copy.scheme );
  table.profiles.push_back( std::move( copy ) );
}

bool ObjectStoreCredentials::empty() const
{
  return accessKeyId.empty() && secretAccessKey.empty() && sessionToken.empty() &&
         region.empty() && endpoint.empty() && !anonymous;
}

Json::Value ObjectStoreCredentials::toJsonShape() const
{
  Json::Value json;
  json["hasAccessKeyId"] = !accessKeyId.empty();
  json["hasSecretAccessKey"] = !secretAccessKey.empty();
  json["hasSessionToken"] = !sessionToken.empty();
  json["hasRegion"] = !region.empty();
  json["hasEndpoint"] = !endpoint.empty();
  json["anonymous"] = anonymous;
  return json;
}

Json::Value ObjectStoreResolution::toJson() const
{
  Json::Value json;
  json["raw"] = uri.display();        // redacted form — never the raw identity
  json["scheme"] = profile.scheme;
  json["provider"] = profile.provider;
  json["vsiPath"] = vsiPath;
  json["bucket"] = bucket;
  json["needsCredentials"] = needsCredentials;
  return json;
}

ObjectStoreResolution resolveObjectStore( const std::string &rawUri )
{
  const std::string trimmed = lowerAscii( rawUri );

  // Direct VSI spelling: "/vsis3/bucket/key".
  for ( const ObjectStoreProfile &profile : objectStoreProfiles() )
  {
    std::string prefix = lowerAscii( profile.vsiPrefix );
    if ( trimmed.rfind( prefix, 0 ) == 0 )
    {
      const std::string remainder = rawUri.substr( prefix.size() );
      const std::size_t slash = remainder.find( '/' );
      if ( slash == std::string::npos || slash == 0 )
        throw GeoError( ErrorCode::InvalidArgument,
                        "object store VSI path needs <prefix><bucket>/<key>: " + profile.vsiPrefix );
      ObjectStoreResolution resolution;
      resolution.uri = ResourceUri::parse( rawUri );
      resolution.profile = profile;
      resolution.vsiPath = profile.vsiPrefix + remainder;
      resolution.bucket = remainder.substr( 0, slash );
      resolution.key = remainder.substr( slash + 1 );
      resolution.needsCredentials = !profile.anonymousSupported;
      return resolution;
    }
  }

  // Scheme spelling: "s3://bucket/key" (parsed manually — ResourceUri's
  // classifier only knows http/https as remote schemes; everything else is
  // shaped data for this seam).
  const std::size_t schemeEnd = trimmed.find( "://" );
  if ( schemeEnd == std::string::npos )
    throw GeoError( ErrorCode::InvalidArgument,
                    "not an object-store spelling (no scheme, no VSI prefix): " + displayForm( rawUri ) );
  const std::string scheme = trimmed.substr( 0, schemeEnd );
  const ObjectStoreProfile profile = findObjectStoreProfile( scheme );
  if ( profile.scheme.empty() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "unknown object-store scheme '" + scheme + "': " + displayForm( rawUri ) );

  const std::string authority = rawUri.substr( schemeEnd + 3 );
  if ( authority.find( '@' ) != std::string::npos )
    throw GeoError( ErrorCode::InvalidArgument,
                    "object store URIs never carry credentials (userinfo refused): " +
                      displayForm( rawUri ) );
  const std::size_t slash = authority.find( '/' );
  if ( slash == std::string::npos || slash == 0 )
    throw GeoError( ErrorCode::InvalidArgument,
                    "object store URI needs <scheme>://<bucket>/<key>" );
  // A query/fragment has no meaning on an object path — refuse rather than
  // silently encode one into the key.
  if ( authority.find( '?' ) != std::string::npos || authority.find( '#' ) != std::string::npos )
    throw GeoError( ErrorCode::InvalidArgument,
                    "object store URIs carry no query or fragment: " + displayForm( rawUri ) );

  ObjectStoreResolution resolution;
  resolution.uri = ResourceUri::parse( rawUri );
  resolution.profile = profile;
  resolution.bucket = authority.substr( 0, slash );
  resolution.key = authority.substr( slash + 1 );
  resolution.vsiPath = profile.vsiPrefix + resolution.bucket + "/" + resolution.key;
  resolution.needsCredentials = true;
  return resolution;
}

CanonicalObjectKey canonicalObjectKey( const std::string &resource )
{
  CanonicalObjectKey result;
  const std::string trimmed = lowerAscii( resource );

  // Direct VSI spelling: "/vsis3/bucket/key".
  for ( const ObjectStoreProfile &profile : objectStoreProfiles() )
  {
    const std::string prefix = lowerAscii( profile.vsiPrefix );
    if ( trimmed.rfind( prefix, 0 ) != 0 )
      continue;
    const std::string remainder = resource.substr( prefix.size() );
    const std::size_t slash = remainder.find( '/' );
    if ( slash == std::string::npos || slash == 0 || slash + 1 > remainder.size() )
      return result; // malformed VSI object path (needs <bucket>/<key>)
    // The same refusals as the scheme branch: VSI object keys carry no
    // userinfo/query/fragment shapes (review R13).
    if ( remainder.find( '@' ) != std::string::npos ||
         remainder.find( '?' ) != std::string::npos ||
         remainder.find( '#' ) != std::string::npos )
      return result;
    result.scheme = profile.scheme == "s3a" || profile.scheme == "s3c" ? "s3" : profile.scheme;
    result.provider = profile.provider;
    result.bucket = remainder.substr( 0, slash );
    result.key = remainder.substr( slash + 1 );
    result.canonical = result.scheme + "://" + result.bucket + "/" + result.key;
    result.valid = !result.key.empty();
    return result;
  }

  // Scheme spelling: "s3://bucket/key" (same manual parse discipline as
  // resolveObjectStore — ResourceUri's classifier only knows http/https).
  const std::size_t schemeEnd = trimmed.find( "://" );
  if ( schemeEnd == std::string::npos || schemeEnd == 0 )
    return result;
  const ObjectStoreProfile profile = findObjectStoreProfile( trimmed.substr( 0, schemeEnd ) );
  if ( profile.scheme.empty() )
    return result;
  const std::string authority = resource.substr( schemeEnd + 3 );
  if ( authority.find( '@' ) != std::string::npos || authority.find( '?' ) != std::string::npos ||
       authority.find( '#' ) != std::string::npos )
    return result; // credential/query/fragment shapes are never object keys
  const std::size_t slash = authority.find( '/' );
  if ( slash == std::string::npos || slash == 0 || slash + 1 > authority.size() )
    return result;
  result.scheme = profile.scheme == "s3a" || profile.scheme == "s3c" ? "s3" : profile.scheme;
  result.provider = profile.provider;
  result.bucket = authority.substr( 0, slash );
  result.key = authority.substr( slash + 1 );
  result.canonical = result.scheme + "://" + result.bucket + "/" + result.key;
  result.valid = !result.key.empty();
  return result;
}

bool isObjectStoreVsiPath( const std::string &fetchablePath )
{
  const std::string lowered = lowerAscii( fetchablePath );
  for ( const ObjectStoreProfile &profile : objectStoreProfiles() )
    if ( lowered.rfind( lowerAscii( profile.vsiPrefix ), 0 ) == 0 )
      return true;
  return false;
}

ObjectStoreIdentityFacts probeObjectStoreIdentity( const std::string &resource )
{
  // Accept every object-store spelling: resolve scheme spellings to their
  // VSI form (the profile table is the one mapping authority).
  std::string vsiPath = resource;
  if ( !isObjectStoreVsiPath( vsiPath ) )
  {
    try
    {
      vsiPath = resolveObjectStore( resource ).vsiPath;
    }
    catch ( const GeoError & )
    {
      throw GeoError( ErrorCode::InvalidArgument,
                      "identity probe needs an object-store resource: " +
                        ResourceUri::parse( resource ).display() );
    }
  }
  // The VSI-stack HEAD lives in the transport layer (remote/) — the same
  // probe the range cache uses for object entries; one implementation, no
  // duplicated header parsing.
  const VsiObjectIdentityFacts remoteFacts = probeVsiObjectIdentity( vsiPath );
  ObjectStoreIdentityFacts facts;
  facts.probed = remoteFacts.probed;
  facts.etag = remoteFacts.etag;
  facts.sizeBytes = remoteFacts.sizeBytes;
  facts.hasSize = remoteFacts.hasSize;
  facts.errorText = remoteFacts.errorText;
  return facts;
}

std::string objectStoreIdentityToken( const std::string &canonicalObjectKey,
                                      const ObjectStoreIdentityFacts &facts )
{
  if ( !facts.provable() )
    return std::string();
  // Assemble the 8.0 remote-identity basis verbatim over the canonical
  // object key (remoteIdentityTokenFromIdentity owns the field tagging,
  // the strong-ETag gate and the injectivity rules — no second builder).
  RemoteSourceIdentity identity;
  identity.state = RemoteSourceState::Fresh;
  identity.validator.etag = facts.etag;
  identity.hasSize = facts.hasSize;
  identity.sizeBytes = facts.sizeBytes;
  return remoteIdentityTokenFromIdentity( canonicalObjectKey, identity );
}

AssetIdentity fabricAssetIdentity( const std::string &resource,
                                   const AssetIdentityOptions &options )
{
  const CanonicalObjectKey objectKey = canonicalObjectKey( resource );
  if ( objectKey.valid )
  {
    AssetIdentity identity;
    identity.resourceKind = objectKey.provider;
    identity.token = objectStoreIdentityToken( objectKey.canonical,
                                               probeObjectStoreIdentity( resource ) );
    if ( !identity.token.empty() )
      identity.strength = "etag";
    return identity; // unprovable object stores stay "" — never fall through
  }
  return assetIdentityToken( resource, options );
}

std::string objectStoreCredentialContext( const std::string &vsiPrefix,
                                          const ObjectStoreCredentials &credentials )
{
  // Field-tagged basis over the NON-secret shape only. The secret access
  // key is deliberately absent: the fingerprint separates PRINCIPALS, it
  // never becomes a credential itself.
  Sha256 basis;
  basis.update( "fabric-credctx:v1\n" );
  basis.update( "prefix=" + sha256Hex( vsiPrefix ) + "\n" );
  basis.update( "endpoint=" + sha256Hex( credentials.endpoint ) + "\n" );
  basis.update( "keyid=" + sha256Hex( credentials.accessKeyId ) + "\n" );
  basis.update( std::string( "session=" ) + ( credentials.sessionToken.empty() ? "0" : "1" ) + "\n" );
  basis.update( std::string( "anonymous=" ) + ( credentials.anonymous ? "1" : "0" ) + "\n" );
  // 16 hex chars (64 bits) — collision separation for cache keys, not a
  // security boundary (a principal is free to share its own cache anyway).
  return toHex( basis.finalize() ).substr( 0, 16 );
}

std::string fabricCachedPath( const std::string &fetchablePath )
{
  if ( !RemoteRangeCache::installed() )
    return fetchablePath;
  const ResourceUri uri = ResourceUri::parse( fetchablePath );
  // The cache's contract is http(s) URLs and object-store VSI paths (the
  // handler speaks both: URL requests go through its http fetcher,
  // object-store payloads through the VSI stack under the live credential
  // window — DECISIONS D-1102). Scheme spellings ("s3://b/k") canonicalize
  // to their VSI form first so s3/s3a/s3c converge on ONE cached resource;
  // local paths and unknown spellings are never wrapped.
  if ( uri.kind == ResourceKind::RemoteHttp )
    return RemoteRangeCache::cachedPath( fetchablePath );
  if ( isObjectStoreVsiPath( fetchablePath ) )
    return RemoteRangeCache::cachedPath( fetchablePath );
  try
  {
    const ObjectStoreResolution resolution = resolveObjectStore( fetchablePath );
    return RemoteRangeCache::cachedPath( resolution.vsiPath );
  }
  catch ( const GeoError & )
  {
    return fetchablePath;
  }
}

ScopedObjectStoreCredentials::ScopedObjectStoreCredentials(
  const std::string &vsiPrefix, const ObjectStoreCredentials &credentials )
  : mPrefix( vsiPrefix )
{
  const bool hasPair = !credentials.accessKeyId.empty() && !credentials.secretAccessKey.empty();
  if ( !credentials.anonymous && !hasPair )
    throw GeoError( ErrorCode::InvalidArgument,
                    "object store credentials need accessKeyId + secretAccessKey "
                    "(or anonymous=true)" );

  if ( mPrefix == "/vsis3/" )
  {
    if ( credentials.anonymous )
    {
      // GDAL renamed the key (AWS_NO_SIGNREQUEST → AWS_NO_SIGN_REQUEST);
      // install BOTH spellings so every GDAL build understands the intent.
      installConfigKey( mSetKeys, kS3Anonymous, "YES" );              // modern
      installConfigKey( mSetKeys, "AWS_NO_SIGNREQUEST", "YES" );      // legacy
    }
    else
    {
      installConfigKey( mSetKeys, "AWS_ACCESS_KEY_ID", credentials.accessKeyId );
      installConfigKey( mSetKeys, "AWS_SECRET_ACCESS_KEY", credentials.secretAccessKey );
      if ( !credentials.sessionToken.empty() )
        installConfigKey( mSetKeys, "AWS_SESSION_TOKEN", credentials.sessionToken );
      if ( !credentials.region.empty() )
        installConfigKey( mSetKeys, "AWS_REGION", credentials.region );
    }
    if ( !credentials.endpoint.empty() )
    {
      // Path-style addressing against a custom endpoint (MinIO/loopback
      // shapes). The endpoint is normalized to the bare host[:port] form
      // with the scheme carried explicitly in AWS_HTTPS: GDAL versions
      // disagree on whether AWS_S3_ENDPOINT may itself carry a scheme
      // (builds that prepend their own composed "http://http://…" request
      // URLs), while every build honors the bare form — the seam, not the
      // GDAL build, owns the scheme/host decomposition. A scheme-less
      // endpoint keeps the ambient AWS_HTTPS default (https).
      std::string endpoint = credentials.endpoint;
      const std::string lowered = [ & ] {
        std::string text = endpoint;
        std::transform( text.begin(), text.end(), text.begin(),
                        [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        return text;
      }();
      const bool explicitHttps = lowered.rfind( "https://", 0 ) == 0;
      const bool explicitHttp = lowered.rfind( "http://", 0 ) == 0;
      if ( explicitHttps )
        endpoint = endpoint.substr( 8 );
      else if ( explicitHttp )
        endpoint = endpoint.substr( 7 );
      while ( !endpoint.empty() && endpoint.back() == '/' )
        endpoint.pop_back();
      installConfigKey( mSetKeys, kS3Endpoint, endpoint );
      installConfigKey( mSetKeys, kS3VirtualHosting, "FALSE" );
      if ( explicitHttp )
        installConfigKey( mSetKeys, kS3Https, "NO" );
      else if ( explicitHttps )
        installConfigKey( mSetKeys, kS3Https, "YES" );
    }
  }
  else if ( mPrefix == "/vsiaz/" )
  {
    if ( credentials.anonymous )
      throw GeoError( ErrorCode::Unsupported,
                      "anonymous azure blob access has no GDAL config form" );
    if ( !credentials.endpoint.empty() )
      throw GeoError( ErrorCode::Unsupported,
                      "custom azure endpoints are not wired in this track" );
    installConfigKey( mSetKeys, "AZURE_STORAGE_ACCOUNT", credentials.accessKeyId );
    if ( !credentials.sessionToken.empty() )
    {
      // A SAS token replaces the account key when present.
      installConfigKey( mSetKeys, "AZURE_STORAGE_SAS_TOKEN", credentials.sessionToken );
    }
    else
    {
      installConfigKey( mSetKeys, "AZURE_STORAGE_ACCESS_KEY", credentials.secretAccessKey );
    }
  }
  else if ( mPrefix == "/vsigs/" )
  {
    if ( !credentials.anonymous )
      throw GeoError( ErrorCode::Unsupported,
                      "gcs credential injection is service-account JSON based — "
                      "not wired in this track (anonymous public buckets only)" );
    installConfigKey( mSetKeys, "GS_NO_SIGN_REQUEST", "YES" );
    installConfigKey( mSetKeys, "GS_NO_SIGNREQUEST", "YES" );   // legacy spelling
  }
  else
  {
    throw GeoError( ErrorCode::InvalidArgument,
                    "not a known network VSI prefix for credentials: " + mPrefix );
  }

  gActiveCredentialWindows.fetch_add( 1 );
  // 11.0 (D-1102): entries created while this window is open carry its
  // principal fingerprint in their cache key — blocks fetched under one
  // account are never served to another. The fingerprint is a non-secret
  // shape hash; nothing credential-shaped ever leaves this call.
  mOwnContext = objectStoreCredentialContext( vsiPrefix, credentials );
  mPriorContext = currentRangeCacheCredentialContext();
  setRangeCacheCredentialContext( mOwnContext );
}

ScopedObjectStoreCredentials::~ScopedObjectStoreCredentials()
{
  for ( auto it = mSetKeys.rbegin(); it != mSetKeys.rend(); ++it )
  {
    if ( it->hadPrior )
      CPLSetConfigOption( it->key.c_str(), it->priorValue.c_str() );
    else
      CPLSetConfigOption( it->key.c_str(), nullptr );
  }
  // GDAL caches authenticated handles per URL (the VSICURL property cache):
  // without this wipe, a LATER window for the same URL would silently reuse
  // the PREVIOUS window's signed context — credentials lingering past their
  // scope. The cache is GDAL-side metadata only; our /vsirangecache/ blocks
  // live in a separate handler and are separated per principal by the
  // credential-context key component (D-1102), never shared across windows.
  if ( !mSetKeys.empty() )
    VSICurlClearCache();
  // Drop OUR fingerprint only (nested/overlapping windows are outside the
  // D-1003 contract, but clearing unconditionally would yank a live
  // sibling's context out from under it). Exact restore, like the config
  // journal above: a still-open OUTER window gets its own fingerprint back —
  // resetting to empty would re-key the outer principal's fetches into the
  // shared context-less keyspace, the cross-principal merge D-1102 forbids.
  // When this is the LAST window there is no sibling left to hand anything
  // to: restoring a (possibly already-closed) prior principal's fingerprint
  // would pin it into the process-global context for every later unwindowed
  // fetch — also a cross-context merge — so the at-rest answer is empty.
  const std::string current = currentRangeCacheCredentialContext();
  if ( !mOwnContext.empty() && current == mOwnContext )
    setRangeCacheCredentialContext( gActiveCredentialWindows.load() == 1
                                      ? std::string()
                                      : mPriorContext );
  gActiveCredentialWindows.fetch_sub( 1 );
}

int activeScopedCredentialWindows() { return gActiveCredentialWindows.load(); }

bool fabricOfflineRefusal( const std::string &fetchablePath, std::string &messageOut )
{
  if ( !offline::enabled() || !offline::isRemoteTarget( fetchablePath ) )
    return false;
  messageOut = offline::refusalMessage( fetchablePath );
  return true;
}

} // namespace sicnu::geo
