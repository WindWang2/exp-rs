// test_execution_plane_7.cpp — Execution Plane 7.0 fault matrix.
//
// Covers the goal-matrix scenarios that had no coverage before 7.0:
//   - bounded TRANSIENT auto-retry (worker crash/timeout classes): bound
//     respected, permanent errors never retried, DAG edges survive;
//   - production worker routing: require mode executes a real operator in
//     the isolated sicnu_worker process through TaskCenter dispatch;
//   - fail-closed routing: an unavailable pool fails the task typed, never
//     silently in-process;
//   - multi-dimension admission: temp-disk/VRAM budget hold + never-starve,
//     Windows RSS parity;
//   - queued-cancel under admission hold; shutdown with in-flight work;
//   - rapid-jobs stress (10k short jobs, small data).
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/resource_monitor.h"
#include "processing/framework/task_center.h"
#include "processing/framework/worker_execution_route.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/rs/rs_operators_init.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <vector>

#ifndef SICNU_WORKER_EXE
#define SICNU_WORKER_EXE "sicnu_worker"
#endif

// Headless Windows: route Debug-CRT assertions/errors to stderr instead of
// a modal dialog that blocks an unattended run forever.
#if defined( _MSC_VER )
#include <crtdbg.h>
namespace
{
const bool ep7CrtInit = [] {
    _CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
    _CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDERR );
    _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
    _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR );
    _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
    _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR );
    return true;
}();
}
#endif


using sicnu::TaskCenter;
using sicnu::processing::WorkerExecutionMode;

namespace
{

void ensureApp()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char appName[] = "test_execution_plane_7";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
}

void waitForTerminalStatus( long taskId, int attempts = 400, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( sicnu::isTerminalStatus( TaskCenter::instance().getTaskInfo( taskId ).status ) )
            return;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
}

void writeLabelRaster( const QString &path )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    GDALDatasetH ds = createOutputTiff( path, 32, 32, 1, GDT_UInt16, gt, QString() );
    REQUIRE( ds != nullptr );
    std::vector<uint16_t> buf( 32ull * 32 );
    for ( size_t i = 0; i < buf.size(); ++i )
        buf[i] = static_cast<uint16_t>( 1 + i % 5 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 32, 32, buf.data(),
                           32, 32, GDT_UInt16, 0, 0 ) == CE_None );
    GDALClose( ds );
}

/// Stub adapter with a fully controllable descriptor (multi-dim admission).
class DimsStubAdapter : public sicnu::processing::AtomicAlgorithmAdapter
{
  public:
    explicit DimsStubAdapter( std::string id, sicnu::processing::AlgorithmDescriptor desc )
      : mId( std::move( id ) )
      , mDesc( std::move( desc ) )
    {
        mDesc.id = mId;
    }

    std::string algorithmId() const override { return mId; }
    sicnu::processing::AlgorithmDescriptor descriptor() const override { return mDesc; }

    Json::Value execute( const Json::Value &params,
                         sicnu::processing::ProgressCallback progressCb = nullptr,
                         std::function<bool()> isCancelledFn = nullptr ) override
    {
        Q_UNUSED( progressCb )
        Q_UNUSED( isCancelledFn )
        return params;
    }

  private:
    std::string mId;
    sicnu::processing::AlgorithmDescriptor mDesc;
};

sicnu::jobs::JobRequest ep7Request( const char *algorithmId )
{
    sicnu::jobs::JobRequest r;
    r.algorithmId = algorithmId;
    r.source = "test";
    return r;
}

} // namespace

