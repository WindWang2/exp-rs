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

// ── Workbench 9.0 M0: per-owner generations (#861 regression) ──────────────
// #861: a single global active-generation counter let one widget's refresh
// mark every other widget's in-flight scan stale; the stale-exit path then
// left those widgets stuck in their busy state. The per-owner channel must
// keep owners independent while preserving supersede/cancel semantics.

TEST_CASE( "RsScanPool: a newer generation supersedes only its own owner",
           "[scan_pool][m0][issue861]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();

  int otherWidget = 0;
  int thisWidget = 0;

  const quint64 otherGen = pool.nextGeneration( &otherWidget );
  const quint64 thisGen = pool.nextGeneration( &thisWidget );

  // A different owner's newer request must NOT invalidate this owner's scan.
  CHECK_FALSE( pool.isStale( otherGen, &otherWidget ) );
  CHECK_FALSE( pool.isStale( thisGen, &thisWidget ) );

  // The owner's own newer request does supersede its older scan.
  const quint64 thisGen2 = pool.nextGeneration( &thisWidget );
  CHECK( pool.isStale( thisGen, &thisWidget ) );
  CHECK_FALSE( pool.isStale( thisGen2, &thisWidget ) );
  CHECK_FALSE( pool.isStale( otherGen, &otherWidget ) );
}

TEST_CASE( "RsScanPool: cancel(gen, owner) flags exactly that generation",
           "[scan_pool][m0][issue861]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();

  int widgetA = 0;
  int widgetB = 0;

  const quint64 genA = pool.nextGeneration( &widgetA );
  const quint64 genB = pool.nextGeneration( &widgetB );

  pool.cancel( genA, &widgetA );
  CHECK( pool.isStale( genA, &widgetA ) );
  CHECK_FALSE( pool.isStale( genB, &widgetB ) );

  // After cancel the owner entry is gone; a brand-new generation for the
  // same owner works normally again (widget reuse after teardown path).
  const quint64 genA2 = pool.nextGeneration( &widgetA );
  CHECK_FALSE( pool.isStale( genA2, &widgetA ) );
}

TEST_CASE( "RsScanPool: owner generations do not leak into the global path",
           "[scan_pool][m0][issue861]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();

  int widget = 0;
  const quint64 ownerGen = pool.nextGeneration( &widget );
  // The legacy global path checks only the global counter and the canceled
  // set — per-owner activity neither supersedes nor is superseded by it.
  CHECK_FALSE( pool.isStale( ownerGen ) );
}

TEST_CASE( "RsScanPool: one widget's newer scan never invalidates another's",
           "[scan_pool][behavior][m0][issue861]" )
{
  ensureApp();
  auto &pool = RsScanPool::instance();

  int widgetA = 0;
  int widgetB = 0;

  // The #861 failure mode in its exact shape: B starts a fresh generation
  // while A's scan is in flight. With the old single global counter, A's
  // token would now read stale even though nothing superseded *A*.
  QSemaphore aStarted;
  QSemaphore bExists;
  std::atomic<bool> aObservedStale{ false };

  const quint64 genA = pool.nextGeneration( &widgetA );
  pool.pool().start( [&] {
    aStarted.release();
    // Rendezvous: A checks staleness only after B's generation provably
    // exists — deterministic, no sleeps.
    bExists.acquire();
    aObservedStale.store( pool.isStale( genA, &widgetA ) );
  } );
  aStarted.acquire();

  const quint64 genB = pool.nextGeneration( &widgetB );
  CHECK_FALSE( pool.isStale( genB, &widgetB ) );
  bExists.release();

  REQUIRE( pool.pool().waitForDone( 10000 ) );
  CHECK_FALSE( aObservedStale.load() );
  CHECK_FALSE( pool.isStale( genA, &widgetA ) );
}
