// remote_source_cache.cpp — see remote_source_cache.h for the contract.
#include "remote_source_cache.h"
#include "data/offline_mode.h"

#include <QDateTime>
#include <QDir>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <thread>
#include <vector>

#include "cpl_port.h"
#include "gdal_priv.h"

namespace sicnu::data
{
namespace
{
/// Sets a GDAL config option only when the user has not set it themselves.
void setDefaultConfig( const char *key, const char *value )
{
    if ( !CPLGetConfigOption( key, nullptr ) )
        CPLSetConfigOption( key, value );
}
} // namespace

void configureRemoteCachingDefaults()
{
    static const bool applied = [] {
        // /vsicurl/ block cache: the process-wide cache GDAL keeps for remote
        // range reads. Bounded; default 128 MiB when untuned.
        setDefaultConfig( "CPL_VSIL_CURL_CACHE_SIZE", "134217728" );
        // Per-handle in-memory block cache for VSI reads.
        setDefaultConfig( "VSI_CACHE", "TRUE" );
        setDefaultConfig( "VSI_CACHE_SIZE", "10485760" );
        // Retry policy for transient remote failures (bounded, with backoff).
        setDefaultConfig( "GDAL_HTTP_MAX_RETRY", "3" );
        setDefaultConfig( "GDAL_HTTP_RETRY_DELAY", "1" );
        setDefaultConfig( "GDAL_HTTP_CONNECT_TIMEOUT", "10" );
        setDefaultConfig( "GDAL_HTTP_TIMEOUT", "30" );
        return true;
    }();
    Q_UNUSED( applied );
}

// ---------------------------------------------------------------------------
// RemoteDatasetPool
// ---------------------------------------------------------------------------

struct RemoteDatasetPool::Impl
{
    /// Reads the per-URL bound once, before the Impl is published to any
    /// other thread, so no reader can observe a half-configured pool.
    static size_t configuredHandlesPerUrl()
    {
        const char *env = std::getenv( "SICNU_REMOTE_POOL_HANDLES" );
        if ( !env )
            return 2;
        const long parsed = std::strtol( env, nullptr, 10 );
        return static_cast<size_t>( std::clamp<long>( parsed, 1, 8 ) );
    }

