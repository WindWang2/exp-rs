// sicnu_app_interface.cpp — QGIS-modeled QgisInterface facade for SICNU GEO RS
#include "sicnu_app_interface.h"
#include "active_view_host.h"
#include "project_context.h"

#include <qgsmapcanvas.h>
#include <layertree/qgslayertreeview.h>
#include <qgsmessagebar.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QApplication>

SicnuAppInterface::SicnuAppInterface( QWidget *mainWindow,
                                      ActiveViewHost *activeViewHost,
                                      sicnu::app::ProjectContext *projectContext,
                                      QObject *parent )
    : QgisInterface()
    , m_mainWindow( mainWindow )
    , m_activeViewHost( activeViewHost )
    , m_bridge( activeViewHost )
    , m_projectContext( projectContext )
{
    setParent( parent );
}

QgsLayerTreeView *SicnuAppInterface::layerTreeView()
{
    return m_activeViewHost ? m_activeViewHost->layerTreeView() : nullptr;
}

QList<QgsMapCanvas *> SicnuAppInterface::mapCanvases()
{
    if ( m_activeViewHost && m_activeViewHost->mapCanvas() )
        return { m_activeViewHost->mapCanvas() };
    return {};
}

QgsMapLayer *SicnuAppInterface::activeLayer()
{
    return m_activeViewHost ? m_activeViewHost->activeLayer() : nullptr;
}

QgsMapCanvas *SicnuAppInterface::mapCanvas()
{
    return m_activeViewHost ? m_activeViewHost->mapCanvas() : nullptr;
}

QWidget *SicnuAppInterface::mainWindow()
{
    return m_mainWindow;
}

QgsMessageBar *SicnuAppInterface::messageBar()
{
    return m_activeViewHost ? m_activeViewHost->messageBar() : nullptr;
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
    Q_UNUSED( baseName );
    // Data/Display seam (ADR 0009/0010/0015): register + display only via ActiveViewHost.
    // No QgsProject::addMapLayer bypass.
    if ( !m_activeViewHost || rasterLayerPath.isEmpty() )
        return nullptr;

    const auto res = m_activeViewHost->openRasterPath( rasterLayerPath );
    if ( !res )
        return nullptr;

    if ( m_projectContext )
    {
        return qobject_cast<QgsRasterLayer *>(
          m_projectContext->displayManager().mapLayer( res.value() ) );
    }

    return qobject_cast<QgsRasterLayer *>( m_activeViewHost->mapLayer( res.value() ) );
}

QgsVectorLayer *SicnuAppInterface::addVectorLayer( const QString &vectorLayerPath, const QString &baseName, const QString &providerKey )
{
    Q_UNUSED( baseName );
    Q_UNUSED( providerKey );
    if ( !m_activeViewHost || vectorLayerPath.isEmpty() )
        return nullptr;

    const auto res = m_activeViewHost->openVectorPath( vectorLayerPath );
    if ( !res )
        return nullptr;

    if ( m_projectContext )
    {
        return qobject_cast<QgsVectorLayer *>(
          m_projectContext->displayManager().mapLayer( res.value() ) );
    }

    return qobject_cast<QgsVectorLayer *>( m_activeViewHost->mapLayer( res.value() ) );
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
