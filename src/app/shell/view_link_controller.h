/***************************************************************************
 * view_link_controller.h — Workbench 10.0 N-view link coordination
 *
 * Linked extent over the QgisDisplayManager's view authority: the
 * controller owns NO canvases — views are registered by id, canvases are
 * re-queried through the manager on every event, and viewAboutToBeRemoved
 * detaches automatically. Per-view link toggles let one view pan while the
 * others stay put; propagation fans one source view's viewport out to every
 * linked peer (center+span, optional scale) with the same CRS
 * transform behavior and reentrancy/throttle guards the proven dual-viewport
 * sync uses.
 *
 * The dual 1x2 viewport sync (RsDualViewportSyncController) is a DIFFERENT
 * surface — the split main canvas — and keeps its own controller; this one
 * is for registered display views (main + secondaries).
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include "display/qgis_display_manager.h"

class QgsMapCanvas;
class QgsRubberBand;

namespace sicnu::app
{

class ViewLinkController : public QObject
{
    Q_OBJECT
  public:
    struct Stats
    {
        quint64 extentEvents = 0;
        quint64 appliedSyncCount = 0;
        quint64 cursorProjections = 0;
    };

    explicit ViewLinkController( sicnu::display::QgisDisplayManager *displayManager,
                                 QObject *parent = nullptr );

    /// Registers a view (main + secondaries). Idempotent. Unknown/dead views
    /// are rejected (the manager must know the id).
    void addView( sicnu::display::DisplayViewId viewId );
    void removeView( sicnu::display::DisplayViewId viewId );
    QVector<sicnu::display::DisplayViewId> views() const { return m_views; }

    bool isLinked( sicnu::display::DisplayViewId viewId ) const;
    bool centerSyncEnabled() const { return mCenterSync; }
    bool scaleSyncEnabled() const { return mScaleSync; }

    /// Testing instrumentation.
    Stats stats() const { return mStats; }
    void resetStats() { mStats = Stats{}; }

  public slots:
    /// Toggles the view's membership in the linked set (a linked change
    /// snaps the view to the first linked peer's viewport).
    void setLinked( sicnu::display::DisplayViewId viewId, bool linked );
    void setCenterSync( bool on );
    void setScaleSync( bool on );

  private slots:
    void onViewAboutToBeRemoved( sicnu::display::DisplayViewId viewId );

  private:
    void onExtentChanged( sicnu::display::DisplayViewId sourceId );
    void propagateFrom( sicnu::display::DisplayViewId sourceId );
    QgsMapCanvas *canvasFor( sicnu::display::DisplayViewId viewId ) const;

    sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
    QVector<sicnu::display::DisplayViewId> m_views;
    QVector<sicnu::display::DisplayViewId> m_linkedViews;
    bool mCenterSync = true;
    bool mScaleSync = true;
    bool mApplying = false;
    QTimer mThrottle;
    sicnu::display::DisplayViewId mPendingSource;
    Stats mStats;
};

} // namespace sicnu::app
