// SICNU GEO RS — RPC DEM visibility: method-driven on I2M profile (dual-window redesign).
//
// Verifies that:
//   - Image 2 Image shell pins I2I profile: DEM hidden, mode toggle hidden.
//   - Params panel ImageToMap profile can show DEM when RPC method is selected.
//   - I2M window exposes SRC + Map canvases and vertical splitter.

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QSplitter>

#include "qgsgcptransformer.h"
#include "qgsgeoreferencermainwindow.h"
#include "qgsgeoref_image_to_map_window.h"
#include "qgsmapcanvas.h"
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

TEST_CASE( "I2I shell: DEM hidden and no mode toggle widget",
           "[georef][window][rpc]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );

  auto *panel = w.findChild<RsGeorefParamsPanel *>();
  REQUIRE( panel != nullptr );
  REQUIRE( panel->profile() == RsGeorefParamsPanel::Profile::ImageToImage );
  REQUIRE_FALSE( panel->isDemSectionVisible() );
  REQUIRE( w.findChild<RsGeorefModeToggle *>() == nullptr );
}

TEST_CASE( "params panel I2M can show dem for RPC", "[georef][panel]" )
{
  ensureApp();
  RsGeorefParamsPanel p;
  p.setProfile( RsGeorefParamsPanel::Profile::ImageToMap );
  p.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  p.setRpcMode( true );
  REQUIRE( p.isDemSectionVisible() );

  p.setProfile( RsGeorefParamsPanel::Profile::ImageToImage );
  p.setRpcMode( false );
  REQUIRE_FALSE( p.isDemSectionVisible() );
}

TEST_CASE( "I2M window has SRC and Map canvases", "[georef][dual]" )
{
  ensureApp();
  QgsGeorefImageToMapWindow w( nullptr, nullptr );
  auto *src = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefI2MSrcCanvas" ) );
  auto *map = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefI2MMapCanvas" ) );
  REQUIRE( src != nullptr );
  REQUIRE( map != nullptr );
  auto *splitter = w.findChild<QSplitter *>( QStringLiteral( "rsGeorefI2MSplitter" ) );
  REQUIRE( splitter != nullptr );
  REQUIRE( splitter->orientation() == Qt::Vertical );

  auto *panel = w.findChild<RsGeorefParamsPanel *>();
  REQUIRE( panel != nullptr );
  REQUIRE( panel->profile() == RsGeorefParamsPanel::Profile::ImageToMap );
}
