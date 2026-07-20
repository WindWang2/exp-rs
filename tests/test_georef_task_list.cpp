#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QTableWidget>

#include "rs_georef_task_list.h"

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
  char fake_argv0[] = "test_georef_task_list";
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

TEST_CASE( "georef task list: begin and finish success", "[georef][tasklist]" )
{
  ensureApp();
  RsGeorefTaskList list;
  REQUIRE( list.entryCount() == 0 );
  REQUIRE_FALSE( list.hasRunning() );

  const int id = list.beginTask(
    RsGeorefTaskList::Kind::WarpI2I,
    QStringLiteral( "src.tif → out.tif" ),
    QStringLiteral( "PolynomialOrder1" ),
    QStringLiteral( "/tmp/src.tif" ),
    QStringLiteral( "/tmp/out.tif" ),
    8, 0.42 );
  REQUIRE( id >= 1 );
  REQUIRE( list.entryCount() == 1 );
  REQUIRE( list.hasRunning() );
  REQUIRE( list.runningCount() == 1 );

  auto *table = list.findChild<QTableWidget *>( QStringLiteral( "rsGeorefTaskTable" ) );
  REQUIRE( table != nullptr );
  REQUIRE( table->rowCount() == 1 );

  list.setProgress( id, 42.5 );
  REQUIRE( list.entryById( id ).progress == 42.5 );

  list.finishSuccess( id, 1500, 4096 );
  REQUIRE_FALSE( list.hasRunning() );
  REQUIRE( list.entryAt( 0 ).status == RsGeorefTaskList::Status::Success );
  REQUIRE( list.entryAt( 0 ).durationMs == 1500 );
  REQUIRE( list.entryAt( 0 ).outputBytes == 4096 );
  REQUIRE( list.entryAt( 0 ).progress == 100.0 );
}

TEST_CASE( "georef task list: fail and clear finished keeps running", "[georef][tasklist]" )
{
  ensureApp();
  RsGeorefTaskList list;
  const int a = list.beginTask( RsGeorefTaskList::Kind::WarpI2M,
                                QStringLiteral( "a" ), QStringLiteral( "RpcPhysical" ),
                                QStringLiteral( "/a.tif" ), QStringLiteral( "/a_out.tif" ),
                                3, -1.0 );
  const int b = list.beginTask( RsGeorefTaskList::Kind::WarpI2I,
                                QStringLiteral( "b" ), QStringLiteral( "Linear" ),
                                QStringLiteral( "/b.tif" ), QStringLiteral( "/b_out.tif" ),
                                4, 1.0 );
  list.finishFailed( a, QStringLiteral( "boom" ), 10 );
  REQUIRE( list.runningCount() == 1 );
  list.clearFinished();
  REQUIRE( list.entryCount() == 1 );
  REQUIRE( list.entryAt( 0 ).id == b );
  REQUIRE( list.hasRunning() );
  list.finishCancelled( b, 5 );
  REQUIRE_FALSE( list.hasRunning() );
}
