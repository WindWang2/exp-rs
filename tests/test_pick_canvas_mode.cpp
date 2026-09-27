#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QString>

#include "qgsgeoreferencermainwindow.h"
#include "qgsmapcanvas.h"
#include "rs_georef_mode_toggle.h"

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }
}

TEST_CASE( "pickCanvas: ImageToImage uses REF; ImageToMap falls back without iface", "[georef][pick]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );
  auto *ref = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsRefCanvas" ) );
  REQUIRE( ref != nullptr );

  REQUIRE( w.pickCanvasForMode( RsGeorefModeToggle::ImageToImage ) == ref );
  // ImageToMap without iface → REF fallback
  REQUIRE( w.pickCanvasForMode( RsGeorefModeToggle::ImageToMap ) == ref );
  // RpcPhysical without iface → same REF fallback
  REQUIRE( w.pickCanvasForMode( RsGeorefModeToggle::RpcPhysical ) == ref );

  // I2I always picks on REF canvas
  REQUIRE( w.pickCanvas() == ref );
}
