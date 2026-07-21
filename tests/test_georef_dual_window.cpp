#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "qgsgeoreferencermainwindow.h"
#include "qgsgeoref_image_to_map_window.h"
#include "qgsgcplistwidget.h"
#include "qgsmapcanvas.h"
#include "qgspointxy.h"
#include "rs_georef_params_panel.h"
#include "rs_georef_task_list.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
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

  // Warp / Base captions above each canvas
  auto *srcLabel = w.findChild<QLabel *>( QStringLiteral( "rsSrcLayerLabel" ) );
  auto *refLabel = w.findChild<QLabel *>( QStringLiteral( "rsRefLayerLabel" ) );
  REQUIRE( srcLabel != nullptr );
  REQUIRE( refLabel != nullptr );
  REQUIRE( srcLabel->text().contains( QStringLiteral( "Warp" ) ) );
  REQUIRE( refLabel->text().contains( QStringLiteral( "Base" ) ) );
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

  auto *srcLabel = w.findChild<QLabel *>( QStringLiteral( "rsGeorefI2MSrcLayerLabel" ) );
  auto *mapLabel = w.findChild<QLabel *>( QStringLiteral( "rsGeorefI2MMapLayerLabel" ) );
  REQUIRE( srcLabel != nullptr );
  REQUIRE( mapLabel != nullptr );
  REQUIRE( srcLabel->text().contains( QStringLiteral( "Warp" ) ) );
  REQUIRE( mapLabel->text().contains( QStringLiteral( "Base" ) ) );
}

TEST_CASE( "I2I GCP tools disabled until both source and reference open", "[georef][dual][tools]" )
{
  QgsGeoreferencerMainWindow w( nullptr, nullptr );
  auto *add = w.findChild<QAction *>( QStringLiteral( "rsGeorefAddPointAction" ) );
  auto *move = w.findChild<QAction *>( QStringLiteral( "rsGeorefMovePointAction" ) );
  auto *del = w.findChild<QAction *>( QStringLiteral( "rsGeorefDeletePointAction" ) );
  REQUIRE( add != nullptr );
  REQUIRE( move != nullptr );
  REQUIRE( del != nullptr );
  REQUIRE_FALSE( add->isEnabled() );
  REQUIRE_FALSE( move->isEnabled() );
  REQUIRE_FALSE( del->isEnabled() );
}

TEST_CASE( "I2M GCP tools disabled until source open", "[georef][dual][tools]" )
{
  QgsGeorefImageToMapWindow w( nullptr, nullptr );
  auto *add = w.findChild<QAction *>( QStringLiteral( "rsGeorefI2MAddPointAction" ) );
  REQUIRE( add != nullptr );
  REQUIRE_FALSE( add->isEnabled() );
}

TEST_CASE( "I2I dual-canvas GCP pick arms both tools and appends pair", "[georef][dual][gcp]" )
{
  QgsGeoreferencerMainWindow w( nullptr, nullptr );
  auto *add = w.findChild<QAction *>( QStringLiteral( "rsGeorefAddPointAction" ) );
  REQUIRE( add != nullptr );
  // Bypass layer gate for unit test of pick/commit path.
  add->setEnabled( true );
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

  // Source must not collapse to (0,0) — regression for empty SRC column.
  {
    auto *table = w.findChild<QgsGCPListWidget *>( QStringLiteral( "rsGcpTable" ) );
    REQUIRE( table != nullptr );
    REQUIRE( table->model() != nullptr );
    const double sx = table->model()->data( table->model()->index( 0, 2 ), Qt::EditRole ).toDouble();
    const double sy = table->model()->data( table->model()->index( 0, 3 ), Qt::EditRole ).toDouble();
    const double dx = table->model()->data( table->model()->index( 0, 4 ), Qt::EditRole ).toDouble();
    const double dy = table->model()->data( table->model()->index( 0, 5 ), Qt::EditRole ).toDouble();
    REQUIRE( sx == 10.0 );
    REQUIRE( sy == 20.0 );
    REQUIRE( dx == 100.0 );
    REQUIRE( dy == 200.0 );
  }

  // Second pair
  w.pickSourceForTest( QgsPointXY( 30, 40 ) );
  w.pickDestForTest( QgsPointXY( 130, 240 ) );
  REQUIRE( w.gcpCountForTest() == 2 );

  // Canvas badge items should exist on both SRC and REF scenes.
  auto *src = w.srcCanvas();
  auto *ref = w.dstCanvas();
  REQUIRE( src != nullptr );
  REQUIRE( ref != nullptr );
  REQUIRE( src->scene() != nullptr );
  REQUIRE( ref->scene() != nullptr );
  // At least 2 graphics items per canvas (one badge per GCP); scenes may
  // also hold other items, so only require non-empty item lists.
  REQUIRE( src->scene()->items().size() >= 2 );
  REQUIRE( ref->scene()->items().size() >= 2 );
}
