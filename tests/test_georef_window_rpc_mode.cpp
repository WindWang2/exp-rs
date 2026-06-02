// SICNU GEO RS — Task 11.4.8 UI test: RPC-mode toggle on the params panel.
//
// Verifies that:
//   - The georeferencer window exposes a RsGeorefModeToggle and a
//     RsGeorefParamsPanel as findable children.
//   - On initial construction (ImageToMap mode) the DEM section is hidden.
//   - Switching to RpcPhysical via the toggle reveals the DEM section and
//     selects TransformMethod::RpcPhysical in the params panel combo.
//   - Switching back to ImageToMap re-hides the DEM section.

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>

#include "qgsgcptransformer.h"
#include "qgsgeoreferencermainwindow.h"
#include "rs_georef_mode_toggle.h"
#include "rs_georef_params_panel.h"

#include <cstdlib>

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

TEST_CASE( "RPC mode: DEM section visible only when RPC selected",
           "[georef][window][rpc]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );

  auto *panel = w.findChild<RsGeorefParamsPanel *>();
  auto *toggle = w.findChild<RsGeorefModeToggle *>();
  REQUIRE( panel != nullptr );
  REQUIRE( toggle != nullptr );

  // Default mode is ImageToMap → DEM section hidden.
  REQUIRE_FALSE( panel->isDemSectionVisible() );

  toggle->setMode( RsGeorefModeToggle::RpcPhysical );
  REQUIRE( panel->isDemSectionVisible() );
  REQUIRE( panel->transformMethod() ==
           QgsGcpTransformerInterface::TransformMethod::RpcPhysical );

  toggle->setMode( RsGeorefModeToggle::ImageToMap );
  REQUIRE_FALSE( panel->isDemSectionVisible() );
}
