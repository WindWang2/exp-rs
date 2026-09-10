/***************************************************************************
  geospatial/remote/remote_identity_token.cpp — fail-closed identity tokens.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/remote/remote_identity_token.h"

#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/sha256.h"

namespace sicnu::geo
{

namespace
{

/// The canonical URL form that carries IDENTITY but not credentials:
/// scheme://host/path[?query]. A query containing ANY credential-shaped key
/// marks the whole query as signing context (presigned URLs are one opaque
/// credential bundle) and is dropped entirely — identity is then the object
/// path, so re-signing the same object yields the SAME token. Non-credential
/// queries (content-selecting parameters like a storage version id) are kept.
std::string identityUrl( const std::string &url )
{
  const ResourceUri uri = ResourceUri::parse( url );
  if ( uri.kind != ResourceKind::RemoteHttp )
    return uri.canonical();
  bool hasCredentialKey = false;
  std::size_t start = 0;
  while ( start < uri.query.size() )
  {
    const std::size_t amp = uri.query.find( '&', start );
    const std::string pair = uri.query.substr(
      start, amp == std::string::npos ? std::string::npos : amp - start );
    const std::size_t eq = pair.find( '=' );
    const std::string key = eq == std::string::npos ? pair : pair.substr( 0, eq );
    if ( isCredentialQueryKey( key ) )
    {
      hasCredentialKey = true;
      break;
    }
    if ( amp == std::string::npos )
      break;
    start = amp + 1;
  }
  const std::string cleanedQuery = hasCredentialKey ? std::string() : uri.query;
  return uri.scheme + "://" + uri.host + uri.path +
         ( cleanedQuery.empty() ? "" : "?" + cleanedQuery );
}

} // namespace

std::string remoteIdentityBasis( const std::string &url,
                                 const RemoteIdentityTokenOptions &options )
{
  RemoteValidatorOptions validatorOptions;
  validatorOptions.timeoutSeconds = options.timeoutSeconds;
  validatorOptions.connectTimeoutSeconds = options.connectTimeoutSeconds;
  validatorOptions.maxRetries = options.maxRetries;
  validatorOptions.probeBytes = options.probeBytes;

  RemoteSourceValidator validator = RemoteSourceValidator::probe( url, validatorOptions );
  const RemoteSourceIdentity &identity = validator.identity();
  // Fail-closed: only a STRONG ETag proves byte-level freshness. Weak ETags
  // prove semantic equality at best; Last-Modified/size prove nothing.
  if ( !identity.validator.hasStrongEtag() )
    return std::string();

  std::string basis = identityUrl( url );
  basis += "\netag=" + identity.validator.etag;
  if ( identity.hasSize )
    basis += "\nsize=" + std::to_string( identity.sizeBytes );
  return basis;
}

std::string remoteIdentityToken( const std::string &url,
                                 const RemoteIdentityTokenOptions &options )
{
  const std::string basis = remoteIdentityBasis( url, options );
  if ( basis.empty() )
    return std::string();
  return std::string( kRemoteIdentityTokenPrefix ) + ":v1:" +
         sha256Hex( basis );
}

} // namespace sicnu::geo
