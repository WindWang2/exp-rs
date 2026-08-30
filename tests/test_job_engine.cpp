#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
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
    eng.clearExecutors();
    eng.setMaxWorkers( 2 );
  }

  ~EngineGuard()
  {
    auto &eng = JobEngine::instance();
    eng.waitUntilIdleForTests( 15000 );
    eng.setListener( nullptr );
    eng.clearExecutors();
    eng.setFallbackExecutor( {} );
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

TEST_CASE( "setMaxWorkers honors overrides with a safe floor (#661)", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  eng.setMaxWorkers( 1 );
  REQUIRE( eng.maxWorkers() == 1 );
  eng.setMaxWorkers( 0 );
  REQUIRE( eng.maxWorkers() == 1 );
  eng.setMaxWorkers( 99 );
  REQUIRE( eng.maxWorkers() == 99 );
  eng.setMaxWorkers( 3 );
  REQUIRE( eng.maxWorkers() == 3 );
}

TEST_CASE( "registerExecutor prefix runs without RSOperator", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  eng.registerExecutor( "mock:", []( const JobRequest &req, RSOperatorContext &ctx ) {
    ctx.logInfo( "mock-exec" );
    Json::Value r( Json::objectValue );
    r["echo"] = req.params.get( "x", 0 ).asInt();
    r["output"] = "/tmp/mock.out";
    return r;
  } );

  JobRequest req;
  req.algorithmId = "mock:thing";
  req.params["x"] = 42;
  req.title = "mock";
  req.source = "test";

  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
  REQUIRE( snap->result["echo"].asInt() == 42 );
  REQUIRE( snap->result["output"].asString() == "/tmp/mock.out" );
}

TEST_CASE( "submit callable one-shot executor", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  std::atomic<bool> cancelCalled{false};

  JobRequest req;
  req.algorithmId = "callable:gdal";
  req.title = "lambda";
  req.source = "test";

  const auto id = eng.submit(
    std::move( req ),
    []( const JobRequest &, RSOperatorContext &ctx ) {
      ctx.reportProgress( 0.5, "halfway" );
      Json::Value r( Json::objectValue );
      r["output"] = "/tmp/callable.tif";
      return r;
    },
    [&cancelCalled]() { cancelCalled.store( true ); } );

  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
  REQUIRE( snap->result["output"].asString() == "/tmp/callable.tif" );
  REQUIRE_FALSE( cancelCalled.load() );
}

TEST_CASE( "callable cancel hook fires while running", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.setMaxWorkers( 2 );

  std::atomic<bool> cancelHookFired{false};
  std::atomic<bool> started{false};

  JobRequest req;
  req.algorithmId = "callable:long";
  req.title = "long";
  req.source = "test";

  const auto id = eng.submit(
    std::move( req ),
    [&started]( const JobRequest &, RSOperatorContext &ctx ) {
      started.store( true );
      for ( int i = 0; i < 500; ++i )
      {
        ctx.throwIfCancelled();
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
      }
      Json::Value r( Json::objectValue );
      r["ok"] = true;
      return r;
    },
    [&cancelHookFired]() { cancelHookFired.store( true ); } );

  for ( int i = 0; i < 200 && !started.load(); ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  REQUIRE( started.load() );

  REQUIRE( eng.cancel( id ) );
  eng.waitUntilIdleForTests();

  REQUIRE( cancelHookFired.load() );
  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );
}

TEST_CASE( "pruneCompleted keeps the newest terminal records", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  // Isolate from records left by earlier cases (singleton engine).
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  // Staggered sleeps make the finish order == submission order with distinct
  // finishedAtMs, so "oldest" (by finishedAtMs) is unambiguous.
  std::vector<std::string> ids;
  for ( int i = 0; i < 5; ++i )
  {
    JobRequest req;
    req.algorithmId = "test:sleep";
    req.params["ms"] = ( i + 1 ) * 40;
    req.title = "prune-" + std::to_string( i );
    req.source = "test";
    ids.push_back( eng.submit( req ) );
  }
  eng.waitUntilIdleForTests();

  const auto before = eng.list();
  REQUIRE( before.size() == 5 );
  for ( const auto &rec : before )
    REQUIRE( rec.state == JobState::Succeeded );

  const auto removed = eng.pruneCompleted( 2 );
  REQUIRE( removed == 3 );

  // Only the two newest (last-submitted) terminal records remain.
  auto remaining = eng.list();
  REQUIRE( remaining.size() == 2 );
  for ( const auto &rec : remaining )
  {
    REQUIRE( rec.state == JobState::Succeeded );
    REQUIRE( ( rec.id == ids[3] || rec.id == ids[4] ) );
  }

  // Pruned jobIds are unknown to snapshot().
  for ( int i = 0; i < 3; ++i )
    REQUIRE_FALSE( eng.snapshot( ids[i] ).has_value() );
  REQUIRE( eng.snapshot( ids[3] ).has_value() );
  REQUIRE( eng.snapshot( ids[4] ).has_value() );

  // clearCompleted() == pruneCompleted(0): removes everything terminal.
  eng.clearCompleted();
  REQUIRE( eng.list().empty() );
}

TEST_CASE( "removeCompleted drops exactly the requested terminal records", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  // Isolate from records left by earlier cases (singleton engine).
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::vector<std::string> ids;
  for ( int i = 0; i < 3; ++i )
  {
    JobRequest req;
    req.algorithmId = "test:add";
    req.params["a"] = i;
    req.params["b"] = 1;
    req.title = "remove-" + std::to_string( i );
    req.source = "test";
    ids.push_back( eng.submit( req ) );
  }
  eng.waitUntilIdleForTests();
  REQUIRE( eng.list().size() == 3 );

  // Unknown ids are ignored; only the exact terminal record is removed.
  REQUIRE( eng.removeCompleted( {ids[1], "job-does-not-exist"} ) == 1 );
  REQUIRE_FALSE( eng.snapshot( ids[1] ).has_value() );
  REQUIRE( eng.snapshot( ids[0] ).has_value() );
  REQUIRE( eng.snapshot( ids[2] ).has_value() );
  REQUIRE( eng.list().size() == 2 );
}

TEST_CASE( "prune never touches queued or running records", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  // Isolate from records left by earlier cases (singleton engine).
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  // One completed job first, so there is a terminal record to prune.
  JobRequest quick;
  quick.algorithmId = "test:add";
  quick.params["a"] = 1;
  quick.params["b"] = 1;
  quick.title = "quick";
  quick.source = "test";
  const auto quickId = eng.submit( quick );
  eng.waitUntilIdleForTests();
  {
    auto snap = eng.snapshot( quickId );
    REQUIRE( snap.has_value() );
    REQUIRE( snap->state == JobState::Succeeded );
  }

  // Two blocking jobs occupy both workers; the next submission stays queued.
  // Released on scope exit so a failed assertion cannot strand the workers.
  std::atomic<bool> release{false};
  struct ReleaseOnExit
  {
    std::atomic<bool> &flag;
    ~ReleaseOnExit() { flag.store( true ); }
  } releaseGuard{release};
  auto blocking = [&release]( const JobRequest &, RSOperatorContext & ) {
    while ( !release.load() )
      std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    Json::Value r( Json::objectValue );
    r["ok"] = true;
    return r;
  };

  JobRequest blockReq;
  blockReq.algorithmId = "callable:prune-block";
  blockReq.title = "block";
  blockReq.source = "test";
  const auto blockA = eng.submit( blockReq, blocking );
  const auto blockB = eng.submit( blockReq, blocking );

  for ( int i = 0; i < 200; ++i )
  {
    const auto sa = eng.snapshot( blockA );
    const auto sb = eng.snapshot( blockB );
    if ( sa && sb && sa->state == JobState::Running && sb->state == JobState::Running )
      break;
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }

  JobRequest queuedReq;
  queuedReq.algorithmId = "test:add";
  queuedReq.params["a"] = 2;
  queuedReq.params["b"] = 2;
  queuedReq.title = "queued";
  queuedReq.source = "test";
  const auto queuedId = eng.submit( queuedReq );

  {
    auto sa = eng.snapshot( blockA );
    auto sb = eng.snapshot( blockB );
    auto sq = eng.snapshot( queuedId );
    REQUIRE( sa.has_value() );
    REQUIRE( sa->state == JobState::Running );
    REQUIRE( sb.has_value() );
    REQUIRE( sb->state == JobState::Running );
    REQUIRE( sq.has_value() );
    REQUIRE( sq->state == JobState::Queued );
  }

  // pruneCompleted(0) removes ALL terminal records — only `quick` qualifies;
  // the running and queued records must survive untouched.
  REQUIRE( eng.pruneCompleted( 0 ) == 1 );
  REQUIRE_FALSE( eng.snapshot( quickId ).has_value() );
  {
    auto sa = eng.snapshot( blockA );
    auto sb = eng.snapshot( blockB );
    auto sq = eng.snapshot( queuedId );
    REQUIRE( sa.has_value() );
    REQUIRE( sa->state == JobState::Running );
    REQUIRE( sb.has_value() );
    REQUIRE( sb->state == JobState::Running );
    REQUIRE( sq.has_value() );
    REQUIRE( sq->state == JobState::Queued );
  }

  release.store( true );
  eng.waitUntilIdleForTests();
  {
    auto sa = eng.snapshot( blockA );
    auto sb = eng.snapshot( blockB );
    auto sq = eng.snapshot( queuedId );
    REQUIRE( sa.has_value() );
    REQUIRE( sa->state == JobState::Succeeded );
    REQUIRE( sb.has_value() );
    REQUIRE( sb->state == JobState::Succeeded );
    REQUIRE( sq.has_value() );
    REQUIRE( sq->state == JobState::Succeeded );
  }
}

// ---------------------------------------------------------------------------
// ADR 0062 — registry fallback seam (provider algorithms become executable).
// ---------------------------------------------------------------------------
namespace {

/// Minimal AtomicAlgorithmAdapter for exercising the fallback seam without a
/// real QgsProcessingAlgorithm. Records the params it ran with and whether the
/// progress callback fired.
class StubNonRsAdapter : public sicnu::processing::AtomicAlgorithmAdapter
{
  public:
    explicit StubNonRsAdapter( std::string id ) : mId( std::move( id ) ) {}

    std::string algorithmId() const override { return mId; }
    sicnu::processing::AlgorithmDescriptor descriptor() const override
    {
      sicnu::processing::AlgorithmDescriptor d;
      d.id = mId;
      d.displayName = mId;
      return d;
    }

    Json::Value execute( const Json::Value &params,
                         sicnu::processing::ProgressCallback progressCb = nullptr,
                         std::function<bool()> = nullptr ) override
    {
      mLastParams = params;
      if ( progressCb )
      {
        progressCb( 50, "halfway" );
        progressCb( 100, "done" );
      }
      Json::Value r( Json::objectValue );
      r["echo"] = params.get( "x", 0 ).asInt();
      r["output"] = "/tmp/stub_adapter.out";
      return r;
    }

    const Json::Value &lastParams() const { return mLastParams; }

  private:
    std::string mId;
    Json::Value mLastParams;
};

/// Mirrors the production injection in main.cpp: resolve an algorithm id via
/// the AtomicAlgorithmRegistry and bridge progress back to the job context.
void installRegistryFallback()
{
  sicnu::jobs::JobEngine::instance().setFallbackExecutor(
    []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) {
      const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
      if ( !adapter )
        throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
      sicnu::processing::ProgressCallback bridge = [&ctx]( int percent, const std::string &message ) {
        ctx.reportProgress( percent / 100.0, message );
      };
      return adapter->execute( req.params, bridge );
    } );
}

} // namespace

