#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAction>
#include <QLabel>
#include <QSignalSpy>

#include "qgsgeoreferencermainwindow.h"
#include "rs_georef_mode_toggle.h"

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

TEST_CASE( "ModeToggle: switching emits modeChanged", "[georef][window][mode]" )
{
  ensureApp();
  RsGeorefModeToggle t;
  QSignalSpy spy( &t, &RsGeorefModeToggle::modeChanged );
  t.setMode( RsGeorefModeToggle::RpcPhysical );
  REQUIRE( spy.count() == 1 );
  REQUIRE( t.currentMode() == RsGeorefModeToggle::RpcPhysical );
}
