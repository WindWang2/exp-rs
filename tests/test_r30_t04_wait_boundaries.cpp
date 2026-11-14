// tests/test_r30_t04_wait_boundaries.cpp — R30-T04 WP03 (D2): the documented
// wait/lifecycle contracts of TaskCenter::waitForTask, locked as regression
// fences at the boundaries the R29 work established:
//
//   W1  a JobEngine WORKER thread waiting gets an immediate truthful
//       snapshot (never parks on the condition, never a fabricated
//       terminal record) — the #862 anti-deadlock contract.
//   W2  unknown task id → immediate not-found snapshot (taskId stays the
//       struct default -1), for zero and negative timeouts alike.
//   W3  timeout expiry on a RUNNING task returns the RUNNING snapshot —
//       "still running" is never dressed up as terminal.
//   W4  zero/negative timeout policy on a live task: immediate non-terminal
//       snapshot, bounded cost.
//   W5  an app-thread wait SERVICES the host event loop (the premise that
//       keeps JobEngine's host lane reachable from GUI waiters): a
//       host-affine operator submitted through TaskCenter completes while
//       the very same thread sits in waitForTask.
#include <catch2/catch_test_macros.hpp>

#include "support/bounded_wait.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"
#include "processing/framework/task_center.h"

#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <chrono>
#include <string>

using namespace sicnu::jobs;
using namespace sicnu::operators;
using namespace sicnu::operators::schema;

namespace
{

QCoreApplication *waitBoundariesApp()
{
  if ( QCoreApplication::instance() )
    return QCoreApplication::instance();
  static int argc = 1;
  static char arg0[] = "test_r30_t04_wait_boundaries";
  static char *argv[] = { arg0, nullptr };
  return new QCoreApplication( argc, argv );
}

std::atomic<bool> g_probeRanOnAppThread{ false };

// Host-affine probe: the body runs on the Qt application thread via the
// JobEngine host lane (requiresHostQtThread), proving W5's premise.
class WaitProbeOperator : public RSOperator
{
  public:
    std::string name() const override { return "test:r30waitprobe"; }
    std::string displayName() const override { return "R30 Wait Probe"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Wait-boundary host probe."; }
    bool requiresHostQtThread() const override { return true; }

    Json::Value schema() const override
    {
      Json::Value params( Json::objectValue );
      Json::Value outputs( Json::objectValue );
      return makeRootSchema( displayName(), description(), params, outputs );
    }

    Json::Value run( const Json::Value &, RSOperatorContext & ) override
    {
      g_probeRanOnAppThread.store( QThread::currentThread()
                                   == QCoreApplication::instance()->thread() );
      return Json::Value( Json::objectValue );
    }
};

struct WaitBoundariesGuard
{
  WaitBoundariesGuard()
  {
    waitBoundariesApp();
    auto &reg = sicnu::operators::RSOperatorRegistry::instance();
    if ( !reg.hasOperator( "test:r30waitprobe" ) )
    {
      reg.registerOperator( "test:r30waitprobe", []() -> std::unique_ptr<sicnu::operators::RSOperator> {
        return std::make_unique<WaitProbeOperator>();
      } );
    }
    auto &eng = JobEngine::instance();
    eng.shutdownForTests();
    eng.setListener( nullptr );
    eng.clearExecutors();
    eng.setMaxWorkers( 2 );
  }

  ~WaitBoundariesGuard()
  {
    JobEngine::instance().waitUntilIdleForTests( 15000 );
    JobEngine::instance().setListener( nullptr );
    JobEngine::instance().clearExecutors();
    JobEngine::instance().setFallbackExecutor( {} );
  }
};

bool isTerminal( sicnu::TaskStatus status )
{
  return status == sicnu::TaskStatus::Completed || status == sicnu::TaskStatus::Failed
         || status == sicnu::TaskStatus::Canceled;
}

} // namespace

TEST_CASE( "W1: worker-originated waitForTask returns an immediate truthful snapshot",
           "[r30][t04][wait_boundaries]" )
{
  WaitBoundariesGuard guard;
  auto &center = sicnu::TaskCenter::instance();

  sicnu_test::TestGate parkGate;
  std::atomic<bool> parked{ false };

  // The parked task occupies a worker and stays Running.
  const long parkedId = center.submitJob(
    JobRequest{ .algorithmId = "callable:waitb-park", .source = "test" },
    [ &parked, &parkGate ]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
      parked.store( true );
      parkGate.wait();
      return Json::Value( Json::objectValue );
    } );
  REQUIRE( parkedId > 0 );
  REQUIRE( sicnu_test::waitUntil( [ &parked ] { return parked.load(); }, 10000 ) );

