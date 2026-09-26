// test_georef_dual_window_workbench.cpp — D14 Package G: headless dual-window
// workbench tests (QT_QPA_PLATFORM=offscreen). Named *_workbench because the
// repo already ships tests/test_georef_dual_window.cpp for the QGIS
// georeferencer main window (ADR 0159 / DECISIONS D14-5). The expected values
// come from hand-composed control point geometry (exact affine / broken
// translation), and the reentrancy contract is checked through the guard probe.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#include <qgsmapcanvas.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>

#include "app/workbench/georef_dual_window.h"

#include <cmath>
#include "support/qt_lifecycle.h"

using Catch::Matchers::WithinAbs;
using namespace rs::app;

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_georef_dual_window_workbench";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  /// Exact affine truth: rotate 90 degrees about the origin and shift by (5, 3):
  /// (u, v) -> (5 - v, 3 + u).
  QgsPointXY affinePoint( double u, double v ) { return QgsPointXY( 5.0 - v, 3.0 + u ); }
} // namespace

TEST_CASE( "test_georef_dual_window_workbench - Window constructs headless and exposes the sync mode", "[georef_ui][d14]" )
{
  ensureApp();
  GeorefDualWindow window;
  window.show(); // offscreen platform: widget is realized without a display

  REQUIRE( window.sourceCanvas() != nullptr );
  REQUIRE( window.referenceCanvas() != nullptr );
  REQUIRE( window.gcpTableRowCount() == 0 );
  REQUIRE_THAT( window.displayedGlobalRmse(), WithinAbs( 0.0, 0.0 ) );
  REQUIRE_FALSE( window.isApplyingSync() );

  window.setSyncMode( ViewportSyncMode::ExtentSync );
  REQUIRE( window.syncMode() == ViewportSyncMode::ExtentSync );
  window.setSyncMode( ViewportSyncMode::None );
  REQUIRE( window.syncMode() == ViewportSyncMode::None );
  window.setSyncMode( ViewportSyncMode::FullLock );
  REQUIRE( window.syncMode() == ViewportSyncMode::FullLock );
}

TEST_CASE( "test_georef_dual_window_workbench - Extent sync engages the guard, mirrors extents, and never recurses", "[georef_ui][d14]" )
{
  ensureApp();
  GeorefDualWindow window;
  auto *source = window.sourceCanvas();
  auto *reference = window.referenceCanvas();
  REQUIRE( source != nullptr );
  REQUIRE( reference != nullptr );
  source->resize( 200, 200 );
  reference->resize( 200, 200 );
  source->setExtent( QgsRectangle( 0, 0, 100, 100 ) );
  reference->setExtent( QgsRectangle( 0, 0, 100, 100 ) );

  window.setSyncMode( ViewportSyncMode::ExtentSync );

  // 50 high-frequency drag events; the throttle coalesces them while the
  // guard keeps the recursion depth at exactly 1.
  for ( int i = 1; i <= 50; ++i )
  {
    source->setExtent( QgsRectangle( i, i, 100 + i, 100 + i ) );
    // The synchronous extentsChanged echo has already been captured: the
    // guard is up right now and any reflected event would be dropped.
    if ( i == 1 )
      REQUIRE( window.isApplyingSync() );
  }

  QTest::qWait( 60 ); // > 16 ms throttle: let the pending sync apply
  REQUIRE_FALSE( window.isApplyingSync() );

  // Mirror applied: the reference canvas carries the source extent center.
  const QgsRectangle srcExtent = source->extent();
  const QgsRectangle refExtent = reference->extent();
  REQUIRE( std::abs( refExtent.center().x() - srcExtent.center().x() ) < 1e-6 );
  REQUIRE( std::abs( refExtent.center().y() - srcExtent.center().y() ) < 1e-6 );
  REQUIRE( srcExtent.width() > 0.0 );
  // A runaway recursion would have blown the stack before this line.
}

