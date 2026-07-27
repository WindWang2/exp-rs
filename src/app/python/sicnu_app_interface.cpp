// sicnu_app_interface.cpp — QGIS-modeled QgisInterface facade for SICNU GEO RS
#include "sicnu_app_interface.h"
#include "active_view_host.h"
#include "project_context.h"

#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <qgsmessagebar.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QFileInfo>
#include <QApplication>

SicnuAppInterface::SicnuAppInterface( QWidget *mainWindow,
                                      ActiveViewHost *activeViewHost,
                                      sicnu::app::ProjectContext *projectContext,
                                      QgsMapCanvas *canvas,
                                      QgsMessageBar *messageBar,
                                      QObject *parent )
    : QgisInterface()
    , m_mainWindow( mainWindow )
    , m_activeViewHost( activeViewHost )
    , m_projectContext( projectContext )
    , m_canvas( canvas )
    , m_messageBar( messageBar )
{
    setParent( parent );
}

QgsLayerTreeView *SicnuAppInterface::layerTreeView()
{
    if ( m_activeViewHost )
        return m_activeViewHost->layerTreeView();
    return nullptr;
}

QList<QgsMapCanvas *> SicnuAppInterface::mapCanvases()
{
    if ( m_canvas )
        return { m_canvas };
    return {};
}

QgsMapLayer *SicnuAppInterface::activeLayer()
{
    if ( m_activeViewHost )
        return m_activeViewHost->activeLayer();
    return nullptr;
}

QgsMapCanvas *SicnuAppInterface::mapCanvas()
{
    if ( m_canvas )
        return m_canvas;
    if ( m_activeViewHost && m_activeViewHost->layerTreeModel() )
    {
        // Fallback
    }
    return nullptr;
}

QWidget *SicnuAppInterface::mainWindow()
{
    return m_mainWindow;
}

QgsMessageBar *SicnuAppInterface::messageBar()
{
    return m_messageBar;
}

QFont SicnuAppInterface::defaultStyleSheetFont()
{
    return QApplication::font();
}

QMenu *SicnuAppInterface::pluginMenu()
{
    if ( !m_pluginMenu && m_mainWindow )
    {
        if ( auto *mainWin = qobject_cast<QMainWindow *>( m_mainWindow ) )
        {
            m_pluginMenu = mainWin->menuBar()->addMenu( tr( "插件" ) );
        }
    }
    return m_pluginMenu;
}

QToolBar *SicnuAppInterface::pluginToolBar()
{
    if ( !m_pluginToolBar && m_mainWindow )
    {
        if ( auto *mainWin = qobject_cast<QMainWindow *>( m_mainWindow ) )
        {
            m_pluginToolBar = mainWin->addToolBar( tr( "插件工具栏" ) );
            m_pluginToolBar->setObjectName( QStringLiteral( "pluginToolBar" ) );
        }
    }
    return m_pluginToolBar;
}

QgsRasterLayer *SicnuAppInterface::addRasterLayer( const QString &rasterLayerPath, const QString &baseName )
{
    if ( m_activeViewHost )
    {
        const auto res = m_activeViewHost->openRasterPath( rasterLayerPath );
        if ( res && m_projectContext )
        {
            const auto mainView = m_projectContext->displayManager().view( m_projectContext->mainViewId() );
            if ( mainView && !mainView->layerIds().isEmpty() )
            {
                const auto displayLayerId = mainView->layerIds().last();
                return qobject_cast<QgsRasterLayer *>( m_projectContext->displayManager().mapLayer( displayLayerId ) );
            }
        }
    }
    // Headless / fallback
    const QString name = baseName.isEmpty() ? QFileInfo( rasterLayerPath ).baseName() : baseName;
    auto *layer = new QgsRasterLayer( rasterLayerPath, name );
    if ( layer->isValid() )
    {
        QgsProject::instance()->addMapLayer( layer );
        return layer;
    }
    delete layer;
    return nullptr;
}

QgsVectorLayer *SicnuAppInterface::addVectorLayer( const QString &vectorLayerPath, const QString &baseName, const QString &providerKey )
{
    Q_UNUSED( providerKey );
    if ( m_activeViewHost )
    {
        const auto res = m_activeViewHost->openVectorPath( vectorLayerPath );
        if ( res && m_projectContext )
        {
            const auto mainView = m_projectContext->displayManager().view( m_projectContext->mainViewId() );
            if ( mainView && !mainView->layerIds().isEmpty() )
            {
                const auto displayLayerId = mainView->layerIds().last();
                return qobject_cast<QgsVectorLayer *>( m_projectContext->displayManager().mapLayer( displayLayerId ) );
            }
        }
    }
    // Headless / fallback
    const QString name = baseName.isEmpty() ? QFileInfo( vectorLayerPath ).baseName() : baseName;
    auto *layer = new QgsVectorLayer( vectorLayerPath, name, QStringLiteral( "ogr" ) );
    if ( layer->isValid() )
    {
        QgsProject::instance()->addMapLayer( layer );
        return layer;
    }
    delete layer;
    return nullptr;
}

void SicnuAppInterface::addPluginToMenu( const QString &name, QAction *action )
{
    if ( !action )
        return;
    QMenu *menu = pluginMenu();
    if ( !menu )
        return;

    if ( !name.isEmpty() )
    {
        QMenu *subMenu = nullptr;
        for ( QAction *act : menu->actions() )
        {
            if ( act->menu() && act->menu()->title() == name )
            {
                subMenu = act->menu();
                break;
            }
        }
        if ( !subMenu )
        {
            subMenu = menu->addMenu( name );
        }
        subMenu->addAction( action );
    }
    else
    {
        menu->addAction( action );
    }
}

void SicnuAppInterface::removePluginMenu( const QString &name, QAction *action )
{
    if ( !action )
        return;
    QMenu *menu = pluginMenu();
    if ( !menu )
        return;

    if ( !name.isEmpty() )
    {
        for ( QAction *act : menu->actions() )
        {
            if ( act->menu() && act->menu()->title() == name )
            {
                act->menu()->removeAction( action );
                break;
            }
        }
    }
    else
    {
        menu->removeAction( action );
    }
}

int SicnuAppInterface::addToolBarIcon( QAction *action )
{
    if ( !action )
        return -1;
    QToolBar *tb = pluginToolBar();
    if ( tb )
    {
        tb->addAction( action );
        return 0;
    }
    return -1;
}

QAction *SicnuAppInterface::addToolBarWidget( QWidget *widget )
{
    if ( !widget )
        return nullptr;
    QToolBar *tb = pluginToolBar();
    if ( tb )
    {
        return tb->addWidget( widget );
    }
    return nullptr;
}

void SicnuAppInterface::removeToolBarIcon( QAction *action )
{
    if ( !action )
        return;
    QToolBar *tb = pluginToolBar();
    if ( tb )
    {
        tb->removeAction( action );
    }
}

void SicnuAppInterface::addDockWidget( Qt::DockWidgetArea area, QDockWidget *dockWidget )
{
    if ( !dockWidget || !m_mainWindow )
        return;
    if ( auto *mainWin = qobject_cast<QMainWindow *>( m_mainWindow ) )
    {
        mainWin->addDockWidget( area, dockWidget );
    }
}

void SicnuAppInterface::removeDockWidget( QDockWidget *dockWidget )
{
    if ( !dockWidget || !m_mainWindow )
        return;
    if ( auto *mainWin = qobject_cast<QMainWindow *>( m_mainWindow ) )
    {
        mainWin->removeDockWidget( dockWidget );
    }
}
