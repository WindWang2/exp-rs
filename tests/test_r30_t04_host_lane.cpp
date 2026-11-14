// test_r30_t04_host_lane.cpp — R30-T04 WP02/D1: the JobEngine host-thread
// lane's Pending/Running/Abandoned state contract, proven with CONTROLLED
// interleavings instead of stress loops.
//
// Determinism technique (no production pause seam needed): the Qt posted-
// event queue is FIFO for equal priorities. Queue a PARKER handler that
// blocks on a semaphore BEFORE the lane's functor is posted; the lane's
// queued invocation then sits strictly behind the parker, so the test
// decides exactly when (and whether) the host thread ever delivers it.
//
//   T1  worker abandons (Pending→Abandoned) while the callback is parked
//       behind the parker, then the callback arrives LATE → claim CAS fails,
//       body never runs, no side effects, no dangling-frame touch
//       ("worker exits, callback late" interleaving; bounded: no grace).
//   T2  body claims (Pending→Running) and parks INSIDE run(); cancel armed
//       mid-body (from a helper thread — the parked body owns the app
//       thread, so the main thread cannot arm it) → the worker must NOT
//       abandon a started body; the body unwinds cooperatively over its
//       still-alive frame (the epilogue member-touches run AFTER the
//       parking point on both the cancel and the complete path) and the
//       job lands Cancelled through the ordinary operator-cancel path.
//   T3  cancel armed before the lane claim, host servicing normally →
//       either claim-then-observe-cancel or abandon wins; both must end
//       Cancelled with zero body side effects (interleaving-tolerant).
//   T4  the healthy lane still runs the body ON the Qt application thread
//       and returns its result (regression lock for the CAS refactor).
//
// This binary owns a QCoreApplication on the Catch2 main thread (heap-owned,
// created by the first case that needs it — JobEngine marshals to
// QCoreApplication::instance(), which must be alive and pumping).

#include <catch2/catch_test_macros.hpp>

#include "support/bounded_wait.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace sicnu::jobs;
using namespace sicnu::operators;
using namespace sicnu::operators::schema;

