// test_dual_viewport_teardown_r4.cpp — WP-C teardown fixtures:
// RsDualViewportSyncController over a canvas pair (shell/…controller).
//
// Contract under test: the sync controller holds raw canvas pointers and no
// ownership; both destruction orders (canvases first / controller first)
// must be memory-safe, and a controller deleted while canvases live must not
// keep receiving canvas extent signals (Qt disconnect-on-destroy contract).
// The process must exit() cleanly afterwards with no _Exit defense.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QPointer>
#include <QtTest>

#include <qgsmapcanvas.h>
#include <qgsrectangle.h>

#include "shell/rs_dual_viewport_sync_controller.h"

using Catch::Approx;

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_dual_viewport_teardown_r4";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
  }

  struct CanvasPair
  {
    std::unique_ptr<QgsMapCanvas> primary;
    std::unique_ptr<QgsMapCanvas> secondary;

    CanvasPair()
    {
      primary = std::make_unique<QgsMapCanvas>();
      secondary = std::make_unique<QgsMapCanvas>();
      primary->resize( 300, 300 );
      secondary->resize( 300, 300 );
      primary->setExtent( QgsRectangle( 100, 100, 200, 200 ) );
    }
  };
} // namespace

TEST_CASE( "Dual viewport teardown: canvases die before controller", "[teardown][r4]" )
{
  ensureApp();
  CanvasPair pair;
  RsDualViewportSyncController ctl( pair.primary.get(), pair.secondary.get() );
  pair.primary->setExtent( QgsRectangle( 0, 0, 10, 10 ) );
  QTest::qWait( 40 );
  REQUIRE( ctl.stats().appliedSyncCount >= 1 );

  // Wrong-order story: both canvases go first; the (still-alive) controller
  // must not touch them via queued extent signals afterwards.
  QPointer<QgsMapCanvas> primaryGuard( pair.primary.get() );
  pair.primary.reset();
  pair.secondary.reset();
  QTest::qWait( 60 );
  REQUIRE( primaryGuard.isNull() );
}

TEST_CASE( "Dual viewport teardown: controller dies before canvases", "[teardown][r4]" )
{
  ensureApp();
  CanvasPair pair;
  QPointer<RsDualViewportSyncController> guard;
  QgsRectangle lastSynced;
  {
    RsDualViewportSyncController ctl( pair.primary.get(), pair.secondary.get() );
    guard = &ctl;
    pair.primary->setExtent( QgsRectangle( 5, 5, 15, 15 ) );
    QTest::qWait( 40 );
    lastSynced = pair.secondary->extent();
    REQUIRE( !lastSynced.isEmpty() );
  }
  // Qt disconnect-on-destroy: a further primary pan must NOT reach the dead
  // controller — the secondary stays exactly where the last live sync left it.
  pair.primary->setExtent( QgsRectangle( 20, 20, 30, 30 ) );
  QTest::qWait( 60 );
  REQUIRE( guard.isNull() );
  const QgsRectangle afterDeath = pair.secondary->extent();
  REQUIRE( afterDeath.xMinimum() == Approx( lastSynced.xMinimum() ).margin( 1e-3 ) );
  REQUIRE( afterDeath.yMinimum() == Approx( lastSynced.yMinimum() ).margin( 1e-3 ) );
}

TEST_CASE( "Dual viewport teardown: rapid extent churn then delete-all", "[teardown][r4]" )
{
  ensureApp();
  QPointer<QgsMapCanvas> primaryGuard;
  {
    CanvasPair pair;
    RsDualViewportSyncController ctl( pair.primary.get(), pair.secondary.get() );
    primaryGuard = pair.primary.get();
    for ( int i = 0; i < 20; ++i )
    {
      pair.primary->setExtent( QgsRectangle( i, i, i + 8, i + 8 ) );
      QTest::qWait( 4 );
    }
  }
  QTest::qWait( 80 );
  REQUIRE( primaryGuard.isNull() );
}

TEST_CASE( "Dual viewport teardown: sequential controllers over one canvas pair", "[teardown][r4]" )
{
  ensureApp();
  CanvasPair pair;
  for ( int round = 0; round < 3; ++round )
  {
    QPointer<RsDualViewportSyncController> guard;
    {
      RsDualViewportSyncController ctl( pair.primary.get(), pair.secondary.get() );
      guard = &ctl;
      pair.primary->setExtent( QgsRectangle( round, round, round + 10, round + 10 ) );
      QTest::qWait( 30 );
      REQUIRE( ctl.stats().appliedSyncCount >= 1 );
    }
    QTest::qWait( 30 );
    REQUIRE( guard.isNull() );
  }
}
