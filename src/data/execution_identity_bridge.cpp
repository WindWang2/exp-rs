// execution_identity_bridge.cpp — geospatial-backed input identity resolver.
#include "execution_identity_bridge.h"

#include "execution_identity_resolver.h"

#include "geospatial/remote/remote_identity_token.h"

#include <chrono>
#include <cstdlib>
#include <list>
#include <mutex>
#include <string>
#include <utility>

namespace sicnu::data
{

namespace
{

/// Bounded insertion-order session cache: canonical path → identity token +
/// last network confirmation. Mirrors the TTL discipline of the geospatial
/// resolver (remote_identity_resolver.cpp — the TaskCenter-default when no
/// host installs this bridge): the cache mutex guards ONLY the map and is
/// NEVER held across probe network I/O; results are published under the lock
/// afterwards.
///
/// This is what makes TaskCenter::warmExecutionIdentityCache effective for
/// the SHIPPED bridge (review P1): the warm-up pass performs the (blocking)
/// probe lock-free on the submitting thread BEFORE TaskCenter::m_mutex is
/// taken, and the collector's consult — which used to run under the
/// scheduler mutex — then hits a fresh entry: pure bookkeeping, no network
/// under any lock. Before this cache the bridge re-probed on EVERY consult,
/// so a warm pass was wasted work and fingerprint-enabled submissions still
/// stalled the whole scheduler behind the mutex.
constexpr size_t kSessionCacheCapacity = 256;

/// Revalidation TTL: an entry younger than this is returned WITHOUT a
/// network round trip. Shared with the geospatial resolver via
/// SICNU_REMOTE_IDENTITY_TTL_MS (0 = always revalidate). The short
/// stale-window is the same documented tradeoff: the token is IDENTITY
/// (which execution a cache entry belongs to), not proof — serving still
/// validates bytes.
std::chrono::milliseconds revalidateTtl()
{
  static const std::chrono::milliseconds ttl = [] {
    const char *raw = std::getenv( "SICNU_REMOTE_IDENTITY_TTL_MS" );
    if ( !raw )
      return std::chrono::milliseconds( 5000 );
    const long long parsed = std::atoll( raw );
    return parsed > 0 ? std::chrono::milliseconds( parsed )
                      : std::chrono::milliseconds( 0 );
  }();
  return ttl;
}

struct SessionEntry
{
  QString token;
  std::chrono::steady_clock::time_point lastChecked{};
};

struct SessionCache
{
  std::mutex mutex;
  std::list<std::pair<QString, SessionEntry>> entries; // front = most recent

  bool find( const QString &path, SessionEntry *out )
  {
    for ( auto it = entries.begin(); it != entries.end(); ++it )
    {
      if ( it->first == path )
      {
        *out = it->second;
        entries.splice( entries.begin(), entries, it ); // touch recency
        return true;
      }
    }
    return false;
  }
  void store( const QString &path, const QString &token )
  {
    for ( auto it = entries.begin(); it != entries.end(); ++it )
    {
      if ( it->first == path )
      {
        entries.erase( it );
        break;
      }
    }
    entries.push_front( { path, SessionEntry{ token, std::chrono::steady_clock::now() } } );
    if ( entries.size() > kSessionCacheCapacity )
      entries.pop_back();
  }
  void drop( const QString &path )
  {
    for ( auto it = entries.begin(); it != entries.end(); ++it )
    {
      if ( it->first == path )
      {
        entries.erase( it );
        return;
      }
    }
  }
};

SessionCache &sessionCache()
{
  static SessionCache cache;
  return cache;
}

/// Network probe of @p canonicalPath, publish the verdict under the cache
/// lock, return the token. Caller must NOT hold the cache mutex.
QString probeAndStore( const QString &canonicalPath )
{
  QString token;
  try
  {
    // Shortened probe budget: fingerprinting runs on the submission path and
    // may cover several inputs; a dead origin must stall admission briefly,
    // not for the full interactive budget. (Remote inputs are usually
    // registered and never reach the resolver — this is the fallback path.)
    sicnu::geo::RemoteIdentityTokenOptions options;
    options.timeoutSeconds = 5;
    options.connectTimeoutSeconds = 3;
    options.maxRetries = 0;
    const std::string raw = sicnu::geo::remoteIdentityToken( canonicalPath.toStdString(), options );
    token = raw.empty() ? QString() : QString::fromStdString( raw );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    token = QString();
  }
  {
    std::lock_guard<std::mutex> lock( sessionCache().mutex );
    if ( token.isEmpty() )
      sessionCache().drop( canonicalPath ); // no identity ⇒ no vouch; re-probe next time
    else
      sessionCache().store( canonicalPath, token );
  }
  return token;
}

/// QString seam → TTL-cached geospatial token. Empty in ⇒ empty out; ANY
/// probe failure (offline, no strong validator, non-remote spelling such as
/// /vsimem/ or OGR connection strings — the probe throws InvalidArgument for
/// those) folds into "" = uncacheable. The admission path must never see an
/// exception from identity work. An empty verdict is never cached (failures
/// stay uncached, successes vouched), so behavior off the TTL window is
/// identical to the uncached resolver.
QString cachedRemoteIdentity( const QString &canonicalPath )
{
  if ( canonicalPath.isEmpty() )
    return QString();
  SessionEntry entry;
  {
    std::lock_guard<std::mutex> lock( sessionCache().mutex );
    if ( !sessionCache().find( canonicalPath, &entry ) )
      return probeAndStore( canonicalPath ); // first sight: probe unlocked
  }
  const auto age = std::chrono::steady_clock::now() - entry.lastChecked;
  if ( age < revalidateTtl() )
    return entry.token; // recent knowledge: no network under any lock
  return probeAndStore( canonicalPath ); // TTL expired: re-probe — the probe
                                         // is the identity proof, and strong
                                         // ETag rotation is the invalidation
                                         // story
}

} // namespace

InputIdentityResolver *installGeospatialInputIdentityResolver()
{
  return setExecutionIdentityResolver( InputIdentityResolver( cachedRemoteIdentity ) );
}

} // namespace sicnu::data
