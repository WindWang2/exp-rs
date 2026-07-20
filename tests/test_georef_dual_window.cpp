#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "qgsgeoreferencermainwindow.h"
#include "qgsgeoref_image_to_map_window.h"
#include "qgsmapcanvas.h"
#include "qgspointxy.h"
#include "rs_georef_params_panel.h"
#include "rs_georef_task_list.h"

#include <QAction>
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

  REQUIRE( w.findChild<QWidget *>( QStringLiteral( "rsGeorefModeToggle" ) ) == nullptr );

  auto *panel = w.findChild<RsGeorefParamsPanel *>();
  REQUIRE( panel != nullptr );
  REQUIRE( panel->profile() == RsGeorefParamsPanel::Profile::ImageToImage );

  auto *tasks = w.findChild<RsGeorefTaskList *>( QStringLiteral( "rsGeorefTaskList" ) );
  REQUIRE( tasks != nullptr );
  REQUIRE( tasks->entryCount() == 0 );
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

TEST_CASE( "I2I dual-canvas GCP pick arms both tools and appends pair", "[georef][dual][gcp]" )
{
  QgsGeoreferencerMainWindow w( nullptr, nullptr );
  auto *add = w.findChild<QAction *>( QStringLiteral( "rsGeorefAddPointAction" ) );
  REQUIRE( add != nullptr );
  add->setChecked( true );
  REQUIRE( w.srcCanvas() != nullptr );
  REQUIRE( w.dstCanvas() != nullptr );
  REQUIRE( w.srcCanvas()->mapTool() != nullptr );
  REQUIRE( w.dstCanvas()->mapTool() != nullptr );
  REQUIRE( w.srcCanvas()->mapTool() != w.dstCanvas()->mapTool() );

  REQUIRE( w.gcpCountForTest() == 0 );
  REQUIRE_FALSE( w.hasPendingSourceForTest() );

  // Dest first → no GCP (must pick SRC first).
  w.pickDestForTest( QgsPointXY( 100, 200 ) );
  REQUIRE( w.gcpCountForTest() == 0 );

  w.pickSourceForTest( QgsPointXY( 10, 20 ) );
  REQUIRE( w.hasPendingSourceForTest() );
  REQUIRE( w.gcpCountForTest() == 0 );

  w.pickDestForTest( QgsPointXY( 100, 200 ) );
  REQUIRE_FALSE( w.hasPendingSourceForTest() );
  REQUIRE( w.gcpCountForTest() == 1 );

  // Second pair
  w.pickSourceForTest( QgsPointXY( 30, 40 ) );
  w.pickDestForTest( QgsPointXY( 130, 240 ) );
  REQUIRE( w.gcpCountForTest() == 2 );
}
