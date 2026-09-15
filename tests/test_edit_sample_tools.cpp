// test_edit_sample_tools.cpp — F11 Package B: brush/erase sample painting.
//
// Drives the tools with synthetic QgsMapMouseEvent on an offscreen canvas
// (test_roi_tool_polygon pattern). Oracles:
//   * brush stroke area vs the independent regular-polygon formula
//     (1/2 · n · r² · sin(2π/n), n = 4·kDiscSegments for a GEOS point
//     buffer with n/4 segments per quadrant);
//   * erase survivor ids are hand-known;
//   * one stroke = one undo step (feature counts via live iteration).
#include <catch2/catch_test_macros.hpp>

#include "editing/rs_edit_command_guard.h"
#include "editing/rs_edit_session.h"
#include "editing/rs_sample_brush_tool.h"
#include "editing/rs_sample_erase_tool.h"

#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QSignalSpy>

#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include <cmath>

namespace
{

QApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_edit_sample_tools";
    static char *argv[] = { arg0, nullptr };
    return qApp ? nullptr : new QApplication( argc, argv );
}

qlonglong liveCount( QgsVectorLayer *layer )
{
    qlonglong n = 0;
    QgsFeatureIterator it = layer->getFeatures();
    QgsFeature f;
    while ( it.nextFeature( f ) )
        ++n;
    return n;
}

/// 1 canvas pixel = 1 map unit; layer CRS is EPSG:4326, so the brush radius
/// is 10 DEGREES (D7: radius lives in layer CRS units — asserted here too).
struct ToolFixture
{
    ToolFixture()
    {
        canvas.resize( 500, 500 );
        // Identity canvas↔layer CRS: tool coordinate conversion must be a
        // no-op so the hand-placed screen positions map 1:1 (y flipped).
        canvas.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
        canvas.setExtent( QgsRectangle( 0, 0, 500, 500 ) );
    }

    QgsMapCanvas canvas;
    QgsVectorLayer layer{ QStringLiteral( "MultiPolygon?crs=EPSG:4326&field=class:int" ),
                          QStringLiteral( "samples" ), QStringLiteral( "memory" ) };
    RsEditSession session;
};

// Screen positions are DERIVED from map coordinates through the canvas
// pixel map — robust against y-flip and any device pixel ratio.
QPointF toScreen( QgsMapCanvas *canvas, double mapX, double mapY )
{
    const QgsPointXY screen = canvas->mapSettings().mapToPixel().transform( mapX, mapY );
    return QPointF( screen.x(), screen.y() );
}

