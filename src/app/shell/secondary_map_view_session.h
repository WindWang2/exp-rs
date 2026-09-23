// secondary_map_view_session.h — the main window's secondary view lifecycle
//
// Owns the three pieces of secondary-view state the window used to scatter
// across members (the reusable widget, the engine Display View id, the
// pixel-sync controller) as ONE testable session object, so the open/close/
// reopen state machine is drivable without the full shell.
#pragma once

#include "display/qgis_display_manager.h"
#include "secondary_map_view_widget.h"

#include <qgsmapcanvas.h>

#include <QObject>
#include <QPointer>

#include <functional>

class QAction;
class QSplitter;
class RsDualViewportSyncController;

namespace sicnu::app
{
class ProjectContext;
}

/**
 * Secondary map view session (Wave D).
 *
 * Lifecycle contract (the B2 fix): the widget survives close() hidden for
 * reuse, but the sync controller does NOT — close() deletes it (its canvas
 * pairing is torn down), so open() must re-create it on EVERY open. The
 * controller used to be created inside the widget-creation guard, leaving a
 * close→reopen dual view permanently unsynced with the View toggle dead.
 * open() likewise re-creates the engine Display View and re-joins the link
 * authorities on every reopen, so each open lands on a fresh, correctly
 * wired view id — never a stale pairing and never duplicated connections
 * (widget signal connections are made exactly once, in ensureWidget).
 */
class SecondaryMapSession : public QObject
{
    Q_OBJECT

  public:
    /// @param registerLinkedView invoked once per newly created engine view;
    ///        the window joins it to the Visual Analytics link authorities.
    SecondaryMapSession(
        QgsMapCanvas *mainCanvas, QSplitter *splitter,
        sicnu::app::ProjectContext *projectContext,
        const std::function<void( sicnu::display::DisplayViewId )> &registerLinkedView,
        QObject *parent = nullptr );
    ~SecondaryMapSession() override;

    SecondaryMapSession( const SecondaryMapSession & ) = delete;
    SecondaryMapSession &operator=( const SecondaryMapSession & ) = delete;

    /// Opens (or reopens) the secondary view. Returns false on failure with
    /// a user-facing message in @p errorOut; the session stays closed and
    /// consistent (no half-wired sync controller, toggles unchecked).
    /// Idempotent while open.
    bool open( QString *errorOut = nullptr );

    /// Closes the view: releases the engine view, hides the (reusable)
    /// widget, and tears the sync controller down. Safe when already closed.
    void close();

    bool isOpen() const { return !m_viewId.isNull(); }

    sicnu::display::DisplayViewId viewId() const { return m_viewId; }
    SecondaryMapViewWidget *widget() const { return m_widget.data(); }
    /// The live pixel-sync controller; null while closed. Re-created by
    /// every successful open().
    RsDualViewportSyncController *syncController() const { return m_sync; }

    /// The View-menu toggles this session keeps in sync with its state.
    void setActions( QAction *viewToggle, QAction *syncToggle );

  signals:
    /// Re-emitted from the widget (whose connections are owned here); the
    /// window binds its own slots to these.
    void activateRequested();
    void closeRequested();
    void syncFromMainRequested();

  private:
    void ensureWidget();
    void tearDownSync();

    QPointer<QgsMapCanvas> m_mainCanvas;
    QSplitter *m_splitter = nullptr;
    sicnu::app::ProjectContext *m_projectContext = nullptr;
    std::function<void( sicnu::display::DisplayViewId )> m_registerLinkedView;

    QPointer<SecondaryMapViewWidget> m_widget;
    sicnu::display::DisplayViewId m_viewId;
    RsDualViewportSyncController *m_sync = nullptr;

    QAction *m_viewAction = nullptr;
    QAction *m_syncAction = nullptr;
};
