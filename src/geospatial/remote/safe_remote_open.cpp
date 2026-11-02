// safe_remote_open.cpp — see safe_remote_open.h for the fail-closed chain.
#include "safe_remote_open.h"

#include "offline_gate.h"
#include "range_cache.h"

#include "util/resource_uri.h"

#include <cstdint>

namespace sicnu::geo::remote
{

namespace
{
constexpr char kVsicurlPrefix[] = "/vsicurl/";
} // namespace

const char *safeOpenStatusName( SafeOpenStatus status )
{
  switch ( status )
  {
    case SafeOpenStatus::Ok:
      return "ok";
    case SafeOpenStatus::SchemeRefused:
      return "scheme-refused";
    case SafeOpenStatus::OfflineRefused:
      return "offline-refused";
    case SafeOpenStatus::PrivateTarget:
      return "private-target";
    case SafeOpenStatus::CacheUnavailable:
      return "cache-unavailable";
  }
  return "unknown";
}

const char *SafeRemotePathResult::code() const
{
  switch ( status )
  {
    case SafeOpenStatus::Ok:
      return "remote.open.ok";
    case SafeOpenStatus::SchemeRefused:
      return "remote.open.scheme_refused";
    case SafeOpenStatus::OfflineRefused:
      return "remote.open.offline_refused";
    case SafeOpenStatus::PrivateTarget:
      return "remote.open.private_target";
    case SafeOpenStatus::CacheUnavailable:
      return "remote.open.cache_unavailable";
  }
  return "remote.open.unknown";
}

SafeRemotePathResult safeRemoteRasterPath( const std::string &urlOrVsicurl )
{
  SafeRemotePathResult result;

  // Accept both the bare URL and the legacy /vsicurl/ spelling so callers
  // migrating off raw opens keep working (the STAC dialog hands the
  // vsicurl-wrapped form today).
  std::string url = urlOrVsicurl;
  if ( url.rfind( kVsicurlPrefix, 0 ) == 0 )
    url = url.substr( sizeof( kVsicurlPrefix ) - 1 );

  // 1. Scheme gate: only http(s) remote URLs ride the guarded transport.
  const ResourceUri uri = ResourceUri::parse( url );
  if ( uri.kind != ResourceKind::RemoteHttp )
  {
    result.message = "safe remote open: not an http(s) URL ("
                     + std::string( resourceKindName( uri.kind ) ) + "): " + url;
    return result;
  }

  // 2. Offline gate: refuse before any transport could open a socket.
  if ( offline::enabled() && offline::isRemoteTarget( url ) )
  {
    result.status = SafeOpenStatus::OfflineRefused;
    result.message = offline::refusalMessage( url );
    return result;
  }

  // 3. Private-network deny-by-default: literal loopback/RFC1918/link-local/
  //    cloud-metadata (and their well-known names) never reach a transport.
  //    This check is DNS-free — the refusal itself touches no network.
  if ( offline::isPrivateHost( url ) )
  {
    result.status = SafeOpenStatus::PrivateTarget;
    result.message = "safe remote open: private/loopback/cloud-metadata target "
                     "refused: "
                     + url;
    return result;
  }

  // 4. Guarded transport gate: the range cache's VSI handler owns the actual
  //    fetches (per-hop egress policy, redirect guard, DNS pin). Without it
  //    there is NO guarded path — refuse rather than degrade to raw
  //    /vsicurl/ (the exact bypass this API exists to close).
  if ( !RemoteRangeCache::installed() )
  {
    result.status = SafeOpenStatus::CacheUnavailable;
    result.message = "safe remote open: remote range cache is not installed — "
                     "refusing to open unguarded: "
                     + url;
    return result;
  }

  result.path = RemoteRangeCache::cachedPath( url );
  result.status = SafeOpenStatus::Ok;
  return result;
}

} // namespace sicnu::geo::remote