TEST_CASE( "fallback executor runs registry adapter when RSOperator misses", "[job][fallback]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  const std::string algoId = "test:stub_adapter_" + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() );
  auto stub = std::make_shared<StubNonRsAdapter>( algoId );
  sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter( stub );
  installRegistryFallback();

  JobRequest req;
  req.algorithmId = algoId;
  req.params["x"] = 7;
  req.title = "stub";
  req.source = "test";

  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
  REQUIRE( snap->result["echo"].asInt() == 7 );
  REQUIRE( snap->result["output"].asString() == "/tmp/stub_adapter.out" );
  // Progress bridge wired QgsFeedback % into the job record.
  REQUIRE( snap->progress == 1.0 );

  sicnu::processing::AtomicAlgorithmRegistry::instance().unregisterAdapter( algoId );
}

TEST_CASE( "unknown algorithm still fails when fallback also misses", "[job][fallback]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  installRegistryFallback();

  JobRequest req;
  req.algorithmId = "test:definitely_nonexistent_algorithm";
  req.title = "miss";
  req.source = "test";

  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Failed );
  REQUIRE( snap->error.find( "Unknown algorithm" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// Concurrency, Cancellation & Shutdown Lifecycle Hardening Test Suite
// ---------------------------------------------------------------------------

TEST_CASE( "queued job cancelled before worker pickup", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::promise<void> startA, startB;
  std::promise<void> unblock;
  auto sharedUnblock = unblock.get_future().share();

  auto blocker = [sharedUnblock]( std::promise<void> &started ) {
    return [sharedUnblock, &started]( const JobRequest &, RSOperatorContext & ) {
      started.set_value();
      sharedUnblock.wait();
      Json::Value r( Json::objectValue );
      r["ok"] = true;
      return r;
    };
  };

  JobRequest reqBlock;
  reqBlock.algorithmId = "callable:block";
  const auto idA = eng.submit( reqBlock, blocker( startA ) );
  const auto idB = eng.submit( reqBlock, blocker( startB ) );

  startA.get_future().wait();
  startB.get_future().wait();

  // Workers saturated (m_running == 2). Submit a queued job.
  std::atomic<bool> queuedExecuted{false};
  JobRequest reqQueued;
  reqQueued.algorithmId = "callable:queued";
  const auto idQueued = eng.submit(
    reqQueued,
    [&queuedExecuted]( const JobRequest &, RSOperatorContext & ) {
      queuedExecuted.store( true );
      Json::Value r( Json::objectValue );
      return r;
    } );

  // Cancel it while still queued
  auto snapQueuedBefore = eng.snapshot( idQueued );
  REQUIRE( snapQueuedBefore.has_value() );
  REQUIRE( snapQueuedBefore->state == JobState::Queued );

  REQUIRE( eng.cancel( idQueued ) );

  auto snapQueuedAfter = eng.snapshot( idQueued );
  REQUIRE( snapQueuedAfter.has_value() );
  REQUIRE( snapQueuedAfter->state == JobState::Cancelled );

  // Unblock workers and let engine idle
  unblock.set_value();
  eng.waitUntilIdleForTests();

  REQUIRE_FALSE( queuedExecuted.load() );
  auto snapFinal = eng.snapshot( idQueued );
  REQUIRE( snapFinal.has_value() );
  REQUIRE( snapFinal->state == JobState::Cancelled );
}

TEST_CASE( "repeated cancel on running and completed jobs", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::atomic<int> cancelHookCount{0};
  std::promise<void> started;
  std::promise<void> proceed;
  auto sharedProceed = proceed.get_future().share();

  JobRequest req;
  req.algorithmId = "callable:repeat_cancel";
  const auto id = eng.submit(
    req,
    [&started, sharedProceed]( const JobRequest &, RSOperatorContext &ctx ) {
      started.set_value();
      sharedProceed.wait();
      ctx.throwIfCancelled();
      Json::Value r( Json::objectValue );
      return r;
    },
    [&cancelHookCount]() { cancelHookCount.fetch_add( 1 ); } );

  started.get_future().wait();

  // Cancel running job 3 times in a row
  REQUIRE( eng.cancel( id ) );
  REQUIRE( eng.cancel( id ) );
  REQUIRE( eng.cancel( id ) );

  // Cancel hook must be consumed and fire exactly ONCE
  REQUIRE( cancelHookCount.load() == 1 );

  proceed.set_value();
  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );

  // Cancelling a completed (terminal) job must return false
  REQUIRE_FALSE( eng.cancel( id ) );
  REQUIRE( cancelHookCount.load() == 1 );
}

TEST_CASE( "cancel hook reentrancy does not deadlock", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::promise<void> started;
  std::promise<void> proceed;
  auto sharedProceed = proceed.get_future().share();

  std::atomic<bool> hookCompleted{false};
  std::string id;

  JobRequest req;
  req.algorithmId = "callable:reentrant_hook";
  id = eng.submit(
    req,
    [&started, sharedProceed]( const JobRequest &, RSOperatorContext &ctx ) {
      started.set_value();
      sharedProceed.wait();
      ctx.throwIfCancelled();
      Json::Value r( Json::objectValue );
      return r;
    },
    [&eng, &id, &hookCompleted]() {
      // Re-enter JobEngine from cancel hook
      auto snap = eng.snapshot( id );
      auto all = eng.list();
      eng.cancel( "non-existent-job" );
      hookCompleted.store( true );
    } );

  started.get_future().wait();

  REQUIRE( eng.cancel( id ) );
  REQUIRE( hookCompleted.load() );

  proceed.set_value();
  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );
}

