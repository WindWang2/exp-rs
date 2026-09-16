/***************************************************************************
 * va_cursor_probe.h — Linked Visual Analytics 11.0 async hover sampling
 *
 * The asynchronous half of the cursor link (the geometry half lives in
 * ViewLinkController): sample the active raster at a cursor point WITHOUT
 * ever doing I/O on the GUI thread. Requests are coalesced
 * newest-wins (only the newest point is ever sampled), starts are
 * dwell-throttled, and every job rides the sanctioned RsScanPool
 * generation contract — a result whose generation went stale is dropped
 * before delivery, and a destroyed probe can never receive one.
 *
 * The injected RasterProvider is re-queried on every accepted request on
 * the GUI thread and must not keep ownership: the probe captures only
 * the raster's PATH plus the CRS-transformed map point, so no layer
 * handle is ever touched off-thread (same contract as VaWorkbenchPanel's
 * jobs, which go through the geospatial RasterReader seam).
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

class QgsPointXY;
class QgsRasterLayer;

namespace sicnu::app::va
{

class VaCursorProbe : public QObject
{
    Q_OBJECT
  public:
    /// Returns the raster to sample (may be null — nothing is sampled).
    /// Called on the GUI thread per accepted request.
    using RasterProvider = std::function<QgsRasterLayer *()>;

    /// Minimum interval between job starts; faster pointer moves coalesce
    /// into the newest pending point.
    static constexpr int kDwellThrottleMs = 120;

    explicit VaCursorProbe( RasterProvider provider, QObject *parent = nullptr );
    ~VaCursorProbe() override;

    /// Requests a sample at @p point (in @p crsWkt), @p band 1-based.
    /// Newest-wins: any pending-but-not-started point is replaced; an
    /// in-flight job keeps running and its result is dropped if superseded
    /// before delivery.
    void request( const QgsPointXY &point, const QString &crsWkt, int band = 1 );

    /// Supersedes everything in flight and drops any pending point.
    void cancel();

    bool isBusy() const { return m_busy.load( std::memory_order_acquire ); }

    /// Testing instrumentation.
    struct Stats
    {
        quint64 requests = 0;      ///< accepted requests (newest-wins folds
                                   ///< into the pending one do not count)
        quint64 coalesced = 0;     ///< pointer moves folded into a pending one
        quint64 staleDrops = 0;    ///< finished results dropped as stale
        quint64 delivered = 0;     ///< samples actually delivered
    };
    Stats stats() const { return m_stats; }
    void resetStats() { m_stats = Stats{}; }

  signals:
    /// One finished sample for the newest generation, delivered on the GUI
    /// thread. `ok` is true only for a real value; `noData` marks an
    /// in-raster NoData hit; `message` carries the honest reason otherwise
    /// (outside extent, rotated geotransform, open/read failure, unbuildable
    /// CRS transform). Requests accepted before the latest generation are
    /// never delivered.
    void sampled( bool ok, double value, int band, bool noData, const QString &message );

  private:
    /// GUI-thread-only pending state (never touched from the pool thread):
    /// the newest requested point that has not started a job yet.
    struct PendingPoint
    {
        bool valid = false;
        double x = 0, y = 0;
        QString crsWkt;
        int band = 1;
    };

    void startPending();

    RasterProvider m_provider;
    std::atomic<quint64> m_generation { 0 };
    std::atomic<bool> m_busy { false };

    PendingPoint m_pending;
    qint64 m_lastStartMs = 0;

    Stats m_stats;
};

} // namespace sicnu::app::va
