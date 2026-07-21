#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace sicnu::jobs;
using namespace sicnu::operators;
using namespace sicnu::operators::schema;

namespace {

class TestAddOperator : public RSOperator
{
  public:
    std::string name() const override { return "test:add"; }
    std::string displayName() const override { return "Add Two Numbers"; }
    std::string group() const override { return "math"; }
    std::string description() const override { return "Adds two numbers."; }

    Json::Value schema() const override
    {
      Json::Value params( Json::objectValue );
      params["a"] = makeNumberParam( "a", "First summand", 0.0 );
      params["b"] = makeNumberParam( "b", "Second summand", 0.0 );

      Json::Value outputs( Json::objectValue );
      outputs["result"] = makeNumberParam( "result", "Sum" );

      Json::Value root = makeRootSchema( displayName(), description(), params, outputs );
      root["required"] = makeRequired( {"a", "b"} );
      return root;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
      if ( !params.isMember( "a" ) || !params.isMember( "b" ) )
      {
        throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                               "Parameters 'a' and 'b' are required" );
      }
      if ( !params["a"].isNumeric() || !params["b"].isNumeric() )
      {
        throw RSOperatorError( ErrorCode::TypeMismatch,
                               "Parameters 'a' and 'b' must be numeric" );
      }

      context.throwIfCancelled();
      context.logInfo( "Starting addition" );

      Json::Value result( Json::objectValue );
      result["result"] = params["a"].asDouble() + params["b"].asDouble();
      return result;
    }
};

std::atomic<int> g_sleepConcurrent{0};
std::atomic<int> g_sleepPeak{0};

class TestSleepOperator : public RSOperator
{
  public:
    std::string name() const override { return "test:sleep"; }
    std::string displayName() const override { return "Sleep"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Sleeps for ms milliseconds."; }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
      int ms = 100;
      if ( params.isMember( "ms" ) && params["ms"].isNumeric() )
        ms = params["ms"].asInt();
      if ( ms < 0 )
        ms = 0;

      struct ConcurrentGuard
      {
        ConcurrentGuard()
        {
          const int cur = g_sleepConcurrent.fetch_add( 1 ) + 1;
          int peak = g_sleepPeak.load();
          while ( cur > peak && !g_sleepPeak.compare_exchange_weak( peak, cur ) )
          {
          }
        }
        ~ConcurrentGuard() { g_sleepConcurrent.fetch_sub( 1 ); }
      } guard;

      constexpr int stepMs = 10;
      for ( int elapsed = 0; elapsed < ms; elapsed += stepMs )
      {
        context.throwIfCancelled();
        std::this_thread::sleep_for( std::chrono::milliseconds( stepMs ) );
      }
      context.throwIfCancelled();

      Json::Value result( Json::objectValue );
      result["sleptMs"] = ms;
      return result;
    }
};

void ensureTestOperatorsRegistered()
{
  auto &reg = RSOperatorRegistry::instance();
  if ( !reg.hasOperator( "test:add" ) )
  {
    reg.registerOperator( "test:add", []() -> std::unique_ptr<RSOperator> {
      return std::make_unique<TestAddOperator>();
    } );
  }
  if ( !reg.hasOperator( "test:sleep" ) )
  {
    reg.registerOperator( "test:sleep", []() -> std::unique_ptr<RSOperator> {
      return std::make_unique<TestSleepOperator>();
    } );
  }
}

struct EngineGuard
{
  EngineGuard()
  {
    ensureTestOperatorsRegistered();
    auto &eng = JobEngine::instance();
    eng.setListener( nullptr );
    eng.setMaxWorkers( 2 );
  }

  ~EngineGuard()
  {
    auto &eng = JobEngine::instance();
    eng.waitUntilIdleForTests( 15000 );
    eng.setListener( nullptr );
  }

  JobEngine &engine() { return JobEngine::instance(); }
};

} // namespace

TEST_CASE( "job types compile", "[job]" )
{
  JobRequest r;
  r.algorithmId = "test:noop";
  REQUIRE( r.algorithmId == "test:noop" );
}

TEST_CASE( "submit runs operator and succeeds", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  JobRequest req;
  req.algorithmId = "test:add";
  req.params["a"] = 2;
  req.params["b"] = 3;
  req.title = "add";
  req.source = "test";

  const auto id = eng.submit( req );
  REQUIRE_FALSE( id.empty() );

  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
  REQUIRE( snap->result.isMember( "result" ) );
  REQUIRE( snap->result["result"].asDouble() == 5.0 );
}

TEST_CASE( "max workers limits concurrency", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.setMaxWorkers( 2 );
  REQUIRE( eng.maxWorkers() == 2 );

  g_sleepConcurrent.store( 0 );
  g_sleepPeak.store( 0 );

  std::vector<std::string> ids;
  for ( int i = 0; i < 4; ++i )
  {
    JobRequest req;
    req.algorithmId = "test:sleep";
    req.params["ms"] = 200;
    req.title = "sleep-" + std::to_string( i );
    req.source = "test";
    ids.push_back( eng.submit( req ) );
  }

  eng.waitUntilIdleForTests();

  for ( const auto &id : ids )
  {
    auto snap = eng.snapshot( id );
    REQUIRE( snap.has_value() );
    REQUIRE( snap->state == JobState::Succeeded );
  }

  REQUIRE( g_sleepPeak.load() <= 2 );
  REQUIRE( g_sleepPeak.load() >= 1 );
}

TEST_CASE( "cancel cooperative", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.setMaxWorkers( 2 );

  JobRequest req;
  req.algorithmId = "test:sleep";
  req.params["ms"] = 5000;
  req.title = "long-sleep";
  req.source = "test";

  const auto id = eng.submit( req );

  bool becameRunning = false;
  for ( int i = 0; i < 200; ++i )
  {
    auto snap = eng.snapshot( id );
    if ( snap && snap->state == JobState::Running )
    {
      becameRunning = true;
      break;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  REQUIRE( becameRunning );

  REQUIRE( eng.cancel( id ) );
  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );
}

TEST_CASE( "failed operator sets error", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  JobRequest req;
  req.algorithmId = "test:add";
  // missing a/b → MissingRequiredParameter
  req.params = Json::Value( Json::objectValue );
  req.title = "add-fail";
  req.source = "test";

  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Failed );
  REQUIRE_FALSE( snap->error.empty() );
}

TEST_CASE( "setMaxWorkers clamps to 2..4", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  eng.setMaxWorkers( 1 );
  REQUIRE( eng.maxWorkers() == 2 );
  eng.setMaxWorkers( 99 );
  REQUIRE( eng.maxWorkers() == 4 );
  eng.setMaxWorkers( 3 );
  REQUIRE( eng.maxWorkers() == 3 );
}