  // The observer task runs ON a worker and asks waitForTask about the
  // parked task. Contract: immediate snapshot (no pool-slot parking), the
  // observed status is whatever is true right now (non-terminal here), and
  // no exception — the wait is what is refused, not the query.
  std::atomic<bool> observedNonTerminal{ false };
  std::atomic<long long> observedElapsedMs{ -1 };
  std::atomic<bool> waitThrew{ false };
  const long observerId = center.submitJob(
    JobRequest{ .algorithmId = "callable:waitb-observe", .source = "test" },
    [ & ]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
      const auto started = std::chrono::steady_clock::now();
      try
      {
        const sicnu::AlgorithmTaskInfo snap = center.waitForTask( parkedId, std::chrono::milliseconds( 4000 ) );
        observedNonTerminal.store( snap.taskId == parkedId && !isTerminal( snap.status ) );
      }
      catch ( ... )
      {
        waitThrew.store( true );
      }
      observedElapsedMs.store(
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - started )
          .count() );
      return Json::Value( Json::objectValue );
    } );
  REQUIRE( observerId > 0 );

  // Wait for the observer's terminal state (bounded).
  REQUIRE( sicnu_test::waitUntil(
    [ &center, observerId ] {
      const auto snap = center.getTaskInfo( observerId );
      return snap.taskId == observerId && isTerminal( snap.status );
    },
    15000 ) );

  CHECK_FALSE( waitThrew.load() );
  CHECK( observedNonTerminal.load() );
  // Immediate return: far below the 4 s timeout a parked wait would burn.
  CHECK( observedElapsedMs.load() < 2000 );

  parkGate.open();
  CHECK_FALSE( parkGate.timedOut() );
  JobEngine::instance().waitUntilIdleForTests( 15000 );
}

TEST_CASE( "W2+W3+W4: timeout boundaries on unknown, running and live tasks",
           "[r30][t04][wait_boundaries]" )
{
  WaitBoundariesGuard guard;
  auto &center = sicnu::TaskCenter::instance();

  // --- W2: unknown id → not-found snapshot, immediately, for any timeout.
  for ( const auto timeout : { std::chrono::milliseconds( 0 ),
                               std::chrono::milliseconds( -5 ),
                               std::chrono::milliseconds( 100 ) } )
  {
    const auto started = std::chrono::steady_clock::now();
    const sicnu::AlgorithmTaskInfo notFound = center.waitForTask( -424242, timeout );
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
    CHECK( notFound.taskId == -1 ); // truthful not-found, not a fabricated record
    CHECK( elapsed < 400 );
  }

  // --- live task for W3/W4.
  sicnu_test::TestGate parkGate;
  std::atomic<bool> parked{ false };
  const long parkedId = center.submitJob(
    JobRequest{ .algorithmId = "callable:waitb-park2", .source = "test" },
    [ &parked, &parkGate ]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
      parked.store( true );
      parkGate.wait();
      return Json::Value( Json::objectValue );
    } );
  REQUIRE( parkedId > 0 );
  REQUIRE( sicnu_test::waitUntil( [ &parked ] { return parked.load(); }, 10000 ) );

  // --- W3: timeout expiry returns the RUNNING snapshot, not a terminal
  // fabrication, roughly at the timeout (no early give-up, no overrun).
  {
    const auto started = std::chrono::steady_clock::now();
    const sicnu::AlgorithmTaskInfo snap = center.waitForTask( parkedId, std::chrono::milliseconds( 150 ) );
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
    REQUIRE( snap.taskId == parkedId );
    CHECK( !isTerminal( snap.status ) ); // "still running" stays "still running"
    CHECK( elapsed >= 140 );
    CHECK( elapsed < 2000 );
  }

  // --- W4: zero and negative timeouts return immediately with the live
  // (non-terminal) snapshot — the documented "return current state now"
  // policy, bounded cost.
  for ( const auto timeout : { std::chrono::milliseconds( 0 ),
                               std::chrono::milliseconds( -1 ) } )
  {
    const auto started = std::chrono::steady_clock::now();
    const sicnu::AlgorithmTaskInfo snap = center.waitForTask( parkedId, timeout );
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
    CHECK( snap.taskId == parkedId );
    CHECK( !isTerminal( snap.status ) );
    CHECK( elapsed < 100 );
  }

  parkGate.open();
  CHECK_FALSE( parkGate.timedOut() );
  JobEngine::instance().waitUntilIdleForTests( 15000 );
}

TEST_CASE( "W5: an app-thread wait services the host lane until the task completes",
           "[r30][t04][wait_boundaries]" )
{
  WaitBoundariesGuard guard;
  auto &center = sicnu::TaskCenter::instance();
  g_probeRanOnAppThread.store( false );

  // A host-affine operator marshals through the Qt application thread's
  // event loop; this test thread IS that thread, and it is about to sit in
  // waitForTask. The wait must pump the lane (ExcludeUserInput slices),
  // not starve it into the cancel-grace failure.
  JobRequest req;
  req.algorithmId = "test:r30waitprobe";
  req.source = "test";
  const long taskId = center.submitJob( req );
  REQUIRE( taskId > 0 );

  const sicnu::AlgorithmTaskInfo snap =
    center.waitForTask( taskId, std::chrono::milliseconds( 10000 ) );
  REQUIRE( snap.taskId == taskId );
  CHECK( snap.status == sicnu::TaskStatus::Completed );
  CHECK( g_probeRanOnAppThread.load() );
}
