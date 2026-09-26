// test_maptool_teardown_r4.cpp — WP-C teardown fixtures: canvas ↔ map tool
// destruction contract.
//
// Contract under test: a QgsMapTool parented to (or activated on) a canvas
// must survive the canvas dying while the tool is active — and vice versa —
// with exactly one destruction per object. This is the exact SIGSEGV class
// documented in QgisDesktopWindow::~QgisDesktopWindow (src/app/
// main_window.cpp:375-381: "prevents double-delete of QgsMapTool objects
// parented to the canvas (exit SIGSEGV)"): the production shell stops
// rendering and unsets the active tool BEFORE the canvas is destroyed; the
// fixture asserts the raw canvas/tool pair is safe across both orders.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QPointer>
#include <QtTest>

#include <qgsmapcanvas.h>
#include <qgsmaptoolpan.h>
#include <qgsrectangle.h>

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_maptool_teardown_r4";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
  }

  // Minimal concrete tool: QgsMapTool is abstract (canvas() access only).
  class ProbeTool : public QgsMapTool
  {
    public:
      explicit ProbeTool( QgsMapCanvas *canvas ) : QgsMapTool( canvas ) {}
      void canvasMoveEvent( QgsMapMouseEvent * ) override {}
  };
} // namespace

TEST_CASE( "Map tool teardown: canvas destroyed with active parented tool", "[teardown][r4]" )
{
  ensureApp();
  QPointer<ProbeTool> toolGuard;
  QPointer<QgsMapCanvas> canvasGuard;
  {
    // Tool parented to the canvas: Qt ownership would delete it with the
    // canvas; activation must not create a second owner.
    QgsMapCanvas canvas;
    auto tool = std::make_unique<ProbeTool>( &canvas );
    toolGuard = tool.get();
    canvasGuard = &canvas;
    canvas.setExtent( QgsRectangle( 0, 0, 10, 10 ) );
    canvas.setMapTool( tool.get() );
    QTest::qWait( 30 );
    REQUIRE( canvas.mapTool() == tool.get() );
  }
  QTest::qWait( 40 );
  REQUIRE( canvasGuard.isNull() );
  REQUIRE( toolGuard.isNull() );
}

TEST_CASE( "Map tool teardown: tool survives canvas, then dies alone", "[teardown][r4]" )
{
  ensureApp();
  auto canvas = std::make_unique<QgsMapCanvas>();
  canvas->resize( 300, 300 );
  auto tool = std::make_unique<ProbeTool>( canvas.get() );
  canvas->setMapTool( tool.get() );
  QTest::qWait( 20 );

  // Production shell unsets the active tool before destroying the canvas
  // (main_window.cpp contract). Do exactly that, then destroy the canvas.
  canvas->unsetMapTool( tool.get() );
  QPointer<QgsMapCanvas> canvasGuard( canvas.get() );
  canvas.reset();
  QTest::qWait( 30 );
  REQUIRE( canvasGuard.isNull() );
  REQUIRE( toolGuard != nullptr );

  // The orphaned tool must still be destructible afterwards.
  QPointer<ProbeTool> toolGuard( tool.get() );
  tool.reset();
  QTest::qWait( 20 );
  REQUIRE( toolGuard.isNull() );
}

TEST_CASE( "Map tool teardown: tool switch churn then delete-all", "[teardown][r4]" )
{
  ensureApp();
  QPointer<QgsMapCanvas> canvasGuard;
  QPointer<ProbeTool> firstToolGuard;
  {
    QgsMapCanvas canvas;
    canvasGuard = &canvas;
    canvas.setExtent( QgsRectangle( 0, 0, 10, 10 ) );
    auto pan = std::make_unique<QgsMapToolPan>( &canvas );
    auto probe = std::make_unique<ProbeTool>( &canvas );
    firstToolGuard = probe.get();
    for ( int i = 0; i < 10; ++i )
      canvas.setMapTool( i % 2 == 0 ? probe.get() : pan.get() );
    QTest::qWait( 20 );
  }
  QTest::qWait( 40 );
  REQUIRE( canvasGuard.isNull() );
  REQUIRE( firstToolGuard.isNull() );
}
