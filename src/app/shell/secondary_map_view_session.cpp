// secondary_map_view_session.cpp — secondary view lifecycle implementation
#include "secondary_map_view_session.h"

#include "project_context.h"
#include "secondary_map_view_widget.h"
#include "rs_dual_viewport_sync_controller.h"

#include <QAction>
#include <QSignalBlocker>
#include <QSplitter>

SecondaryMapSession::SecondaryMapSession(
    QgsMapCanvas *mainCanvas, QSplitter *splitter,
    sicnu::app::ProjectContext *projectContext,
    const std::function<void( sicnu::display::DisplayViewId )> &registerLinkedView,
    QObject *parent )
    : QObject( parent )
    , m_mainCanvas( mainCanvas )
    , m_splitter( splitter )
    , m_projectContext( projectContext )
    , m_registerLinkedView( registerLinkedView )
{
}

SecondaryMapSession::~SecondaryMapSession()
{
    // The engine view must not outlive the session: release it while the
    // project context is still alive (the window's destructor calls this
    // before resetting its ProjectContext).
    if ( m_projectContext && !m_viewId.isNull() )
        ( void ) m_projectContext->removeView( m_viewId );
    // m_sync dies with this QObject (parented); the widget dies with the
    // splitter, as before.
}

void SecondaryMapSession::ensureWidget()
{
    if ( m_widget )
        return;

    m_widget = new SecondaryMapViewWidget( m_splitter );
    m_splitter->addWidget( m_widget );
    m_splitter->setStretchFactor( 0, 1 );
    m_splitter->setStretchFactor( 1, 1 );
    m_splitter->setSizes( { 600, 600 } );

    // Widget connections are made exactly once per widget lifetime — the
    // widget is reused across open/close cycles, so these survive a close.
    connect( m_widget, &SecondaryMapViewWidget::activateRequested,
             this, &SecondaryMapSession::activateRequested );
    connect( m_widget, &SecondaryMapViewWidget::closeRequested,
             this, &SecondaryMapSession::closeRequested );
    connect( m_widget, &SecondaryMapViewWidget::syncFromMainRequested,
             this, &SecondaryMapSession::syncFromMainRequested );
}

void SecondaryMapSession::tearDownSync()
{
    // Tear down the dual-viewport sync controller — its secondary canvas
    // pairing is gone. open() re-creates it against the live pairing.
    if ( !m_sync )
        return;
    m_sync->setEnabled( false );
    delete m_sync;
    m_sync = nullptr;
    if ( m_syncAction )
    {
        QSignalBlocker b( m_syncAction );
        m_syncAction->setChecked( false );
    }
}

bool SecondaryMapSession::open( QString *errorOut )
{
    if ( !m_projectContext || !m_splitter || m_mainCanvas.isNull() )
        return false; // preconditions; the host guards these first
    if ( errorOut )
        errorOut->clear();

    ensureWidget();

    // (B2) Re-create the sync controller on EVERY open, not only when the
    // widget is first created: close() deleted it, the widget survived. The
    // View toggle follows the fresh controller (default enabled).
    if ( !m_sync )
    {
        m_sync = new RsDualViewportSyncController(
            m_mainCanvas, m_widget->canvas(), this );
        if ( m_syncAction )
        {
            QSignalBlocker b( m_syncAction );
            m_syncAction->setChecked( true );
        }
    }

    // Snap the secondary canvas to the primary's current viewport so the two
    // views start pixel-aligned.
    m_sync->snapSecondaryToPrimary();

    if ( m_viewId.isNull() )
    {
        const auto created =
            m_projectContext->createSecondaryView( m_widget->viewSpec() );
        if ( !created )
        {
            // Consistent closed state: a half-open session (sync wired,
            // toggles checked, no engine view) would misrepresent the
            // surface. Matches close() semantics.
            tearDownSync();
            if ( m_viewAction )
            {
                QSignalBlocker b( m_viewAction );
                m_viewAction->setChecked( false );
            }
            if ( errorOut )
                *errorOut = tr( "Cannot create the second display view." );
            return false;
        }
        m_viewId = created.value();
        m_widget->setViewId( m_viewId );
        // Linked Visual Analytics 11.0: join the new view to the link
        // authorities (extent/cursor groups + layer visibility link).
        if ( m_registerLinkedView )
            m_registerLinkedView( m_viewId );
    }

    m_widget->show();
    if ( m_viewAction )
    {
        QSignalBlocker b( m_viewAction );
        m_viewAction->setChecked( true );
    }
    return true;
}

void SecondaryMapSession::close()
{
    if ( m_projectContext && !m_viewId.isNull() )
    {
        ( void ) m_projectContext->removeView( m_viewId );
        m_viewId = {};
    }

    if ( m_widget )
    {
        m_widget->hide();
        m_widget->setViewId( {} );
        m_widget->setActiveHighlight( false );
    }

    tearDownSync();

    if ( m_viewAction )
    {
        QSignalBlocker b( m_viewAction );
        m_viewAction->setChecked( false );
    }
}

void SecondaryMapSession::setActions( QAction *viewToggle, QAction *syncToggle )
{
    m_viewAction = viewToggle;
    m_syncAction = syncToggle;
}
