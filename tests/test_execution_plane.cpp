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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
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

/// envelopeFor with parameters (#698 lineage test needs an input path param).
Json::Value envelopeFor( const std::string &name, const Json::Value &parameters )
{
  Json::Value envelope( Json::objectValue );
  envelope["name"] = name;
  envelope["parameters"] = parameters;
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

/// Verification handler that always downgrades the payload (the "all pixels
/// are NoData" shape the copilot verification produces on failure).
Json::Value verifyFailHandler( const QString &, const QString & )
{
  Json::Value v( Json::objectValue );
  v["ok"] = false;
  v["kind"] = "raster";
  v["summary"] = Json::Value( Json::objectValue );
  Json::Value issues( Json::arrayValue );
  issues.append( "all pixels are NoData" );
  v["issues"] = issues;
  v["warnings"] = Json::Value( Json::arrayValue );
  return v;
}

/// Verification handler that accepts the payload.
Json::Value verifyOkHandler( const QString &, const QString & )
{
  Json::Value v( Json::objectValue );
  v["ok"] = true;
  v["kind"] = "raster";
  v["summary"] = Json::Value( Json::objectValue );
  v["issues"] = Json::Value( Json::arrayValue );
  v["warnings"] = Json::Value( Json::arrayValue );
  return v;
}

/// Registers a TaskTemporary + DeletableSource asset over @a path — the shape
/// a production output commit produces (and the only shape the rollback's reap
/// is allowed to delete).
sicnu::data::AssetId registerCommittedAsset( sicnu::data::DataManager &manager, const QString &path )
{
  sicnu::data::SourceDescriptor src;
  src.canonicalSource = path;
  src.providerKey = QStringLiteral( "gdal" );
  sicnu::data::RegisterRequest req{ src };
  req.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
  req.additionalCapabilities = sicnu::data::AssetCapability::DeletableSource;
  const auto reg = manager.registerSource( req );
  REQUIRE( !reg.assetId.isNull() );
  return reg.assetId;
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
    [&]( const sicnu::AlgorithmTaskInfo &, std::string &, std::string &, std::string & ) {
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
    return [counter]( const sicnu::AlgorithmTaskInfo &, std::string &path, std::string &, std::string & ) {
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
// #698: commits must carry INPUT lineage — a parameter path that references
// a registered asset becomes a DerivationInput so derivedFrom() traces the
// graph (previously every commit recorded only operator+parameters).
// ---------------------------------------------------------------------------
TEST_CASE( "agent tool call records input lineage for registered parameter paths (#698)",
           "[processing][execution_plane][agent_path][provenance]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString inputPath = dir.path() + QStringLiteral( "/input_scene.tif" );
  writeSmallGeoTiff( inputPath );
  const QString tempPath = dir.path() + QStringLiteral( "/result.tif" );
  writeSmallGeoTiff( tempPath );
  REQUIRE( QFileInfo::exists( inputPath ) );
  REQUIRE( QFileInfo::exists( tempPath ) );

  sicnu::data::DataManager dataManager;
  // Register the input scene so the commit can resolve it to an asset.
  sicnu::data::RegisterRequest inputRequest;
  inputRequest.source.canonicalSource = inputPath;
  inputRequest.source.providerKey = QStringLiteral( "gdal" );
  const auto inputRegistered = dataManager.registerSource( inputRequest );
  REQUIRE_FALSE( inputRegistered.assetId.isNull() );

  registerStub( "stub:lineage_producer", BehavioralStubAdapter::Mode::WriteOutput, 0, tempPath );

  ToolCallDispatcher dispatcher;
  dispatcher.setSourceTag( QStringLiteral( "agent" ) );
  dispatcher.setDataManager( &dataManager );

  Json::Value parameters( Json::objectValue );
  parameters["input"] = inputPath.toStdString();
  const Json::Value result = dispatcher.dispatchAndAwait(
    envelopeFor( "stub:lineage_producer", parameters ), std::chrono::seconds( 8 ) );

  REQUIRE( result["status"].asString() == "success" );
  const QString committed = QString::fromStdString( result["output"].asString() );
  REQUIRE( committed.endsWith( QStringLiteral( "_committed.tif" ) ) );

  const auto outputId = sicnu::data::AssetId::fromString(
    QString::fromStdString( result["assetId"].asString() ) );
  REQUIRE( outputId.has_value() );
  const auto provenance = dataManager.provenance( *outputId );
  REQUIRE( provenance.has_value() );
  REQUIRE( provenance->inputs.size() == 1 );
  CHECK( provenance->inputs.first().assetId == inputRegistered.assetId );

  // The lineage graph answers in both directions now.
  const auto derived = dataManager.derivedOutputsOf( inputRegistered.assetId );
  REQUIRE( derived.size() == 1 );
  CHECK( derived.first() == *outputId );
}

// ---------------------------------------------------------------------------
// 8b. P0-L1 regression: agent tool call must not emit the temp-path
// layerAutoLoadRequested signal. The single layer load comes from the stable
// asset via DataManager::assetAdded / QgisDisplayManager in the app layer.
// ---------------------------------------------------------------------------
TEST_CASE( "agent tool call commits a stable asset without temp-path auto-load",
           "[processing][execution_plane][agent_path][layer]" )
{
  ensureCoreApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString tempPath = dir.path() + QStringLiteral( "/result.tif" );
  writeSmallGeoTiff( tempPath );
  REQUIRE( QFileInfo::exists( tempPath ) );

  registerStub( "stub:producer_noload", BehavioralStubAdapter::Mode::WriteOutput, 0, tempPath );

  sicnu::data::DataManager dataManager;
  ToolCallDispatcher dispatcher;
  dispatcher.setSourceTag( QStringLiteral( "agent" ) );
  dispatcher.setDataManager( &dataManager );

  QSignalSpy loadSpy( &TaskCenter::instance(), &TaskCenter::layerAutoLoadRequested );

  const Json::Value result =
    dispatcher.dispatchAndAwait( envelopeFor( "stub:producer_noload" ), std::chrono::seconds( 8 ) );

  REQUIRE( result["status"].asString() == "success" );
  REQUIRE( loadSpy.count() == 0 );

  const QString committed = QString::fromStdString( result["output"].asString() );
  REQUIRE( committed.endsWith( QStringLiteral( "_committed.tif" ) ) );
  REQUIRE( QFileInfo::exists( committed ) );
  REQUIRE_FALSE( QFileInfo::exists( tempPath ) );

  const auto assets = dataManager.assets();
  REQUIRE( assets.size() == 1 );
  CHECK( assets.first().source().canonicalSource == committed );
}


// ---------------------------------------------------------------------------
// #702.6: watch() returns a removable token so a never-terminal task does not
// pin its captured state for the process lifetime.
// ---------------------------------------------------------------------------
TEST_CASE( "watch token can be removed before the terminal transition (#702)",
           "[processing][execution_plane][watch]" )
{
  registerStub( "stub:watch_token", BehavioralStubAdapter::Mode::SleepThenComplete, /*sleepMs=*/1500 );
  SchedulingGuard schedulingGuard;

  ExecutionRequest request;
  request.algorithmId = QStringLiteral( "stub:watch_token" );
  const long taskId = ExecutionPlane::instance().submit( request ).taskId();
  REQUIRE( taskId > 0 );

  std::atomic<int> deliveries{ 0 };
  const long token = ExecutionPlane::instance().watch(
      taskId, [ &deliveries ]( const sicnu::AlgorithmTaskInfo & ) { deliveries.fetch_add( 1 ); } );
  REQUIRE( token > 0 );

  // Removing the registration before the terminal transition must suppress
  // the callback (before the fix the token was discarded and the callback
  // state was pinned for the process lifetime).
  ExecutionPlane::instance().removeWatch( taskId, token );

  TaskCenter::instance().waitForTask( taskId, std::chrono::seconds( 10 ) );
  REQUIRE( deliveries.load() == 0 );

  // A watch registered on an already-terminal task fires inline and reports
  // the sentinel (nothing left to remove).
  long inlineToken = 0;
  ExecutionPlane::instance().watch(
      taskId, [ &inlineToken ]( const sicnu::AlgorithmTaskInfo & ) { inlineToken = -2; } );
  REQUIRE( inlineToken == -2 );
}

// ---------------------------------------------------------------------------
// 10. Verification-failure rollback contract matrix (#1042) and the affinity
//     starvation payload contract (#1056): every completion path that
//     publishes a committed payload runs the rollback inside the plane's
//     publication gate, and an affinity-starved await never publishes the
//     uncommitted temporary output.
// ---------------------------------------------------------------------------
TEST_CASE( "verification downgrade rolls back the committed asset at the plane publication gate (#1042)",
           "[processing][execution_plane][verification][rollback]" )
{
  ensureCoreApp();
  sicnu::data::DataManager manager;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString committedPath = dir.path() + QStringLiteral( "/roll_me_committed.tif" );
  writeSmallGeoTiff( committedPath );
  const sicnu::data::AssetId assetId = registerCommittedAsset( manager, committedPath );

  std::atomic<int> rollbackCalls{ 0 };
  auto rollback = [&, managerPtr = &manager]( Json::Value &payload ) {
    ++rollbackCalls;
    ToolCallDispatcher::rollbackVerificationFailure( payload, managerPtr );
  };
  std::atomic<int> commits{ 0 };
  auto committer = [&commits, assetIdStr = assetId.toString().toStdString(),
                    pathStd = committedPath.toStdString()](
                       const sicnu::AlgorithmTaskInfo &, std::string &outCommittedPath,
                       std::string &, std::string &outAssetId ) -> bool {
    ++commits;
    outCommittedPath = pathStd;
    outAssetId = assetIdStr;
    return true;
  };

  sicnu::AlgorithmTaskInfo info;
  info.taskId = 9104201; // unique in this binary: the commit cache is keyed by task id
  info.algorithmId = QStringLiteral( "rs:ndvi" );
  info.status = sicnu::TaskStatus::Completed;
  info.outputLayerPath = committedPath;

  // First builder: commit + verification downgrade + rollback in one gate.
  const Json::Value payload = ExecutionPlane::instance().buildCommittedResultPayload(
    info, committer, verifyFailHandler, rollback );
  REQUIRE( payload["status"].asString() == "error" );
  REQUIRE( payload["verified"].asBool() == false );
  REQUIRE( commits.load() == 1 );
  REQUIRE( rollbackCalls.load() == 1 );
  // Insulator: the committed asset is gone from the catalog AND from disk.
  CHECK_FALSE( manager.asset( assetId ).has_value() );
  CHECK_FALSE( QFileInfo::exists( committedPath ) );

  // Second builder (copilot-style duplicate): the cache hit re-applies the
  // rollback (idempotent no-op) and publishes the same downgraded payload —
  // never a cached "success".
  const Json::Value cached = ExecutionPlane::instance().buildCommittedResultPayload(
    info, committer, verifyFailHandler, rollback );
  REQUIRE( cached["status"].asString() == "error" );
  REQUIRE( cached["verified"].asBool() == false );
  REQUIRE( commits.load() == 1 ); // the commit stayed exactly-once
  CHECK_FALSE( manager.asset( assetId ).has_value() );
  CHECK_FALSE( cached.isMember( "rollbackErrors" ) ); // repeat reap is a clean no-op
}

TEST_CASE( "verified commits keep the asset — no rollback without a downgrade",
           "[processing][execution_plane][verification][success]" )
{
  ensureCoreApp();
  sicnu::data::DataManager manager;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString committedPath = dir.path() + QStringLiteral( "/keep_me_committed.tif" );
  writeSmallGeoTiff( committedPath );
  const sicnu::data::AssetId assetId = registerCommittedAsset( manager, committedPath );

  std::atomic<int> rollbackCalls{ 0 };
  auto rollback = [&, managerPtr = &manager]( Json::Value &payload ) {
    ++rollbackCalls;
    ToolCallDispatcher::rollbackVerificationFailure( payload, managerPtr );
  };
  auto committer = [assetIdStr = assetId.toString().toStdString(),
                    pathStd = committedPath.toStdString()](
                       const sicnu::AlgorithmTaskInfo &, std::string &outCommittedPath,
                       std::string &, std::string &outAssetId ) -> bool {
    outCommittedPath = pathStd;
    outAssetId = assetIdStr;
    return true;
  };

  sicnu::AlgorithmTaskInfo info;
  info.taskId = 9104202;
  info.algorithmId = QStringLiteral( "rs:ndvi" );
  info.status = sicnu::TaskStatus::Completed;
  info.outputLayerPath = committedPath;

  const Json::Value payload = ExecutionPlane::instance().buildCommittedResultPayload(
    info, committer, verifyOkHandler, rollback );
  REQUIRE( payload["status"].asString() == "success" );
  REQUIRE( payload["verified"].asBool() == true );
  REQUIRE( rollbackCalls.load() == 1 ); // the gate ran, the rollback was a no-op
  CHECK( manager.asset( assetId ).has_value() );
  CHECK( QFileInfo::exists( committedPath ) );
}

TEST_CASE( "awaitResult applies the verification rollback on the sync commit path (#1042)",
           "[processing][execution_plane][await][rollback]" )
{
  ensureCoreApp();
  sicnu::data::DataManager manager;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString tempPath = dir.path() + QStringLiteral( "/sync_roll.tif" );
  const QString committedPath = dir.path() + QStringLiteral( "/sync_roll_committed.tif" );
  writeSmallGeoTiff( tempPath );
  writeSmallGeoTiff( committedPath );
  const sicnu::data::AssetId assetId = registerCommittedAsset( manager, committedPath );

  registerStub( "stub:sync_rollback", BehavioralStubAdapter::Mode::WriteOutput, 0, tempPath );
  ExecutionRequest request;
  request.algorithmId = QStringLiteral( "stub:sync_rollback" );
  const ExecutionHandle handle = ExecutionPlane::instance().submit( request );
  REQUIRE( handle.taskId() > 0 );
  REQUIRE( handle.await( std::chrono::seconds( 8 ) ) );

  std::atomic<int> rollbackCalls{ 0 };
  auto rollback = [&, managerPtr = &manager]( Json::Value &payload ) {
    ++rollbackCalls;
    ToolCallDispatcher::rollbackVerificationFailure( payload, managerPtr );
  };
  std::atomic<int> commits{ 0 };
  auto committer = [&commits, assetIdStr = assetId.toString().toStdString(),
                    pathStd = committedPath.toStdString()](
                       const sicnu::AlgorithmTaskInfo &, std::string &outCommittedPath,
                       std::string &, std::string &outAssetId ) -> bool {
    ++commits;
    outCommittedPath = pathStd;
    outAssetId = assetIdStr;
    return true;
  };

  const Json::Value payload = ExecutionPlane::instance().awaitResult(
    handle.taskId(), std::chrono::seconds( 8 ), committer, /*affinityContext=*/nullptr,
    /*cancelOnTimeout=*/false, verifyFailHandler, rollback );

  REQUIRE( payload["status"].asString() == "error" );
  REQUIRE( payload["verified"].asBool() == false );
  REQUIRE( commits.load() == 1 );
  REQUIRE( rollbackCalls.load() == 1 );
  CHECK_FALSE( manager.asset( assetId ).has_value() );
  CHECK_FALSE( QFileInfo::exists( committedPath ) );
}

TEST_CASE( "awaitResult affinity starvation returns a structured error, never the temp output (#1056)",
           "[processing][execution_plane][affinity][timeout]" )
{
  ensureCoreApp();

  // A worker thread that never pumps an event loop: queued deliveries to a
  // QObject moved here can never run, which is exactly the starvation the
  // 5 s affinity window guards against.
  class NonPumpingThread : public QThread
  {
    public:
      using QThread::QThread;
      void run() override
      {
        while ( !mStop.load() )
          std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
      }
      void requestStop() { mStop.store( true ); }
      std::atomic<bool> mStop{ false };
  };

  sicnu::data::DataManager manager;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString tempPath = dir.path() + QStringLiteral( "/starved_output.tif" );
  writeSmallGeoTiff( tempPath );

  registerStub( "stub:affinity_starve", BehavioralStubAdapter::Mode::WriteOutput, 0, tempPath );
  ExecutionRequest request;
  request.algorithmId = QStringLiteral( "stub:affinity_starve" );
  const ExecutionHandle handle = ExecutionPlane::instance().submit( request );
  REQUIRE( handle.taskId() > 0 );
  REQUIRE( handle.await( std::chrono::seconds( 8 ) ) );

  NonPumpingThread worker;
  worker.start();
  QObject bridge;
  bridge.moveToThread( &worker );

  std::atomic<int> commits{ 0 };
  auto committer = [&commits]( const sicnu::AlgorithmTaskInfo &, std::string &path,
                               std::string &, std::string & ) -> bool {
    ++commits;
    path = "/tmp/should_never_commit.tif";
    return true;
  };
  std::atomic<int> rollbackCalls{ 0 };
  auto rollback = [&, managerPtr = &manager]( Json::Value &payload ) {
    ++rollbackCalls;
    ToolCallDispatcher::rollbackVerificationFailure( payload, managerPtr );
  };

  const auto start = std::chrono::steady_clock::now();
  const Json::Value payload = ExecutionPlane::instance().awaitResult(
    handle.taskId(), std::chrono::seconds( 8 ), committer, &bridge,
    /*cancelOnTimeout=*/false, verifyOkHandler, rollback );
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start )
                           .count();

  worker.requestStop();
  worker.wait();

  INFO( "elapsed ms: " << elapsedMs << ", status: " << payload["status"].asString() );
  REQUIRE( payload["status"].asString() == "error" );
  REQUIRE( payload["errorKind"].asString() == "commit_delivery_timeout" );
  REQUIRE( payload["taskId"].asInt64() == handle.taskId() );
  REQUIRE( payload["algorithmId"].asString() == "stub:affinity_starve" );
  // The uncommitted temporary output path must NOT be published: TaskCenter
  // may reap it, so the result would dangle.
  CHECK_FALSE( payload.isMember( "output" ) );
  REQUIRE( commits.load() == 0 );
  REQUIRE( rollbackCalls.load() == 0 );
  REQUIRE( elapsedMs < 8000 );
}

TEST_CASE( "async watcher delivery rolls back on verification failure (#1042)",
           "[processing][execution_plane][watcher][rollback]" )
{
  ensureCoreApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString tempPath = dir.path() + QStringLiteral( "/watch_roll.tif" );
  writeSmallGeoTiff( tempPath );
  REQUIRE( QFileInfo::exists( tempPath ) );

  registerStub( "stub:watch_rollback", BehavioralStubAdapter::Mode::WriteOutput, 0, tempPath );

  sicnu::data::DataManager dataManager;
  ToolCallDispatcher dispatcher; // production wiring: watcher commits through the plane
  dispatcher.setSourceTag( QStringLiteral( "agent" ) );
  dispatcher.setDataManager( &dataManager );
  dispatcher.setOutputVerificationHandler( verifyFailHandler );

  std::atomic<int> delivered{ 0 };
  Json::Value deliveredPayload;
  QString submitError;
  REQUIRE( dispatcher.submit( envelopeFor( "stub:watch_rollback" ),
                              [&]( const Json::Value &payload ) {
                                deliveredPayload = payload;
                                ++delivered;
                              },
                              &submitError, nullptr ) );
  REQUIRE( submitError.isEmpty() );

  // Pump the test thread's loop (the bridge lives here) until the queued
  // delivery lands; the commit and rollback then run inline on this thread —
  // the DataManager's owning thread — so assertions are deterministic.
  QEventLoop loop;
  QTimer poller;
  poller.setInterval( 25 );
  QObject::connect( &poller, &QTimer::timeout, [&] {
    if ( delivered.load() >= 1 )
      loop.quit();
  } );
  QTimer::singleShot( 8000, &loop, &QEventLoop::quit );
  poller.start();
  loop.exec();
  poller.stop();

  REQUIRE( delivered.load() == 1 );
  REQUIRE( deliveredPayload["status"].asString() == "error" );
  REQUIRE( deliveredPayload["verified"].asBool() == false );
  // The real production commit produced exactly one asset; the rollback reaped
  // it (catalog entry + stable file).
  CHECK( dataManager.assets().isEmpty() );
  const QString committed = tempPath.left( tempPath.length() - 4 ) + QStringLiteral( "_committed.tif" );
  CHECK_FALSE( QFileInfo::exists( committed ) );
}

TEST_CASE( "dispatchAndAwait sync path rolls back on verification failure (#1042)",
           "[processing][execution_plane][dispatch_and_await][rollback]" )
{
  ensureCoreApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString tempPath = dir.path() + QStringLiteral( "/await_roll.tif" );
  writeSmallGeoTiff( tempPath );
  REQUIRE( QFileInfo::exists( tempPath ) );

  registerStub( "stub:await_rollback", BehavioralStubAdapter::Mode::WriteOutput, 0, tempPath );

  sicnu::data::DataManager dataManager;
  ToolCallDispatcher dispatcher;
  dispatcher.setSourceTag( QStringLiteral( "agent" ) );
  dispatcher.setDataManager( &dataManager );
  dispatcher.setOutputVerificationHandler( verifyFailHandler );

  const Json::Value result =
    dispatcher.dispatchAndAwait( envelopeFor( "stub:await_rollback" ), std::chrono::seconds( 8 ) );

  REQUIRE( result["status"].asString() == "error" );
  REQUIRE( result["verified"].asBool() == false );
  CHECK( dataManager.assets().isEmpty() );
  const QString committed = tempPath.left( tempPath.length() - 4 ) + QStringLiteral( "_committed.tif" );
  CHECK_FALSE( QFileInfo::exists( committed ) );
  // The temporary output was consumed by the commit and then reaped with the
  // asset — neither path may survive as a published result.
  CHECK_FALSE( QFileInfo::exists( tempPath ) );
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

  // Restore both singletons for any test registered after this one.
  sicnu::TaskCenter::instance().shutdownForTests();
}
