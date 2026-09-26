// test_canvas_teardown_r4.cpp — WP-C teardown fixtures: QgsMapCanvas family.
//
// Layer-1 (canvas/window) teardown contract: a canvas that has been used for
// extent/CRS work must be destructible (a) while its render queue is still
// warming, (b) before/after its peers, and (c) with CRS state resident — and
// the process must survive glibc exit() afterwards without any _Exit defense.
// Truth sources: Qt parentless-QObject destruction contract; QGIS canvas
// rendering stop contract exercised by QgisDesktopWindow::~QgisDesktopWindow
// (src/app/main_window.cpp:333 — stopRenderingAndSettle / unsetMapTool).
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QPointer>
#include <QtTest>

#include <qgscoordinatereferencesystem.h>
#include <qgsmapcanvas.h>
#include <qgsrectangle.h>

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_canvas_teardown_r4";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    // Heap-owned: deleted by the shared TeardownListener in ordered teardown
    // (support/qt_lifecycle.h). Never a value static — see retirement design.
    return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
  }
}

TEST_CASE( "Canvas teardown: used canvas with CRS extent deletes cleanly", "[teardown][r4]" )
{
  ensureApp();
  QPointer<QgsMapCanvas> guard;
  {
    QgsMapCanvas canvas;
    guard = &canvas;
    canvas.resize( 300, 300 );
    canvas.mapSettings().setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
    canvas.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
    QTest::qWait( 50 );
    REQUIRE( guard.data() == &canvas );
  }
  // Destruction contract: object gone, no deferred callbacks into it.
  QTest::qWait( 50 );
  REQUIRE( guard.isNull() );
}

TEST_CASE( "Canvas teardown: delete while refresh scheduled does not crash", "[teardown][r4]" )
{
  ensureApp();
  QPointer<QgsMapCanvas> guard;
  {
    QgsMapCanvas canvas;
    guard = &canvas;
    canvas.resize( 300, 300 );
    canvas.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
    // Destroy on the very next event-loop turn: refresh/render work may be
    // queued for the canvas (QgsMapCanvas::refresh defers via the event
    // loop). The canvas destructor owns stopping that work.
    QTest::qWait( 1 );
  }
  QTest::qWait( 120 );
  REQUIRE( guard.isNull() );
}

TEST_CASE( "Canvas teardown: peer deleted first leaves survivor valid", "[teardown][r4]" )
{
  ensureApp();
  QgsMapCanvas survivor;
  survivor.resize( 300, 300 );
  survivor.setExtent( QgsRectangle( 0, 0, 50, 50 ) );

  QPointer<QgsMapCanvas> firstGuard;
  {
    QgsMapCanvas first;
    firstGuard = &first;
    first.resize( 300, 300 );
    first.setExtent( QgsRectangle( 0, 0, 10, 10 ) );
    QTest::qWait( 30 );
  }
  QTest::qWait( 30 );
  REQUIRE( firstGuard.isNull() );

  // Survivor still answers coordinate queries after its peer died.
  const QgsRectangle extent = survivor.extent();
  REQUIRE( extent.width() > 0.0 );
}

TEST_CASE( "Canvas teardown: extent churn then immediate delete is safe", "[teardown][r4]" )
{
  ensureApp();
  QPointer<QgsMapCanvas> guard;
  {
    QgsMapCanvas canvas;
    guard = &canvas;
    canvas.resize( 300, 300 );
    for ( int i = 0; i < 25; ++i )
    {
      canvas.setExtent( QgsRectangle( i, i, i + 10, i + 10 ) );
      QTest::qWait( 2 );
    }
  }
  QTest::qWait( 120 );
  REQUIRE( guard.isNull() );
}

TEST_CASE( "Canvas teardown: CRS switch churn then delete is safe", "[teardown][r4]" )
{
  ensureApp();
  QPointer<QgsMapCanvas> guard;
  {
    QgsMapCanvas canvas;
    guard = &canvas;
    canvas.resize( 300, 300 );
    // Each distinct CRS exercises the PROJ-backed CRS cache; the retirement
    // hypothesis says these caches must be invalidated while the thread-local
    // PROJ context lives (qgsapplication.cpp invalidateCaches contract).
    const char *epsgs[] = { "EPSG:4326", "EPSG:3857", "EPSG:32633", "EPSG:4326" };
    for ( const char *code : epsgs )
    {
      canvas.mapSettings().setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( code ) ) );
      canvas.zoomToFullExtent();
      QTest::qWait( 10 );
    }
  }
  QTest::qWait( 80 );
  REQUIRE( guard.isNull() );
}
