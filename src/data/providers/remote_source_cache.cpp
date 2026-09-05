// remote_source_cache.cpp — see remote_source_cache.h for the contract.
#include "remote_source_cache.h"

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
    std::mutex mutex;
    std::condition_variable cv;
    std::map<QString, std::vector<std::shared_ptr<PooledRemoteHandle>>> handles;
    /// Per-URL GDALOpen calls in flight (reserved before releasing the pool
    /// mutex for network I/O, so the per-URL bound can never be overshot).
    std::map<QString, size_t> opening;
    size_t handlesPerUrl = 2;
};

RemoteDatasetPool &RemoteDatasetPool::instance()
{
    static RemoteDatasetPool pool;
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
    configureRemoteCachingDefaults();
    if ( !m_impl )
    {
        m_impl = new Impl;
        if ( const char *env = std::getenv( "SICNU_REMOTE_POOL_HANDLES" ) )
        {
            const long parsed = std::strtol( env, nullptr, 10 );
            m_impl->handlesPerUrl =
                static_cast<size_t>( std::clamp<long>( parsed, 1, 8 ) );
        }
    }

    std::unique_lock<std::mutex> lock( m_impl->mutex );
    while ( true )
    {
        std::vector<std::shared_ptr<PooledRemoteHandle>> &bucket = m_impl->handles[url];
        for ( auto &handle : bucket )
        {
            std::unique_lock<std::mutex> handleLock( handle->mutex, std::try_to_lock );
            if ( handleLock.owns_lock() )
                return RemoteDatasetLease( handle, std::move( handleLock ) );
        }
        const size_t inFlight = m_impl->opening[url];
        if ( bucket.size() + inFlight < m_impl->handlesPerUrl )
        {
            // Reserve a slot, then open OUTSIDE the pool mutex: GDALOpen on
            // /vsicurl/ does network I/O (bounded by the configured connect/
            // transfer timeouts), so never hold the global lock across it.
            m_impl->opening[url] = inFlight + 1;
            lock.unlock();
            GDALDatasetH dataset = GDALOpenEx( url.toUtf8().constData(), oflag,
                                               nullptr, nullptr, nullptr );
            m_openCount.fetch_add( 1, std::memory_order_relaxed );
            lock.lock();
            m_impl->opening[url] -= 1;
            if ( !dataset )
                return RemoteDatasetLease{};
            auto handle = std::make_shared<PooledRemoteHandle>();
            handle->dataset = dataset;
            handle->url = url;
            m_impl->handles[url].push_back( handle );
            std::unique_lock<std::mutex> handleLock( handle->mutex );
            return RemoteDatasetLease( handle, std::move( handleLock ) );
        }
        // All handles busy and the bound reached: wait for a return. Leases
        // do not know the pool, so wake on a short poll — checkout cost is
        // dominated by remote I/O anyway.
        m_impl->cv.wait_for( lock, std::chrono::milliseconds( 20 ) );
    }
}

void RemoteDatasetPool::clear()
{
    if ( !m_impl )
        return;
    std::unique_lock<std::mutex> lock( m_impl->mutex );
    // Wait for all leases to return (every handle must be lockable), then
    // close. Retry loop keeps it simple and bounded by lease lifetimes.
    bool allFree = false;
    while ( !allFree )
    {
        allFree = true;
        for ( auto &[url, bucket] : m_impl->handles )
        {
            Q_UNUSED( url );
            for ( auto &handle : bucket )
            {
                std::unique_lock<std::mutex> handleLock( handle->mutex, std::try_to_lock );
                if ( !handleLock.owns_lock() )
                {
                    allFree = false;
                    break;
                }
            }
            if ( !allFree )
                break;
        }
        if ( !allFree )
        {
            lock.unlock();
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            lock.lock();
        }
    }
    for ( auto &[url, bucket] : m_impl->handles )
    {
        Q_UNUSED( url );
        for ( auto &handle : bucket )
            if ( handle->dataset )
                GDALClose( handle->dataset );
    }
    m_impl->handles.clear();
    m_impl->cv.notify_all();
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
