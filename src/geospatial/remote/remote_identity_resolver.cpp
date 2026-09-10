// remote_identity_resolver.cpp — see remote_identity_resolver.h for the contract.
#include "remote_identity_resolver.h"

#include "remote_source_validator.h"

#include <list>
#include <mutex>
#include <string>

namespace sicnu::geo
{
namespace
{
/// Bounded insertion-order session cache: request URL → confirmed strong
/// ETag. Bounded so a very long session cannot grow it without limit; the
/// oldest entry is evicted when the cap is hit (it will simply be re-probed
/// on the next use — a performance bound, never a correctness one).
constexpr size_t kSessionCacheCapacity = 256;

struct SessionCache
{
    std::mutex mutex;
    std::list<std::pair<std::string, std::string>> entries; // front = most recent
    bool contains( const std::string &url, std::string *etagOut ) const
    {
        for ( const auto &entry : entries )
        {
            if ( entry.first == url )
            {
                *etagOut = entry.second;
                return true;
            }
        }
        return false;
    }
    void touch( const std::string &url, const std::string &etag )
    {
        for ( auto it = entries.begin(); it != entries.end(); ++it )
        {
            if ( it->first == url )
            {
                entries.erase( it );
                break;
            }
        }
        entries.push_front( { url, etag } );
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
} // namespace

std::function<std::string( const std::string &path )> makeRemoteInputIdentityResolver()
{
    return []( const std::string &url ) -> std::string {
        // Only remote http(s) resources resolve here; the collector keeps
        // its local/VSI handling.
        if ( !RemoteSourceValidator::isRemoteUrl( url ) )
            return {};

        SessionCache &cache = sessionCache();
        {
            std::lock_guard<std::mutex> lock( cache.mutex );
            std::string cachedEtag;
            if ( cache.contains( url, &cachedEtag ) )
            {
                // Revalidate the cached identity against the origin: an
                // Unchanged strong validator keeps the token; anything else
                // re-probes below (Changed captures the new identity,
                // Inconclusive fails closed).
                auto validator = RemoteSourceValidator::fromIdentity(
                    [&] {
                        RemoteSourceIdentity identity;
                        identity.url = url; // validator redacts for reports
                        identity.state = RemoteSourceState::Fresh;
                        identity.validator.etag = cachedEtag;
                        return identity;
                    }(),
                    url );
                const auto result = validator.revalidate( probeOptions() );
                if ( result.outcome == RevalidationOutcome::Unchanged )
                    return cachedEtag;
                if ( result.outcome == RevalidationOutcome::Changed )
                {
                    // Content changed: the NEW validator set is the identity
                    // source. A follow-up probe captures the fresh state.
                    auto refreshed = validator.refresh( probeOptions() );
                    const std::string etag = strongEtagStd( refreshed.identity() );
                    std::lock_guard<std::mutex> relock( cache.mutex );
                    if ( etag.empty() )
                        cache.drop( url );
                    else
                        cache.touch( url, etag );
                    return etag;
                }
                // Inconclusive (offline / transport error): no proof ⇒ no
                // identity this submission (the input is uncacheable).
                return {};
            }
        }

        // First sight in this session: probe and require a strong ETag.
        auto validator = RemoteSourceValidator::probe( url, probeOptions() );
        const std::string etag = strongEtagStd( validator.identity() );
        if ( !etag.empty() )
        {
            std::lock_guard<std::mutex> lock( cache.mutex );
            cache.touch( url, etag );
        }
        return etag;
    };
}

} // namespace sicnu::geo