TEST_CASE( "racing completion vs cancel terminates cleanly", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 4 );

  for ( int round = 0; round < 20; ++round )
  {
    std::atomic<bool> startRace{false};
    JobRequest req;
    req.algorithmId = "callable:race";
    const auto id = eng.submit(
      req,
      [&startRace]( const JobRequest &, RSOperatorContext & ) {
        while ( !startRace.load() )
          std::this_thread::yield();
        Json::Value r( Json::objectValue );
        r["done"] = true;
        return r;
      } );

    std::thread cancelThread( [&eng, id, &startRace]() {
      startRace.store( true );
      eng.cancel( id );
    } );

    cancelThread.join();
    eng.waitUntilIdleForTests();

    auto snap = eng.snapshot( id );
    REQUIRE( snap.has_value() );
    REQUIRE( ( snap->state == JobState::Succeeded || snap->state == JobState::Cancelled ) );
  }
}

TEST_CASE( "executor throws exception during cancellation marks Cancelled", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::promise<void> started;
  std::promise<void> proceed;
  auto sharedProceed = proceed.get_future().share();

  JobRequest req;
  req.algorithmId = "callable:throw_on_cancel";
  const auto id = eng.submit(
    req,
    [&started, sharedProceed]( const JobRequest &, RSOperatorContext &ctx ) {
      started.set_value();
      sharedProceed.wait();
      if ( ctx.isCancelled() )
        throw std::runtime_error( "Task aborted due to cancellation" );
      Json::Value r( Json::objectValue );
      return r;
    } );

  started.get_future().wait();
  REQUIRE( eng.cancel( id ) );
  proceed.set_value();

  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );
}

