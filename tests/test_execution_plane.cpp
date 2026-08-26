// tests/test_execution_plane.cpp
//
// Unified Execution Plane regression suite. Exercises the REAL spine
// (ExecutionPlane → TaskCenter → JobEngine → registry adapter) with stub
// algorithms, plus the ToolCallDispatcher production wiring that rides it.
//
// Coverage required by the execution-plane task:
//   1. dispatchAndAwait does not deadlock on the bridge thread (#559)
//   2. completion callback fires exactly once
//   3. cancel during execution (Cancelling → Canceled, no commit)
//   4. timeout + late completion (single truthful outcome, no double commit)
//   5. OutputCommitter failure downgrades the payload
//   6. TaskCenter waiting-resource → running admission (and slot release)
//   7. worker callback QObject lifetime (dispatcher destroyed mid-flight)
//   8. agent tool call through the unified path (committed asset + provenance)
//   9. shutdown wakes sync awaiters (LAST test — latches the singletons)
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include <cpl_conv.h>
#include <gdal.h>

#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"

using namespace sicnu::processing;
using sicnu::processing::ExecutionHandle;
using sicnu::processing::ExecutionPlane;
using sicnu::TaskCenter;
using sicnu::TaskStatus;

namespace
{

// One QCoreApplication for the whole binary (leaked, matching the pattern of
// test_agent_workflow_executor; issue #568 forbids per-TEST_CASE instances).
// It is NEVER exec()'d — tests needing queued delivery pump a QEventLoop
// explicitly, which is exactly the "async consumers pump" contract.
QCoreApplication *ensureCoreApp()
{
  static QCoreApplication *app = [] {
    int argc = 1;
    static char arg0[] = "test_execution_plane";
    char *argv[] = { arg0, nullptr };
    return new QCoreApplication( argc, argv );
  }();
  return app;
}

// Bridge AtomicAlgorithmRegistry into JobEngine exactly like production
// (main.cpp ADR 0062 wiring) so plane submissions actually execute.
void wireRegistryFallback()
{
  sicnu::jobs::JobEngine::instance().setFallbackExecutor(
    []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
      const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
      if ( !adapter )
        throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
      sicnu::processing::ProgressCallback progressBridge;
      progressBridge = [&ctx]( int percent, const std::string &message ) {
        ctx.reportProgress( percent / 100.0, message );
      };
      return adapter->execute( req.params, progressBridge,
                               [&ctx]() { return ctx.isCancelled(); } );
    } );
}

/// Behavior-configurable stub algorithm executed on JobEngine workers.
class BehavioralStubAdapter : public AtomicAlgorithmAdapter
{
  public:
    enum class Mode
    {
      Immediate,         ///< return at once
      CancelAwareWait,   ///< loop until the cancel flag is observed
      SleepThenComplete, ///< sleep N ms (ignores cancellation), then succeed
      WriteOutput,       ///< report a prepared file as "output"
      Fail
    };

    BehavioralStubAdapter( std::string id, Mode mode, int sleepMs = 200, QString outputPath = {} )
      : mId( std::move( id ) )
      , mMode( mode )
      , mSleepMs( sleepMs )
      , mOutputPath( std::move( outputPath ) )
    {
    }

    std::string algorithmId() const override { return mId; }
    AlgorithmDescriptor descriptor() const override { return AlgorithmDescriptor{}; }

    Json::Value execute( const Json::Value &params, ProgressCallback,
                         std::function<bool()> isCancelled ) override
    {
      switch ( mMode )
      {
        case Mode::Immediate:
        {
          Json::Value result( Json::objectValue );
          result["status"] = "ok";
          result["echo"] = params;
          return result;
        }
        case Mode::CancelAwareWait:
        {
          const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
          while ( std::chrono::steady_clock::now() < deadline )
          {
            if ( isCancelled && isCancelled() )
            {
              // finishSuccess sees the armed cancel flag → JobState::Cancelled.
              Json::Value result( Json::objectValue );
              result["status"] = "cancelled";
              return result;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
          }
          return Json::Value( Json::objectValue );
        }
        case Mode::SleepThenComplete:
        {
          std::this_thread::sleep_for( std::chrono::milliseconds( mSleepMs ) );
          Json::Value result( Json::objectValue );
          result["status"] = "ok";
          return result;
        }
        case Mode::WriteOutput:
        {
          Json::Value result( Json::objectValue );
          result["status"] = "ok";
          result["output"] = mOutputPath.toStdString();
          return result;
        }
        case Mode::Fail:
          throw std::runtime_error( "stub failure" );
      }
      return Json::Value( Json::objectValue );
    }

  private:
    std::string mId;
    Mode mMode;
    int mSleepMs;
    QString mOutputPath;
};

/// Registers a stub and wires the engine fallback; each TEST_CASE starts from
/// a clean registry (the registry is a singleton shared within this binary).
void registerStub( const std::string &id, BehavioralStubAdapter::Mode mode,
                   int sleepMs = 200, const QString &outputPath = {} )
{
  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<BehavioralStubAdapter>( id, mode, sleepMs, outputPath ) );
}

