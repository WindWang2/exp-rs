/***************************************************************************
    rs_dual_viewport_sync_controller.h
    --------------------------------------
    Dual 1x2 viewport pan/zoom/rotation synchronization controller.
 ***************************************************************************/
#ifndef RS_DUAL_VIEWPORT_SYNC_CONTROLLER_H
#define RS_DUAL_VIEWPORT_SYNC_CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QTimer>

class QgsMapCanvas;

/**
 * \brief Pixel-level synchronization between the two canvases of the dual
 *        1x2 map viewport (main + secondary).
 *
 * Unlike RsTwinCanvasSyncController (georeferencer), which copies only the
 * extent (the two georef canvases have different CRS/geotransforms), this
 * controller copies extent *and* scale *and* rotation so the two peer canvases
 * — which share one CRS and identical layer sets — stay pixel-aligned during
 * pan and zoom.
 *
 * Design:
 *   - Listens to QgsMapCanvas::extentsChanged on both canvases.
 *   - 16ms throttle coalesces signal storms during interactive pan/zoom (~60 FPS rate-limit).
 *   - mApplying reentrancy guard breaks feedback loops.
 *   - setEnabled(bool) toggles sync at runtime (View-menu "Sync pan/zoom").
 *   - setScaleSync(bool) optionally disables the scale copy so users can
 *     zoom the two viewports independently while still sharing pan center.
 *   - Uses QPointer to safely handle canvas destruction during pending timers.
 *   - Non-intrusive statistics counters for deterministic verification.
 */
class RsDualViewportSyncController : public QObject
{
    Q_OBJECT

  public:
    struct Stats
    {
        quint64 extentChangedEvents = 0;
        quint64 appliedSyncCount = 0;
        quint64 canvasRefreshRequests = 0;
    };

    RsDualViewportSyncController( QgsMapCanvas *primary, QgsMapCanvas *secondary,
                                  QObject *parent = nullptr );

    bool isEnabled() const { return mEnabled; }
    bool scaleSyncEnabled() const { return mScaleSync; }

    /// Testing instrumentation
    Stats stats() const { return mStats; }
    void resetStats() { mStats = Stats{}; }

  public slots:
    void setEnabled( bool on );
    /// When false, only the extent (pan center + span) is copied, not scale.
    void setScaleSync( bool on );

    /// Snap the secondary canvas to the primary's current viewport immediately
    /// (bypassing the throttle). Used when first enabling dual view.
    void snapSecondaryToPrimary();

  private slots:
    void onPrimaryExtentChanged();
    void onSecondaryExtentChanged();
    void onCanvasDestroyed( QObject *obj );

  private:
    void applySync( QgsMapCanvas *source, QgsMapCanvas *target );
    void applyFromPrimary();
    void applyFromSecondary();
    void schedule( bool fromPrimary );

    QPointer<QgsMapCanvas> mPrimary;
    QPointer<QgsMapCanvas> mSecondary;
    bool mEnabled = true;
    bool mScaleSync = true;
    bool mApplying = false;
    QTimer mThrottle;

    enum class Pending : int
    {
        None,
        FromPrimary,
        FromSecondary,
    };
    Pending mPending = Pending::None;

    Stats mStats;
};

#endif // RS_DUAL_VIEWPORT_SYNC_CONTROLLER_H
