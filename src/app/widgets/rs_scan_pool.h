/***************************************************************************
 * rs_scan_pool.h — bounded worker pool for UI-triggered GDAL scans (#797)
 *
 * ROI statistics and histogram scans used QThreadPool::globalInstance():
 * one scene-wide GDAL scan could saturate every global worker, starving
 * canvas rendering and every other global-pool user for the duration of a
 * multi-GB read. This pool caps scan concurrency (2 workers) and provides
 * generation-based cooperative cancellation: a worker checks isStale()
 * between bands/blocks and exits early when a newer request superseded it
 * or its own generation was canceled.
 *
 * This is NOT a scheduler — it is a bounded queue for UI-side read-only
 * scans. Real processing still goes through TaskCenter → JobEngine.
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace sicnu::app
{

class RsScanPool : public QObject
{
    Q_OBJECT
  public:
    static RsScanPool &instance();

    /// The bounded scan pool — never QThreadPool::globalInstance().
    QThreadPool &pool() { return m_pool; }

    /// Opens a new request generation, implicitly superseding every older
    /// one for the same owner (or globally if owner is null).
    quint64 nextGeneration( const void *owner = nullptr );

    /// QObject-owner overload: same generation semantics, plus the pool
    /// erases the owner's record when the object is destroyed. Without the
    /// destroyed hook the raw-address map entry outlived its widget
    /// forever — an unbounded leak that also pinned cancel()'s minLive low,
    /// and a successor allocated at the same address inherited the dead
    /// owner's generation record (#1453). The erased record is itself the
    /// tombstone: isStale() reads a missing owner as stale, so workers
    /// still in flight for the destroyed owner stop instead of outliving
    /// their cancel marker (#1474 review).
    quint64 nextGeneration( const QObject *owner );

    /// Cancels one specific generation (e.g. its widget going away) without
    /// touching other widgets' in-flight scans.
    void cancel( quint64 generation, const void *owner = nullptr );

    /// Cooperative cancellation flag for @p generation.
    bool isStale( quint64 generation, const void *owner = nullptr ) const
    {
        if ( owner )
        {
            const std::lock_guard<std::mutex> lock( m_canceledMutex );
            auto it = m_ownerActiveGeneration.find( owner );
            // A forgotten owner has no live anchor left: its record is gone
            // (destroyed hook / forgetOwner) and cancel()'s bounded pruning
            // may since have retired its cancel marker, after which nothing
            // could ever classify its in-flight generations again. Fail
            // closed: workers polling a destroyed owner read stale (#1474
            // review).
            if ( it == m_ownerActiveGeneration.end() )
                return true;
            if ( generation < it->second )
                return true;
            return m_canceled.count( generation ) != 0;
        }
        if ( generation < m_activeGeneration.load( std::memory_order_acquire ) )
            return true;
        const std::lock_guard<std::mutex> lock( m_canceledMutex );
        return m_canceled.count( generation ) != 0;
    }

  private:
    RsScanPool();

    /// QObject::destroyed payload is the dying owner itself — the exact key
    /// to erase. UniqueConnection keeps one hook per owner across repeated
    /// nextGeneration calls; DirectConnection is safe because owners
    /// register from (and are destroyed on) the GUI thread.
    void ownerDestroyed( QObject *owner );
    void forgetOwner( const void *owner );

    QThreadPool m_pool;
    std::atomic<quint64> m_activeGeneration { 1 };
    mutable std::mutex m_canceledMutex;
    std::unordered_set<quint64> m_canceled;
    std::unordered_map<const void *, quint64> m_ownerActiveGeneration;
};

} // namespace sicnu::app
