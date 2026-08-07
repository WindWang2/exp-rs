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

TEST_CASE( "Dual viewport: primary extent propagates to secondary", "[app][dual_viewport]" )
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
}

TEST_CASE( "Dual viewport: secondary extent back-propagates to primary", "[app][dual_viewport]" )
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
}

TEST_CASE( "Dual viewport: disabled => no propagation", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );
  ctl.setEnabled( false );
  const QgsRectangle originalSecondary = secondary.extent();

  primary.setExtent( QgsRectangle( 500, 500, 600, 600 ) );
  QTest::qWait( 80 );

  REQUIRE( secondary.extent().xMinimum() == Approx( originalSecondary.xMinimum() ).margin( 1e-6 ) );
  REQUIRE( secondary.extent().xMaximum() == Approx( originalSecondary.xMaximum() ).margin( 1e-6 ) );
}

TEST_CASE( "Dual viewport: snapSecondaryToPrimary applies immediately", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  primary.resize( 300, 300 );
  secondary.resize( 300, 300 );

  RsDualViewportSyncController ctl( &primary, &secondary );
  primary.setExtent( QgsRectangle( 10, 10, 20, 20 ) );

  // Before snap, secondary has not yet followed (throttle not pumped).
  ctl.snapSecondaryToPrimary();

  REQUIRE( secondary.extent().xMinimum() == Approx( primary.extent().xMinimum() ).margin( 1e-3 ) );
  REQUIRE( secondary.extent().xMaximum() == Approx( primary.extent().xMaximum() ).margin( 1e-3 ) );
}

TEST_CASE( "Dual viewport: defaults enabled with scale sync on", "[app][dual_viewport]" )
{
  ensureApp();
  QgsMapCanvas primary;
  QgsMapCanvas secondary;
  RsDualViewportSyncController ctl( &primary, &secondary );
  REQUIRE( ctl.isEnabled() );
  REQUIRE( ctl.scaleSyncEnabled() );

  ctl.setScaleSync( false );
  REQUIRE_FALSE( ctl.scaleSyncEnabled() );
}