namespace
{

// Heap-owned process-wide application for this binary. Created on first
// use from the Catch2 main thread (the lane's host thread); never destroyed
// at exit (immortal, same ownership stance as the JobEngine singleton, so
// a late parked functor can never dereference a destroyed app).
QCoreApplication *hostLaneApp()
{
  if ( QCoreApplication::instance() )
    return QCoreApplication::instance();
  static int argc = 1;
  static char arg0[] = "test_r30_t04_host_lane";
  static char *argv[] = { arg0, nullptr };
  return new QCoreApplication( argc, argv );
}

// Shared observable side effects for the probe operator bodies. Body writes
// happen on the Qt application thread; the test observes them from the same
// thread after processEvents, and the worker-side park polls use atomics.
// The state is process-global and reset EXPLICITLY by each case (resetProbe)
// — run() must never reset it, or the body would wipe the very selector
// (parkInBody) the case armed.
struct HostProbeState
{
  std::atomic<int> bodyEntries{ 0 };      // run() reached
  std::atomic<int> epilogues{ 0 };        // run() reached its post-park lines
  std::atomic<bool> bodyStarted{ false }; // body park point reached
  std::atomic<bool> ranOnAppThread{ false };
  QSemaphore bodyGate;                    // body parks here when armed
  bool parkInBody = false;                // body semantics selector
};

HostProbeState g_probe;

void resetProbe()
{
  g_probe.bodyEntries.store( 0 );
  g_probe.epilogues.store( 0 );
  g_probe.bodyStarted.store( false );
  g_probe.ranOnAppThread.store( false );
  while ( g_probe.bodyGate.available() > 0 )
    g_probe.bodyGate.tryAcquire(); // drain any leftover release
  g_probe.parkInBody = false;
}

// Host-affine probe operator: R30-T04's subject is the LANE, not operator
// logic, so params are optional and the body only records/observes.
class HostProbeOperator : public RSOperator
{
  public:
    std::string name() const override { return "test:r30hostprobe"; }
    std::string displayName() const override { return "R30 Host Lane Probe"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Host-lane state-contract probe."; }
    bool requiresHostQtThread() const override { return true; }

    Json::Value schema() const override
    {
      Json::Value params( Json::objectValue );
      params["value"] = makeNumberParam( "value", "Echo value", 0.0 );
      Json::Value outputs( Json::objectValue );
      outputs["value"] = makeNumberParam( "value", "Echoed value", 0.0 );
      return makeRootSchema( displayName(), description(), params, outputs );
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
      HostProbeState &s = g_probe;
      s.bodyEntries.fetch_add( 1 );
      s.ranOnAppThread.store( QThread::currentThread() == QCoreApplication::instance()->thread() );
      if ( s.parkInBody )
      {
        s.bodyStarted.store( true );
        // Bounded cooperative park: wake on the gate or on cancellation so
        // a failing test can never wedge a worker into the ctest timeout
        // (TestGate discipline, support/bounded_wait.h).
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
        while ( !s.bodyGate.tryAcquire() )
        {
          if ( context.isCancelled() || std::chrono::steady_clock::now() >= deadline )
            break;
          std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
      }
      // Epilogue AFTER the park: these member touches run on BOTH the
      // cancel and the complete path. If the worker had abandoned a
      // started body, they would race the frame's destruction.
      s.epilogues.fetch_add( 1 );
      if ( context.isCancelled() )
        throw RSOperatorError( ErrorCode::Cancelled, "host body observed cancel" );
      Json::Value result( Json::objectValue );
      result["value"] = params.isMember( "value" ) ? params["value"] : Json::Value( 0 );
      return result;
    }
};

struct HostLaneGuard
{
  HostLaneGuard()
  {
    hostLaneApp();
    auto &reg = RSOperatorRegistry::instance();
    if ( !reg.hasOperator( "test:r30hostprobe" ) )
    {
      reg.registerOperator( "test:r30hostprobe", []() -> std::unique_ptr<RSOperator> {
        return std::make_unique<HostProbeOperator>();
      } );
    }
    auto &eng = JobEngine::instance();
    eng.setListener( nullptr );
    eng.clearExecutors();
    eng.setMaxWorkers( 2 );
  }

  ~HostLaneGuard()
  {
    auto &eng = JobEngine::instance();
    eng.waitUntilIdleForTests( 15000 );
    eng.setListener( nullptr );
    eng.clearExecutors();
    eng.setFallbackExecutor( {} );
  }

  JobEngine &engine() { return JobEngine::instance(); }
};

void pumpEvents()
{
  QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
}

bool waitUntilJobState( JobEngine &eng, const std::string &id, JobState state,
                        const std::function<void()> &pump = {} )
{
  return sicnu_test::waitUntil(
    [&eng, id, state] {
      const auto snap = eng.snapshot( id );
      return snap.has_value() && snap->state == state;
    },
    10000, nullptr, pump );
}

bool waitUntilJobRunning( JobEngine &eng, const std::string &id,
                          const std::function<void()> &pump = {} )
{
  return sicnu_test::waitUntil(
    [&eng, id] {
      const auto snap = eng.snapshot( id );
      return snap.has_value() && snap->state == JobState::Running;
    },
    10000, nullptr, pump );
}

} // namespace

TEST_CASE( "T1: late delivery after worker abandon never runs the body (Pending→Abandoned wins)",
           "[jobs][host_lane][r30_t04]" )
{
  HostLaneGuard guard;
  auto &eng = guard.engine();
  QCoreApplication *app = hostLaneApp();
  resetProbe();
  HostProbeState &s = g_probe;
  s.parkInBody = false; // T1's body would run to completion if wrongly claimed

  // Parker: blocks the host event queue head until the test releases it.
  // Everything the lane queues afterwards sits strictly behind it (FIFO).
  // The gate is heap-shared (R1 review P3) AND released by a scope guard
  // (R2 review P4): a failing REQUIRE that aborts the case mid-flight must
  // neither leave the queued parker referencing a destroyed stack frame
  // nor park the NEXT case's processEvents on an un-released gate (the
  // suite would then hang to the ctest timeout instead of reporting the
  // real failure).
  const auto queueGate = std::make_shared<QSemaphore>();
  struct GateReleaser
  {
    std::shared_ptr<QSemaphore> gate;
    bool released = false;
    ~GateReleaser()
    {
      if ( !released )
        gate->release();
    }
  } parkerReleaser{ queueGate, false };
  QMetaObject::invokeMethod( app, [queueGate]() { queueGate->acquire(); },
                             Qt::QueuedConnection );

  JobRequest req;
  req.algorithmId = "test:r30hostprobe";
  req.params["value"] = 7;
  req.title = "t1-late-callback";
  req.source = "test";
  const auto id = eng.submit( req );
  REQUIRE_FALSE( id.empty() );

  // The worker must reach the lane park (Running) before cancel is armed so
  // the queued-job synchronous-cancel shortcut cannot take this case. No
  // event pumping here: the parker would block the main thread.
  REQUIRE( waitUntilJobRunning( eng, id ) );
  REQUIRE( eng.cancel( id ) );
  // Worker: cancel armed + invocation still Pending → wins the abandon CAS,
  // bounded (no grace window to wait out), job lands Cancelled.
  REQUIRE( waitUntilJobState( eng, id, JobState::Cancelled ) );
  const auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  CAPTURE( snap->statusMessage, snap->error );
  // The abandon path's own message — distinct from the operator's cancel.
  CHECK( snap->error.find( "did not service host-thread execution" )
         != std::string::npos );

  // NOW the late callback flows: parker runs, then the lane's invocation.
  // The claim CAS must fail (phase is Abandoned): no body, no side effects,
  // no touch of the unwound runOperatorJob frame.
  queueGate->release();
  parkerReleaser.released = true; // explicit release consumed the guard
  QCoreApplication::processEvents( QEventLoop::AllEvents, 500 );

  CHECK( s.bodyEntries.load() == 0 );
  CHECK( s.epilogues.load() == 0 );
}

TEST_CASE( "T2: a started host body is never abandoned; cancel unwinds cooperatively over the live frame",
           "[jobs][host_lane][r30_t04]" )
{
  HostLaneGuard guard;
  auto &eng = guard.engine();
  resetProbe();
  HostProbeState &s = g_probe;
  s.parkInBody = true; // body claims, parks INSIDE run() before its epilogue

  JobRequest req;
  req.algorithmId = "test:r30hostprobe";
  req.params["value"] = 11;
  req.title = "t2-started-await";
  req.source = "test";
  const auto id = eng.submit( req );
  REQUIRE_FALSE( id.empty() );

  // The parked body owns the app thread inside the delivering processEvents
  // call, so the main thread cannot arm the cancel itself — a helper thread
  // does it as soon as the body has provably claimed the lane.
  std::thread canceller( [ &eng, id, &s ] {
    if ( !sicnu_test::waitUntil( [ &s ] { return s.bodyStarted.load(); }, 10000 ) )
      return;
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) ); // park observed
    eng.cancel( id );
  } );