TEST_CASE( "executor throws non-std exception handled safely", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  JobRequest req;
  req.algorithmId = "callable:throw_int";
  const auto id = eng.submit(
    req,
    []( const JobRequest &, RSOperatorContext & ) -> Json::Value {
      throw 42; // Non-std exception
    } );

  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Failed );
  REQUIRE_FALSE( snap->error.empty() );

  // Engine continues working properly for subsequent jobs
  JobRequest req2;
  req2.algorithmId = "callable:after_int";
  const auto id2 = eng.submit(
    req2,
    []( const JobRequest &, RSOperatorContext & ) {
      Json::Value r( Json::objectValue );
      r["ok"] = true;
      return r;
    } );

  eng.waitUntilIdleForTests();
  auto snap2 = eng.snapshot( id2 );
  REQUIRE( snap2.has_value() );
  REQUIRE( snap2->state == JobState::Succeeded );
}

TEST_CASE( "cancel hook throwing exception does not crash engine", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::promise<void> started;
  std::promise<void> proceed;
  auto sharedProceed = proceed.get_future().share();

  JobRequest req;
  req.algorithmId = "callable:bad_hook";
  const auto id = eng.submit(
    req,
    [&started, sharedProceed]( const JobRequest &, RSOperatorContext &ctx ) {
      started.set_value();
      sharedProceed.wait();
      ctx.throwIfCancelled();
      Json::Value r( Json::objectValue );
      return r;
    },
    []() {
      throw std::runtime_error( "Evil cancel hook" );
    } );

  started.get_future().wait();
  REQUIRE_NOTHROW( eng.cancel( id ) );
  proceed.set_value();

  eng.waitUntilIdleForTests();
  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );
}

