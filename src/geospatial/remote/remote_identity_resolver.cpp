// remote_identity_resolver.cpp — see remote_identity_resolver.h for the contract.
//
// Locking discipline (adversarial-review P0 fix): the session-cache mutex
// guards ONLY the map — it is NEVER held across probe/revalidate network
// I/O. Network work happens on the caller's thread with no lock; results
// are published under the lock afterwards. The installing layer (TaskCenter)
// warms the cache on the submitting thread BEFORE taking the scheduler
// mutex, so a consult that must not block can rely on a recent entry (see
// the TTL below).
#include "remote_identity_resolver.h"

#include "remote_source_validator.h"

#include <chrono>
#include <cstdlib>
#include <list>
#include <mutex>
#include <string>

namespace sicnu::geo
{
namespace
{
/// Bounded insertion-order session cache: request URL → confirmed strong
/// ETag + last network check. Bounded so a very long session cannot grow it
/// without limit; the oldest entry is evicted when the cap is hit (it will
/// simply be re-probed on the next use — a performance bound, never a
/// correctness one).
constexpr size_t kSessionCacheCapacity = 256;

/// Revalidation TTL: an entry younger than this is returned WITHOUT a
/// network round trip. This is what makes the "warm before the scheduler
/// lock, cheap consult under it" pattern work, and it rate-limits probes of
/// a slow origin. Configurable via SICNU_REMOTE_IDENTITY_TTL_MS (0 = always
/// revalidate — maximum freshness, maximum network). The short stale-window
/// is a documented tradeoff: the ETag is IDENTITY (which execution a cache
/// entry belongs to), not proof — serving still validates bytes.
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
    std::string etag;
    std::chrono::steady_clock::time_point lastChecked{};
};

struct SessionCache
{
    std::mutex mutex;
    std::list<std::pair<std::string, SessionEntry>> entries; // front = most recent

    bool find( const std::string &url, SessionEntry *out ) const
    {
        for ( const auto &entry : entries )
        {
            if ( entry.first == url )
            {
                *out = entry.second;
                return true;
            }
        }
        return false;
    }
    void store( const std::string &url, const std::string &etag )
    {
        for ( auto it = entries.begin(); it != entries.end(); ++it )
        {
            if ( it->first == url )
            {
                entries.erase( it );
                break;
            }
        }
        entries.push_front( { url, SessionEntry{ etag, std::chrono::steady_clock::now() } } );
        if ( entries.size() > kSessionCacheCapacity )
            entries.pop_back();
    }
    void drop( const std::string &url )
    {
        for ( auto it = entries.begin(); it != entries.end(); ++it )
        {
            if ( it->first == url )
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

/// Extracts the strong ETag of a probed identity, or an empty string when
/// the identity is not strong-ETag-fresh (weak / absent / offline).
std::string strongEtagStd( const RemoteSourceIdentity &identity )
{
    if ( identity.state != RemoteSourceState::Fresh )
        return {};
    if ( !identity.validator.hasStrongEtag() )
        return {};
    return identity.validator.etag;
}

/// Bounded probe options: the identity probe must never dominate a
/// submission; one retry max, seconds-scale budgets (validator defaults).
RemoteValidatorOptions probeOptions()
{
    RemoteValidatorOptions options;
    options.timeoutSeconds = 10;
    options.connectTimeoutSeconds = 5;
    options.maxRetries = 1;
    return options;
}

/// Network probe of @p url, publish the strong ETag (or drop the entry),
/// return the verdict token. Caller must NOT hold the cache mutex.
std::string probeAndStore( SessionCache &cache, const std::string &url )
{
    auto validator = RemoteSourceValidator::probe( url, probeOptions() );
    const std::string etag = strongEtagStd( validator.identity() );
    std::lock_guard<std::mutex> lock( cache.mutex );
    if ( etag.empty() )
        cache.drop( url );
    else
        cache.store( url, etag );
    return etag;
}
} // namespace

std::function<std::string( const std::string &path )> makeRemoteInputIdentityResolver()
{
    return []( const std::string &url ) -> std::string {
        // Only remote http(s) resources resolve here; the collector keeps
        // its local/VSI handling.
        if ( !RemoteSourceValidator::isRemoteUrl( url ) )
            return {};

        SessionCache &cache = sessionCache();
        SessionEntry entry;
        {
            std::lock_guard<std::mutex> lock( cache.mutex );
            if ( !cache.find( url, &entry ) )
                return probeAndStore( cache, url ); // first sight: probe unlocked
        }

        const auto age = std::chrono::steady_clock::now() - entry.lastChecked;
        if ( age < revalidateTtl() )
            return entry.etag; // recent knowledge: no network under any lock

        // Revalidate unlocked (conditional GET against the stored validators).
        auto validator = RemoteSourceValidator::fromIdentity(
            [&] {
                RemoteSourceIdentity identity;
                identity.url = url; // validator redacts for reports
                identity.state = RemoteSourceState::Fresh;
                identity.validator.etag = entry.etag;
                return identity;
            }(),
            url );
        const auto result = validator.revalidate( probeOptions() );
        if ( result.outcome == RevalidationOutcome::Unchanged )
        {
            std::lock_guard<std::mutex> lock( cache.mutex );
            cache.store( url, entry.etag ); // refresh lastChecked
            return entry.etag;
        }
        if ( result.outcome == RevalidationOutcome::Changed )
        {
            // Content changed: a fresh probe captures the NEW strong ETag —
            // the identity rotates, which is the entire invalidation story.
            return probeAndStore( cache, url );
        }
        // Inconclusive (offline / transport error): no proof ⇒ no identity
        // this submission (the input is uncacheable). Keep the entry but do
        // not vouch for it.
        return {};
    };
}

} // namespace sicnu::geo
