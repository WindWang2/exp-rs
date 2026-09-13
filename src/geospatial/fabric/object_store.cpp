/***************************************************************************
  geospatial/fabric/object_store.cpp — object storage profile seam.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/object_store.h"

#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"

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

const char *kS3Anonymous = "AWS_NO_SIGNREQUEST";
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

const ObjectStoreProfile *findObjectStoreProfile( const std::string &scheme )
{
  const std::string wanted = lowerAscii( scheme );
  ProfileTable &table = profileTable();
  std::lock_guard<std::mutex> lock( table.mutex );
  for ( const ObjectStoreProfile &profile : table.profiles )
    if ( profile.scheme == wanted )
      return &table.profiles[static_cast<std::size_t>( &profile - table.profiles.data() )];
  return nullptr;
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
  const ObjectStoreProfile *profile = findObjectStoreProfile( scheme );
  if ( !profile )
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
  resolution.profile = *profile;
  resolution.bucket = authority.substr( 0, slash );
  resolution.key = authority.substr( slash + 1 );
  resolution.vsiPath = profile->vsiPrefix + resolution.bucket + "/" + resolution.key;
  resolution.needsCredentials = true;
  return resolution;
}

std::string fabricCachedPath( const std::string &fetchablePath )
{
  if ( !RemoteRangeCache::installed() )
    return fetchablePath;
  return RemoteRangeCache::cachedPath( fetchablePath );
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
      installConfigKey( mSetKeys, kS3Anonymous, "YES" );
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
      // shapes); https endpoints keep the default AWS_HTTPS.
      installConfigKey( mSetKeys, kS3Endpoint, credentials.endpoint );
      installConfigKey( mSetKeys, kS3VirtualHosting, "FALSE" );
      if ( credentials.endpoint.rfind( "http://", 0 ) == 0 )
        installConfigKey( mSetKeys, kS3Https, "NO" );
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
    installConfigKey( mSetKeys, "GS_NO_SIGNREQUEST", "YES" );
  }
  else
  {
    throw GeoError( ErrorCode::InvalidArgument,
                    "not a known network VSI prefix for credentials: " + mPrefix );
  }

  gActiveCredentialWindows.fetch_add( 1 );
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