TEST_CASE( "listener throwing exception does not cause double finish", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  eng.setListener( []( const JobRecord & ) {
    throw std::runtime_error( "Failing listener" );
  } );

  JobRequest req;
  req.algorithmId = "callable:listener_throw";
  const auto id = eng.submit(
    req,
    []( const JobRequest &, RSOperatorContext & ) {
      Json::Value r( Json::objectValue );
      r["ok"] = true;
      return r;
    } );

  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
}

TEST_CASE( "listener reentrancy does not deadlock", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::atomic<int> notifications{0};
  eng.setListener( [&eng, &notifications]( const JobRecord &rec ) {
    notifications.fetch_add( 1 );
    auto snap = eng.snapshot( rec.id );
    auto list = eng.list();
  } );

  JobRequest req;
  req.algorithmId = "callable:listener_reentrant";
  const auto id = eng.submit(
    req,
    []( const JobRequest &, RSOperatorContext &ctx ) {
      ctx.reportProgress( 0.5, "midway" );
      Json::Value r( Json::objectValue );
      r["ok"] = true;
      return r;
    } );

  eng.waitUntilIdleForTests();

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
  REQUIRE( notifications.load() >= 3 ); // Queued, Started, Progress, Succeeded
}

TEST_CASE( "shutdown cancels unpicked queued jobs cleanly", "[job][lifecycle]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::promise<void> startA, startB;
  std::promise<void> proceed;
  auto sharedProceed = proceed.get_future().share();

  auto blocker = [sharedProceed]( std::promise<void> &started ) {
    return [sharedProceed, &started]( const JobRequest &, RSOperatorContext & ) {
      started.set_value();
      sharedProceed.wait();
      Json::Value r( Json::objectValue );
      return r;
    };
  };

  JobRequest reqBlock;
  reqBlock.algorithmId = "callable:block";
  const auto idA = eng.submit( reqBlock, blocker( startA ) );
  const auto idB = eng.submit( reqBlock, blocker( startB ) );

  startA.get_future().wait();
  startB.get_future().wait();

  // Submit queued jobs
  std::vector<std::string> queuedIds;
  for ( int i = 0; i < 3; ++i )
  {
    JobRequest q;
    q.algorithmId = "callable:queued";
    queuedIds.push_back( eng.submit( q, []( const JobRequest &, RSOperatorContext & ) {
      return Json::Value( Json::objectValue );
    } ) );
  }

  // Call shutdown while blockers are still held — this guarantees the
  // queue is drained (queued→Cancelled) before any worker can pick them.
  // Release blockers from a background thread so running jobs finish and
  // the join inside shutdown() can complete.
  std::thread releaser( [&proceed]() {
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    proceed.set_value();
  } );
  eng.shutdown();
  releaser.join();

  for ( const auto &qid : queuedIds )
  {
    auto snap = eng.snapshot( qid );
    REQUIRE( snap.has_value() );
    REQUIRE( snap->state == JobState::Cancelled );
  }
}

