// test_edit_snapping.cpp — F11 Package C: snapping configuration authority.
//
// Oracle: values read back from the canvas's own QgsSnappingUtils (the
// single engine) must equal what the controller was told; an independent
// snapToMap() probe must actually snap to a hand-placed vertex.
#include <catch2/catch_test_macros.hpp>

#include "editing/rs_snapping_controller.h"

#include <QApplication>

#include <qgsmapcanvas.h>
#include <qgspointlocator.h>
#include <qgsproject.h>
#include <qgssnappingutils.h>
#include <qgsvectorlayer.h>

namespace
{

QApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_edit_snapping";
    static char *argv[] = { arg0, nullptr };
    return qApp ? nullptr : new QApplication( argc, argv );
}

} // namespace

TEST_CASE( "controller configures the canvas snapping engine (read-back)",
           "[editing][snapping][f11]" )
{
    ensureApp();
    QgsMapCanvas canvas;
    canvas.resize( 400, 400 );
    RsSnappingController controller( &canvas );

    // Baseline: snapping off by default on a fresh canvas.
    CHECK_FALSE( controller.enabled() );

    controller.enableVertexSegment( 7.5, Qgis::MapToolUnit::Pixels, true );
    CHECK( controller.enabled() );
    CHECK( controller.types() == ( Qgis::SnappingTypes( Qgis::SnappingType::Vertex )
                                   | Qgis::SnappingType::Segment ) );
    CHECK( controller.tolerance() == 7.5 );
    CHECK( controller.units() == Qgis::MapToolUnit::Pixels );
    CHECK( controller.mode() == Qgis::SnappingMode::ActiveLayer );
    CHECK( controller.intersectionSnapping() );

    // The exact same engine instance the canvas exposes — no second config.
    CHECK( controller.snappingUtils() == canvas.snappingUtils() );

    bool notified = false;
    QObject::connect( &controller, &RsSnappingController::snappingChanged,
                      [&notified]() { notified = true; } );
    controller.enableVertexSegment( 1.0, Qgis::MapToolUnit::Project, false );
    CHECK( notified );
    CHECK( controller.tolerance() == 1.0 );
    CHECK_FALSE( controller.intersectionSnapping() );
}

TEST_CASE( "configured snapping actually snaps to a known vertex",
           "[editing][snapping][f11][oracle]" )
{
    ensureApp();
    QgsProject::instance()->clear();
    QgsMapCanvas canvas;
    canvas.resize( 400, 400 );
    // snapToMap() refuses without valid map settings (extent + layers +
    // destination CRS) — set them up before probing.
    canvas.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
    canvas.setExtent( QgsRectangle( -5, -5, 5, 5 ) );
    RsSnappingController controller( &canvas );
    controller.enableVertexSegment( 0.5, Qgis::MapToolUnit::Project, false );

    // Heap-allocated: addMapLayer + removeMapLayer transfer ownership to
    // and from the project, which deletes the layer on removal.
    QgsVectorLayer *layer = new QgsVectorLayer( QStringLiteral( "LineString?crs=EPSG:4326" ),
                                                QStringLiteral( "snapline" ), QStringLiteral( "memory" ) );
    REQUIRE( layer->isValid() );
    QgsFeature f( layer->fields() );
    f.setGeometry( QgsGeometry::fromPolylineXY(
      { QgsPointXY( 0.0, 0.0 ), QgsPointXY( 1.0, 1.0 ) } ) );
    REQUIRE( layer->startEditing() );
    REQUIRE( layer->addFeature( f ) );
    REQUIRE( layer->commitChanges() );
    QgsProject::instance()->addMapLayer( layer, false );
    canvas.setLayers( QList<QgsMapLayer *>() << layer );

    // The engine's ActiveLayer mode snaps on its configured current layer.
    canvas.snappingUtils()->setCurrentLayer( layer );

    // Independent probe: 0.25 degrees away from the (0,0) vertex, well
    // inside the 0.5 tolerance — the match must be the exact hand-placed
    // vertex, not something derived from our own code.
    INFO( "snap enabled: " << controller.enabled() );
    const QgsPointLocator::Match match =
      canvas.snappingUtils()->snapToMap( QgsPointXY( 0.25, 0.0 ) );
    REQUIRE( match.isValid() );
    CHECK( match.type() == QgsPointLocator::Vertex );
    CHECK( match.point().x() == 0.0 );
    CHECK( match.point().y() == 0.0 );

    // Outside the tolerance: no snap.
    const QgsPointLocator::Match far =
      canvas.snappingUtils()->snapToMap( QgsPointXY( 5.0, 5.0 ) );
    CHECK_FALSE( far.isValid() );

    QgsProject::instance()->removeMapLayer( layer );
}
