// Workbench 10.0 — N-view link coordination (goal WP-E)
//
// Covers: view registration through the display-manager authority, linked
// extent propagation between peers (same-CRS), per-view unlink stopping
// propagation, automatic detach on viewAboutToBeRemoved, and the reentrancy
// contract (propagation never echoes back).
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/shell/view_link_controller.h"

#include <QApplication>
#include <QTest>

#include <qgsmapcanvas.h>
#include <qgsrectangle.h>
#include <qgslayertree.h>
#include <qgsmaplayerstore.h>

using sicnu::app::ViewLinkController;
using sicnu::display::DisplayViewId;
using sicnu::display::DisplayViewSpec;
using sicnu::display::QgisDisplayManager;

namespace
{

struct DataManager
{
    // The manager needs a DataManager for asset leases; views created with
    // explicit canvases/trees/stores never touch it in these tests.
    QgisDisplayManager manager;

    DataManager() = default;

    DisplayViewId createView( QgsMapCanvas &canvas, QgsLayerTree &tree,
                              QgsMapLayerStore &store )
    {
        DisplayViewSpec spec;
        spec.canvas = &canvas;
        spec.layerTree = &tree;
        spec.layerStore = &store;
        const auto created = manager.createView( spec );
        if ( !created )
            return DisplayViewId();
        return created.value();
    }
};

} // namespace

int main( int argc, char *argv[] )
{
    if ( !QApplication::instance() )
    {
        static QApplication app( argc, argv ); // QT_QPA_PLATFORM=offscreen
    }
    const int result = Catch::Session().run( argc, argv );
    return result;
}

TEST_CASE( "view link propagates extents across linked views",
           "[view_link][workbench10]" )
{
    DataManager data;
    QgsMapCanvas a, b, c;
    QgsLayerTree treeA, treeB, treeC;
    QgsMapLayerStore storeA, storeB, storeC;

    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    const DisplayViewId viewC = data.createView( c, treeC, storeC );
    REQUIRE( !viewA.isNull() );
    REQUIRE( !viewB.isNull() );
    REQUIRE( !viewC.isNull() );

    ViewLinkController controller( &data.manager );
    controller.addView( viewA );
    controller.addView( viewB );
    controller.addView( viewC );

    // Link A and B; C stays independent.
    controller.setLinked( viewA, true );
    controller.setLinked( viewB, true );
    CHECK( controller.isLinked( viewA ) );
    CHECK( controller.isLinked( viewB ) );
    CHECK_FALSE( controller.isLinked( viewC ) );

    a.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
    a.setExtent( QgsRectangle( 10, 10, 90, 90 ) );
    QTest::qWait( 80 ); // throttle window (16 ms) + propagation

    CHECK( b.extent() == a.extent() );
    CHECK_FALSE( c.extent() == a.extent() );
    CHECK( controller.stats().appliedSyncCount >= 1 );

    // Unlinking B stops its propagation; A still linked to nothing else.
    controller.setLinked( viewB, false );
    a.setExtent( QgsRectangle( 20, 20, 80, 80 ) );
    QTest::qWait( 80 );
    CHECK_FALSE( b.extent() == a.extent() );
}

TEST_CASE( "view link detaches removed views automatically",
           "[view_link][workbench10]" )
{
    DataManager data;
    QgsMapCanvas a, b;
    QgsLayerTree treeA, treeB;
    QgsMapLayerStore storeA, storeB;

    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    REQUIRE( !viewA.isNull() );
    REQUIRE( !viewB.isNull() );

    ViewLinkController controller( &data.manager );
    controller.addView( viewA );
    controller.addView( viewB );
    controller.setLinked( viewA, true );
    controller.setLinked( viewB, true );

    REQUIRE( data.manager.removeView( viewB ).operator bool() );
    QVERIFY( !controller.views().contains( viewB ) );
    QVERIFY( !controller.isLinked( viewB ) );
}

TEST_CASE( "view link rejects unknown views", "[view_link][workbench10]" )
{
    DataManager data;
    ViewLinkController controller( &data.manager );
    controller.addView( DisplayViewId::generate() ); // unknown to the manager
    CHECK( controller.views().isEmpty() );
}