/// RAII guard restoring TaskCenter scheduling configuration so admission
/// tweaks from one TEST_CASE do not leak into the next.
struct SchedulingGuard
{
  ~SchedulingGuard() { TaskCenter::instance().resetResourceProfileLimits(); }
};

/// Poll until @a predicate or a ~5s deadline; returns the predicate value.
bool eventually( const std::function<bool()> &predicate,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds( 5000 ) )
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while ( std::chrono::steady_clock::now() < deadline )
  {
    if ( predicate() )
      return true;
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }
  return predicate();
}

Json::Value envelopeFor( const std::string &name )
{
  Json::Value envelope( Json::objectValue );
  envelope["name"] = name;
  envelope["parameters"] = Json::Value( Json::objectValue );
  return envelope;
}

/// Minimal real GeoTIFF so OutputCommitter validation succeeds.
void writeSmallGeoTiff( const QString &path )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  float tile[ 64 ];
  for ( int i = 0; i < 64; ++i )
    tile[i] = static_cast<float>( i );
  CPLErr err = GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 8, 8,
                             tile, 8, 8, GDT_Float32, 0, 0 );
  REQUIRE( err == CE_None );
  GDALClose( ds );
}

} // namespace

// ---------------------------------------------------------------------------
// 1. #559 regression: dispatchAndAwait on the bridge thread must complete
//    without that thread's event loop running. The old wiring queued the
//    completion payload onto the (blocked) bridge thread → hang until timeout.
// ---------------------------------------------------------------------------
TEST_CASE( "dispatchAndAwait does not deadlock on the bridge thread (#559)",
           "[processing][execution_plane][deadlock]" )
{
  ensureCoreApp();
  registerStub( "stub:immediate", BehavioralStubAdapter::Mode::Immediate );

  ToolCallDispatcher dispatcher; // production wiring: sink/watch/syncAwait on the plane
  const Json::Value envelope = envelopeFor( "stub:immediate" );

  const auto start = std::chrono::steady_clock::now();
  const Json::Value result = dispatcher.dispatchAndAwait( envelope, std::chrono::seconds( 8 ) );
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start )
                           .count();

  INFO( "elapsed ms: " << elapsedMs << ", payload status: " << result["status"].asString() );
  REQUIRE( result["status"].asString() == "success" );
  REQUIRE( elapsedMs < 6000 );
  REQUIRE( result["taskId"].asInt64() > 0 );
}

// ---------------------------------------------------------------------------
// 2. Completion callbacks fire exactly once per task.
// ---------------------------------------------------------------------------
TEST_CASE( "ExecutionPlane terminal callbacks fire exactly once",
           "[processing][execution_plane][completion]" )
{
  registerStub( "stub:once", BehavioralStubAdapter::Mode::Immediate );

  ExecutionRequest request;
  request.algorithmId = QStringLiteral( "stub:once" );
  request.source = QStringLiteral( "test" );
  const ExecutionHandle handle = ExecutionPlane::instance().submit( request );
  REQUIRE( handle.taskId() > 0 );
  REQUIRE( handle.await( std::chrono::seconds( 5 ) ) );

  std::atomic<int> fired{ 0 };
  REQUIRE( ExecutionPlane::instance().watch( handle.taskId(),
                                             [&]( const sicnu::AlgorithmTaskInfo & ) { ++fired; } ) );
  // Terminal already reached: the watch fires inline exactly once.
  REQUIRE( fired.load() == 1 );

  // A second registration on the same task also fires exactly once — never
  // zero (lost) and never twice (duplicate terminal transitions are no-ops).
  std::atomic<int> second{ 0 };
  REQUIRE( ExecutionPlane::instance().watch( handle.taskId(),
                                             [&]( const sicnu::AlgorithmTaskInfo & ) { ++second; } ) );
  REQUIRE( second.load() == 1 );
}

