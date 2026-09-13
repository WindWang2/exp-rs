// main_window_view.cpp — Map view and navigation actions
#include "main_window.h"
#include "active_view_host.h"
#include "project_context.h"
#include "shell/rs_session_map_workspace.h"
#include "shell/secondary_map_view_widget.h"
#include "shell/rs_dual_viewport_sync_controller.h"

#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgscoordinatetransform.h>
#include <georeferencer/qgsgeoreferencermainwindow.h>
#include <georeferencer/qgsgeoref_image_to_map_window.h>
#include <georeferencer/qgsgeoref_shell_window.h>

#ifdef SICNU_HAS_CLASSIFY
#include "classification/qgsclassificationmainwindow.h"
#endif
#ifdef SICNU_HAS_OBIA
#include "rs_obia_main_window.h"
#endif

// ── View Actions ──────────────────────────────────────────────────────────
void QgisDesktopWindow::zoomIn() { m_mapCanvas->zoomIn(); }
void QgisDesktopWindow::zoomOut() { m_mapCanvas->zoomOut(); }
void QgisDesktopWindow::panMap() { m_mapCanvas->setMapTool(m_panTool); }
void QgisDesktopWindow::identifyFeatures() { m_mapCanvas->setMapTool(m_identifyTool); }

void QgisDesktopWindow::measureDistance()
{
    m_mapCanvas->setMapTool( m_measureDistanceTool );
    statusBar()->showMessage( tr( "Distance measure: click to add points; double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::measureArea()
{
    m_mapCanvas->setMapTool( m_measureAreaTool );
    statusBar()->showMessage( tr( "Area measure: click to add points; double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::openGeoreferencer()
{
    openGeorefImageToImage();
}

namespace
{
/// Register a session map as a secondary Display View (no dual bridges).
bool bindSessionSecondaryView( sicnu::app::ProjectContext *ctx,
                               RsSessionMapWorkspace *session,
                               sicnu::display::DisplayViewId &outId )
{
    if ( !ctx || !session || !outId.isNull() )
        return false;
    session->releaseLocalBridge();
    const auto created = ctx->createSecondaryView( session->viewSpec() );
    if ( !created )
    {
        session->restoreLocalBridge();
        return false;
    }
    outId = created.value();
    return true;
}
} // namespace

void QgisDesktopWindow::openGeorefImageToImage()
{
    if ( !m_georefI2I )
    {
        m_georefI2I = new QgsGeoreferencerMainWindow( nullptr, this );
        m_georefI2I->setAttribute( Qt::WA_DeleteOnClose, false );
        m_georefI2I->setWindowTitle( tr( "Image Registration · Image 2 Image" ) );

        connect( m_georefI2I, &QgsGeorefShellWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     if ( loadDataLayer( path ) )
                         statusBar()->showMessage(
                             tr( "Loaded the correction result into the main view: %1" ).arg( path ), 5000 );
                     else
                         statusBar()->showMessage(
                             tr( "Failed to load the correction result into the main view: %1" ).arg( path ), 6000 );
                 } );

        if ( m_projectContext )
        {
            ( void ) bindSessionSecondaryView(
                m_projectContext.get(), m_georefI2I->srcSessionMap(), m_georefI2ISrcViewId );
            ( void ) bindSessionSecondaryView(
                m_projectContext.get(), m_georefI2I->dstSessionMap(), m_georefI2IDstViewId );
        }
    }
    m_georefI2I->show();
    m_georefI2I->raise();
    m_georefI2I->activateWindow();
}

void QgisDesktopWindow::openGeorefImageToMap()
{
    if ( !m_georefI2M )
    {
        m_georefI2M = new QgsGeorefImageToMapWindow( nullptr, this );
        m_georefI2M->setAttribute( Qt::WA_DeleteOnClose, false );
        m_georefI2M->setWindowTitle( tr( "Image Registration · Image 2 Map" ) );

        connect( m_georefI2M, &QgsGeorefShellWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     if ( loadDataLayer( path ) )
                         statusBar()->showMessage(
                             tr( "Loaded the correction result into the main view: %1" ).arg( path ), 5000 );
                     else
                         statusBar()->showMessage(
                             tr( "Failed to load the correction result into the main view: %1" ).arg( path ), 6000 );
                 } );

        if ( m_projectContext )
        {
            ( void ) bindSessionSecondaryView(
                m_projectContext.get(), m_georefI2M->srcSessionMap(), m_georefI2MSrcViewId );
        }
    }
    m_georefI2M->show();
    m_georefI2M->raise();
    m_georefI2M->activateWindow();
}

#ifdef SICNU_HAS_CLASSIFY
void QgisDesktopWindow::openClassificationWindow()
{
    if ( !m_classifyWindow )
    {
        // iface = nullptr: load-to-main uses requestLoadToMainMap → loadDataLayer.
        m_classifyWindow = new QgsClassificationMainWindow( nullptr, this );
        m_classifyWindow->setAttribute( Qt::WA_DeleteOnClose, false );

        // Inject the shell Data Manager so merged-class outputs are registered
        // as Data Assets (Ticket 02). Other post-process ops are unaffected.
        if ( m_projectContext )
            m_classifyWindow->setDataManager( &m_projectContext->dataManager() );

        connect( m_classifyWindow, &QgsClassificationMainWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     if ( loadDataLayer( path ) )
                     {
                         statusBar()->showMessage(
                             tr( "Loaded the classification result into the main view: %1" ).arg( path ), 5000 );
                     }
                     else
                     {
                         statusBar()->showMessage(
                             tr( "Failed to load the classification result into the main view: %1" ).arg( path ), 6000 );
                     }
                 } );

        // Wave E: register session map as secondary Display View (DM owns bridge).
        if ( m_projectContext
             && !bindSessionSecondaryView( m_projectContext.get(),
                                           m_classifyWindow->sessionMap(),
                                           m_classifyViewId ) )
        {
            statusBar()->showMessage(
                tr( "Classification session not registered as a display view (using session-local layer stack)" ), 4000 );
        }
    }
    m_classifyWindow->show();
    m_classifyWindow->raise();
    m_classifyWindow->activateWindow();
}
#else
void QgisDesktopWindow::openClassificationWindow() {
    QMessageBox::information(this, tr("Classification"),
        tr("Supervised classification requires OpenCV with the ml module.\n"
           "Install opencv (including opencv-ml) and rebuild:\n"
           "  cd build && cmake .. && make -j$(nproc)"));
}
#endif

#ifdef SICNU_HAS_OBIA
#include "rs_obia_main_window.h"
void QgisDesktopWindow::openObiaWindow()
{
    if ( !m_obiaWindow )
    {
        auto *obia = new RsObiaMainWindow( this );
        obia->setAttribute( Qt::WA_DeleteOnClose, false );
        // Product UX: load classified result into main project map on request.
        connect( obia, &RsObiaMainWindow::requestLoadToMainMap,
                 this, [this]( const QString &path ) {
                     if ( path.isEmpty() )
                         return;
                     if ( loadDataLayer( path ) )
                     {
                         statusBar()->showMessage(
                             tr( "Loaded the OBIA classification result into the main view: %1" ).arg( path ), 5000 );
                     }
                     else
                     {
                         statusBar()->showMessage(
                             tr( "Failed to load the OBIA result into the main view: %1" ).arg( path ), 6000 );
                     }
                 } );

        // Wave E: register OBIA session map as secondary Display View.
        if ( m_projectContext
             && !bindSessionSecondaryView( m_projectContext.get(),
                                           obia->sessionMap(),
                                           m_obiaViewId ) )
        {
            statusBar()->showMessage(
                tr( "OBIA session not registered as a display view (using session-local layer stack)" ), 4000 );
        }
        m_obiaWindow = obia;
    }
    m_obiaWindow->show();
    m_obiaWindow->raise();
    m_obiaWindow->activateWindow();
}
#else
void QgisDesktopWindow::openObiaWindow() {
    QMessageBox::information(this, tr("OBIA"),
        tr("Object-based classification requires OpenCV ml module.\n"
           "Build with SICNU_HAS_OBIA=ON to enable this feature."));
}
#endif


// ── Multi-view shell (Wave D) ─────────────────────────────────────────────

void QgisDesktopWindow::toggleSecondaryMapView( bool on )
{
    if ( on )
        openSecondaryMapView();
    else
        closeSecondaryMapView();
}

void QgisDesktopWindow::openSecondaryMapView()
{
    if ( !m_projectContext || !m_mapSplitter )
        return;

    if ( !m_secondaryMapView )
    {
        m_secondaryMapView = new SecondaryMapViewWidget( m_mapSplitter );
        m_mapSplitter->addWidget( m_secondaryMapView );
        m_mapSplitter->setStretchFactor( 0, 1 );
        m_mapSplitter->setStretchFactor( 1, 1 );
        m_mapSplitter->setSizes( { 600, 600 } );

        connect( m_secondaryMapView, &SecondaryMapViewWidget::activateRequested,
                 this, &QgisDesktopWindow::activateSecondaryMapView );
        connect( m_secondaryMapView, &SecondaryMapViewWidget::closeRequested,
                 this, &QgisDesktopWindow::closeSecondaryMapView );
        connect( m_secondaryMapView, &SecondaryMapViewWidget::syncFromMainRequested,
                 this, &QgisDesktopWindow::syncMainLayersToSecondaryView );

        // Wire pixel-level pan/zoom sync between the two canvases (Loop K4).
        if ( !m_dualViewportSync )
        {
            m_dualViewportSync = new RsDualViewportSyncController(
                m_mapCanvas, m_secondaryMapView->canvas(), this );
            // Default the View-menu toggle to checked once sync is live.
            if ( m_dualViewportSyncAction )
            {
                QSignalBlocker b( m_dualViewportSyncAction );
                m_dualViewportSyncAction->setChecked( true );
            }
        }
    }

    // Snap the secondary canvas to the primary's current viewport so the two
    // views start pixel-aligned.
    if ( m_dualViewportSync )
        m_dualViewportSync->snapSecondaryToPrimary();

    if ( m_secondaryViewId.isNull() )
    {
        const auto created =
            m_projectContext->createSecondaryView( m_secondaryMapView->viewSpec() );
        if ( !created )
        {
            QMessageBox::warning( this, tr( "Second View" ),
                                  tr( "Cannot create the second display view." ) );
            if ( m_secondaryViewAction )
            {
                QSignalBlocker b( m_secondaryViewAction );
                m_secondaryViewAction->setChecked( false );
            }
            return;
        }
        m_secondaryViewId = created.value();
        m_secondaryMapView->setViewId( m_secondaryViewId );
    }

    m_secondaryMapView->show();
    if ( m_secondaryViewAction )
    {
        QSignalBlocker b( m_secondaryViewAction );
        m_secondaryViewAction->setChecked( true );
    }
    statusBar()->showMessage( tr( "Second view open. Use 'Active' to switch the display target." ), 4000 );
}

void QgisDesktopWindow::closeSecondaryMapView()
{
    if ( m_projectContext && !m_secondaryViewId.isNull() )
    {
        // If secondary was active, fall back to main before teardown.
        if ( m_activeViewHost
             && m_activeViewHost->activeViewId() == m_secondaryViewId )
            activateMainMapView();

        ( void ) m_projectContext->removeView( m_secondaryViewId );
        m_secondaryViewId = {};
    }

    if ( m_secondaryMapView )
    {
        m_secondaryMapView->hide();
        m_secondaryMapView->setViewId( {} );
        m_secondaryMapView->setActiveHighlight( false );
    }
    // Tear down the dual-viewport sync controller — its secondary canvas is gone.
    if ( m_dualViewportSync )
    {
        m_dualViewportSync->setEnabled( false );
        delete m_dualViewportSync;
        m_dualViewportSync = nullptr;
        if ( m_dualViewportSyncAction )
        {
            QSignalBlocker b( m_dualViewportSyncAction );
            m_dualViewportSyncAction->setChecked( false );
        }
    }
    if ( m_secondaryViewAction )
    {
        QSignalBlocker b( m_secondaryViewAction );
        m_secondaryViewAction->setChecked( false );
    }
    statusBar()->showMessage( tr( "Second view closed" ), 2500 );
}

void QgisDesktopWindow::activateMainMapView()
{
    if ( !m_activeViewHost || !m_projectContext )
        return;
    m_activeViewHost->setActiveViewId( m_projectContext->mainViewId() );
    if ( m_secondaryMapView )
        m_secondaryMapView->setActiveHighlight( false );
    statusBar()->showMessage( tr( "Active view: main view" ), 2500 );
}

void QgisDesktopWindow::activateSecondaryMapView()
{
    if ( !m_activeViewHost || m_secondaryViewId.isNull() )
    {
        openSecondaryMapView();
        if ( m_secondaryViewId.isNull() )
            return;
    }
    if ( !m_activeViewHost->setActiveViewId( m_secondaryViewId ) )
    {
        statusBar()->showMessage( tr( "Cannot activate the second view" ), 3000 );
        return;
    }
    if ( m_secondaryMapView )
        m_secondaryMapView->setActiveHighlight( true );
    statusBar()->showMessage( tr( "Active view: second view (open / show operations route here)" ), 3500 );
}

void QgisDesktopWindow::syncMainLayersToSecondaryView()
{
    if ( !m_projectContext || m_secondaryViewId.isNull() )
    {
        statusBar()->showMessage( tr( "Open the second view first" ), 3000 );
        return;
    }

    auto &display = m_projectContext->displayManager();
    const auto mainView = display.view( m_projectContext->mainViewId() );
    if ( !mainView || mainView->layerIds().isEmpty() )
    {
        statusBar()->showMessage( tr( "The main view has no display layers to sync" ), 3000 );
        return;
    }

    int cloned = 0;
    for ( const auto &layerId : mainView->layerIds() )
    {
        const auto result = display.cloneLayer( layerId, m_secondaryViewId );
        if ( result )
            ++cloned;
    }
    statusBar()->showMessage(
        tr( "Cloned %1 main view layers into the second view" ).arg( cloned ), 4000 );
}

void QgisDesktopWindow::toggleDualViewportSync( bool on )
{
    if ( !m_dualViewportSync )
    {
        // No controller yet — keep the action unchecked until the secondary
        // view is opened (which lazily creates the controller).
        if ( m_dualViewportSyncAction )
        {
            QSignalBlocker b( m_dualViewportSyncAction );
            m_dualViewportSyncAction->setChecked( false );
        }
        statusBar()->showMessage( tr( "Open the second view first to enable linked viewports" ), 3000 );
        return;
    }
    m_dualViewportSync->setEnabled( on );
    if ( on )
        m_dualViewportSync->snapSecondaryToPrimary();
    statusBar()->showMessage( on ? tr( "Linked viewports enabled" ) : tr( "Linked viewports paused" ), 2500 );
}

void QgisDesktopWindow::zoomFullExtent()
{
    m_mapCanvas->zoomToFullExtent();
    statusBar()->showMessage(tr("Full Extent"), 2000);
}

void QgisDesktopWindow::zoomToLayer()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (!selected.isEmpty()) {
        QgsMapLayer *target = selected.first();
        QgsRectangle extent = target->extent();
        const QgsCoordinateReferenceSystem canvasCrs = m_mapCanvas->mapSettings().destinationCrs();
        if ( target->crs().isValid() && canvasCrs.isValid() && target->crs() != canvasCrs )
        {
            try
            {
                const QgsCoordinateTransform ct( target->crs(), canvasCrs, QgsProject::instance() );
                extent = ct.transformBoundingBox( extent );
            }
            catch ( ... )
            {
            }
        }
        m_mapCanvas->setExtent(extent);
        m_mapCanvas->refresh();
        statusBar()->showMessage(tr("Zoom to Layer"), 2000);
    }
}

void QgisDesktopWindow::refreshMap()
{
    m_mapCanvas->refresh();
    statusBar()->showMessage(tr("Canvas refreshed"), 2000);
}
