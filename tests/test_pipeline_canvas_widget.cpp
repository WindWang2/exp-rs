// tests/test_pipeline_canvas_widget.cpp — canvas & connection math (D17 Package F)
//
// Ground truth: the closed-form Bézier evaluation B(0.5) = 0.125·P0 +
// 0.375·P1 + 0.375·P2 + 0.125·P3 computed in the test, the [0.2, 3.0]
// zoom clamp constants, and the 12 px snap predicate evaluated at
// hand-placed coordinates.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <QApplication>
#include <QPainterPath>

#include <chrono>

#include "app/pipeline/pipeline_canvas_widget.h"
#include "app/pipeline/pipeline_connection_item.h"
#include "app/pipeline/pipeline_node_item.h"
#include "app/pipeline/pipeline_port_item.h"
#include "app/pipeline/pipeline_scene.h"
#include "workflow/workflow_ir_v2.h"

using namespace sicnu::app::pipeline;
using namespace sicnu::workflow;

namespace {

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        static int fake_argc = 1;
        static char fake_argv[] = "test_pipeline_canvas_widget";
        static char *fake_argv_ptr[] = { fake_argv };
        app = new QApplication( fake_argc, fake_argv_ptr );
    }
    return app;
}

NodeFact canvasNode( const QString &id, double x, double y, int inputs = 1, int outputs = 1 )
{
    NodeFact node;
    node.nodeId = id;
    node.operatorId = QStringLiteral( "rs:step" );
    node.displayName = id;
    node.canvasPosition = QPointF( x, y );
    for ( int i = 0; i < inputs; ++i )
        node.inputPorts.append( PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ),
                                          QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, true } );
    for ( int i = 0; i < outputs; ++i )
        node.outputPorts.append( PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ),
                                           QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, false } );
    return node;
}

EdgeFact canvasEdge( const QString &id, const QString &from, const QString &to )
{
    return EdgeFact{ id, from, QStringLiteral( "output" ), to, QStringLiteral( "input" ) };
}

} // namespace

TEST_CASE( "Cubic Bézier midpoint matches the analytical coordinate", "[d17][workflow][ui]" )
{
    const QPointF p0( 100.0, 100.0 );
    const QPointF p3( 300.0, 200.0 );

    // Independent analytical evaluation: dx=200 -> delta=100 ->
    // P1=(200,100), P2=(200,200); B(0.5) = (200.0, 150.0).
    const QPainterPath path = PipelineConnectionItem::calculateBezierSpline( p0, p3 );
    const QPointF mid = path.pointAtPercent( 0.5 );
    REQUIRE_THAT( mid.x(), Catch::Matchers::WithinAbs( 200.0, 0.5 ) );
    REQUIRE_THAT( mid.y(), Catch::Matchers::WithinAbs( 150.0, 0.5 ) );

    // Endpoints are interpolated exactly.
    REQUIRE( path.pointAtPercent( 0.0 ) == p0 );
    REQUIRE_THAT( path.pointAtPercent( 1.0 ).x(), Catch::Matchers::WithinAbs( p3.x(), 1e-6 ) );
    REQUIRE_THAT( path.pointAtPercent( 1.0 ).y(), Catch::Matchers::WithinAbs( p3.y(), 1e-6 ) );

    // Near-horizontal short edges clamp the control offset to 30 px.
    const QPainterPath shortPath = PipelineConnectionItem::calculateBezierSpline( QPointF( 0, 0 ), QPointF( 20, 0 ) );
    const QPointF shortMid = shortPath.pointAtPercent( 0.5 );
    REQUIRE_THAT( shortMid.x(), Catch::Matchers::WithinAbs( 10.0, 0.5 ) );
    REQUIRE_THAT( shortMid.y(), Catch::Matchers::WithinAbs( 0.0, 0.5 ) );
}

