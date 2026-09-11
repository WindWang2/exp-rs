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

#include <cctype>

namespace sicnu::geo
{

namespace
{

bool queryHasCredentialKey( const std::string &query )
{
  std::size_t start = 0;
  while ( start < query.size() )
  {
    const std::size_t amp = query.find( '&', start );
    const std::string pair = query.substr(
      start, amp == std::string::npos ? std::string::npos : amp - start );
    const std::size_t eq = pair.find( '=' );
    std::string key = eq == std::string::npos ? pair : pair.substr( 0, eq );
    std::string decodedKey;
    if ( percentDecode( key, decodedKey ) || !decodedKey.empty() )
      key = decodedKey; // "?%74oken=…" must not evade the scan
    if ( isCredentialQueryKey( key ) )
      return true;
    if ( amp == std::string::npos )
      break;
    start = amp + 1;
  }
  return false;
}

/// The canonical URL form that carries IDENTITY but not credentials:
/// scheme://host/path[?query] (host lowercased: DNS names are
/// case-insensitive and spelling must not fork identity). A query containing
/// ANY credential-shaped key marks the whole query as signing context
/// (presigned URLs are one opaque credential bundle) and is dropped
/// entirely — identity is then the object path, so re-signing the same
/// object yields the SAME token. Non-credential queries (content-selecting
/// parameters like a storage version id) are kept.
std::string identityUrl( const std::string &url )
{
  ResourceUri uri = ResourceUri::parse( url );
  if ( uri.kind == ResourceKind::VsiRemote && !uri.remoteUrl().empty() )
  {
    // VSI spellings carry the remote URL verbatim in their payload — run the
    // SAME credential scan over the embedded URL instead of keeping the raw
    // spelling (canonical() does not redact).
    uri = ResourceUri::parse( uri.remoteUrl() );
  }
  if ( uri.kind != ResourceKind::RemoteHttp )
    return uri.canonical();
  std::string host = uri.host;
  for ( char &c : host )
    c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
  const std::string cleanedQuery = queryHasCredentialKey( uri.query ) ? std::string() : uri.query;
  return uri.scheme + "://" + host + uri.path +
         ( cleanedQuery.empty() ? "" : "?" + cleanedQuery );
}

/// The identity basis over an ALREADY-CAPTURED identity (no extra probe).
std::string basisFromIdentity( const std::string &url, const RemoteSourceIdentity &identity )
{
  // Fail-closed: only a STRONG ETag proves byte-level freshness. Weak ETags
  // prove semantic equality at best; Last-Modified/size prove nothing. An
  // origin-controlled ETag carrying control characters is refused outright —
  // it could forge basis field boundaries (an embedded newline would let an
  // ETag spoof the size line that follows it).
  if ( !identity.validator.hasStrongEtag() )
    return std::string();
  for ( const char c : identity.validator.etag )
  {
    if ( static_cast<unsigned char>( c ) < 0x20 )
      return std::string();
  }
  std::string basis = identityUrl( url );
  basis += "\netag=" + identity.validator.etag;
  if ( identity.hasSize )
    basis += "\nsize=" + std::to_string( identity.sizeBytes );
  return basis;
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
  return basisFromIdentity( url, validator.identity() );
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

std::string remoteIdentityTokenFromIdentity( const std::string &url,
                                             const RemoteSourceIdentity &identity )
{
  const std::string basis = basisFromIdentity( url, identity );
  if ( basis.empty() )
    return std::string();
  return std::string( kRemoteIdentityTokenPrefix ) + ":v1:" + sha256Hex( basis );
}

} // namespace sicnu::geo
