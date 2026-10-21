// tests/test_r19_02_workflow_cancel_order.cpp
//
// WorkflowRuntime cooperative-cancellation ORDER contract (#1654 residual
// window, R19-02 T1): requestCancel()'s flag store and runStep()'s
// stale-cancel consume must be totally ordered by the runtime's mutex.
//
//   (a) a requestCancel() that has fully RETURNED before runStep() is
//       invoked aimed at an earlier (or no) step: the new step must run
//       uncancelled — the consume semantics the first #1654 fix added.
//   (b) a requestCancel() issued after the operator has ENTERED its run
//       loop must reach the in-flight step (poll path).
//   (c) concurrency fence: requestCancel() racing runStep()'s entry may
//       legally produce either outcome for the raced step, but must never
//       hang, never throw anything but a cancellation error, and never
//       poison a LATER step of the same session.
//   (d) negative: requestCancel() on an unknown session is a no-op, not a
//       crash.
//
// The ordering fix itself (both stores moved under m_mutex) is verified by
// inspection plus this behavioural net: pre-fix, the two stores execute
// outside any lock, so (c)'s "no poison / no lost cancel" outcome is a
// timing coincidence rather than a guarantee.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "support/bounded_wait.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_runtime.h"

using namespace sicnu::workflow;
using namespace sicnu::operators;

namespace {

int &cancelOrderAppArgc()
{
  static int argc = 1;
  return argc;
}
char cancelOrderAppArgv0[] = "test_r19_02_workflow_cancel_order";
char *cancelOrderAppArgv[] = {cancelOrderAppArgv0, nullptr};

void ensureQtApp()
{
  if ( !QCoreApplication::instance() )
    new QCoreApplication( cancelOrderAppArgc(), cancelOrderAppArgv );
}

/// Completes immediately: the raced step and the poison-check step.
class FastOperator : public RSOperator
{
public:
  std::string name() const override { return "test:r19_02_fast"; }
  std::string displayName() const override { return "Fast"; }
  std::string group() const override { return "test"; }
  std::string description() const override { return "Returns at once."; }

  Json::Value schema() const override { return Json::Value( Json::objectValue ); }

  Json::Value run( const Json::Value &, RSOperatorContext & ) override
  {
    Json::Value result( Json::objectValue );
    result["result"] = "ok";
    return result;
  }
};

/// Loops until the cooperative cancel flag fires; signals entry first.
std::atomic<bool> g_slowEntered{ false };

class SlowPollOperator : public RSOperator
{
public:
  std::string name() const override { return "test:r19_02_slow_poll"; }
  std::string displayName() const override { return "SlowPoll"; }
  std::string group() const override { return "test"; }
  std::string description() const override { return "Loops until cancelled."; }

  Json::Value schema() const override { return Json::Value( Json::objectValue ); }
  Json::Value metadata() const override
  {
    Json::Value meta( Json::objectValue );
    meta["supportsCancellation"] = true;
    return meta;
  }

