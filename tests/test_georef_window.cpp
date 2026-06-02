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

#include <cstdlib>

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup when
// the test process exercised qgis_core/qgis_gui — observed both pre- and
// post-Task 11.4.5. Bypass the C++ destructor sequence with std::_Exit
// once Catch has reported the final result; all assertions are recorded
// before this point.
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
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  // Singleton QApplication — Qt does not permit two instances in one process.
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

TEST_CASE( "GeorefMainWindow: constructs with mode toggle and Apply action", "[georef][window]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );
  REQUIRE( w.findChild<RsGeorefModeToggle *>() != nullptr );
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
