#include "layer_tree_menu.h"
#include "active_view_host.h"
#include "workbench/command_registry.h"

// QGIS includes
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsmapcanvas.h>
#include <qgslayertreenode.h>
#include <qgslayertreelayer.h>
#include <qgslayertreegroup.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include <QPointer>
#include <QFileDialog>

LayerTreeMenuProvider::LayerTreeMenuProvider( QgsLayerTreeView *view, ActiveViewHost *activeViewHost,
                                              sicnu::app::CommandRegistry *commandRegistry )
    : mView( view ), m_activeViewHost( activeViewHost ), m_registry( commandRegistry ) {}

QMenu *LayerTreeMenuProvider::createContextMenu()
{
    QMenu *menu = new QMenu();
    QModelIndex index = mView ? mView->currentIndex() : QModelIndex();
    QgsLayerTreeNode *node = index.isValid() ? mView->index2node( index ) : nullptr;
    QPointer<ActiveViewHost> hostPtr( m_activeViewHost );

    auto addRasterAction = [hostPtr]() {
        if ( hostPtr ) {
            const QString file = QFileDialog::getOpenFileName(
                nullptr, QObject::tr( "Add Raster Layer" ), QString(),
                QObject::tr( "Raster Files (*.tif *.tiff *.img *.dat *.pix *.vrt *.nc *.hdf *.h5 *.png *.jpg *.jpeg);;All Files (*.*)" ) );
            if ( !file.isEmpty() )
                hostPtr->openRasterPath( file );
        }
    };

    auto addVectorAction = [hostPtr]() {
        if ( hostPtr ) {
            const QString file = QFileDialog::getOpenFileName(
                nullptr, QObject::tr( "Add Vector Layer" ), QString(),
                QObject::tr( "Vector Files (*.shp *.gpkg *.geojson *.kml *.tab *.mif);;All Files (*.*)" ) );
            if ( !file.isEmpty() )
                hostPtr->openVectorPath( file );
        }
    };

    /// Registry projection (Workbench 5.0): one handler + one availability
    /// contract shared with the ribbon. Falls back to nothing when the
    /// registry was not wired (headless test provider).
    auto addCommand = [menu, this]( const QString &id ) -> QAction * {
        if ( !m_registry )
            return nullptr;
        QAction *action = m_registry->action( id );
        if ( !action )
            return nullptr;
        QAction *projection = menu->addAction( action->icon(), action->text() );
        projection->setEnabled( action->isEnabled() );
        projection->setToolTip( action->toolTip() );
        projection->setStatusTip( action->statusTip() );
        const QString reason = m_registry->unavailabilityReason( id );
        if ( !action->isEnabled() && !reason.isEmpty() )
            projection->setToolTip( QObject::tr( "%1\n⚠ %2" ).arg( action->toolTip(), reason ) );
        // #1097: registry may deleteLater() the backing action while the menu
        // is still open (unregisterCommandsMatching) — hold a QPointer.
        const QPointer<QAction> actionGuard( action );
        QObject::connect( projection, &QAction::triggered, projection, [actionGuard] {
            if ( actionGuard && actionGuard->isEnabled() )
                actionGuard->trigger();
        } );
        return projection;
    };

    if ( !node ) {
        if ( m_activeViewHost ) {
            QAction *actRaster = menu->addAction( QObject::tr( "Add Raster Layer..." ), menu, addRasterAction );
            actRaster->setToolTip( QObject::tr( "Open and load a multiband remote-sensing raster layer" ) );
            actRaster->setStatusTip( QObject::tr( "Add a raster imagery layer to the current project" ) );
            QAction *actVector = menu->addAction( QObject::tr( "Add Vector Layer..." ), menu, addVectorAction );
            actVector->setToolTip( QObject::tr( "Open and load a vector feature layer (Shapefile / GeoPackage)" ) );
            actVector->setStatusTip( QObject::tr( "Add a vector layer to the current project" ) );
        }
        menu->addSeparator();
        if ( mView ) {
            menu->addAction( mView->defaultActions()->actionAddGroup() );
        }
        return menu;
    }

    QgsLayerTreeViewDefaultActions *defActions = mView->defaultActions();

    if ( node->nodeType() == QgsLayerTreeNode::NodeGroup ) {
        menu->addAction( defActions->actionZoomToGroup( nullptr ) );
        menu->addAction( defActions->actionRenameGroupOrLayer() );
        // Group removal must go through ActiveViewHost so every layer in the
        // group is removed via QgisDisplayManager (Display Layer records and
        // Asset leases are released). The stock default action deletes the
        // tree nodes / project layers behind the manager's back, stranding
        // both ("asset is leased / cannot unload").
        if ( m_activeViewHost ) {
            QAction *removeAction = menu->addAction( QObject::tr( "&Remove" ) );
            removeAction->setToolTip(
                QObject::tr( "Remove the group and its layers from the display" ) );
            const QPointer<QgsLayerTreeNode> nodeGuard( node );
            QObject::connect( removeAction, &QAction::triggered, menu,
                              [hostPtr, nodeGuard]() {
                if ( !hostPtr || !nodeGuard )
                    return;
                auto *group = static_cast<QgsLayerTreeGroup *>( nodeGuard.data() );
                QList<QgsMapLayer *> layers;
                const QList<QgsLayerTreeLayer *> layerNodes = group->findLayers();
                for ( QgsLayerTreeLayer *layerNode : layerNodes ) {
                    if ( QgsMapLayer *layer = layerNode->layer() )
                        layers.append( layer );
                }
                hostPtr->removeLayers( layers );
                // removeLayers() ran a modal confirmation: the node may have
                // been destroyed from underneath meanwhile, so re-check the
                // guard. The group node is removed only when it no longer
                // holds layers (all removed, or it never had any) — a failed
                // per-layer removal keeps the group with its surviving
                // layers, and declining the confirmation removes nothing.
                if ( nodeGuard ) {
                    auto *survivingGroup = static_cast<QgsLayerTreeGroup *>( nodeGuard.data() );
                    if ( survivingGroup->findLayers().isEmpty() ) {
                        if ( QgsLayerTreeGroup *parent =
                                 qobject_cast<QgsLayerTreeGroup *>( survivingGroup->parent() ) )
                            parent->removeChildNode( survivingGroup );
                    }
                }
            } );
        } else {
            menu->addAction( defActions->actionRemoveGroupOrLayer() );
        }
        menu->addSeparator();
        menu->addAction( defActions->actionAddGroup() );
        menu->addAction( defActions->actionMutuallyExclusiveGroup() );
    } else if ( node->nodeType() == QgsLayerTreeNode::NodeLayer ) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>( node );
        QgsMapLayer *layer = layerNode->layer();
        QPointer<QgsMapLayer> layerPtr( layer );

        // Common layer commands through the registry projection (5.0 C/J):
        // identical handlers + availability as the ribbon 地图 tab.
        addCommand( QStringLiteral( "layer.zoomTo" ) );

        if ( layer && layer->type() == Qgis::LayerType::Raster ) {
            QAction *zoomNative = menu->addAction( QObject::tr( "Zoom to Native Resolution (1:1)" ) );
            zoomNative->setToolTip( QObject::tr( "Display the current raster at 1:1 native pixel resolution" ) );
            zoomNative->setStatusTip( QObject::tr( "Zoom to native pixel resolution" ) );
            QObject::connect( zoomNative, &QAction::triggered, menu, [hostPtr, layerPtr]() {
                if ( hostPtr && layerPtr ) {
                    hostPtr->zoomToNativeResolution( layerPtr.data() );
                }
            } );
        }

        addCommand( QStringLiteral( "layer.attributeTable" ) );
        addCommand( QStringLiteral( "layer.properties" ) );
        menu->addSeparator();
        menu->addAction( defActions->actionRenameGroupOrLayer() );
        menu->addAction( defActions->actionShowFeatureCount() );
        addCommand( QStringLiteral( "layer.remove" ) );
        menu->addSeparator();
        menu->addAction( defActions->actionMoveToTop() );
        menu->addAction( defActions->actionMoveToBottom() );
        menu->addAction( defActions->actionGroupSelected() );
    }

    menu->addSeparator();
    if ( m_activeViewHost ) {
        QAction *actRaster = menu->addAction( QObject::tr( "Add Raster Layer..." ), menu, addRasterAction );
        actRaster->setToolTip( QObject::tr( "Open and load a multiband remote-sensing raster layer" ) );
        actRaster->setStatusTip( QObject::tr( "Add a raster imagery layer to the current project" ) );
        QAction *actVector = menu->addAction( QObject::tr( "Add Vector Layer..." ), menu, addVectorAction );
        actVector->setToolTip( QObject::tr( "Open and load a vector feature layer (Shapefile / GeoPackage)" ) );
        actVector->setStatusTip( QObject::tr( "Add a vector layer to the current project" ) );
    }

    return menu;
}
