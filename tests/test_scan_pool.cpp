// Workbench 6.0 Milestone C — bounded UI scan pool contract (#797)
#include <catch2/catch_test_macros.hpp>

#include "app/widgets/rs_scan_pool.h"

#include <QCoreApplication>
#include <QSemaphore>
#include <QThreadPool>

#include <atomic>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_scan_pool";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QCoreApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QCoreApplication( fake_argc, fake_argv );
  return app;
}

} // namespace

using sicnu::app::RsScanPool;

TEST_CASE( "RsScanPool is bounded and never the global pool", "[scan_pool][ux6]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();
  // #797: GDAL scans get their own two workers so rendering and other
  // global-pool users can never be starved by a scene-wide read.
  REQUIRE( pool.pool().maxThreadCount() == 2 );
  REQUIRE( &pool.pool() != QThreadPool::globalInstance() );
}

TEST_CASE( "RsScanPool: nextGeneration supersedes older generations",
           "[scan_pool][ux6]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();

  const quint64 first = pool.nextGeneration();
  REQUIRE_FALSE( pool.isStale( first ) );

  const quint64 second = pool.nextGeneration();
  CHECK( pool.isStale( first ) );   // older → superseded
  CHECK_FALSE( pool.isStale( second ) );
}

TEST_CASE( "RsScanPool: targeted cancel flags only its own generation",
           "[scan_pool][ux6]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();

  const quint64 a = pool.nextGeneration();
  const quint64 b = pool.nextGeneration();
  pool.cancel( a );
  CHECK( pool.isStale( a ) );
  CHECK_FALSE( pool.isStale( b ) ); // other requests unaffected
}

TEST_CASE( "RsScanPool: a canceled worker exits at its next checkpoint",
           "[scan_pool][behavior][ux6]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();

  QSemaphore started;
  QSemaphore release;
  std::atomic<bool> exitedAtCheckpoint{ false };

  const quint64 generation = pool.nextGeneration();
  pool.pool().start( [&] {
    started.acquire();
    release.acquire(); // simulate the first phase of a GDAL scan
    // Cooperative cancellation checkpoint (#797): superseded work bails out
    // instead of continuing to the next band/block.
    if ( pool.isStale( generation ) )
    {
      exitedAtCheckpoint = true;
      return;
    }
  } );
  started.release();
  pool.cancel( generation );
  release.release();

  // Bounded completion wait — no timing assertion.
  REQUIRE( pool.pool().waitForDone( 10000 ) );
  REQUIRE( exitedAtCheckpoint.load() );
}
