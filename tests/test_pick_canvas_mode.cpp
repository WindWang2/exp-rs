#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QString>

#include "qgsgeoreferencermainwindow.h"
#include "qgsmapcanvas.h"
#include "rs_georef_mode_toggle.h"

#include <cstdlib>

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup when
// the test process exercised qgis_core/qgis_gui. Bypass the C++ destructor
// sequence with std::_Exit once Catch has reported the final result.
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

  // pickCanvas() follows the current mode toggle (default ImageToMap)
  REQUIRE( w.pickCanvas() == ref );
}
