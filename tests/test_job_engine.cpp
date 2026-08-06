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
                         sicnu::processing::ProgressCallback progressCb = nullptr ) override
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
