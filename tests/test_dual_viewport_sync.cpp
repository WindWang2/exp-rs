#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QtTest>

#include "qgsmapcanvas.h"
#include "qgsrectangle.h"

#include "shell/rs_dual_viewport_sync_controller.h"

#include <cstdlib>
#include <memory>

using Catch::Approx;

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup when
// run after a Catch2 process that exercised QgsMapCanvas; bypass it with
// std::_Exit once Catch has reported the final result.
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        std::_Exit( stats.aborting || stats.totals.testCases.failed > 0 ? 1 : 0 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_dual_viewport_sync";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      static QApplication app( fake_argc, fake_argv );
      return &app;
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "Dual viewport: 1. primary -> secondary pan", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );

  primary.setExtent( QgsRectangle( 100, 100, 200, 200 ) );
  QTest::qWait( 80 );

  REQUIRE( secondary.extent().xMinimum() == Approx( primary.extent().xMinimum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().xMaximum() == Approx( primary.extent().xMaximum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().yMinimum() == Approx( primary.extent().yMinimum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().yMaximum() == Approx( primary.extent().yMaximum() ).margin( 1e-3 ) );
  REQUIRE( ctl.stats().appliedSyncCount >= 1 );
}

TEST_CASE( "Dual viewport: 2. secondary -> primary pan", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );

  secondary.setExtent( QgsRectangle( -50, -50, 50, 50 ) );
  QTest::qWait( 80 );

  REQUIRE( primary.extent().xMinimum() == Approx( secondary.extent().xMinimum() ).margin( 1e-3 ) );
  REQUIRE( primary.extent().xMaximum() == Approx( secondary.extent().xMaximum() ).margin( 1e-3 ) );
  REQUIRE( primary.extent().yMinimum() == Approx( secondary.extent().yMinimum() ).margin( 1e-3 ) );
  REQUIRE( primary.extent().yMaximum() == Approx( secondary.extent().yMaximum() ).margin( 1e-3 ) );
  REQUIRE( ctl.stats().appliedSyncCount >= 1 );
}

TEST_CASE( "Dual viewport: 3. rapid 500 extentChanged updates coalesce efficiently", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );
  ctl.resetStats();

  const int eventCount = 500;
  for ( int i = 1; i <= eventCount; ++i )
  {
    primary.setExtent( QgsRectangle( i, i, i + 100, i + 100 ) );
    if ( i % 50 == 0 )
    {
      // Simulate real-world interactive dragging intervals (sub-millisecond to small ticks)
      QTest::qWait( 1 );
    }
  }

  // Wait for trailing 16ms throttle to flush
  QTest::qWait( 80 );

  const auto stats = ctl.stats();
  REQUIRE( stats.extentChangedEvents >= eventCount );
  // 500 events should be heavily coalesced down to a small fraction of applications
  REQUIRE( stats.appliedSyncCount < 30 );
  REQUIRE( stats.canvasRefreshRequests < 30 );

  // Final destination state must strictly match the final source state (latest-event-wins)
  REQUIRE( secondary.extent().xMinimum() == Approx( primary.extent().xMinimum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().xMaximum() == Approx( primary.extent().xMaximum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().yMinimum() == Approx( primary.extent().yMinimum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().yMaximum() == Approx( primary.extent().yMaximum() ).margin( 1e-3 ) );
}

TEST_CASE( "Dual viewport: 4. scale sync on vs off", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );
  REQUIRE( ctl.scaleSyncEnabled() );

  // With scale sync on:
  primary.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
  QTest::qWait( 80 );
  REQUIRE( secondary.scale() == Approx( primary.scale() ).epsilon( 1e-4 ) );

  // Turn scale sync off:
  ctl.setScaleSync( false );
  REQUIRE_FALSE( ctl.scaleSyncEnabled() );

  // Pan primary
  primary.setExtent( QgsRectangle( 200, 200, 300, 300 ) );
  QTest::qWait( 80 );

  // Pan extent center should sync
  REQUIRE( secondary.extent().center().x() == Approx( primary.extent().center().x() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().center().y() == Approx( primary.extent().center().y() ).margin( 1e-3 ) );
}

TEST_CASE( "Dual viewport: 5. rotation sync", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );

  primary.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
  primary.setRotation( 45.0 );
  QTest::qWait( 80 );

  REQUIRE( secondary.rotation() == Approx( 45.0 ).margin( 1e-4 ) );
  REQUIRE( secondary.extent().xMinimum() == Approx( primary.extent().xMinimum() ).margin( 1e-3 ) );
}

TEST_CASE( "Dual viewport: 6. enable/disable during pending event", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );

  const QgsRectangle originalSecondary = secondary.extent();

  // Trigger an update on primary
  primary.setExtent( QgsRectangle( 500, 500, 600, 600 ) );

  // Immediately disable before 16ms timer fires
  ctl.setEnabled( false );
  QTest::qWait( 80 );

  // Secondary must NOT receive the pending update
  REQUIRE( secondary.extent().xMinimum() == Approx( originalSecondary.xMinimum() ).margin( 1e-6 ) );
  REQUIRE( secondary.extent().xMaximum() == Approx( originalSecondary.xMaximum() ).margin( 1e-6 ) );
}

TEST_CASE( "Dual viewport: 7. canvas destroyed during pending timer", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  primary.resize( 300, 300 );

  auto secondary = std::make_unique<QgsMapCanvas>();
  secondary->resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, secondary.get() );

  // Fire update
  primary.setExtent( QgsRectangle( 10, 10, 20, 20 ) );

  // Destroy secondary canvas while timer is active
  secondary.reset();

  // Wait for timer to expire — must not crash or access dangling pointer
  QTest::qWait( 80 );

  // Firing further updates on primary with destroyed secondary should also safely no-op
  primary.setExtent( QgsRectangle( 30, 30, 40, 40 ) );
  QTest::qWait( 80 );
  REQUIRE( primary.extent().center().x() == Approx( 35.0 ).margin( 1e-3 ) );
}

TEST_CASE( "Dual viewport: 8. rapid alternating source viewport events", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );

  // Rapidly alternate driving primary and secondary
  for ( int i = 1; i <= 50; ++i )
  {
    if ( i % 2 == 1 )
      primary.setExtent( QgsRectangle( i * 10, i * 10, i * 10 + 100, i * 10 + 100 ) );
    else
      secondary.setExtent( QgsRectangle( i * 10, i * 10, i * 10 + 100, i * 10 + 100 ) );
  }

  // Final event was on secondary (i = 50: (500, 500, 600, 600))
  QTest::qWait( 80 );

  REQUIRE( primary.extent().xMinimum() == Approx( secondary.extent().xMinimum() ).margin( 1e-3 ) );
  REQUIRE( primary.extent().xMaximum() == Approx( secondary.extent().xMaximum() ).margin( 1e-3 ) );
  REQUIRE( primary.extent().yMinimum() == Approx( secondary.extent().yMinimum() ).margin( 1e-3 ) );
  REQUIRE( primary.extent().yMaximum() == Approx( secondary.extent().yMaximum() ).margin( 1e-3 ) );
}

TEST_CASE( "Dual viewport: 9. snapSecondaryToPrimary applies immediately", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );
  primary.setExtent( QgsRectangle( 10, 10, 20, 20 ) );

  ctl.snapSecondaryToPrimary();

  REQUIRE( secondary.extent().xMinimum() == Approx( primary.extent().xMinimum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().xMaximum() == Approx( primary.extent().xMaximum() ).margin( 1e-3 ) );
}
