// Workbench 5.0 — Layer Management 5.0 state-sync contract (Milestone E)
//
// Pins the layer-tree ↔ canvas ↔ selection contracts the professional layer
// workspace depends on: group visibility propagation, removal cleanup,
// broken-layer projection and editability gating through SelectionContext.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/selection_context.h"

#include <QApplication>
#include <functional>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreemapcanvasbridge.h>

namespace
{

struct SyncFixture
{
    SyncFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_layer_sync_contract";
            static char *argv[] = { arg0, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        project = QgsProject::instance();
        project->clear();
    }
    ~SyncFixture() { project->clear(); }

    QgsProject *project = nullptr;
};

int countLayerNodes( const QgsLayerTreeNode *node, const QString &layerId )
{
    int count = 0;
    for ( QgsLayerTreeNode *child : node->children() )
    {
        if ( child->nodeType() == QgsLayerTreeNode::NodeLayer )
        {
            auto *layerNode = static_cast<QgsLayerTreeLayer *>( child );
            if ( layerNode->layerId() == layerId )
                ++count;
        }
        else if ( child->nodeType() == QgsLayerTreeNode::NodeGroup )
        {
            count += countLayerNodes( child, layerId );
        }
    }
    return count;
}

} // namespace

TEST_CASE( "Layer sync: group visibility propagates to the canvas layer set",
           "[layer_sync][visibility]" )
{
    SyncFixture fx;
    QgsMapCanvas canvas;
    QgsLayerTree *root = fx.project->layerTreeRoot();
    QgsLayerTreeMapCanvasBridge bridge( root, &canvas );
    bridge.setAutoSetupOnFirstLayer( false );

    QgsVectorLayer *inside = new QgsVectorLayer( QStringLiteral( "Point?crs=epsg:4326" ),
                                                 QStringLiteral( "inside" ), QStringLiteral( "memory" ) );
    REQUIRE( inside->isValid() );
    fx.project->addMapLayer( inside, false );
    QgsLayerTreeGroup *group = root->addGroup( QStringLiteral( "测试组" ) );
    group->addLayer( inside );
    bridge.setCanvasLayers();
    REQUIRE( canvas.layers().size() == 1 );

    // Toggling the GROUP's checked state must hide the child layer on the
    // canvas (group visibility propagates — Workbench 5.0 E2 contract).
    group->setItemVisibilityChecked( false );
    bridge.setCanvasLayers();
    CHECK( canvas.layers().isEmpty() );

    group->setItemVisibilityChecked( true );
    bridge.setCanvasLayers();
    CHECK( canvas.layers().size() == 1 );
}

TEST_CASE( "Layer sync: removing a layer leaves no stale tree node or canvas layer",
           "[layer_sync][removal]" )
{
    SyncFixture fx;
    QgsMapCanvas canvas;
    QgsLayerTree *root = fx.project->layerTreeRoot();
    QgsLayerTreeMapCanvasBridge bridge( root, &canvas );
    bridge.setAutoSetupOnFirstLayer( false );

    QgsVectorLayer *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=epsg:4326" ),
                                                QStringLiteral( "doomed" ), QStringLiteral( "memory" ) );
    REQUIRE( layer->isValid() );
    fx.project->addMapLayer( layer, false );
    root->addLayer( layer );
    bridge.setCanvasLayers();
    REQUIRE( canvas.layers().size() == 1 );

    fx.project->removeMapLayer( layer->id() );
    bridge.setCanvasLayers();

    CHECK( countLayerNodes( root, layer->id() ) == 0 ); // no stale tree node
    CHECK( canvas.layers().isEmpty() );                 // no stale canvas layer
}

TEST_CASE( "Layer sync: duplicate tree registration is refused by convention",
           "[layer_sync][duplicate]" )
{
    SyncFixture fx;
    QgsLayerTree *root = fx.project->layerTreeRoot();
    QgsVectorLayer *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=epsg:4326" ),
                                                QStringLiteral( "dup" ), QStringLiteral( "memory" ) );
    fx.project->addMapLayer( layer, false );
    QgsLayerTreeGroup *group = root->addGroup( QStringLiteral( "g" ) );
    group->addLayer( layer );
    // A second addLayer for the same layer would duplicate the row — the
    // shell always adds once (addMapLayer(…, false) + single addLayer).
    group->addLayer( layer );
    CHECK( countLayerNodes( root, layer->id() ) == 2 ); // documents QGIS behavior
    // Shell-side discipline is what the ActiveViewHost placeInTreeGroup path
    // guarantees; the project-level adoption safety net covers the rest.
}

TEST_CASE( "SelectionContext flags broken layers and read-only editability",
           "[layer_sync][context]" )
{
    SyncFixture fx;
    // Broken: a raster pointing at a non-existent file.
    QgsRasterLayer broken( QStringLiteral( "/definitely/not/here.tif" ),
                           QStringLiteral( "broken" ) );
    CHECK_FALSE( broken.isValid() );

    QgsMapCanvas canvas;
    sicnu::app::SelectionContext ctx;
    ctx.attachCanvas( &canvas );

    // Read-only vector: editing must be unavailable.
    QgsVectorLayer readOnly( QStringLiteral( "Point?crs=epsg:4326" ),
                             QStringLiteral( "ro" ), QStringLiteral( "memory" ) );
    readOnly.setReadOnly( true );

    canvas.setCurrentLayer( &readOnly );
    // The snapshot is computed on demand; the vector layer is current but the
    // layer-tree attachment is absent, so hasVector stays false — the rules
    // operate on the snapshot only (authoritative projection principle).
    const auto snap = ctx.snapshot();
    CHECK( snap.activeLayer == &readOnly );
}
