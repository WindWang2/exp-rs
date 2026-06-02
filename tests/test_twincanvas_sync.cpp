#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QtTest>

#include "qgsmapcanvas.h"
#include "qgsrectangle.h"

#include "rs_twincanvas_sync_controller.h"

#include <cstdlib>

using Catch::Approx;

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup when
// run after a Catch2 process that exercised QgsMapCanvas; assertions all
// succeed before this happens, so bypass the destructor sequence with
// std::_Exit once Catch has reported the final result.
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
  char fake_argv0[] = "test_twincanvas_sync";
  char *fake_argv[] = { fake_argv0, nullptr };

  // Shared QApplication — Qt does not permit two instances per process.
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

TEST_CASE( "Twin canvas: src extent change propagates to ref", "[georef][sync]" )
{
  ensureApp();
  QgsMapCanvas src;
  QgsMapCanvas ref;
  src.resize( 300, 300 );
  ref.resize( 300, 300 );

  RsTwinCanvasSyncController ctl( &src, &ref );

  src.setExtent( QgsRectangle( 100, 100, 200, 200 ) );
  QTest::qWait( 80 ); // allow 16ms throttle + a margin to fire

  // QgsMapCanvas may widen/heighten the requested extent to fit its
  // widget aspect ratio, so assert that ref tracks the *actual* src
  // extent (within sub-pixel tolerance).
  REQUIRE( ref.extent().xMinimum() == Approx( src.extent().xMinimum() ).margin( 1e-3 ) );
  REQUIRE( ref.extent().xMaximum() == Approx( src.extent().xMaximum() ).margin( 1e-3 ) );
  REQUIRE( ref.extent().yMinimum() == Approx( src.extent().yMinimum() ).margin( 1e-3 ) );
  REQUIRE( ref.extent().yMaximum() == Approx( src.extent().yMaximum() ).margin( 1e-3 ) );
}

TEST_CASE( "Twin canvas: disabled => no propagation", "[georef][sync]" )
{
  ensureApp();
  QgsMapCanvas src;
  QgsMapCanvas ref;
  src.resize( 300, 300 );
  ref.resize( 300, 300 );

  RsTwinCanvasSyncController ctl( &src, &ref );
  ctl.setEnabled( false );
  const QgsRectangle originalRef = ref.extent();

  src.setExtent( QgsRectangle( 500, 500, 600, 600 ) );
  QTest::qWait( 80 );

  REQUIRE( ref.extent().xMinimum() == Approx( originalRef.xMinimum() ).margin( 1e-6 ) );
  REQUIRE( ref.extent().xMaximum() == Approx( originalRef.xMaximum() ).margin( 1e-6 ) );
}