TEST_CASE( "restart engine after shutdownForTests works seamlessly", "[job][lifecycle]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();

  for ( int cycle = 0; cycle < 3; ++cycle )
  {
    eng.shutdownForTests();
    eng.setMaxWorkers( 2 );

    JobRequest req;
    req.algorithmId = "callable:cycle";
    req.params["c"] = cycle;
    const auto id = eng.submit(
      req,
      []( const JobRequest &r, RSOperatorContext & ) {
        Json::Value res( Json::objectValue );
        res["c"] = r.params["c"];
        return res;
      } );

    eng.waitUntilIdleForTests();

    auto snap = eng.snapshot( id );
    REQUIRE( snap.has_value() );
    REQUIRE( snap->state == JobState::Succeeded );
    REQUIRE( snap->result["c"].asInt() == cycle );
  }
}

TEST_CASE( "exclusive job drains running work and runs alone", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 3 );

  std::atomic<int> concurrentRunning{0};
  std::atomic<int> exclusivePeakConcurrent{0};
  std::atomic<bool> exclusiveRanAlone{true};

  std::promise<void> normal1Started;
  std::promise<void> normal1Proceed;
  auto sharedNormal1Proceed = normal1Proceed.get_future().share();

  JobRequest normal1;
  normal1.algorithmId = "callable:normal1";
  normal1.exclusive = false;
  const auto id1 = eng.submit(
    normal1,
    [&normal1Started, sharedNormal1Proceed, &concurrentRunning]( const JobRequest &, RSOperatorContext & ) {
      concurrentRunning.fetch_add( 1 );
      normal1Started.set_value();
      sharedNormal1Proceed.wait();
      concurrentRunning.fetch_sub( 1 );
      return Json::Value( Json::objectValue );
    } );

  normal1Started.get_future().wait();

  // Normal 1 is running. Submit exclusive job and normal 2.
  JobRequest exclusiveReq;
  exclusiveReq.algorithmId = "callable:exclusive";
  exclusiveReq.exclusive = true;
  const auto idEx = eng.submit(
    exclusiveReq,
    [&concurrentRunning, &exclusivePeakConcurrent, &exclusiveRanAlone]( const JobRequest &, RSOperatorContext & ) {
      const int cur = concurrentRunning.fetch_add( 1 ) + 1;
      if ( cur > 1 )
        exclusiveRanAlone.store( false );
      int peak = exclusivePeakConcurrent.load();
      while ( cur > peak && !exclusivePeakConcurrent.compare_exchange_weak( peak, cur ) ) {}
      std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
      concurrentRunning.fetch_sub( 1 );
      return Json::Value( Json::objectValue );
    } );

  JobRequest normal2;
  normal2.algorithmId = "callable:normal2";
  normal2.exclusive = false;
  const auto id2 = eng.submit(
    normal2,
    [&concurrentRunning]( const JobRequest &, RSOperatorContext & ) {
      concurrentRunning.fetch_add( 1 );
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
      concurrentRunning.fetch_sub( 1 );
      return Json::Value( Json::objectValue );
    } );

  // Normal 1 finishes, allowing exclusive to pick up, then normal 2
  normal1Proceed.set_value();
  eng.waitUntilIdleForTests();

  auto s1 = eng.snapshot( id1 );
  auto sEx = eng.snapshot( idEx );
  auto s2 = eng.snapshot( id2 );

  REQUIRE( s1.has_value() );
  REQUIRE( s1->state == JobState::Succeeded );
  REQUIRE( sEx.has_value() );
  REQUIRE( sEx->state == JobState::Succeeded );
  REQUIRE( s2.has_value() );
  REQUIRE( s2->state == JobState::Succeeded );

  REQUIRE( exclusiveRanAlone.load() );
  REQUIRE( exclusivePeakConcurrent.load() == 1 );
}

