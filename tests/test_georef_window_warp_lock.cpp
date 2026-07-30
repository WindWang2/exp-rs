#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QAction>
#include <QApplication>

#include "qgsgcplistwidget.h"
#include "qgsgeoreferencermainwindow.h"

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

TEST_CASE( "Warp lock: while warp pending GCP table is disabled and Apply disabled",
           "[georef][window][warplock]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );

  auto *table = w.findChild<QgsGCPListWidget *>( QStringLiteral( "rsGcpTable" ) );
  REQUIRE( table != nullptr );
  const bool tableStartedEnabled = table->isEnabled();

  auto *applyAction = w.findChild<QAction *>( QStringLiteral( "rsGeorefApplyAction" ) );
  REQUIRE( applyAction != nullptr );
  const bool applyStartedEnabled = applyAction->isEnabled();

  w.setWarpInProgressForTest( true );
  REQUIRE_FALSE( table->isEnabled() );
  REQUIRE_FALSE( applyAction->isEnabled() );

  w.setWarpInProgressForTest( false );
  REQUIRE( table->isEnabled() );
  REQUIRE( applyAction->isEnabled() == applyStartedEnabled );
}