  Json::Value run( const Json::Value &, RSOperatorContext &context ) override
  {
    g_slowEntered.store( true );
    for ( int i = 0; i < 1'000'000; ++i )
    {
      context.throwIfCancelled();
      std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    Json::Value result( Json::objectValue );
    result["result"] = "finished-unexpectedly";
    return result;
  }
};

void registerTestOperators()
{
  auto &reg = RSOperatorRegistry::instance();
  if ( !reg.hasOperator( "test:r19_02_fast" ) )
    reg.registerOperator( "test:r19_02_fast",
                          []() -> std::unique_ptr<RSOperator> { return std::make_unique<FastOperator>(); } );
  if ( !reg.hasOperator( "test:r19_02_slow_poll" ) )
    reg.registerOperator( "test:r19_02_slow_poll",
                          []() -> std::unique_ptr<RSOperator> { return std::make_unique<SlowPollOperator>(); } );
}

WorkflowDefinition singleStepDefinition( const char *id, const char *operatorId )
{
  WorkflowDefinition def;
  def.id = id;
  def.title = id;
  StepDef step;
  step.id = "step";
  step.title = "Step";
  step.kind = StepKind::Operator;
  step.operatorId = operatorId;
  def.steps.push_back( step );
  return def;
}

} // namespace

TEST_CASE( "requestCancel that returned before runStep is consumed by the new step",
           "[workflow][cancel][r19_02]" )
{
  ensureQtApp();
  registerTestOperators();

  WorkflowRuntime runtime( /*loadBuiltins=*/false );
  runtime.registerDefinition( singleStepDefinition( "wf:r19_02_consume", "test:r19_02_fast" ) );
  const std::string sessionId = runtime.open( "wf:r19_02_consume" );
  REQUIRE_FALSE( sessionId.empty() );

  // The cancel fully completes BEFORE the step starts: it is aimed at an
  // earlier (here: no) step and must not cancel this one.
  REQUIRE_NOTHROW( runtime.requestCancel( sessionId ) );

  Json::Value result;
  REQUIRE_NOTHROW( result = runtime.runStep( sessionId, "step" ) );
  CHECK( result["result"].asString() == "ok" );

  runtime.close( sessionId );
}

TEST_CASE( "requestCancel after the operator entered cancels the in-flight step",
           "[workflow][cancel][r19_02]" )
{
  ensureQtApp();
  registerTestOperators();
  g_slowEntered.store( false );

  WorkflowRuntime runtime( /*loadBuiltins=*/false );
  runtime.registerDefinition( singleStepDefinition( "wf:r19_02_honor", "test:r19_02_slow_poll" ) );
  const std::string sessionId = runtime.open( "wf:r19_02_honor" );
  REQUIRE_FALSE( sessionId.empty() );

  std::string runError;
  bool threw = false;
  std::thread runner( [&]() {
    try
    {
      runtime.runStep( sessionId, "step" );
    }
    catch ( const std::exception &e )
    {
      threw = true;
      runError = e.what();
    }
  } );

  REQUIRE( sicnu_test::waitUntil( [] { return g_slowEntered.load(); }, 5000,
                                  [] { return "SlowPollOperator never entered its run loop"; } ) );
  runtime.requestCancel( sessionId );
  runner.join();

  REQUIRE( threw );
  CHECK( runError.find( "cancel" ) != std::string::npos );

  runtime.close( sessionId );
}

TEST_CASE( "concurrent requestCancel and runStep never hang, misreport, or poison a later step",
           "[workflow][cancel][r19_02][concurrency]" )
{
  ensureQtApp();
  registerTestOperators();

  WorkflowRuntime runtime( /*loadBuiltins=*/false );
  runtime.registerDefinition( singleStepDefinition( "wf:r19_02_race", "test:r19_02_fast" ) );

  constexpr int kIterations = 200;
  for ( int i = 0; i < kIterations; ++i )
  {
    const std::string sessionId = runtime.open( "wf:r19_02_race" );

    std::atomic<bool> runnerStarted{ false };
    std::string outcome = "none";
    std::thread runner( [&]() {
      runnerStarted.store( true );
      try
      {
        runtime.runStep( sessionId, "step" );
        outcome = "completed";
      }
      catch ( const std::exception &e )
      {
        // Only a cancellation error is legal here; anything else is a bug.
        const std::string msg = e.what();
        outcome = ( msg.find( "cancel" ) != std::string::npos ) ? "cancelled" : ( "unexpected:" + msg );
      }
    } );

    // Race the cancel against runStep's entry (its stale-flag consume).
    REQUIRE( sicnu_test::waitUntil( [&] { return runnerStarted.load(); }, 5000,
                                    [] { return "runner thread never started"; } ) );
    REQUIRE_NOTHROW( runtime.requestCancel( sessionId ) );
    runner.join();

    CHECK( ( outcome == "completed" || outcome == "cancelled" ) );

    // The cancel must not poison a LATER step of the same session: whatever
    // happened to the raced step, the next one runs uncancelled.
    Json::Value followUp;
    REQUIRE_NOTHROW( followUp = runtime.runStep( sessionId, "step" ) );
    CHECK( followUp["result"].asString() == "ok" );

    runtime.close( sessionId );
  }
}

TEST_CASE( "requestCancel on an unknown session is a no-op",
           "[workflow][cancel][r19_02]" )
{
  ensureQtApp();
  WorkflowRuntime runtime( /*loadBuiltins=*/false );
  REQUIRE_NOTHROW( runtime.requestCancel( "no-such-session" ) );
}