TEST_CASE( "exclusive job cancelled while queued unblocks subsequent jobs", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::promise<void> startA;
  std::promise<void> proceed;
  auto sharedProceed = proceed.get_future().share();

  JobRequest reqBlock;
  reqBlock.algorithmId = "callable:block";
  const auto idBlock = eng.submit(
    reqBlock,
    [&startA, sharedProceed]( const JobRequest &, RSOperatorContext & ) {
      startA.set_value();
      sharedProceed.wait();
      return Json::Value( Json::objectValue );
    } );

  startA.get_future().wait();

  // Exclusive job queued
  JobRequest reqEx;
  reqEx.algorithmId = "callable:ex";
  reqEx.exclusive = true;
  const auto idEx = eng.submit( reqEx, []( const JobRequest &, RSOperatorContext & ) {
    return Json::Value( Json::objectValue );
  } );

  // Normal job queued behind exclusive job
  std::atomic<bool> normalRan{false};
  JobRequest reqNormal;
  reqNormal.algorithmId = "callable:normal";
  const auto idNormal = eng.submit( reqNormal, [&normalRan]( const JobRequest &, RSOperatorContext & ) {
    normalRan.store( true );
    return Json::Value( Json::objectValue );
  } );

  // Cancel the queued exclusive job
  REQUIRE( eng.cancel( idEx ) );
  auto snapEx = eng.snapshot( idEx );
  REQUIRE( snapEx.has_value() );
  REQUIRE( snapEx->state == JobState::Cancelled );

  // Normal job behind it must not be stalled!
  proceed.set_value();
  eng.waitUntilIdleForTests();

  REQUIRE( normalRan.load() );
  auto snapNormal = eng.snapshot( idNormal );
  REQUIRE( snapNormal.has_value() );
  REQUIRE( snapNormal->state == JobState::Succeeded );
}

TEST_CASE( "rapid concurrent submit and cancel stress loop", "[job][stress]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 4 );

  constexpr int threadCount = 4;
  constexpr int iterationsPerThread = 25;
  std::vector<std::thread> threads;
  std::vector<std::string> allIds;
  std::mutex idMutex;

  for ( int t = 0; t < threadCount; ++t )
  {
    threads.emplace_back( [&eng, &allIds, &idMutex, t]() {
      for ( int i = 0; i < iterationsPerThread; ++i )
      {
        JobRequest req;
        req.algorithmId = "callable:stress";
        req.exclusive = ( ( t + i ) % 5 == 0 );

        auto id = eng.submit(
          req,
          []( const JobRequest &, RSOperatorContext &ctx ) {
            for ( int step = 0; step < 10; ++step )
            {
              ctx.throwIfCancelled();
              std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );
            }
            return Json::Value( Json::objectValue );
          } );

        {
          std::lock_guard<std::mutex> lk( idMutex );
          allIds.push_back( id );
        }

        if ( i % 2 == 0 )
          eng.cancel( id );
      }
    } );
  }

  for ( auto &t : threads )
    t.join();

  eng.waitUntilIdleForTests( 20000 );

  for ( const auto &id : allIds )
  {
    auto snap = eng.snapshot( id );
    REQUIRE( snap.has_value() );
    REQUIRE( ( snap->state == JobState::Succeeded || snap->state == JobState::Cancelled ) );
  }
}