TEST_CASE( "Zoom clamps to [0.2, 3.0] on both sides", "[d17][workflow][ui]" )
{
    ensureApp();
    PipelineCanvasWidget canvas;

    canvas.setZoomLevel( 1.0 );
    REQUIRE_THAT( canvas.zoomLevel(), Catch::Matchers::WithinAbs( 1.0, 1e-9 ) );

    canvas.setZoomLevel( 5.0 );
    REQUIRE_THAT( canvas.zoomLevel(), Catch::Matchers::WithinAbs( 3.0, 1e-9 ) );

    canvas.setZoomLevel( 0.01 );
    REQUIRE_THAT( canvas.zoomLevel(), Catch::Matchers::WithinAbs( 0.2, 1e-9 ) );

    canvas.setZoomLevel( 1.75 );
    REQUIRE_THAT( canvas.zoomLevel(), Catch::Matchers::WithinAbs( 1.75, 1e-9 ) );
}

TEST_CASE( "loadWorkflow projects nodes and edges; export reads positions back", "[d17][workflow][ui]" )
{
    ensureApp();
    PipelineCanvasWidget canvas;

    WorkflowDefinition def;
    def.workflowId = QStringLiteral( "wf-canvas-1" );
    def.nodes = { canvasNode( "a", 0, 0 ), canvasNode( "b", 260, 0 ), canvasNode( "c", 520, 120 ) };
    def.edges = { canvasEdge( "e1", "a", "b" ), canvasEdge( "e2", "b", "c" ) };

    canvas.loadWorkflow( def );

    REQUIRE( canvas.scene()->nodeItem( "a" ) != nullptr );
    REQUIRE( canvas.scene()->nodeItem( "b" ) != nullptr );
    REQUIRE( canvas.scene()->nodeItem( "ghost" ) == nullptr );

    const WorkflowDefinition exported = canvas.exportWorkflow();
    REQUIRE( exported.nodes.size() == 3 );
    REQUIRE( exported.edges.size() == 2 );
    REQUIRE( exported.nodes == def.nodes ); // untouched positions round-trip

    // Moving an item updates the exported geometry, and nothing else.
    canvas.scene()->nodeItem( "b" )->setPos( 300.0, 90.0 );
    const WorkflowDefinition moved = canvas.exportWorkflow();
    REQUIRE( moved.findNode( "b" )->canvasPosition == QPointF( 300.0, 90.0 ) );
    REQUIRE( moved.findNode( "a" )->canvasPosition == QPointF( 0.0, 0.0 ) );
    REQUIRE( moved.edges == def.edges );
}

TEST_CASE( "Port snapping: 11.9 px hits, 12.1 px misses", "[d17][workflow][ui]" )
{
    ensureApp();
    PipelineScene scene;

    auto *node = new PipelineNodeItem( QStringLiteral( "n" ), QStringLiteral( "N" ) );
    node->setPos( 500.0, 500.0 );
    node->addInputPort( QStringLiteral( "input" ) );
    scene.addNodeItem( node );

    const QPointF portCenter = node->inputPortScenePos( 0 );
    const QPointF inside( portCenter.x() + 11.9, portCenter.y() );
    const QPointF outside( portCenter.x() + 12.1, portCenter.y() );

    REQUIRE( PipelineScene::withinSnapRadius( portCenter, inside, PipelineScene::kSnapRadiusPx ) );
    REQUIRE_FALSE( PipelineScene::withinSnapRadius( portCenter, outside, PipelineScene::kSnapRadiusPx ) );

    // The scene resolves the port item within the radius, and nothing far.
    REQUIRE( scene.portAtScenePos( inside ) != nullptr );
    REQUIRE( scene.portAtScenePos( inside )->portName == QStringLiteral( "input" ) );
    REQUIRE( scene.portAtScenePos( portCenter + QPointF( 40.0, 0.0 ) ) == nullptr );

    // Exact boundary honouring: distance exactly 12.0 is a hit (<=).
    const QPointF exact( portCenter.x() + PipelineScene::kSnapRadiusPx, portCenter.y() );
    REQUIRE( PipelineScene::withinSnapRadius( portCenter, exact, PipelineScene::kSnapRadiusPx ) );
}