    const size_t handlesPerUrl = configuredHandlesPerUrl();
    std::mutex mutex;
    std::condition_variable cv;
    std::map<QString, std::vector<std::shared_ptr<PooledRemoteHandle>>> handles;
    /// Per-URL GDALOpen calls in flight (reserved before releasing the pool
    /// mutex for network I/O, so the per-URL bound can never be overshot).
    std::map<QString, size_t> opening;
};

RemoteDatasetPool &RemoteDatasetPool::instance()
{
    static RemoteDatasetPool pool;
    return pool;
}

RemoteDatasetPool::Impl &RemoteDatasetPool::impl()
{
    // Function-local static: initialized exactly once, thread-safely; every
    // later call synchronizes-with the completed construction. This replaces
    // the racy `if ( !m_impl ) m_impl = new Impl;` pattern (#1047): no double
    // allocation, no leak, no divergent mutex domains.
    static Impl pool;
    return pool;
}

RemoteDatasetLease::RemoteDatasetLease( std::shared_ptr<PooledRemoteHandle> handle,
                                        std::unique_lock<std::mutex> &&lock )
    : m_handle( std::move( handle ) ), m_lock( std::move( lock ) )
{
}

RemoteDatasetLease::~RemoteDatasetLease() = default;

RemoteDatasetLease::RemoteDatasetLease( RemoteDatasetLease &&other ) noexcept
    : m_handle( std::move( other.m_handle ) ),
      m_lock( std::move( other.m_lock ) )
{
}

RemoteDatasetLease &RemoteDatasetLease::operator=( RemoteDatasetLease &&other ) noexcept
{
    if ( this != &other )
    {
        if ( m_lock )
            m_lock->unlock();
        m_lock.reset();
        m_handle = std::move( other.m_handle );
        m_lock = std::move( other.m_lock );
    }
    return *this;
}

RemoteDatasetLease RemoteDatasetPool::acquire( const QString &url, unsigned int oflag )
{
    // Offline gate (goal D7): refuse before GDALOpenEx can touch the network.
    // The caller sees the same empty lease as a failed open; the typed
    // refusal surfaces from the resolve layer's diagnostic.
    if ( offline::enabled() && offline::isRemoteTarget( url ) )
        return RemoteDatasetLease{};

    configureRemoteCachingDefaults();

    Impl &pool = impl();
    std::unique_lock<std::mutex> lock( pool.mutex );
    while ( true )
    {
        std::vector<std::shared_ptr<PooledRemoteHandle>> &bucket = pool.handles[url];
        for ( auto &handle : bucket )
        {
            std::unique_lock<std::mutex> handleLock( handle->mutex, std::try_to_lock );
            if ( handleLock.owns_lock() )
                return RemoteDatasetLease( handle, std::move( handleLock ) );
        }
        const size_t inFlight = pool.opening[url];
        if ( bucket.size() + inFlight < pool.handlesPerUrl )
        {
            // Reserve a slot, then open OUTSIDE the pool mutex: GDALOpen on
            // /vsicurl/ does network I/O (bounded by the configured connect/
            // transfer timeouts), so never hold the global lock across it.
            pool.opening[url] = inFlight + 1;
            // Everything that can allocate (the handle and the URL's UTF-8
            // form) runs while the pool mutex is still held: a throwing
            // allocation must never escape with the reservation dangling,
            // because a stuck reservation silently lowers the per-URL bound
            // and wedges clear()'s in-flight drain.
            auto handle = std::make_shared<PooledRemoteHandle>();
            handle->url = url;
            const QByteArray utf8 = url.toUtf8();
            lock.unlock();
            GDALDatasetH dataset = nullptr;
            try
            {
                dataset = GDALOpenEx( utf8.constData(), oflag, nullptr,
                                      nullptr, nullptr );
            }
            catch ( ... )
            {
                // Restore the pool state before propagating: GDAL normally
                // reports failures as a null handle, but an exception cannot
                // be allowed to leak the reservation.
                lock.lock();
                pool.opening[url] -= 1;
                if ( pool.opening[url] == 0 )
                    pool.opening.erase( url );
                if ( pool.handles[url].empty() )
                    pool.handles.erase( url );
                throw;
            }
            m_openCount.fetch_add( 1, std::memory_order_relaxed );
            lock.lock();
            pool.opening[url] -= 1;
            if ( !dataset )
            {
                // #1097: operator[] left empty map nodes for every failed URL;
                // drop idle buckets so STAC churn cannot grow unbounded.
                if ( pool.opening[url] == 0 )
                    pool.opening.erase( url );
                if ( pool.handles[url].empty() )
                    pool.handles.erase( url );
                return RemoteDatasetLease{};
            }
            handle->dataset = dataset;
            try
            {
                pool.handles[url].push_back( handle );
            }
            catch ( ... )
            {
                // Nothing references the open dataset once the shared_ptr
                // goes out of scope here, so close it before rethrowing.
                GDALClose( dataset );
                throw;
            }
            // #1097: bound distinct URL keys; evict idle (no in-flight, all
            // handles unlocked) entries when the map grows too large.
            constexpr size_t kMaxUrlEntries = 512;
            while ( pool.handles.size() > kMaxUrlEntries )
            {
                bool evicted = false;
                for ( auto it = pool.handles.begin(); it != pool.handles.end(); )
                {
                    if ( it->first == url )
                    {
                        ++it;
                        continue;
                    }
                    const auto openIt = pool.opening.find( it->first );
                    const size_t inflight = openIt == pool.opening.end() ? 0 : openIt->second;
                    if ( inflight != 0 )
                    {
                        ++it;
                        continue;
                    }
                    bool busy = false;
                    for ( auto &h : it->second )
                    {
                        std::unique_lock<std::mutex> hl( h->mutex, std::try_to_lock );
                        if ( !hl.owns_lock() )
                        {
                            busy = true;
                            break;
                        }
                        if ( h->dataset )
                        {
                            GDALClose( h->dataset );
                            h->dataset = nullptr;
                        }
                    }
                    if ( busy )
                    {
                        ++it;
                        continue;
                    }
                    pool.opening.erase( it->first );
                    it = pool.handles.erase( it );
                    evicted = true;
                    break;
                }
                if ( !evicted )
                    break;
            }
            std::unique_lock<std::mutex> handleLock( handle->mutex );
            return RemoteDatasetLease( handle, std::move( handleLock ) );
        }
        // All handles busy and the bound reached: wait for a return. Leases
        // do not know the pool, so wake on a short poll — checkout cost is
        // dominated by remote I/O anyway.
        pool.cv.wait_for( lock, std::chrono::milliseconds( 20 ) );
    }
}

void RemoteDatasetPool::clear()
{
    Impl &pool = impl();
    std::unique_lock<std::mutex> lock( pool.mutex );
    // Wait until no open is in flight and all leases have returned (every
    // handle must be lockable), then close. Retry loop keeps it simple and
    // bounded by lease lifetimes / the configured GDAL timeouts.
    auto noOpenInFlight = []( const std::map<QString, size_t> &opening ) {
        // Entries can stay in the map with a zero count (acquire reserves via
        // operator[]), so emptiness is not a valid in-flight predicate.
        return std::all_of( opening.begin(), opening.end(),
                            []( const auto &slot ) { return slot.second == 0; } );
    };
    bool allFree = false;
    while ( !allFree )
    {
        allFree = noOpenInFlight( pool.opening );
        if ( allFree )
        {
            for ( const auto &[url, bucket] : pool.handles )
            {
                Q_UNUSED( url );
                for ( const auto &handle : bucket )
                {
                    std::unique_lock<std::mutex> handleLock( handle->mutex,
                                                             std::try_to_lock );
                    if ( !handleLock.owns_lock() )
                    {
                        allFree = false;
                        break;
                    }
                }
                if ( !allFree )
                    break;
            }
        }
        if ( !allFree )
        {
            lock.unlock();
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            lock.lock();
        }
    }
    // Mutex held: acquire cannot hand out or add handles while we close, so
    // no handle cached at entry survives this sweep.
    for ( const auto &[url, bucket] : pool.handles )
    {
        Q_UNUSED( url );
        for ( const auto &handle : bucket )
            if ( handle->dataset )
                GDALClose( handle->dataset );
    }
    pool.handles.clear();
    pool.cv.notify_all();
}

// ---------------------------------------------------------------------------
// RemoteSourceCache
// ---------------------------------------------------------------------------

namespace
{
QString validatorKey( const QString &url )
{
    // One logical record per URL; the digest-free logical key keeps versions
    // monotonic when a source's token legitimately changes.
    return QStringLiteral( "remote-source/%1" ).arg( url );
}
} // namespace

void RemoteSourceCache::recordKnownGood( const QString &url, const QString &token )
{
    if ( url.isEmpty() )
        return;
    const QString key = validatorKey( url );
    if ( const auto existing = m_store.latestByLogicalKey( key ) )
    {
        // Same token: do not fabricate version churn for repeated validations.
        const QString recorded =
            existing->metadata.value( QStringLiteral( "validatorToken" ) ).toString();
        if ( recorded == token && m_store.touch( existing->artifactId ) )
            return;
    }
    ArtifactRegistration reg;
    reg.logicalKey = key;
    // Payload bookkeeping rows need no real payload; point them at the URL.
    reg.requireExistingPayload = false;
    reg.storagePath = url;
    reg.kind = QStringLiteral( "remote_source" );
    reg.metadata = QJsonObject{
        { QStringLiteral( "validatorToken" ), token },
        { QStringLiteral( "recordedAt" ),
          QDateTime::currentDateTimeUtc().toString( Qt::ISODate ) },
    };
    m_store.registerArtifact( reg );
}

QString RemoteSourceCache::recordedToken( const QString &url ) const
{
    const auto record = m_store.latestByLogicalKey( validatorKey( url ) );
    if ( !record )
        return QString();
    return record->metadata.value( QStringLiteral( "validatorToken" ) ).toString();
}

bool RemoteSourceCache::isStale( const QString &url )
{
    if ( !m_validator )
        return false; // conservative: no validator ⇒ never falsely stale
    const QString recorded = recordedToken( url );
    if ( recorded.isEmpty() )
        return false; // never validated: give the source the benefit of the doubt
    const QString current = m_validator->currentValidatorToken( url );
    if ( current.isEmpty() )
        return false; // offline / unknown: assume unchanged (offline fallback)
    return current != recorded;
}

void RemoteSourceCache::forget( const QString &url )
{
    const auto record = m_store.latestByLogicalKey( validatorKey( url ) );
    if ( record )
        m_store.forget( record->artifactId );
}

} // namespace sicnu::data
