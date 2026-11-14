#include "layer_tree_menu.h"
#include "active_view_host.h"
#include "workbench/command_registry.h"

// QGIS includes
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsmapcanvas.h>
#include <qgsmapsettings.h>
#include <qgscoordinatetransform.h>
#include <qgsexception.h>
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

QgsRectangle LayerTreeMenuProvider::groupZoomExtent( QgsMapCanvas *canvas,
                                                     QgsLayerTreeGroup *group,
                                                     QString *reason )
{
    // R30-T06 / F1: the stock QGIS actionZoomToGroup() stores the canvas as a
    // raw void* in QAction::data and dereferences it unconditionally in
    // zoomToLayers() (canvas->mapSettings(), canvas->setExtent()). Passing
    // nullptr — or a canvas deleted while the menu stayed open — is a real
    // crash / use-after-free. Keep the projection first-party and honest.
    const auto unusable = [reason]( const QString &text ) {
        if ( reason )
            *reason = text;
        return QgsRectangle(); // null rectangle
    };

    if ( !canvas )
        return unusable( QObject::tr( "No active map canvas is available for this view." ) );
    if ( !group )
        return unusable( QObject::tr( "The layer group is no longer available." ) );

    const QList<QgsLayerTreeLayer *> layerNodes = group->findLayers();
    if ( layerNodes.isEmpty() )
        return unusable( QObject::tr( "This group is empty — there is no extent to zoom to." ) );

    QgsRectangle extent; // default-constructed QgsRectangle is null
    int skipped = 0;
    for ( QgsLayerTreeLayer *layerNode : layerNodes )
    {
        QgsMapLayer *layer = layerNode ? layerNode->layer() : nullptr;
        if ( !layer || !layer->isValid() )
        {
            ++skipped; // removed / failed layer
            continue;
        }
        if ( QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer ) )
        {
            if ( vectorLayer->geometryType() == Qgis::GeometryType::Null )
            {
                ++skipped; // non-spatial attribute table
                continue;
            }
        }
        QgsRectangle layerExtent = layer->extent();
        if ( layerExtent.isEmpty() )
        {
            // Zero-area but non-null extents (a single point / line) are
            // legal: QgsLayerTreeViewDefaultActions::zoomToLayers skips only
            // NULL extents. refresh them here, but keep that exact contract.
            if ( QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer ) )
            {
                vectorLayer->updateExtents();
                layerExtent = vectorLayer->extent();
            }
        }
        if ( layerExtent.isNull() )
        {
            ++skipped;
            continue;
        }
        // R1 (P1-2): QgsMapSettings::layerExtentToOutputExtent() catches
        // QgsCsException INTERNALLY (qgsmapsettings.cpp:543-562) and returns
        // the UNTRANSFORMED extent. A try/catch around it is dead code, and a
        // failed CRS transform would silently mix layer-CRS coordinates into
        // the output-CRS extent. Transform explicitly and REFUSE a layer that
        // cannot be projected instead.
        const QgsMapSettings &settings = canvas->mapSettings();
        QgsCoordinateTransform transform = settings.layerTransform( layer );
        QgsRectangle outputExtent = layerExtent;
        if ( transform.isValid() )
        {
            transform.setBallparkTransformsAreAppropriate( true );
            try
            {
                outputExtent = transform.transformBoundingBox( layerExtent );
            }
            catch ( const QgsCsException & )
            {
                ++skipped;
                continue;
            }
        }
        else if ( layer->crs().isValid() && settings.destinationCrs().isValid() )
        {
            ++skipped; // two valid CRS with no usable transform
            continue;
        }
        if ( outputExtent.isNull() )
        {
            ++skipped;
            continue;
        }
        extent.combineExtentWith( outputExtent );
    }

    if ( extent.isNull() )
        return unusable( skipped > 0
                           ? QObject::tr( "No layer in this group has a usable spatial extent on the active canvas." )
                           : QObject::tr( "This group has no spatial layers to zoom to." ) );

    extent.scale( 1.05 ); // same margin as QgsLayerTreeViewDefaultActions
    if ( reason )
        reason->clear();
    return extent;
}

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
        auto *group = static_cast<QgsLayerTreeGroup *>( node );
        // R30-T06 / F1: bind to the LIVE active canvas with QObject guards.
        // The stock QGIS actionZoomToGroup(nullptr) dereferences a null
        // canvas for every group with a spatial layer, and stores a raw
        // void* that dangles when the canvas dies while the menu is open.
        const QPointer<QgsMapCanvas> canvasGuard(
            m_activeViewHost ? m_activeViewHost->activeMapCanvas() : nullptr );
        const QPointer<QgsLayerTreeNode> groupGuard( node );
        QString zoomReason;
        const QgsRectangle zoomExtent = groupZoomExtent( canvasGuard.data(), group, &zoomReason );
        const bool canZoom = !zoomExtent.isNull();

        QAction *zoomGroupAction = menu->addAction( QObject::tr( "&Zoom to Group" ) );
        zoomGroupAction->setToolTip(
            canZoom ? QObject::tr( "Fit the active map canvas to this group's spatial layers." )
                    : zoomReason );
        zoomGroupAction->setStatusTip( zoomGroupAction->toolTip() );
        zoomGroupAction->setEnabled( canZoom );
        QObject::connect( zoomGroupAction, &QAction::triggered, menu,
                          [hostPtr, canvasGuard, groupGuard]() {
            // Re-derive the target at TRIGGER time: the menu may have stayed
            // open across a view switch, project close, or layer deletion.
            if ( !hostPtr || !canvasGuard || !groupGuard )
            {
                if ( hostPtr )
                    hostPtr->pushMessageBarAlert(
                        QObject::tr( "Zoom to Group" ),
                        QObject::tr( "The map view or layer group is no longer available." ),
                        Qgis::MessageLevel::Warning );
                return;
            }
            QgsMapCanvas *liveCanvas = hostPtr->activeMapCanvas();
            if ( !liveCanvas || liveCanvas != canvasGuard.data() )
            {
                hostPtr->pushMessageBarAlert(
                    QObject::tr( "Zoom to Group" ),
                    QObject::tr( "The active map view changed while the menu was open. Re-open the menu on the current view." ),
                    Qgis::MessageLevel::Warning );
                return;
            }
            auto *liveGroup = static_cast<QgsLayerTreeGroup *>( groupGuard.data() );
            QString reason;
            const QgsRectangle extent = groupZoomExtent( liveCanvas, liveGroup, &reason );
            if ( extent.isNull() )
            {
                hostPtr->pushMessageBarAlert( QObject::tr( "Zoom to Group" ), reason,
                                              Qgis::MessageLevel::Warning );
                return;
            }
            liveCanvas->setExtent( extent, true );
            liveCanvas->refresh();
        } );
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