// ---------------------------------------------------------------------------
// 3. Cancel during execution: Cancelling is observable, the worker stops, the
//    terminal state is Canceled and no output commit runs.
// ---------------------------------------------------------------------------
TEST_CASE( "cancel during execution reaches Canceled without committing",
           "[processing][execution_plane][cancel]" )
{
  registerStub( "stub:cancel_aware", BehavioralStubAdapter::Mode::CancelAwareWait );

  ExecutionRequest request;
  request.algorithmId = QStringLiteral( "stub:cancel_aware" );
  const ExecutionHandle handle = ExecutionPlane::instance().submit( request );
  REQUIRE( handle.taskId() > 0 );
  REQUIRE( eventually( [&] {
    return TaskCenter::instance().getTaskInfo( handle.taskId() ).status == TaskStatus::Running;
  } ) );

  handle.cancel();

  // Between the cancel request and the worker's terminal record the task is
  // explicitly Cancelling (not silently Running, not terminal).
  REQUIRE( eventually( [&] {
    return TaskCenter::instance().getTaskInfo( handle.taskId() ).status == TaskStatus::Cancelling;
  } ) );

  REQUIRE( handle.await( std::chrono::seconds( 5 ) ) );
  REQUIRE( TaskCenter::instance().getTaskInfo( handle.taskId() ).status == TaskStatus::Canceled );

  // Canceled tasks never run the output committer; the payload reports the
  // cancellation as an error while keeping the task id for correlation.
  std::atomic<int> commits{ 0 };
  const Json::Value payload = ExecutionPlane::instance().awaitResult(
    handle.taskId(), std::chrono::milliseconds( 500 ),
    [&]( const sicnu::AlgorithmTaskInfo &, std::string &, std::string & ) {
      ++commits;
      return true;
    } );
  REQUIRE( payload["status"].asString() == "error" );
  REQUIRE( commits.load() == 0 );
}

// ---------------------------------------------------------------------------
// 4. Timeout + late completion: the deadline triggers a cancel; the grace
//    window lets the (cancel-ignoring) worker land its terminal record so the
//    payload stays truthful, and the commit runs at most once.
// ---------------------------------------------------------------------------
TEST_CASE( "timeout cancels and a late completion yields one truthful outcome",
           "[processing][execution_plane][timeout]" )
{
  registerStub( "stub:slow", BehavioralStubAdapter::Mode::SleepThenComplete, /*sleepMs=*/400 );

  ExecutionRequest request;
  request.algorithmId = QStringLiteral( "stub:slow" );
  const ExecutionHandle handle = ExecutionPlane::instance().submit( request );

  std::atomic<int> commits{ 0 };
  auto countingCommitter = []( std::atomic<int> *counter ) {
    return [counter]( const sicnu::AlgorithmTaskInfo &, std::string &path, std::string & ) {
      ++( *counter );
      path = "/tmp/committed-once.tif";
      return true;
    };
  }( &commits );

  const auto start = std::chrono::steady_clock::now();
  const Json::Value payload = ExecutionPlane::instance().awaitResult(
    handle.taskId(), std::chrono::milliseconds( 60 ), countingCommitter, nullptr,
    /*cancelOnTimeout=*/true );
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start )
                           .count();

  // The stub ignores cancellation, so JobEngine's finishSuccess records
  // Cancelled when the cancel flag is armed: the truthful outcome. The grace
  // window waited for it rather than returning a guess.
  REQUIRE( elapsedMs >= 300 );
  REQUIRE( payload["status"].asString() == "error" );
  REQUIRE( commits.load() == 0 ); // canceled ⇒ never committed
  REQUIRE( eventually( [&] {
    return TaskCenter::instance().getTaskInfo( handle.taskId() ).status == TaskStatus::Canceled;
  } ) );

  // A hard-timeout variant: a worker that outlives the grace window returns
  // the timeout error promptly (bounded by grace, not by the stub's runtime).
  registerStub( "stub:very_slow", BehavioralStubAdapter::Mode::SleepThenComplete, /*sleepMs=*/2600 );
  ExecutionRequest longRequest;
  longRequest.algorithmId = QStringLiteral( "stub:very_slow" );
  const ExecutionHandle longHandle = ExecutionPlane::instance().submit( longRequest );

  const auto hardStart = std::chrono::steady_clock::now();
  const Json::Value timeoutPayload = ExecutionPlane::instance().awaitResult(
    longHandle.taskId(), std::chrono::milliseconds( 50 ), countingCommitter, nullptr,
    /*cancelOnTimeout=*/true );
  const auto hardElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - hardStart )
                               .count();

  REQUIRE( timeoutPayload["status"].asString() == "error" );
  REQUIRE( timeoutPayload["errorMessage"].asString() == "Tool call timed out" );
  REQUIRE( hardElapsedMs < 5000 ); // ≈ grace window, far below the stub's runtime

  // Drain the cancel-ignoring worker before the next test so it does not
  // occupy a worker slot / RAM estimate in the admission test below.
  REQUIRE( eventually( [&] { return isTerminalStatus(
                                TaskCenter::instance().getTaskInfo( longHandle.taskId() ).status ); },
                       std::chrono::milliseconds( 8000 ) ) );
}

