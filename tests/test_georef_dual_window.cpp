#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "qgsgeoreferencermainwindow.h"
#include "qgsgeoref_image_to_map_window.h"
#include "qgsmapcanvas.h"
#include "rs_georef_params_panel.h"

#include <QApplication>
#include <QSplitter>

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

static int argc = 1;
static char arg0[] = "test_georef_dual_window";
static char *argv[] = { arg0, nullptr };
static QApplication *app = []() {
  static QApplication a( argc, argv );
  return &a;
}();

TEST_CASE( "I2I window has horizontal twin canvases", "[georef][dual]" )
{
  QgsGeoreferencerMainWindow w( nullptr, nullptr );
  w.setWindowTitle( QStringLiteral( "Image Registration · Image 2 Image" ) );
  // Existing objectNames on the I2I shell
  auto *src = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsSrcCanvas" ) );
  auto *ref = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsRefCanvas" ) );
  REQUIRE( src != nullptr );
  REQUIRE( ref != nullptr );
  auto *splitter = w.findChild<QSplitter *>( QStringLiteral( "rsGeorefSplitter" ) );
  REQUIRE( splitter != nullptr );
  REQUIRE( splitter->orientation() == Qt::Horizontal );

  auto *toggle = w.findChild<QWidget *>( QStringLiteral( "rsGeorefModeToggle" ) );
  if ( toggle )
    REQUIRE( toggle->isHidden() );

  auto *panel = w.findChild<RsGeorefParamsPanel *>();
  REQUIRE( panel != nullptr );
  REQUIRE( panel->profile() == RsGeorefParamsPanel::Profile::ImageToImage );
}

TEST_CASE( "I2M window has SRC and Map canvases", "[georef][dual]" )
{
  QgsGeorefImageToMapWindow w( nullptr, nullptr );
  auto *src = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefI2MSrcCanvas" ) );
  auto *map = w.findChild<QgsMapCanvas *>( QStringLiteral( "rsGeorefI2MMapCanvas" ) );
  REQUIRE( src != nullptr );
  REQUIRE( map != nullptr );
  auto *splitter = w.findChild<QSplitter *>( QStringLiteral( "rsGeorefI2MSplitter" ) );
  REQUIRE( splitter != nullptr );
  REQUIRE( splitter->orientation() == Qt::Vertical );
}