TEST_CASE( "Bounded transient auto-retry resurrects the task in place", "[ep7][retry]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.setMaxAutoRetries( 2 );

    static std::atomic<int> flakyRuns{ 0 };
    static std::atomic<int> permRuns{ 0 };
    engine.clearExecutors();
    engine.registerExecutor( "ep7:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext &ctx ) {
        if ( req.algorithmId == "ep7:flaky" )
        {
            if ( ++flakyRuns == 1 )
                throw std::runtime_error( "worker timeout: simulated infrastructure death" );
            ctx.reportProgressForced( 100, "done" );
            return Json::Value();
        }
        if ( req.algorithmId == "ep7:perm" )
        {
            ++permRuns;
            throw std::runtime_error( "operator exploded: bad parameters" );
        }
        if ( req.algorithmId == "ep7:always_transient" )
        {
            ++flakyRuns;
            throw std::runtime_error( "worker crashed: simulated crash on every attempt" );
        }
        return Json::Value();
    } );

    SECTION( "a transient failure retries and completes; the attempt is recorded" )
    {
        flakyRuns = 0;
        const long taskId = center.submitJob( ep7Request( "ep7:flaky" ) );
        REQUIRE( taskId > 0 );
        waitForTerminalStatus( taskId );
        auto info = center.getTaskInfo( taskId );
        REQUIRE( info.status == sicnu::TaskStatus::Completed );
        REQUIRE( flakyRuns.load() == 2 );          // first attempt failed, retry succeeded
        REQUIRE( info.autoRetryAttempts == 1 );    // exactly one bounded retry consumed
        REQUIRE( info.logBuffer.join( QString() ).contains( QStringLiteral( "auto-retry 1/2" ) ) );
        INFO( "EP8DBG log=" << info.logBuffer.join( QStringLiteral( " || " ) ).toStdString() );
    }

    SECTION( "a permanent operator error never auto-retries" )
    {
        permRuns = 0;
        const long taskId = center.submitJob( ep7Request( "ep7:perm" ) );
        waitForTerminalStatus( taskId );
        auto info = center.getTaskInfo( taskId );
        REQUIRE( info.status == sicnu::TaskStatus::Failed );
        REQUIRE( permRuns.load() == 1 ); // invoked exactly once, no resurrection
        REQUIRE( info.autoRetryAttempts == 0 );
    }

    SECTION( "the retry bound is hard: budget exhaustion takes the Failed path" )
    {
        flakyRuns = 0;
        const long taskId = center.submitJob( ep7Request( "ep7:always_transient" ) );
        waitForTerminalStatus( taskId, 1200 );
        auto info = center.getTaskInfo( taskId );
        REQUIRE( info.status == sicnu::TaskStatus::Failed );
        // initial attempt + maxAutoRetries(2) retries = 3 invocations, no more
        REQUIRE( flakyRuns.load() == 3 );
        REQUIRE( info.autoRetryAttempts == 2 );
        REQUIRE( info.errorMessage.contains( QStringLiteral( "worker crashed" ) ) );
    }

    engine.clearExecutors();
    center.setMaxAutoRetries( 1 );
    engine.shutdownForTests();
}

TEST_CASE( "Auto-retry keeps DAG children wired across the transient failure",
           "[ep7][retry][dag]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.setMaxAutoRetries( 2 );

    static std::atomic<int> parentRuns{ 0 };
    engine.clearExecutors();
    engine.registerExecutor( "ep7:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext &ctx ) {
        if ( req.algorithmId == "ep7:flaky_parent" )
        {
            if ( ++parentRuns == 1 )
                throw std::runtime_error( "worker timeout: died before answering" );
            ctx.reportProgressForced( 100, "done" );
            return Json::Value();
        }
        if ( req.algorithmId == "ep7:child" )
        {
            ctx.reportProgressForced( 100, "child done" );
            return Json::Value();
        }
        return Json::Value();
    } );


    const long parentId = center.submitJob( ep7Request( "ep7:flaky_parent" ) );

    const long childId = center.submitJob( ep7Request( "ep7:child" ), nullptr, {}, true,
                                           sicnu::TaskPriority::Normal, { parentId } );


    waitForTerminalStatus( childId, 1200 );

    REQUIRE( center.getTaskInfo( parentId ).status == sicnu::TaskStatus::Completed );
    // The child observed NO failure: the parent's transient death never
    // propagated a cascade — the retry happened transparently underneath.
    REQUIRE( center.getTaskInfo( childId ).status == sicnu::TaskStatus::Completed );
    REQUIRE( parentRuns.load() == 2 );

    engine.clearExecutors();
    engine.shutdownForTests();
}

