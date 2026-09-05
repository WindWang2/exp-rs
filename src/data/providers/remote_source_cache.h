// remote_source_cache.h — Remote COG caching layer (Data Plane 3.0, Phase F).
//
// Three cooperating pieces, none of which performs network I/O in src/data
// (the layer stays network-free; validators are injected by the host):
//
// 1. configureRemoteCachingDefaults(): GDAL /vsicurl/ cache + retry tuning —
//    the block cache GDAL keeps for remote reads, bounded, once per process.
// 2. RemoteDatasetPool: bounded, refcounted GDAL dataset handles per remote
//    URL. Concurrent consumers of one /vsicurl/ source coalesce onto at most
//    handlesPerUrl open handles instead of N+1 GDALOpen round trips; each
//    handle is used under a per-handle mutex (GDAL datasets are not
//    thread-safe for concurrent reads on one handle).
// 3. RemoteSourceValidator seam + RemoteSourceCache bookkeeping: the host can
//    inject a validator that resolves an URL's current validator token
//    (ETag / Last-Modified). Recorded tokens live in the ArtifactStore keyed
//    by URL; restore paths can consult staleness without network access.
#pragma once

#include "../artifact_store.h"

#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include <gdal.h>

namespace sicnu::data
{

/// One-time GDAL remote-caching defaults. Idempotent; user-set config always
/// wins (values are applied only when not already present, matching
/// configureRemoteHttpDefaults' contract).
void configureRemoteCachingDefaults();

// ---------------------------------------------------------------------------
// RemoteDatasetPool
// ---------------------------------------------------------------------------

struct PooledRemoteHandle
{
    GDALDatasetH dataset = nullptr;
    QString url;
    /// Serializes access to @p dataset: a lease holds the lock for its
    /// lifetime (GDAL datasets are not thread-safe for concurrent access).
    std::mutex mutex;
};

/// A checked-out, serialized GDAL dataset handle. Move-only; the lock is
/// held until the lease is destroyed. Construction takes ownership of an
/// ALREADY-LOCKED handle lock (the pool locks before handing out).
class RemoteDatasetLease
{
  public:
    RemoteDatasetLease() = default;
    RemoteDatasetLease( std::shared_ptr<PooledRemoteHandle> handle,
                        std::unique_lock<std::mutex> &&lock );
    ~RemoteDatasetLease();
    RemoteDatasetLease( RemoteDatasetLease &&other ) noexcept;
    RemoteDatasetLease &operator=( RemoteDatasetLease &&other ) noexcept;
    RemoteDatasetLease( const RemoteDatasetLease & ) = delete;
    RemoteDatasetLease &operator=( const RemoteDatasetLease & ) = delete;

    explicit operator bool() const { return m_handle != nullptr; }
    GDALDatasetH get() const { return m_handle ? m_handle->dataset : nullptr; }

  private:
    std::shared_ptr<PooledRemoteHandle> m_handle;
    std::optional<std::unique_lock<std::mutex>> m_lock;
};

/// Bounded per-URL GDAL dataset pool. Thread-safe.
class RemoteDatasetPool
{
  public:
    /// Singleton used by the data layer. handlesPerUrl defaults to 2
    /// (env SICNU_REMOTE_POOL_HANDLES, clamped to [1, 8]).
    static RemoteDatasetPool &instance();

    /// Checks out a handle for @p url (open with @p oflag, e.g. GA_READONLY),
    /// opening a new one only when all existing handles for the URL are busy
    /// and the per-URL bound is not reached; otherwise blocks.
    RemoteDatasetLease acquire( const QString &url, unsigned int oflag );

    /// Drops every cached handle (process teardown / tests). Blocks until all
    /// leases are returned.
    void clear();

    /// Diagnostics (tests): number of GDALOpen calls actually performed.
    qint64 openCount() const { return m_openCount.load(); }

  private:
    RemoteDatasetPool() = default;
    struct Impl;
    Impl *m_impl = nullptr;
    std::atomic<qint64> m_openCount{ 0 };
};

// ---------------------------------------------------------------------------
// Remote source staleness bookkeeping (validator seam)
// ---------------------------------------------------------------------------

/// Resolves the current validator token (ETag, else Last-Modified, else
/// content length) of a remote URL. Host-injected; the no-op default always
/// answers "unknown" and never touches the network.
class RemoteSourceValidator
{
  public:
    virtual ~RemoteSourceValidator() = default;
    /// Returns the current validator token, or an empty string when unknown
    /// (offline, validator not configured, network error). Must not block
    /// unboundedly (hosts enforce their own timeouts).
    virtual QString currentValidatorToken( const QString &url ) = 0;
};

/// Bookkeeping over the ArtifactStore: records the validator token observed
/// when a remote source was last known-good, and answers whether a source is
/// stale against a freshly validated token. Pure metadata — no network.
class RemoteSourceCache
{
  public:
    explicit RemoteSourceCache( ArtifactStore &store ) : m_store( store ) {}

    void setValidator( RemoteSourceValidator *validator ) { m_validator = validator; }

    /// Records @p url as known-good with @p token (call after a successful
    /// validation or a fresh registration).
    void recordKnownGood( const QString &url, const QString &token );

    /// Last recorded token for @p url, or empty.
    QString recordedToken( const QString &url ) const;

    /// True when a validator is configured AND it observes a different token
    /// than recorded (or the URL vanished). Conservative: without a validator
    /// nothing is ever stale (no false invalidation offline).
    bool isStale( const QString &url );

    /// Forgets the recorded state for @p url.
    void forget( const QString &url );

  private:
    ArtifactStore &m_store;
    RemoteSourceValidator *m_validator = nullptr;
};

} // namespace sicnu::data