  // Main thread pumps: the delivery call blocks inside the body's park
  // until the helper arms the cancel; the body then unwinds through its
  // epilogue and the engine finishes the job Cancelled. Join the helper
  // BEFORE any fatal assertion so a failing case can never destroy a
  // joinable thread (SIGABRT masking the real result).
  const bool terminal = sicnu_test::waitUntil(
    [ &eng, id ] {
      const auto snap = eng.snapshot( id );
      return snap.has_value()
             && ( snap->state == JobState::Cancelled || snap->state == JobState::Succeeded );
    },
    15000, nullptr, pumpEvents );
  canceller.join();
  REQUIRE( terminal );
  REQUIRE( waitUntilJobState( eng, id, JobState::Cancelled ) );

  // The body reached its epilogue — over the still-alive operator frame —
  // before the worker was released: a started body is awaited, not
  // abandoned.
  CHECK( s.bodyEntries.load() == 1 );
  CHECK( s.epilogues.load() == 1 );
  CHECK( s.ranOnAppThread.load() );
}

TEST_CASE( "T3: cancel armed before the lane claim ends Cancelled with zero body side effects",
           "[jobs][host_lane][r30_t04]" )
{
  HostLaneGuard guard;
  auto &eng = guard.engine();
  resetProbe();
  HostProbeState &s = g_probe;
  s.parkInBody = false;

  JobRequest req;
  req.algorithmId = "test:r30hostprobe";
  req.title = "t3-pre-claim-cancel";
  req.source = "test";
  const auto id = eng.submit( req );
  REQUIRE_FALSE( id.empty() );

  // No pumping before cancel: the queued invocation cannot be delivered
  // while the main thread never processes events, so the cancel is
  // deterministically armed before the lane's claim.
  REQUIRE( waitUntilJobRunning( eng, id ) );
  REQUIRE( eng.cancel( id ) );

  // Service the host loop while waiting: whichever side wins the race
  // (claim-then-observe-cancel no-op, or worker abandon), the terminal
  // state is Cancelled and the body never produced a side effect.
  REQUIRE( waitUntilJobState( eng, id, JobState::Cancelled, pumpEvents ) );

  // Flush any still-queued invocation so side-effect checks observe the
  // final state of the queue, not an intermediate one.
  QCoreApplication::processEvents( QEventLoop::AllEvents, 500 );

  const auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  CHECK( snap->state == JobState::Cancelled );
  CHECK( s.bodyEntries.load() == 0 );
  CHECK( s.epilogues.load() == 0 );
}

TEST_CASE( "T4: healthy lane executes the body on the Qt application thread",
           "[jobs][host_lane][r30_t04]" )
{
  HostLaneGuard guard;
  auto &eng = guard.engine();
  resetProbe();
  HostProbeState &s = g_probe;
  s.parkInBody = false;

  JobRequest req;
  req.algorithmId = "test:r30hostprobe";
  req.params["value"] = 42;
  req.title = "t4-healthy-lane";
  req.source = "test";
  const auto id = eng.submit( req );
  REQUIRE_FALSE( id.empty() );

  REQUIRE( waitUntilJobState( eng, id, JobState::Succeeded, pumpEvents ) );

  const auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
  REQUIRE( snap->result.isMember( "value" ) );
  CHECK( snap->result["value"].asDouble() == 42.0 );
  CHECK( s.bodyEntries.load() == 1 );
  CHECK( s.ranOnAppThread.load() );
}