TEST_CASE( "Pending connection commits through the snap seam", "[d17][workflow][ui]" )
{
    ensureApp();
    PipelineScene scene;

    auto *source = new PipelineNodeItem( QStringLiteral( "src" ), QStringLiteral( "S" ) );
    source->setPos( 100.0, 100.0 );
    source->addOutputPort( QStringLiteral( "output" ) );
    auto *target = new PipelineNodeItem( QStringLiteral( "dst" ), QStringLiteral( "D" ) );
    target->setPos( 400.0, 100.0 );
    target->addInputPort( QStringLiteral( "input" ) );
    scene.addNodeItem( source );
    scene.addNodeItem( target );

    QString createdFrom, createdTo;
    QObject::connect( &scene, &PipelineScene::connectionCreated,
                      [&]( const QString &s, const QString &, const QString &t, const QString & ) {
                          createdFrom = s;
                          createdTo = t;
                      } );

    scene.beginPendingConnection( QStringLiteral( "src" ), QStringLiteral( "output" ),
                                  source->outputPortScenePos( 0 ) );
    scene.updatePendingConnection( target->inputPortScenePos( 0 ) );
    REQUIRE( scene.finishPendingConnection( scene.portAtScenePos( target->inputPortScenePos( 0 ) ) ) );

    REQUIRE( createdFrom == QStringLiteral( "src" ) );
    REQUIRE( createdTo == QStringLiteral( "dst" ) );

    // Cancelling a pending wire removes it and emits nothing.
    const int before = scene.items().size();
    scene.beginPendingConnection( QStringLiteral( "src" ), QStringLiteral( "output" ),
                                  source->outputPortScenePos( 0 ) );
    scene.cancelPendingConnection();
    REQUIRE( scene.items().size() == before );
}

TEST_CASE( "Interactive wiring created after load survives exportWorkflow", "[d17][workflow][ui]" )
{
    ensureApp();
    PipelineCanvasWidget canvas;

    WorkflowDefinition def;
    def.workflowId = QStringLiteral( "wf-grow" );
    def.nodes = { canvasNode( "a", 0, 0 ), canvasNode( "b", 260, 0 ) };
    canvas.loadWorkflow( def ); // no edges yet

    // Simulate the interactive commit path (as the scene's mouse flow does).
    REQUIRE( canvas.scene()->nodeItem( "a" ) != nullptr );
    canvas.scene()->beginPendingConnection( QStringLiteral( "a" ), QStringLiteral( "output" ),
                                            canvas.scene()->nodeItem( "a" )->outputPortScenePos( 0 ) );
    PipelinePortItem *target = canvas.scene()->portAtScenePos( canvas.scene()->nodeItem( "b" )->inputPortScenePos( 0 ) );
    REQUIRE( target != nullptr );
    REQUIRE( canvas.scene()->finishPendingConnection( target ) );

    const WorkflowDefinition exported = canvas.exportWorkflow();
    REQUIRE( exported.edges.size() == 1 );
    REQUIRE( exported.edges[0].sourceNodeId == QStringLiteral( "a" ) );
    REQUIRE( exported.edges[0].targetNodeId == QStringLiteral( "b" ) );
    QString error;
    REQUIRE( WorkflowIR::validateSemantics( exported, &error ) );
    INFO( error.toStdString() );
}

TEST_CASE( "100-node load completes within the 50 ms budget offscreen", "[d17][workflow][ui]" )
{
    ensureApp();
    PipelineCanvasWidget canvas;

    // 10 columns x 10 rows; chained column-wise like the E2E scale fixture.
    WorkflowDefinition def;
    def.workflowId = QStringLiteral( "wf-scale-canvas" );
    for ( int i = 0; i < 100; ++i )
        def.nodes.append( canvasNode( QStringLiteral( "scale_%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ),
                                      ( i % 10 ) * 220.0, ( i / 10 ) * 140.0 ) );
    for ( int i = 1; i < 100; ++i )
        def.edges.append( canvasEdge( QStringLiteral( "se%1" ).arg( i ),
                                      QStringLiteral( "scale_%1" ).arg( i - 1, 3, 10, QLatin1Char( '0' ) ),
                                      QStringLiteral( "scale_%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ) ) );

    const auto started = std::chrono::high_resolution_clock::now();
    canvas.loadWorkflow( def );
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::high_resolution_clock::now() - started )
                               .count();

    REQUIRE( elapsedMs < 50 );
    const WorkflowDefinition exported = canvas.exportWorkflow();
    REQUIRE( exported.nodes.size() == 100 );
    REQUIRE( exported.edges.size() == 99 );
}