TEST_CASE( "test_georef_dual_window_workbench - Sync mode None leaves the peer canvas untouched", "[georef_ui][d14]" )
{
  ensureApp();
  GeorefDualWindow window;
  auto *source = window.sourceCanvas();
  auto *reference = window.referenceCanvas();
  source->setExtent( QgsRectangle( 0, 0, 10, 10 ) );
  reference->setExtent( QgsRectangle( 100, 100, 200, 200 ) );

  window.setSyncMode( ViewportSyncMode::None );
  source->setExtent( QgsRectangle( 20, 20, 30, 30 ) );
  QTest::qWait( 60 );

  const QgsRectangle refExtent = reference->extent();
  REQUIRE( refExtent.center().x() > 100.0 ); // still the original viewport
}

TEST_CASE( "test_georef_dual_window_workbench - GCP picking, table binding, and live RMSE refresh", "[georef_ui][d14]" )
{
  ensureApp();
  GeorefDualWindow window;
  window.show();

  // Four points under an exact affine ground truth -> RMSE must be ~0.
  QElapsedTimer timer;
  timer.start();
  const std::vector<std::pair<double, double>> pts{
    {10.0, 10.0}, {90.0, 10.0}, {90.0, 90.0}, {10.0, 90.0}};
  for ( const auto& [u, v] : pts )
    window.onAddGcpPoint( QgsPointXY( u, v ), affinePoint( u, v ) );
  const qint64 elapsedMs = timer.elapsed();

  REQUIRE( window.gcpTableRowCount() == 4 );
  REQUIRE( window.displayedGlobalRmse() >= 0.0 );
  REQUIRE( window.displayedGlobalRmse() < 1e-6 );
  // Model re-solve refreshes within an interactive budget (spec: < 10 ms;
  // a generous offscreen bound of 50 ms guards CI jitter).
  REQUIRE( elapsedMs < 50 );

  // Deleting row 1 removes exactly one point.
  window.onDeleteGcpPoint( 1 );
  REQUIRE( window.gcpTableRowCount() == 3 );

  // Deleting a bogus row is a safe no-op.
  window.onDeleteGcpPoint( 42 );
  REQUIRE( window.gcpTableRowCount() == 3 );
}

TEST_CASE( "test_georef_dual_window_workbench - Model change re-fits and exposes a degraded RMSE for a broken model", "[georef_ui][d14]" )
{
  ensureApp();
  GeorefDualWindow window;

  const std::vector<std::pair<double, double>> pts{
    {0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}};
  for ( const auto& [u, v] : pts )
    window.onAddGcpPoint( QgsPointXY( u, v ), affinePoint( u, v ) );
  REQUIRE_THAT( window.displayedGlobalRmse(), WithinAbs( 0.0, 1e-6 ) );

  // Switch to pure translation (combo index 0): the ground truth contains a
  // 90-degree rotation no translation can absorb -> strictly positive RMSE.
  window.onTransformModelChanged( 0 );
  REQUIRE( window.displayedGlobalRmse() > 1.0 );
}

TEST_CASE( "test_georef_dual_window_workbench - Warp execution emits rectificationFinished with the fitted RMSE", "[georef_ui][d14]" )
{
  ensureApp();
  GeorefDualWindow window;
  window.onAddGcpPoint( QgsPointXY( 0.0, 0.0 ), QgsPointXY( 1.0, 1.0 ) );
  window.onAddGcpPoint( QgsPointXY( 10.0, 0.0 ), QgsPointXY( 11.0, 1.0 ) );
  window.onAddGcpPoint( QgsPointXY( 10.0, 10.0 ), QgsPointXY( 11.0, 11.0 ) );
  const double rmse = window.displayedGlobalRmse();

  QSignalSpy spy( &window, &GeorefDualWindow::rectificationFinished );
  window.onExecuteWarpClicked();
  REQUIRE( spy.count() == 1 );
  const auto args = spy.takeFirst();
  REQUIRE( args.at( 0 ).toString().isEmpty() ); // no raster loaded -> no path
  REQUIRE( args.at( 1 ).toDouble() == rmse );
}

TEST_CASE( "test_georef_dual_window_workbench - Swipe comparison toggles without touching the sync guard", "[georef_ui][d14]" )
{
  ensureApp();
  GeorefDualWindow window;
  window.setSyncMode( ViewportSyncMode::ExtentSync );
  window.onToggleSwipeComparison( true );
  window.onToggleSwipeComparison( false );
  REQUIRE_FALSE( window.isApplyingSync() );
}
