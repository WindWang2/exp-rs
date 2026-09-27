#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QAction>
#include <QLabel>
#include <QSignalSpy>

#include "qgsgeoreferencermainwindow.h"
#include "qgsmapcanvas.h"
#include "rs_georef_mode_toggle.h"
#include "rs_twincanvas_sync_controller.h"

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  // Singleton QApplication — Qt does not permit two instances in one process.
  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "GeorefMainWindow: constructs with Apply + SIFT actions (I2I, no mode toggle UX)", "[georef][window]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );
  // Dual-window redesign: mode toggle is not part of the I2I shell UI.
  REQUIRE( w.findChild<RsGeorefModeToggle *>() == nullptr );
  REQUIRE( w.findChild<QAction *>( "rsGeorefApplyAction" ) != nullptr );
  REQUIRE( w.findChild<QAction *>( "rsGeorefSiftAction" ) != nullptr );
  REQUIRE( w.findChild<QLabel *>( "rsGeorefRmsLabel" ) != nullptr );
}

TEST_CASE( "GeorefMainWindow: has two QgsMapCanvas children with sync controller", "[georef][window]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );
  REQUIRE( w.findChild<QgsMapCanvas *>( "rsSrcCanvas" ) != nullptr );
  REQUIRE( w.findChild<QgsMapCanvas *>( "rsRefCanvas" ) != nullptr );
  REQUIRE( w.findChild<RsTwinCanvasSyncController *>() != nullptr );
}

TEST_CASE( "ModeToggle: switching emits modeChanged", "[georef][window][mode]" )
{
  ensureApp();
  RsGeorefModeToggle t;
  QSignalSpy spy( &t, &RsGeorefModeToggle::modeChanged );
  t.setMode( RsGeorefModeToggle::RpcPhysical );
  REQUIRE( spy.count() == 1 );
  REQUIRE( t.currentMode() == RsGeorefModeToggle::RpcPhysical );
}