TEST_CASE( "dynamic maxWorkers adjustment under load", "[job][concurrency]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.shutdownForTests();
  eng.setMaxWorkers( 2 );

  std::atomic<bool> release{false};
  std::vector<std::string> ids;

  for ( int i = 0; i < 8; ++i )
  {
    JobRequest req;
    req.algorithmId = "callable:dyn_workers";
    ids.push_back( eng.submit(
      req,
      [&release]( const JobRequest &, RSOperatorContext &ctx ) {
        while ( !release.load() )
        {
          ctx.throwIfCancelled();
          std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return Json::Value( Json::objectValue );
      } ) );
  }

  // Adjust workers while jobs are queued and running
  eng.setMaxWorkers( 4 );
  REQUIRE( eng.maxWorkers() == 4 );

  eng.setMaxWorkers( 2 );
  REQUIRE( eng.maxWorkers() == 2 );

  release.store( true );
  eng.waitUntilIdleForTests();

  for ( const auto &id : ids )
  {
    auto snap = eng.snapshot( id );
    REQUIRE( snap.has_value() );
    REQUIRE( snap->state == JobState::Succeeded );
  }
}

// ---------------------------------------------------------------------------
// #661/#686: worker pool defaults to the throttler cap via an injectable
// ceiling, explicit overrides are honored with a safe floor, and the engine
// queue picks best-priority-first.
// ---------------------------------------------------------------------------
TEST_CASE( "default worker count follows the injected concurrency ceiling (#661)", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  JobEngine::setConcurrencyCeilingForTests( 8 );
  eng.shutdownForTests(); // re-derives the default pool size from the ceiling
  REQUIRE( eng.maxWorkers() == 7 );

  JobEngine::setConcurrencyCeilingForTests( 2 );
  eng.shutdownForTests();
  REQUIRE( eng.maxWorkers() == 1 );

  JobEngine::setConcurrencyCeilingForTests( 1 );
  eng.shutdownForTests();
  REQUIRE( eng.maxWorkers() == 1 ); // safe floor on a 1-core machine

  JobEngine::setConcurrencyCeilingForTests( 0 ); // restore host-derived cap
  eng.shutdownForTests();
}

TEST_CASE( "setMaxWorkers honors overrides and clamps misconfiguration to a safe floor (#661)", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  eng.setMaxWorkers( 0 );
  REQUIRE( eng.maxWorkers() == 1 );
  eng.setMaxWorkers( -3 );
  REQUIRE( eng.maxWorkers() == 1 );
  eng.setMaxWorkers( 9 );
  REQUIRE( eng.maxWorkers() == 9 ); // no artificial 2..4 clamp
  eng.setMaxWorkers( 2 );
  REQUIRE( eng.maxWorkers() == 2 );
}

TEST_CASE( "engine queue picks best-priority-first, stable on ties (#686)", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();
  eng.setMaxWorkers( 1 );

  std::atomic<bool> releaseBlocker{ false };
  std::atomic<bool> blockerRunning{ false };
  std::vector<std::string> launchOrder;
  std::mutex orderMutex;

  eng.registerExecutor( "prio:blocker", [&]( const JobRequest &, RSOperatorContext & ) {
    blockerRunning.store( true );
    while ( !releaseBlocker.load() )
      std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    std::lock_guard<std::mutex> lock( orderMutex );
    launchOrder.push_back( "blocker" );
    return Json::Value( Json::objectValue );
  } );
  eng.registerExecutor( "prio:low", [&]( const JobRequest &, RSOperatorContext & ) {
    std::lock_guard<std::mutex> lock( orderMutex );
    launchOrder.push_back( "low" );
    return Json::Value( Json::objectValue );
  } );
  eng.registerExecutor( "prio:high", [&]( const JobRequest &, RSOperatorContext & ) {
    std::lock_guard<std::mutex> lock( orderMutex );
    launchOrder.push_back( "high" );
    return Json::Value( Json::objectValue );
  } );
  eng.registerExecutor( "prio:mid", [&]( const JobRequest &, RSOperatorContext & ) {
    std::lock_guard<std::mutex> lock( orderMutex );
    launchOrder.push_back( "mid" );
    return Json::Value( Json::objectValue );
  } );

  const auto blocker = eng.submit( JobRequest{ "prio:blocker", {}, "b", "test", false, {}, 1 } );
  REQUIRE( !blocker.empty() );

  bool running = false;
  for ( int i = 0; i < 500 && !running; ++i )
  {
    running = eng.snapshot( blocker )->state == JobState::Running;
    if ( !running )
      std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
  }
  REQUIRE( running );

  // Submitted out of order: Low first, Normal second, High last.
  const auto low = eng.submit( JobRequest{ "prio:low", {}, "l", "test", false, {}, 2 } );
  const auto normal = eng.submit( JobRequest{ "prio:mid", {}, "n", "test", false, {}, 1 } );
  const auto high = eng.submit( JobRequest{ "prio:high", {}, "h", "test", false, {}, 0 } );

  releaseBlocker.store( true );
  eng.waitUntilIdleForTests( 15000 );

  std::lock_guard<std::mutex> lock( orderMutex );
  REQUIRE( launchOrder.size() == 4 );
  REQUIRE( launchOrder[0] == "blocker" );
  REQUIRE( launchOrder[1] == "high" );
  REQUIRE( launchOrder[2] == "mid" );
  REQUIRE( launchOrder[3] == "low" );
  eng.clearExecutors();
}

// ---------------------------------------------------------------------------
// #684: production shutdown is latched — late submits are rejected with a
// cancelled record and no worker resurrection; shutdownForTests clears it.
// ---------------------------------------------------------------------------
TEST_CASE( "submit after production shutdown stays rejected (#684)", "[job]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  eng.shutdown();
  REQUIRE( eng.maxWorkers() >= 1 );

  JobRequest req;
  req.algorithmId = "test:add";
  req.params["a"] = 1;
  req.params["b"] = 2;
  req.title = "late";
  req.source = "test";

  const auto first = eng.submit( req );
  auto snap = eng.snapshot( first );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );

  // Latched: a second submit cannot resurrect the pool either.
  const auto second = eng.submit( req );
  snap = eng.snapshot( second );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Cancelled );

  // Explicit test reset re-arms the engine.
  eng.shutdownForTests();
  const auto third = eng.submit( req );
  eng.waitUntilIdleForTests( 15000 );
  snap = eng.snapshot( third );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
}
