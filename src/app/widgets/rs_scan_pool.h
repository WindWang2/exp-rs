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
    /// one. Workers carry the returned token and poll isStale() at loop
    /// boundaries to abandon superseded work promptly.
    quint64 nextGeneration();

    /// Cancels one specific generation (e.g. its widget going away) without
    /// touching other widgets' in-flight scans.
    void cancel( quint64 generation );

    /// Cooperative cancellation flag for @p generation.
    bool isStale( quint64 generation ) const
    {
        if ( generation < m_activeGeneration.load( std::memory_order_acquire ) )
            return true;
        const std::lock_guard<std::mutex> lock( m_canceledMutex );
        return m_canceled.count( generation ) != 0;
    }

  private:
    RsScanPool();

    QThreadPool m_pool;
    std::atomic<quint64> m_activeGeneration { 1 };
    mutable std::mutex m_canceledMutex;
    std::unordered_set<quint64> m_canceled;
};

} // namespace sicnu::app