// ---------------------------------------------------------------------------
// 5. OutputCommitter failure downgrades the payload to an error while the
//    underlying task itself completed.
// ---------------------------------------------------------------------------
TEST_CASE( "OutputCommitter refusal downgrades the result payload",
           "[processing][execution_plane][commit]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // NOT a valid raster: the committer's structural validation must refuse it.
  const QString garbagePath = dir.path() + QStringLiteral( "/garbage.tif" );
  {
    QFile f( garbagePath );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "this is not a raster" );
    f.close();
  }
  registerStub( "stub:bad_output", BehavioralStubAdapter::Mode::WriteOutput, 0, garbagePath );

  sicnu::data::DataManager dataManager;
  ToolCallDispatcher dispatcher; // production wiring
  dispatcher.setDataManager( &dataManager );

  const Json::Value result =
    dispatcher.dispatchAndAwait( envelopeFor( "stub:bad_output" ), std::chrono::seconds( 8 ) );
  REQUIRE( result["status"].asString() == "error" );
  REQUIRE( result.isMember( "commitError" ) );
  // The task itself succeeded; the payload reports the commit failure and
  // keeps the original (temporary) output path for diagnosis.
  REQUIRE( result.isMember( "output" ) );
  REQUIRE( dataManager.assets().isEmpty() ); // nothing registered
}

// ---------------------------------------------------------------------------
// 6. Resource admission: WaitingResource → Running hand-off and slot release.
// ---------------------------------------------------------------------------
TEST_CASE( "resource admission holds a task in WaitingResource then launches it",
           "[processing][execution_plane][admission]" )
{
  SchedulingGuard guard;
  registerStub( "stub:heavy", BehavioralStubAdapter::Mode::SleepThenComplete, /*sleepMs=*/400 );

  auto &center = TaskCenter::instance();
  center.setGlobalConcurrencyLimit( 1 ); // one worker slot
  center.setResourceBudgetMb( 100 );     // and a tight RAM budget

  ExecutionRequest first;
  first.algorithmId = QStringLiteral( "stub:heavy" );
  first.resourceEstimateMb = 80;
  const ExecutionHandle handleA = ExecutionPlane::instance().submit( first );

  ExecutionRequest second;
  second.algorithmId = QStringLiteral( "stub:heavy" );
  second.resourceEstimateMb = 80;
  const ExecutionHandle handleB = ExecutionPlane::instance().submit( second );

  REQUIRE( handleA.taskId() > 0 );
  REQUIRE( handleB.taskId() > 0 );

  // A occupies the single slot; B is held by admission — explicitly
  // WaitingResource, not silently Queued.
  REQUIRE( eventually( [&] {
    return center.getTaskInfo( handleA.taskId() ).status == TaskStatus::Running;
  } ) );
  REQUIRE( eventually( [&] {
    return center.getTaskInfo( handleB.taskId() ).status == TaskStatus::WaitingResource;
  } ) );

  // Admission snapshot agrees: the second heavy candidate does not fit.
  const auto snap = center.admissionSnapshot( QStringLiteral( "stub:heavy" ), 80 );
  REQUIRE( snap.runningCount == 1 );
  REQUIRE( snap.runningMb == 80 );
  REQUIRE_FALSE( snap.wouldAdmit );
  REQUIRE_FALSE( snap.reason.isEmpty() );

  // A completes → slot + RAM release → B launches and completes (the
  // slot-release regression: a leaked slot would starve B forever).
  REQUIRE( handleA.await( std::chrono::seconds( 5 ) ) );
  REQUIRE( eventually( [&] {
    return center.getTaskInfo( handleB.taskId() ).status == TaskStatus::Running;
  } ) );
  REQUIRE( handleB.await( std::chrono::seconds( 5 ) ) );
  REQUIRE( center.getTaskInfo( handleB.taskId() ).status == TaskStatus::Completed );
}