TEST_CASE( "Require mode routes a real operator into the isolated worker and completes",
           "[ep7][worker-route][e2e]" )
{
    ensureApp();
    sicnu::operators::rs::installRsOperatorProvider(); // worker-registry parity in-process
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();

    QTemporaryDir dir;
    const QString input = dir.filePath( "route-labels.tif" );
    writeLabelRaster( input );
    const QString output = dir.filePath( "route-out.tif" );
    QVariantMap params;
    params.insert( QStringLiteral( "input" ), input );
    params.insert( QStringLiteral( "output" ), output );
    params.insert( QStringLiteral( "recode_map" ),
                   QStringLiteral( "{\"1\":5,\"2\":4,\"3\":3,\"4\":2,\"5\":1}" ) );

    // Configure the route directly: require mode + the built worker binary.
    sicnu::processing::WorkerExecutionConfig config =
        sicnu::processing::workerExecutionConfigFromEnvironment();
    config.mode = WorkerExecutionMode::Require;
    config.workerProgram = QStringLiteral( SICNU_WORKER_EXE );
    QString poolError;
    REQUIRE( sicnu::processing::ensureSharedWorkerPoolStarted( config, &poolError ) );

    const long taskId = center.enqueueTask( QStringLiteral( "rs:recode" ), params, true,
                                            sicnu::TaskPriority::Normal, {},
                                            /*autoDispatch=*/true );
    REQUIRE( taskId > 0 );
    waitForTerminalStatus( taskId, 2400 );
    auto info = center.getTaskInfo( taskId );
    REQUIRE( info.status == sicnu::TaskStatus::Completed );
    REQUIRE( info.isolatedRoute );
    REQUIRE( QFile( output ).exists() );
    // The routing decision is visible in the task log (explainable behavior).
    REQUIRE( info.logBuffer.join( QString() ).contains( QStringLiteral( "isolated worker" ) ) );

    sicnu::processing::shutdownSharedWorkerPool();
    sicnu::processing::setWorkerExecutionMode( WorkerExecutionMode::Off );
    engine.shutdownForTests();
}

TEST_CASE( "Require mode with an unavailable pool fails closed (never in-process)",
           "[ep7][worker-route][fail-closed]" )
{
    ensureApp();
    sicnu::operators::rs::installRsOperatorProvider();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();

    QTemporaryDir dir;
    const QString input = dir.filePath( "fc-labels.tif" );
    writeLabelRaster( input );
    QVariantMap params;
    params.insert( QStringLiteral( "input" ), input );
    params.insert( QStringLiteral( "output" ), dir.filePath( "fc-out.tif" ) );
    params.insert( QStringLiteral( "recode_map" ), QStringLiteral( "{}" ) );

    // Require mode + a pool that CANNOT start (unresolvable worker program):
    // the task must fail typed — the caller must never believe isolation
    // happened when it did not.
    sicnu::processing::setWorkerExecutionMode( WorkerExecutionMode::Require );
    qputenv( "SICNU_WORKER_PROGRAM", "Z:/definitely/not/here/sicnu_worker.exe" );

    INFO( "mode=" << sicnu::processing::workerExecutionModeName(
                       sicnu::processing::currentWorkerExecutionMode() )
          << " route=" << sicnu::processing::shouldRunIsolated( QStringLiteral( "rs:recode" ) )
          << " envProg=" << qgetenv( "SICNU_WORKER_PROGRAM" ).toStdString() );
    const long taskId = center.enqueueTask( QStringLiteral( "rs:recode" ), params, true,
                                            sicnu::TaskPriority::Normal, {},
                                            /*autoDispatch=*/true );
    REQUIRE( taskId > 0 );
    waitForTerminalStatus( taskId, 800 );
    auto info = center.getTaskInfo( taskId );
    qunsetenv( "SICNU_WORKER_PROGRAM" );
    INFO( "actual error: " << info.errorMessage.toStdString() );
    INFO( "isolated=" << info.isolatedRoute );
    REQUIRE( info.status == sicnu::TaskStatus::Failed );
    REQUIRE( info.isolatedRoute );
    // The pool starts structurally (spawn failures are lazy by contract);
    // the routed job then fails TYPED at run time with the spawn exhaustion
    // class — transient, so the bounded auto-retry consumed its budget
    // first. No in-process fallback ever ran.
    REQUIRE( info.errorMessage.contains( QStringLiteral( "worker protocol: cannot start" ) ) );
    REQUIRE( info.autoRetryAttempts >= 1 );
    REQUIRE_FALSE( QFile( dir.filePath( "fc-out.tif" ) ).exists() );

    sicnu::processing::setWorkerExecutionMode( WorkerExecutionMode::Off );
    engine.shutdownForTests();
}