void firePress( QgsMapCanvas *canvas, QgsMapTool *tool, double mapX, double mapY )
{
    const QPointF screen = toScreen( canvas, mapX, mapY );
    QMouseEvent me( QEvent::MouseButtonPress, screen,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QgsMapMouseEvent mme( canvas, &me );
    tool->canvasPressEvent( &mme );
}

void fireMove( QgsMapCanvas *canvas, QgsMapTool *tool, double mapX, double mapY )
{
    const QPointF screen = toScreen( canvas, mapX, mapY );
    QMouseEvent me( QEvent::MouseMove, screen,
                    Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QgsMapMouseEvent mme( canvas, &me );
    tool->canvasMoveEvent( &mme );
}

void fireRelease( QgsMapCanvas *canvas, QgsMapTool *tool, double mapX, double mapY )
{
    const QPointF screen = toScreen( canvas, mapX, mapY );
    QMouseEvent me( QEvent::MouseButtonRelease, screen,
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QgsMapMouseEvent mme( canvas, &me );
    tool->canvasReleaseEvent( &mme );
}

void seedSamples( QgsVectorLayer *layer, const QVector<QgsPointXY> &points )
{
    // Seed through a commit, then re-enter edit mode: addFeature outside an
    // explicit command pushes its own undo command, which would pollute the
    // per-stroke undo-depth assertions below.
    if ( !layer->isEditable() )
        REQUIRE( layer->startEditing() );
    if ( points.isEmpty() )
        return; // nothing to seed; leave the current edit session alone
    for ( const QgsPointXY &p : points )
    {
        QgsFeature f( layer->fields() );
        // The sample layer is polygon-typed: seed a small square centered
        // on p (±1 map unit).
        f.setGeometry( QgsGeometry::fromPolygonXY( { QVector<QgsPointXY>{
          QgsPointXY( p.x() - 1, p.y() - 1 ), QgsPointXY( p.x() + 1, p.y() - 1 ),
          QgsPointXY( p.x() + 1, p.y() + 1 ), QgsPointXY( p.x() - 1, p.y() + 1 ),
          QgsPointXY( p.x() - 1, p.y() - 1 ) } } ) );
        REQUIRE( layer->addFeature( f ) );
    }
    const bool committed = layer->commitChanges();
    if ( !committed )
        WARN( "seed commit failed: " << layer->commitErrors().join( QStringLiteral( "; " ) ).toStdString() );
    REQUIRE( committed );
    REQUIRE( layer->startEditing() );
}

} // namespace

TEST_CASE( "brush stroke adds one multipart feature and undoes in one step",
           "[editing][brush][f11][oracle1]" )
{
    ensureApp();
    ToolFixture fx;
    REQUIRE( fx.layer.startEditing() );
    seedSamples( &fx.layer, {} );
    REQUIRE( fx.session.attachLayer( &fx.layer ).isEmpty() );

    RsSampleBrushTool tool( &fx.canvas );
    tool.setTargetLayer( &fx.layer );
    tool.setSession( &fx.session );
    tool.setRadius( 10.0 );
    QSignalSpy committedSpy( &tool, &RsSampleBrushTool::strokeCommitted );

    firePress( &fx.canvas, &tool, 100, 100 );
    fireMove( &fx.canvas, &tool, 130, 100 );
    fireMove( &fx.canvas, &tool, 160, 100 );
    fireRelease( &fx.canvas, &tool, 160, 100 );

    REQUIRE( committedSpy.count() == 1 );
    CHECK( committedSpy.first().at( 0 ).toInt() == 1 );
    REQUIRE( liveCount( &fx.layer ) == 1 );
    REQUIRE( fx.session.state( fx.layer.id() ).undoDepth == 1 );

    // Independent area oracle: three discs, each the regular n-gon the GEOS
    // point buffer produces (n = 4 segments-per-quadrant vertices overall).
    QgsFeature f;
    REQUIRE( fx.layer.getFeatures().nextFeature( f ) );
    const QgsGeometry stroke = f.geometry();
    REQUIRE( stroke.type() == Qgis::GeometryType::Polygon );
    // Area is the oracle, not part count — the union of the touching discs
    // merges freely, so only the total area pins the stroke down.
    const int n = 4 * RsSampleBrushTool::kDiscSegments;
    const double discArea = 0.5 * n * 10.0 * 10.0 * std::sin( 2.0 * M_PI / n );
    // Three discs spaced 30 apart with r=10: discs touch but do NOT overlap
    // (distance 30 > 2r=20), so areas sum exactly.
    CHECK( stroke.area() == Catch::Approx( 3.0 * discArea ).margin( discArea * 0.02 ) );

    // One undo removes the whole stroke.
    REQUIRE( fx.session.undo( fx.layer.id() ) );
    CHECK( liveCount( &fx.layer ) == 0 );
    REQUIRE( fx.session.redo( fx.layer.id() ) );
    CHECK( liveCount( &fx.layer ) == 1 );
}

TEST_CASE( "brush refuses without target layer / non-editable layer / lock",
           "[editing][brush][f11][negative]" )
{
    ensureApp();
    ToolFixture fx;
    fx.layer.startEditing();
    fx.session.attachLayer( &fx.layer );

    // 1) No target layer.
    {
        RsSampleBrushTool tool( &fx.canvas );
        tool.setRadius( 10.0 );
        QSignalSpy refused( &tool, &RsSampleBrushTool::strokeRefused );
        firePress( &fx.canvas, &tool, 50, 50 );
        fireRelease( &fx.canvas, &tool, 50, 50 );
        REQUIRE( refused.count() == 1 );
        CHECK_FALSE( refused.first().at( 0 ).toString().isEmpty() );
    }

    // 2) Layer not editable.
    {
        fx.layer.commitChanges();
        RsSampleBrushTool tool( &fx.canvas );
        tool.setTargetLayer( &fx.layer );
        tool.setSession( &fx.session );
        tool.setRadius( 10.0 );
        QSignalSpy refused( &tool, &RsSampleBrushTool::strokeRefused );
        firePress( &fx.canvas, &tool, 50, 50 );
        fireRelease( &fx.canvas, &tool, 50, 50 );
        REQUIRE( refused.count() == 1 );
    }

    // 3) Session-locked layer.
    {
        REQUIRE( fx.layer.startEditing() );
        if ( !fx.session.isAttached( fx.layer.id() ) )
            REQUIRE( fx.session.attachLayer( &fx.layer ).isEmpty() );
        REQUIRE( fx.session.setLocked( fx.layer.id(), true ) );
        RsSampleBrushTool tool( &fx.canvas );
        tool.setTargetLayer( &fx.layer );
        tool.setSession( &fx.session );
        tool.setRadius( 10.0 );
        QSignalSpy refused( &tool, &RsSampleBrushTool::strokeRefused );
        firePress( &fx.canvas, &tool, 50, 50 );
        fireRelease( &fx.canvas, &tool, 50, 50 );
        REQUIRE( refused.count() == 1 );
        CHECK( liveCount( &fx.layer ) == 0 );
    }
}

TEST_CASE( "brush refuses a single-part target layer explicitly",
           "[editing][brush][f11][negative]" )
{
    ensureApp();
    ToolFixture base;
    // Single-part polygon layer: a multipart stroke would only fail later
    // at commit — the tool must refuse up front, by name.
    QgsVectorLayer layer( QStringLiteral( "Polygon?crs=EPSG:4326&field=class:int" ),
                          QStringLiteral( "single" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.startEditing() );
    REQUIRE( base.session.attachLayer( &layer ).isEmpty() );

    RsSampleBrushTool tool( &base.canvas );
    tool.setTargetLayer( &layer );
    tool.setSession( &base.session );
    tool.setRadius( 10.0 );
    QSignalSpy refused( &tool, &RsSampleBrushTool::strokeRefused );

    // Two disjoint discs (60 units apart, r=10) combine into a MULTIPART
    // stroke — exactly the case a single-part layer must refuse.
    firePress( &base.canvas, &tool, 100, 100 );
    fireMove( &base.canvas, &tool, 160, 100 );
    fireRelease( &base.canvas, &tool, 160, 100 );

    REQUIRE( refused.count() == 1 );
    CHECK( refused.first().at( 0 ).toString().contains( QStringLiteral( "single-part" ) ) );
    CHECK( liveCount( &layer ) == 0 );
}

TEST_CASE( "erase stroke removes exactly the intersecting samples; single undo restores",
           "[editing][erase][f11][oracle1]" )
{
    ensureApp();
    ToolFixture fx;
    REQUIRE( fx.layer.startEditing() );
    seedSamples( &fx.layer, { QgsPointXY( 100, 100 ), QgsPointXY( 300, 300 ),
                              QgsPointXY( 400, 400 ) } );
    REQUIRE( fx.session.attachLayer( &fx.layer ).isEmpty() );
    const qlonglong before = liveCount( &fx.layer );
    REQUIRE( before == 3 );

    RsSampleEraseTool tool( &fx.canvas );
    tool.setTargetLayer( &fx.layer );
    tool.setSession( &fx.session );
    tool.setRadius( 5.0 );
    QSignalSpy committedSpy( &tool, &RsSampleEraseTool::strokeCommitted );

    // One stroke over two of the three points (map coordinates; the event
    // positions are derived through the canvas pixel map).
    firePress( &fx.canvas, &tool, 100, 100 );
    fireMove( &fx.canvas, &tool, 300, 300 );
    fireRelease( &fx.canvas, &tool, 300, 300 );

    REQUIRE( committedSpy.count() == 1 );
    CHECK( committedSpy.first().at( 0 ).toInt() == 2 );
    CHECK( liveCount( &fx.layer ) == 1 );

    // The survivor is the hand-known untouched square centered (400,400).
    QgsFeature survivor;
    REQUIRE( fx.layer.getFeatures().nextFeature( survivor ) );
    const QgsPointXY center = survivor.geometry().boundingBox().center();
    CHECK( center.x() == 400 );
    CHECK( center.y() == 400 );

    // Single undo restores both.
    REQUIRE( fx.session.undo( fx.layer.id() ) );
    CHECK( liveCount( &fx.layer ) == 3 );
}

TEST_CASE( "erase stroke touching nothing commits zero removals",
           "[editing][erase][f11]" )
{
    ensureApp();
    ToolFixture fx;
    REQUIRE( fx.layer.startEditing() );
    seedSamples( &fx.layer, { QgsPointXY( 450, 450 ) } );
    fx.session.attachLayer( &fx.layer );

    RsSampleEraseTool tool( &fx.canvas );
    tool.setTargetLayer( &fx.layer );
    tool.setSession( &fx.session );
    tool.setRadius( 5.0 );
    QSignalSpy committedSpy( &tool, &RsSampleEraseTool::strokeCommitted );

    // Stroke far from the only sample at map (450,450).
    firePress( &fx.canvas, &tool, 50, 50 );
    fireRelease( &fx.canvas, &tool, 50, 50 );

    REQUIRE( committedSpy.count() == 1 );
    CHECK( committedSpy.first().at( 0 ).toInt() == 0 );
    CHECK( liveCount( &fx.layer ) == 1 );
    CHECK( fx.session.state( fx.layer.id() ).undoDepth == 0 );
}