// ---------------------------------------------------------------------------
// 7. QObject lifetime: the dispatcher (and its bridge ownership) may be
//    destroyed while a task is in flight; completion still delivers exactly
//    once. The shared bridge keeps the affinity QObject alive; queued
//    delivery runs as soon as the bridge thread pumps (explicit QEventLoop).
// ---------------------------------------------------------------------------
TEST_CASE( "dispatcher destroyed mid-flight still delivers exactly once",
           "[processing][execution_plane][lifetime]" )
{
  ensureCoreApp();
  registerStub( "stub:outlive", BehavioralStubAdapter::Mode::SleepThenComplete, /*sleepMs=*/250 );

  std::atomic<int> delivered{ 0 };
  std::atomic<long> taskId{ -1 };
  {
    ToolCallDispatcher dispatcher;
    QString error;
    REQUIRE( dispatcher.submit( envelopeFor( "stub:outlive" ),
                                [&]( const Json::Value &payload ) {
                                  ++delivered;
                                  taskId = payload["taskId"].asInt64();
                                },
                                &error, nullptr ) );
    REQUIRE( error.isEmpty() );
  } // dispatcher gone while the task runs

  // Pump the bridge thread's loop until the queued delivery lands (or 3s).
  QEventLoop loop;
  QTimer poller;
  poller.setInterval( 25 );
  QObject::connect( &poller, &QTimer::timeout, [&] {
    if ( delivered.load() >= 1 )
      loop.quit();
  } );
  QTimer::singleShot( 3000, &loop, &QEventLoop::quit );
  poller.start();
  loop.exec();
  poller.stop();

  REQUIRE( delivered.load() == 1 );
  REQUIRE( taskId.load() > 0 );
}

// ---------------------------------------------------------------------------
// 8. Agent tool call through the unified path: temporary output →
//    OutputCommitter → stable committed asset with provenance.
// ---------------------------------------------------------------------------
TEST_CASE( "agent tool call commits a stable asset with provenance",
           "[processing][execution_plane][agent_path]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString tempPath = dir.path() + QStringLiteral( "/result.tif" );
  writeSmallGeoTiff( tempPath );
  REQUIRE( QFileInfo::exists( tempPath ) );

  registerStub( "stub:producer", BehavioralStubAdapter::Mode::WriteOutput, 0, tempPath );

  sicnu::data::DataManager dataManager;
  ToolCallDispatcher dispatcher;
  dispatcher.setSourceTag( QStringLiteral( "agent" ) );
  dispatcher.setDataManager( &dataManager );

  const Json::Value result =
    dispatcher.dispatchAndAwait( envelopeFor( "stub:producer" ), std::chrono::seconds( 8 ) );

  REQUIRE( result["status"].asString() == "success" );
  const QString committed = QString::fromStdString( result["output"].asString() );
  REQUIRE( committed.endsWith( QStringLiteral( "_committed.tif" ) ) );
  REQUIRE( QFileInfo::exists( committed ) );
  REQUIRE_FALSE( QFileInfo::exists( tempPath ) ); // temp consumed by the publish

  // The stable asset is registered with provenance (taskReference/lineage).
  const auto assets = dataManager.assets();
  REQUIRE( assets.size() == 1 );
  const auto provenance = dataManager.provenance( assets.first().id() );
  REQUIRE( provenance.has_value() );
  CHECK( provenance->algorithmId == QStringLiteral( "stub:producer" ) );
  CHECK( provenance->taskReference == QString::number( result["taskId"].asInt64() ) );
}

// ---------------------------------------------------------------------------
// 9. Shutdown with a running task: sync awaiters wake promptly. MUST BE LAST:
//    TaskCenter::shutdown latches for the lifetime of the process.
// ---------------------------------------------------------------------------
TEST_CASE( "shutdown wakes sync awaiters promptly (run last)",
           "[processing][execution_plane][shutdown]" )
{
  registerStub( "stub:shutdown_race", BehavioralStubAdapter::Mode::SleepThenComplete, /*sleepMs=*/400 );

  ExecutionRequest request;
  request.algorithmId = QStringLiteral( "stub:shutdown_race" );
  const ExecutionHandle handle = ExecutionPlane::instance().submit( request );
  REQUIRE( handle.taskId() > 0 );

  std::atomic<bool> awaiterReturned{ false };
  auto awaiter = std::thread( [&handle, &awaiterReturned] {
    // Either the task lands its terminal state or shutdown wakes the wait —
    // both are prompt exits; a stuck await would hold this thread 30s.
    handle.await( std::chrono::seconds( 30 ) );
    awaiterReturned.store( true );
  } );

  const auto start = std::chrono::steady_clock::now();
  TaskCenter::instance().shutdown();
  awaiter.join();
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start )
                           .count();

  INFO( "shutdown+join elapsed ms: " << elapsedMs );
  REQUIRE( awaiterReturned.load() );
  // shutdown() joins JobEngine workers (the stub finishes ≤400ms); the
  // awaiter must release within that window, not after its own 30s timeout.
  REQUIRE( elapsedMs < 5000 );
}