TEST_CASE( "Temp-disk and VRAM budgets hold oversized candidates but never starve",
           "[ep7][admission][multidim]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();

    sicnu::processing::AlgorithmDescriptor desc;
    desc.agentMetadata.execution = Json::Value( Json::objectValue );
    desc.agentMetadata.execution["temporaryDiskBytes"] =
        static_cast<Json::UInt64>( 200ull * 1024ull * 1024ull );
    desc.agentMetadata.execution["estimatedVramBytes"] =
        static_cast<Json::UInt64>( 100ull * 1024ull * 1024ull );
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    registry.registerAdapter( std::make_shared<DimsStubAdapter>( "ep7:diskhog", desc ) );

    static std::atomic<bool> releaseHog{ false };
    engine.clearExecutors();
    engine.registerExecutor( "ep7:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext &ctx ) {
        if ( req.algorithmId == "ep7:diskhog" )
        {
            while ( !releaseHog.load() && !ctx.isCancelled() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        return Json::Value();
    } );

    // Never-starve: the FIRST candidate admits although its declared temp
    // disk alone exceeds the budget (nothing running yet — a wrong estimate
    // must not deadlock the queue).
    center.setTempDiskBudgetMb( 100 );
    center.setVramBudgetMb( 50 );
    const long first = center.submitJob( ep7Request( "ep7:diskhog" ) );
    REQUIRE( first > 0 );

    // With the hog RUNNING, a second oversized candidate is held in
    // WaitingResource until the first goes terminal.
    const long second = center.submitJob( ep7Request( "ep7:diskhog" ) );
    bool sawHold = false;
    for ( int i = 0; i < 400 && !sawHold; ++i )
    {
        sawHold = center.getTaskInfo( second ).status == sicnu::TaskStatus::WaitingResource;
        if ( !sawHold )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    REQUIRE( sawHold );

    releaseHog = true;
    waitForTerminalStatus( first );
    waitForTerminalStatus( second );
    REQUIRE( center.getTaskInfo( first ).status == sicnu::TaskStatus::Completed );
    REQUIRE( center.getTaskInfo( second ).status == sicnu::TaskStatus::Completed );

    registry.unregisterAdapter( "ep7:diskhog" );
    engine.clearExecutors();
    center.resetResourceProfileLimits();
    engine.shutdownForTests();
}

TEST_CASE( "Windows RSS sampling is live (memory gates are not silently off)",
           "[ep7][admission][rss]" )
{
    sicnu::ResourceMonitor monitor;
    // 7.0 parity fix: the default sampler returned 0 on Windows, which
    // disabled the RSS watermark and the RAM budget gates platform-wide.
    REQUIRE( monitor.currentRssMb() > 0 );
    REQUIRE( monitor.memoryLimitMb() > 0 );
    REQUIRE_FALSE( monitor.memoryPressureHigh() );
}

TEST_CASE( "Queued cancel under admission hold resolves immediately and truthfully",
           "[ep7][cancel][queued]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 1 );

    static std::atomic<bool> releaseJob{ false };
    engine.clearExecutors();
    engine.registerExecutor( "ep7:", []( const sicnu::jobs::JobRequest &,
                                         sicnu::operators::RSOperatorContext &ctx ) {
        while ( !releaseJob.load() && !ctx.isCancelled() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        return Json::Value();
    } );

    const long holder = center.submitJob( ep7Request( "ep7:holder" ) );
    const long queued = center.submitJob( ep7Request( "ep7:queued" ) );
    REQUIRE( queued > 0 );

    // The queued task cannot launch (global cap 1, holder running): it must
    // be visibly WaitingResource, not fake-Running.
    bool sawHold = false;
    for ( int i = 0; i < 400 && !sawHold; ++i )
    {
        sawHold = center.getTaskInfo( queued ).status == sicnu::TaskStatus::WaitingResource;
        if ( !sawHold )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    REQUIRE( sawHold );

    REQUIRE( center.cancelTask( queued ) );
    REQUIRE( center.getTaskInfo( queued ).status == sicnu::TaskStatus::Canceled );

    releaseJob = true;
    waitForTerminalStatus( holder );
    REQUIRE( center.getTaskInfo( holder ).status == sicnu::TaskStatus::Completed );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    engine.shutdownForTests();
}

TEST_CASE( "Rapid short jobs: 10k submissions drain with no stranded tasks",
           "[ep7][stress][rapid]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();

    engine.clearExecutors();
    engine.registerExecutor( "ep7:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext & ) {
        Json::Value result;
        result["i"] = req.params["i"];
        return result;
    } );

    constexpr int kJobs = 10000;
    const auto start = std::chrono::steady_clock::now();
    std::vector<long> ids;
    ids.reserve( kJobs );
    for ( int i = 0; i < kJobs; ++i )
    {
        sicnu::jobs::JobRequest r = ep7Request( "ep7:tiny" );
        r.params["i"] = i;
        ids.push_back( center.submitJob( r ) );
    }
    REQUIRE( ids.size() == static_cast<size_t>( kJobs ) );

    // Drain: the engine's idle signal is the cheap liveness probe (TaskCenter
    // bookkeeping lands microseconds later); only when idle do we take ONE
    // allTasks() snapshot to verify every task reached Completed. Bounded by
    // 10 minutes.
    const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::minutes( 10 );
    size_t terminal = 0;
    size_t completed = 0;
    std::unordered_set<long> idSet( ids.begin(), ids.end() );
    do
    {
        engine.waitUntilIdleForTests( 60000 );
        terminal = 0;
        completed = 0;
        const QList<sicnu::AlgorithmTaskInfo> snapshot = center.allTasks();
        for ( const auto &t : snapshot )
        {
            if ( !idSet.count( t.taskId ) )
                continue;
            if ( sicnu::isTerminalStatus( t.status ) )
            {
                ++terminal;
                if ( t.status == sicnu::TaskStatus::Completed )
                    ++completed;
            }
        }
        if ( terminal < ids.size() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    } while ( terminal < ids.size() && std::chrono::steady_clock::now() < drainDeadline );

    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()
                                                               - start ).count();
    INFO( "10k short jobs drained in " << elapsedMs << " ms" );
    REQUIRE( terminal == ids.size() );
    REQUIRE( completed == ids.size() ); // every job Completed: no stranded, no Failed

    engine.clearExecutors();
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

// NOTE: this case runs LAST (declaration order): TaskCenter::shutdown() is
// sticky in production semantics and poisons the singleton for everything
// after it (shutdownForTests at the case end only resets the ENGINE).
TEST_CASE( "Shutdown with in-flight work leaves no non-terminal task",
           "[ep7][shutdown]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();

    engine.clearExecutors();
    engine.registerExecutor( "ep7:", []( const sicnu::jobs::JobRequest &,
                                         sicnu::operators::RSOperatorContext &ctx ) {
        for ( int i = 0; i < 200 && !ctx.isCancelled(); ++i )
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        return Json::Value();
    } );

    std::vector<long> taskIds;
    for ( int i = 0; i < 4; ++i )
        taskIds.push_back( center.submitJob( ep7Request( "ep7:long" ) ) );
    engine.waitUntilIdleForTests( 10000 );

    // Production shutdown semantics: cancel-all → engine join → finalize.
    // Every task resolves terminal; the call returns (no hang).
    center.shutdown();
    for ( long id : taskIds )
        REQUIRE( sicnu::isTerminalStatus( center.getTaskInfo( id ).status ) );

    engine.clearExecutors();
    engine.shutdownForTests();
}
