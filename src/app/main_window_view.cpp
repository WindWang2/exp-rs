// main_window_view.cpp — Map view and navigation actions
#include "main_window.h"
#include "active_view_host.h"
#include "project_context.h"
#include "shell/rs_session_map_workspace.h"
#include "shell/secondary_map_view_widget.h"

#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
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
    statusBar()->showMessage( tr( "Measure Distance: click to add points, double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::measureArea()
{
    m_mapCanvas->setMapTool( m_measureAreaTool );
    statusBar()->showMessage( tr( "Measure Area: click to add points, double-click or right-click to finish" ), 5000 );
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
                             tr( "已加载校正结果到主图：%1" ).arg( path ), 5000 );
                     else
                         statusBar()->showMessage(
                             tr( "加载校正结果到主图失败：%1" ).arg( path ), 6000 );
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
                             tr( "已加载校正结果到主图：%1" ).arg( path ), 5000 );
                     else
                         statusBar()->showMessage(
                             tr( "加载校正结果到主图失败：%1" ).arg( path ), 6000 );
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
                             tr( "已加载分类结果到主图：%1" ).arg( path ), 5000 );
                     }
                     else
                     {
                         statusBar()->showMessage(
                             tr( "加载分类结果到主图失败：%1" ).arg( path ), 6000 );
                     }
                 } );

        // Wave E: register session map as secondary Display View (DM owns bridge).
        if ( m_projectContext
             && !bindSessionSecondaryView( m_projectContext.get(),
                                           m_classifyWindow->sessionMap(),
                                           m_classifyViewId ) )
        {
            statusBar()->showMessage(
                tr( "分类会话未注册为显示视图（使用会话本地图层栈）" ), 4000 );
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
                             tr( "已加载 OBIA 分类结果到主图：%1" ).arg( path ), 5000 );
                     }
                     else
                     {
                         statusBar()->showMessage(
                             tr( "加载 OBIA 结果到主图失败：%1" ).arg( path ), 6000 );
                     }
                 } );

        // Wave E: register OBIA session map as secondary Display View.
        if ( m_projectContext
             && !bindSessionSecondaryView( m_projectContext.get(),
                                           obia->sessionMap(),
                                           m_obiaViewId ) )
        {
            statusBar()->showMessage(
                tr( "OBIA 会话未注册为显示视图（使用会话本地图层栈）" ), 4000 );
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
    }

    if ( m_secondaryViewId.isNull() )
    {
        const auto created =
            m_projectContext->createSecondaryView( m_secondaryMapView->viewSpec() );
        if ( !created )
        {
            QMessageBox::warning( this, tr( "第二视图" ),
                                  tr( "无法创建第二显示视图。" ) );
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
    statusBar()->showMessage( tr( "第二视图已打开。可用「活动」切换显示目标。" ), 4000 );
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
    if ( m_secondaryViewAction )
    {
        QSignalBlocker b( m_secondaryViewAction );
        m_secondaryViewAction->setChecked( false );
    }
    statusBar()->showMessage( tr( "第二视图已关闭" ), 2500 );
}

void QgisDesktopWindow::activateMainMapView()
{
    if ( !m_activeViewHost || !m_projectContext )
        return;
    m_activeViewHost->setActiveViewId( m_projectContext->mainViewId() );
    if ( m_secondaryMapView )
        m_secondaryMapView->setActiveHighlight( false );
    statusBar()->showMessage( tr( "活动视图：主视图" ), 2500 );
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
        statusBar()->showMessage( tr( "无法激活第二视图" ), 3000 );
        return;
    }
    if ( m_secondaryMapView )
        m_secondaryMapView->setActiveHighlight( true );
    statusBar()->showMessage( tr( "活动视图：第二视图（打开/显示将路由到此）" ), 3500 );
}

void QgisDesktopWindow::syncMainLayersToSecondaryView()
{
    if ( !m_projectContext || m_secondaryViewId.isNull() )
    {
        statusBar()->showMessage( tr( "请先打开第二视图" ), 3000 );
        return;
    }

    auto &display = m_projectContext->displayManager();
    const auto mainView = display.view( m_projectContext->mainViewId() );
    if ( !mainView || mainView->layerIds().isEmpty() )
    {
        statusBar()->showMessage( tr( "主视图没有可同步的显示图层" ), 3000 );
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
        tr( "已将 %1 个主视图图层克隆到第二视图" ).arg( cloned ), 4000 );
}

void QgisDesktopWindow::zoomFullExtent()
{
    m_mapCanvas->zoomToFullExtent();
    statusBar()->showMessage("Full extent", 2000);
}

void QgisDesktopWindow::zoomToLayer()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (!selected.isEmpty()) {
        m_mapCanvas->setExtent(selected.first()->extent());
        m_mapCanvas->refresh();
        statusBar()->showMessage("Zoomed to layer", 2000);
    }
}

void QgisDesktopWindow::refreshMap()
{
    m_mapCanvas->refresh();
    statusBar()->showMessage("Map refreshed", 2000);
}
