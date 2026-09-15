/***************************************************************************
 * view_link_controller.h — Workbench 10.0 N-view link coordination,
 * extended by Linked Visual Analytics 11.0
 *
 * Linked extent over the QgisDisplayManager's view authority: the
 * controller owns NO canvases — views are registered by id, canvases are
 * re-queried through the manager on every event, and viewAboutToBeRemoved
 * detaches automatically. Views are linked per named GROUP (11.0): extent
 * propagation fans a source view's viewport out only to its group peers
 * (center+span, optional scale) with the same CRS transform behavior
 * (fail-closed on transform errors — an untransformable target is left
 * alone and counted, never moved with untransformed geometry) and the
 * reentrancy/throttle guards the proven dual-viewport sync uses.
 *
 * 11.0 additions:
 *   * named link groups (setLinkGroup / viewsInGroup; setLinked stays as
 *     the single-"default"-group facade);
 *   * bounded per-view viewport history + restorePreviousViewport (undo;
 *     the restore itself records the pre-restore viewport, so repeated
 *     undo walks back one step at a time);
 *   * cursor link (setCursorSync): each view's QgsMapCanvas::xyCoordinates
 *     is transformed cross-CRS into same-group peers and shown as a
 *     vertex-marker crosshair; markers clear on mouse Leave (event
 *     filter) and are canvas-owned children (no lifetime risk). Pure
 *     geometry — no I/O on the pointer path; raster sampling is
 *     VaCursorProbe's async job.
 *
 * The dual 1x2 viewport sync (RsDualViewportSyncController) is a DIFFERENT
 * surface — the split main canvas — and keeps its own controller; this one
 * is for registered display views (main + secondaries).
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "display/qgis_display_manager.h"

#include <qgscoordinatereferencesystem.h>

class QgsMapCanvas;
class QgsPointXY;
class QgsRectangle;
class QgsVertexMarker;

namespace sicnu::app
{

class ViewLinkController : public QObject
{
    Q_OBJECT
  public:
    static constexpr int kHistoryCapacity = 16;
    static const QString kDefaultGroup;

    struct Stats
    {
        quint64 extentEvents = 0;
        quint64 appliedSyncCount = 0;
        quint64 cursorProjections = 0;
        /// CRS transforms that failed and were skipped (fail-closed), both
        /// extent and cursor paths.
        quint64 transformFailures = 0;
        /// Extent echoes absorbed by the applying guard (never propagated).
        quint64 suppressedExtents = 0;
        /// Successful restorePreviousViewport calls.
        quint64 restoredViewports = 0;
        /// Raw cursor moves observed on registered views.
        quint64 cursorEvents = 0;
    };

    explicit ViewLinkController( sicnu::display::QgisDisplayManager *displayManager,
                                 QObject *parent = nullptr );

    /// Registers a view (main + secondaries). Idempotent. Unknown/dead views
    /// are rejected (the manager must know the id).
    void addView( sicnu::display::DisplayViewId viewId );
    void removeView( sicnu::display::DisplayViewId viewId );
    QVector<sicnu::display::DisplayViewId> views() const;

    // ── Link groups (11.0) ────────────────────────────────────────────
    /// Assigns the view to a link group ("" = unlink). When the group has
    /// other members the view snaps to the first peer's viewport so the
    /// link starts coherent.
    void setLinkGroup( sicnu::display::DisplayViewId viewId, const QString &groupId );
    QString linkGroup( sicnu::display::DisplayViewId viewId ) const;
    QStringList groups() const;
    QVector<sicnu::display::DisplayViewId> viewsInGroup( const QString &groupId ) const;

    /// Single-default-group facade (group "default").
    bool isLinked( sicnu::display::DisplayViewId viewId ) const;
    void setLinked( sicnu::display::DisplayViewId viewId, bool linked );

    bool centerSyncEnabled() const { return mCenterSync; }
    bool scaleSyncEnabled() const { return mScaleSync; }
    bool cursorSyncEnabled() const { return mCursorSync; }

    // ── Viewport history (11.0) ───────────────────────────────────────
    /// Restores the most recent distinct previous viewport of the view
    /// (bounded ring). True when a viewport was restored. When the view is
    /// linked, the restored extent propagates to its group (coherent group
    /// undo).
    bool restorePreviousViewport( sicnu::display::DisplayViewId viewId );
    int historyCount( sicnu::display::DisplayViewId viewId ) const;

    // ── Active view (command surface) ─────────────────────────────────
    /// Tracks the display manager's active view (auto-wired in the ctor
    /// via activeViewChanged; commands operate on it).
    sicnu::display::DisplayViewId activeView() const { return mActiveView; }
    void setActiveView( sicnu::display::DisplayViewId viewId );
    /// undo of the active view's viewport.
    bool restoreActiveViewport();

    /// Testing/diagram instrumentation: cursor crosshairs render markers by
    /// default; off-screen tests may disable them without losing behavior.
    void setCursorMarkersVisible( bool on ) { mCursorMarkers = on; }

    /// Testing instrumentation.
    Stats stats() const { return mStats; }
    void resetStats() { mStats = Stats{}; }

  signals:
    /// Raw cursor geometry from a registered view (its own CRS WKT as the
    /// authority). Emitted for every registered view regardless of link
    /// state so charts can subscribe; pure geometry, high rate.
    void cursorMoved( sicnu::display::DisplayViewId sourceView,
                      const QgsPointXY &point, const QString &sourceCrsWkt );
    /// The pointer left the view (cursor semantics end there).
    void cursorLeft( sicnu::display::DisplayViewId sourceView );

  public slots:
    void setCenterSync( bool on );
    void setScaleSync( bool on );
    void setCursorSync( bool on );
    /// Turns cursor crosshairs off (leaves cursorMoved signals on).
    void clearCursor();

  private slots:
    void onViewAboutToBeRemoved( sicnu::display::DisplayViewId viewId );

  private:
    struct ViewRecord
    {
        sicnu::display::DisplayViewId id;
        QString group;
        /// Previous extents in the view's own CRS, newest first.
        QVector<QgsRectangle> history;
        QPointer<QgsVertexMarker> marker;
        /// Per-canvas connections from addView — disconnected in removeView
        /// so a remove/re-add cycle on a living canvas never double-connects.
        QMetaObject::Connection extentConn;
        QMetaObject::Connection cursorConn;
        bool filterInstalled = false;
        /// WKT cache: generating WKT per pointer move is too expensive; the
        /// cache is keyed by CRS equality (cheap), so a project CRS change
        /// invalidates it naturally.
        QgsCoordinateReferenceSystem cachedCrs;
        QString cachedCrsWkt;
        bool crsCacheValid = false;
    };

    ViewRecord *recordFor( sicnu::display::DisplayViewId viewId );
    const ViewRecord *recordFor( sicnu::display::DisplayViewId viewId ) const;

    void onExtentChanged( sicnu::display::DisplayViewId sourceId );
    void recordHistory( ViewRecord &record, QgsMapCanvas *canvas );
    void propagateFrom( sicnu::display::DisplayViewId sourceId );
    void onCursorMoved( sicnu::display::DisplayViewId sourceId, const QgsPointXY &point );
    void onCursorLeft( sicnu::display::DisplayViewId sourceId );
    void propagateCursorFrom( sicnu::display::DisplayViewId sourceId,
                              const QgsPointXY &point );
    void hideAllMarkers();

    QgsMapCanvas *canvasFor( sicnu::display::DisplayViewId viewId ) const;

    sicnu::display::QgisDisplayManager *m_displayManager = nullptr;
    QVector<ViewRecord> m_views;
    bool mCenterSync = true;
    bool mScaleSync = true;
    bool mCursorSync = true;
    bool mCursorMarkers = true;
    bool mApplying = false;
    sicnu::display::DisplayViewId mActiveView;
    sicnu::display::DisplayViewId mMarkerOwner;
    QTimer mThrottle;
    sicnu::display::DisplayViewId mPendingSource;
    Stats mStats;
};

} // namespace sicnu::app
