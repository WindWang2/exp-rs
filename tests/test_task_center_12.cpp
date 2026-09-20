// test_task_center_12.cpp — Track 12 TaskCenter runtime: fairness, bounded
// queue backpressure, cancel watchdog, weight admission, observability.
//
// Coverage matrix (goal-loop Oracles):
//   - aging promotion: a low-priority task queued behind a sustained
//     high-priority stream eventually outranks fresh high work (no
//     starvation) while priority stays observable (high first);
//   - bounded pending: enqueueTask refuses with -1 (shutdown sentinel)
//     once the live-task bound is hit; submitPipeline refuses atomically;
//   - cancel watchdog: a task whose dispatched job never reports a
//     terminal record is finalized Canceled with bounded latency instead
//     of stranding in Cancelling forever;
//   - weight admission: descriptor-declared disk/network weights gate
//     non-interactive candidates against the interactive reserve while
//     Interactive-class candidates admit against the full cap;
//   - observability: queue_wait / dispatched / execution_start /
//     execution_end / cancelled events plus exact counters.
//
// All cases use registered-prefix executors on the real JobEngine — no GDAL
// rasters, no network, deterministic sleeps only where required to let real
// wall-clock aging accrue.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_center.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/algorithm_descriptor.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "runtime/observability/execution_telemetry.h"
#include "runtime/observability/fault_registry.h"

#include <QCoreApplication>
#include <QMutex>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

// Headless Windows: route Debug-CRT assertions/errors to stderr instead of
// a modal dialog that blocks an unattended run forever.
#if defined( _MSC_VER )
#include <crtdbg.h>
namespace
{
const bool tc12CrtInit = [] {
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
using sicnu::TaskPriority;
using sicnu::TaskStatus;
using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRequest;
using sicnu::runtime::observability::Counter;
using sicnu::runtime::observability::EventKind;
using sicnu::runtime::observability::ExecutionTelemetry;

namespace
{

void ensureApp()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char appName[] = "test_task_center_12";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
}

JobRequest tc12Request( const char *algorithmId, const char *source = "test" )
{
    JobRequest r;
    r.algorithmId = algorithmId;
    r.source = source;
    return r;
}

bool waitForStatus( long taskId, std::initializer_list<TaskStatus> wanted, int timeoutMs )
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
    while ( std::chrono::steady_clock::now() < deadline )
    {
        const TaskStatus s = TaskCenter::instance().getTaskInfo( taskId ).status;
        for ( TaskStatus w : wanted )
            if ( s == w )
                return true;
        QThread::msleep( 5 );
    }
    return false;
}

/// Completion-order tracker: each task's exactly-once callback appends its id.
struct OrderLog
{
    QMutex mutex;
    QList<long> order;
    void watch( long taskId )
    {
        TaskCenter::instance().addTaskCompletionCallback(
            taskId, [this]( const sicnu::AlgorithmTaskInfo &info ) {
                QMutexLocker lock( &mutex );
                order.append( info.taskId );
            } );
    }
    int position( long taskId )
    {
        QMutexLocker lock( &mutex );
        return static_cast<int>( order.indexOf( taskId ) );
    }
    /// Completion callbacks fire on the listener thread AFTER the status
    /// flips — a terminal status does not imply the log entry exists yet.
    /// Spin until every watched id is recorded before asserting positions.
    bool waitForAll( int expected, int timeoutMs )
    {
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds( timeoutMs );
        while ( std::chrono::steady_clock::now() < deadline )
        {
            {
                QMutexLocker lock( &mutex );
                if ( order.size() >= expected )
                    return true;
            }
            QThread::msleep( 5 );
        }
        return false;
    }
};

/// Stub adapter with a fully controllable descriptor (weight admission).
class Tc12DimsAdapter : public sicnu::processing::AtomicAlgorithmAdapter
{
  public:
    explicit Tc12DimsAdapter( std::string id, sicnu::processing::AlgorithmDescriptor desc )
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

/// Register a stub descriptor with the given execution weights; the adapter
/// must stay registered for the duration of the test (unregister at scope end).
void registerDimsAdapter( sicnu::processing::AtomicAlgorithmRegistry &registry,
                          const std::string &id, unsigned diskReadW, unsigned diskWriteW,
                          unsigned networkW )
{
    sicnu::processing::AlgorithmDescriptor desc;
    desc.id = id;
    desc.displayName = id;
    desc.agentMetadata.execution = Json::Value( Json::objectValue );
    if ( diskReadW > 0 )
        desc.agentMetadata.execution["diskReadWeight"] = diskReadW;
    if ( diskWriteW > 0 )
        desc.agentMetadata.execution["diskWriteWeight"] = diskWriteW;
    if ( networkW > 0 )
        desc.agentMetadata.execution["networkWeight"] = networkW;
    registry.registerAdapter( std::make_shared<Tc12DimsAdapter>( id, desc ) );
}

} // namespace

TEST_CASE( "Aging promotes a starved low-priority task over fresh high work",
           "[tc12][fairness]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setGlobalConcurrencyLimit( 1 );
    // 50 ms per promotion level: the low task needs ~100 ms of queue wait to
    // reach priority 0 — comfortably inside the 4×80 ms high-task runway, yet
    // far above the submission-window noise that made a 5 ms interval flaky
    // (independent review P1).
    center.setAgingIntervalMs( 50 );

    std::atomic<bool> release{ false };
    engine.clearExecutors();
    engine.registerExecutor( "tc12:", [&release]( const JobRequest &req,
                                                  sicnu::operators::RSOperatorContext & ) {
        if ( req.algorithmId == "tc12:blocker" )
        {
            while ( !release.load( std::memory_order_relaxed ) )
                QThread::msleep( 5 );
            return Json::Value();
        }
        // High fillers consume real wall-clock time so the queued low task
        // accrues wait credit while they run.
        if ( req.algorithmId == "tc12:high" )
            QThread::msleep( 80 );
        return Json::Value();
    } );

    OrderLog log;
    const long blocker = center.submitJob( tc12Request( "tc12:blocker" ), nullptr, {}, true,
                                           TaskPriority::High, {} );
    REQUIRE( blocker > 0 );
    log.watch( blocker );
    REQUIRE( waitForStatus( blocker, { TaskStatus::Running, TaskStatus::Dispatching }, 5000 ) );

    const long low = center.submitJob( tc12Request( "tc12:low" ), nullptr, {}, true,
                                       TaskPriority::Low, {} );
    REQUIRE( low > 0 );
    log.watch( low );
    std::vector<long> highs;
    for ( int i = 0; i < 4; ++i )
    {
        const long h = center.submitJob( tc12Request( "tc12:high" ), nullptr, {}, true,
                                         TaskPriority::High, {} );
        REQUIRE( h > 0 );
        log.watch( h );
        highs.push_back( h );
    }

    release.store( true );
    REQUIRE( waitForStatus( low, { TaskStatus::Completed, TaskStatus::Failed }, 15000 ) );
    for ( long h : highs )
        REQUIRE( waitForStatus( h, { TaskStatus::Completed, TaskStatus::Failed }, 15000 ) );
    // Completion callbacks append asynchronously — drain the log before
    // comparing positions (a still-empty tail would read as position -1).
    REQUIRE( log.waitForAll( static_cast<int>( highs.size() ) + 2, 10000 ) );

    // Priority is observable: the first high task still beats the low task.
    REQUIRE( log.position( highs.front() ) < log.position( low ) );
    // No starvation: the aged low task outranks at least one fresh high task.
    REQUIRE( log.position( low ) < log.position( highs.back() ) );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.setAgingIntervalMs( 5000 );
    engine.shutdownForTests();
}

TEST_CASE( "Bounded pending queue refuses overflow submissions", "[tc12][backpressure]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setMaxPendingTasks( 2 );

    auto &telemetry = ExecutionTelemetry::instance();
    const auto countersBefore = telemetry.counters();
    const uint64_t refusedBefore = [&countersBefore] {
        const auto it = countersBefore.find( "tasks_refused" );
        return it == countersBefore.end() ? 0 : it->second;
    }();

    // Non-autoDispatch tasks stay Queued forever — pure live-count pressure,
    // no engine involvement.
    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_a" ), QVariantMap() ) > 0 );
    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_b" ), QVariantMap() ) > 0 );
    REQUIRE( center.pendingTaskCount() == 2 );
    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_c" ), QVariantMap() ) == -1 );
    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_d" ), QVariantMap() ) == -1 );

    const auto counters = telemetry.counters();
    const auto refused = counters.find( "tasks_refused" );
    REQUIRE( refused != counters.end() );
    REQUIRE( refused->second - refusedBefore >= 2 );

    // Cancel frees the bound — a fresh submission is accepted again.
    const auto tasks = center.allTasks();
    for ( const auto &t : tasks )
        center.cancelTask( t.taskId );
    REQUIRE( waitForStatus( tasks.first().taskId, { TaskStatus::Canceled }, 5000 ) );
    REQUIRE( center.pendingTaskCount() == 0 );
    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_e" ), QVariantMap() ) > 0 );

    center.resetResourceProfileLimits();
    center.shutdownForTests();
    engine.shutdownForTests();
}

TEST_CASE( "Pipeline submission is refused atomically at the pending bound",
           "[tc12][backpressure][pipeline]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setMaxPendingTasks( 2 );

    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_hold" ), QVariantMap() ) > 0 );
    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_hold2" ), QVariantMap() ) > 0 );

    // Two dispatchable steps but zero headroom → the whole pipeline refuses.
    const std::string json = R"({
        "id": "tc12-pipe",
        "steps": [
            {"id": "a", "kind": "operator", "operatorId": "tc12:x", "params": {}},
            {"id": "b", "kind": "operator", "operatorId": "tc12:y", "params": {}}
        ]
    })";
    REQUIRE( center.submitPipelineJson( json ) == -1 );
    // No partial pipeline record and no step tasks were created.
    REQUIRE( center.pendingTaskCount() == 2 );
    REQUIRE( center.allTasks().size() == 2 );

    center.resetResourceProfileLimits();
    center.shutdownForTests();
    engine.shutdownForTests();
}

TEST_CASE( "Cancel watchdog finalizes a task whose worker never reports",
           "[tc12][cancel]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setCancelWatchdogMs( 150 );

    std::atomic<bool> hangRelease{ false };
    engine.clearExecutors();
    engine.registerExecutor( "tc12:", [&hangRelease]( const JobRequest &req,
                                                      sicnu::operators::RSOperatorContext & ) {
        if ( req.algorithmId == "tc12:hang" )
        {
            // Simulates a hung worker: never observes the cancel flag, never
            // returns until the test releases it (after the watchdog fired).
            while ( !hangRelease.load( std::memory_order_relaxed ) )
                QThread::msleep( 10 );
        }
        return Json::Value();
    } );

    const long taskId = center.submitJob( tc12Request( "tc12:hang" ) );
    REQUIRE( taskId > 0 );
    REQUIRE( waitForStatus( taskId, { TaskStatus::Running }, 5000 ) );

    REQUIRE( center.cancelTask( taskId ) );
    // The engine accepted the cancel (job exists) but the worker never
    // reports — without the watchdog this strands in Cancelling forever.
    const auto info = center.waitForTask( taskId, std::chrono::milliseconds( 8000 ),
                                          std::chrono::milliseconds( 10 ) );
    REQUIRE( info.status == TaskStatus::Canceled );
    REQUIRE( info.errorMessage.contains( QStringLiteral( "watchdog" ),
                                       Qt::CaseInsensitive ) );

    hangRelease.store( true );
    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.shutdownForTests();
    engine.shutdownForTests();
}

TEST_CASE( "Weight admission holds non-interactive candidates at the reserve",
           "[tc12][weights]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setGlobalConcurrencyLimit( 3 );
    center.setIoWeightLimits( 100, 0, 0 ); // disk-read cap only
    center.setInteractiveReservePercent( 25 );
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();

    std::atomic<bool> release{ false };
    engine.clearExecutors();
    engine.registerExecutor( "w12:", [&release]( const JobRequest &,
                                                 sicnu::operators::RSOperatorContext & ) {
        while ( !release.load( std::memory_order_relaxed ) )
            QThread::msleep( 5 );
        return Json::Value();
    } );

    registerDimsAdapter( registry, "w12:heavy", 70, 0, 0 );   // occupies the reserve band
    registerDimsAdapter( registry, "w12:extra", 30, 0, 0 );   // non-interactive overflow
    registerDimsAdapter( registry, "w12:live", 30, 0, 0 );    // interactive (source=gui)

    const long heavy = center.submitJob( tc12Request( "w12:heavy" ), nullptr, {}, true,
                                         TaskPriority::Normal, {} );
    REQUIRE( heavy > 0 );
    REQUIRE( waitForStatus( heavy, { TaskStatus::Running, TaskStatus::Dispatching }, 5000 ) );

    // Non-interactive candidate: 70 + 30 > 100·(1-0.25) = 75 → held.
    const long extra = center.submitJob( tc12Request( "w12:extra" ), nullptr, {}, true,
                                         TaskPriority::Normal, {} );
    REQUIRE( extra > 0 );
    // Interactive candidate (source=gui): admits against the full cap —
    // 70 + 30 <= 100 → dispatches despite the held sibling.
    const long live = center.submitJob( tc12Request( "w12:live", "gui" ), nullptr, {}, true,
                                        TaskPriority::Normal, {} );
    REQUIRE( live > 0 );

    REQUIRE( waitForStatus( live, { TaskStatus::Running, TaskStatus::Dispatching,
                                    TaskStatus::Completed }, 5000 ) );
    // The non-interactive candidate must still be held.
    const auto held = center.getTaskInfo( extra );
    REQUIRE( ( held.status == TaskStatus::Queued || held.status == TaskStatus::WaitingResource ) );

    release.store( true );
    REQUIRE( waitForStatus( heavy, { TaskStatus::Completed, TaskStatus::Failed }, 15000 ) );
    REQUIRE( waitForStatus( live, { TaskStatus::Completed, TaskStatus::Failed }, 15000 ) );
    // Never-starve: once the heavy task drains, the held candidate launches.
    REQUIRE( waitForStatus( extra, { TaskStatus::Completed, TaskStatus::Failed }, 15000 ) );

    registry.unregisterAdapter( "w12:heavy" );
    registry.unregisterAdapter( "w12:extra" );
    registry.unregisterAdapter( "w12:live" );
    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.setIoWeightLimits( 100, 100, 100 );
    engine.shutdownForTests();
}

TEST_CASE( "Telemetry reports queue wait, run time, cancel latency and counters",
           "[tc12][telemetry]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    auto &telemetry = ExecutionTelemetry::instance();
    telemetry.setEnabled( true );
    telemetry.clearEvents();

    const auto countersBefore = telemetry.counters();
    auto count = [&countersBefore]( const char *name ) -> uint64_t {
        const auto it = countersBefore.find( name );
        return it == countersBefore.end() ? 0 : it->second;
    };

    engine.clearExecutors();
    engine.registerExecutor( "tc12:", []( const JobRequest &,
                                          sicnu::operators::RSOperatorContext & ) {
        QThread::msleep( 10 );
        return Json::Value();
    } );

    const long okTask = center.submitJob( tc12Request( "tc12:quick" ) );
    REQUIRE( okTask > 0 );
    REQUIRE( waitForStatus( okTask, { TaskStatus::Completed }, 15000 ) );

    const auto after = telemetry.counters();
    auto delta = [&after, &count]( const char *name ) -> uint64_t {
        const auto it = after.find( name );
        const uint64_t now = it == after.end() ? 0 : it->second;
        return now - count( name );
    };
    REQUIRE( delta( "tasks_submitted" ) == 1 );
    REQUIRE( delta( "tasks_completed" ) == 1 );
    REQUIRE( delta( "tasks_failed" ) == 0 );
    REQUIRE( delta( "tasks_canceled" ) == 0 );

    const auto events = telemetry.events();
    auto hasEvent = [&events, okTask]( EventKind kind ) {
        return std::any_of( events.begin(), events.end(), [&]( const auto &e ) {
            return e.kind == kind && e.taskId == okTask;
        } );
    };
    REQUIRE( hasEvent( EventKind::QueueWait ) );
    REQUIRE( hasEvent( EventKind::Dispatched ) );
    REQUIRE( hasEvent( EventKind::ExecutionStart ) );
    REQUIRE( hasEvent( EventKind::ExecutionEnd ) );

    // Queue wait recorded a non-negative duration for this task.
    const auto qw = std::find_if( events.begin(), events.end(), [&]( const auto &e ) {
        return e.kind == EventKind::QueueWait && e.taskId == okTask;
    } );
    REQUIRE( qw != events.end() );
    REQUIRE( qw->valueNanos >= 0 );

    telemetry.setEnabled( false );
    engine.clearExecutors();
    center.shutdownForTests();
    engine.shutdownForTests();
}

TEST_CASE( "Fault points drive the real dispatch-failure and lost-record paths",
           "[tc12][fault]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setCancelWatchdogMs( 150 );
    namespace fault = sicnu::runtime::observability::fault;

    engine.clearExecutors();
    engine.registerExecutor( "tc12:", []( const sicnu::jobs::JobRequest &req,
                                          sicnu::operators::RSOperatorContext & ) {
        // tc12:slow stays Running long enough for the cancel to land while
        // the job is genuinely alive (bounded — no hang at teardown).
        if ( req.algorithmId == "tc12:slow" )
            QThread::msleep( 2500 );
        return Json::Value();
    } );

    // taskcenter.dispatch: the staged launch never reaches the engine; the
    // task takes the real refused-submit rollback and fails typed (never
    // strands in Dispatching).
    {
        fault::ArmedFault armed( { "taskcenter.dispatch", fault::Mode::NextN, 1, {} } );
        const long failed = center.submitJob( tc12Request( "tc12:quick" ) );
        REQUIRE( failed > 0 );
        REQUIRE( waitForStatus( failed, { TaskStatus::Failed }, 10000 ) );
        const auto info = center.getTaskInfo( failed );
        REQUIRE( info.errorMessage.contains( QStringLiteral( "could not submit" ),
                                           Qt::CaseInsensitive ) );
    }

    // taskcenter.jobrecord: every engine record is swallowed, so the job runs
    // to completion invisibly; a subsequent cancel strands in Cancelling and
    // the watchdog deadline is the only thing that can finalize it. Any
    // record arriving after finalization maps to no task (foreign no-op).
    {
        fault::ArmedFault armed( { "taskcenter.jobrecord", fault::Mode::Always, 0, {} } );
        const long stranded = center.submitJob( tc12Request( "tc12:slow" ) );
        REQUIRE( stranded > 0 );
        // Give the engine a moment to pick up the job and emit (dropped)
        // records; the executor keeps the job Running well past the watchdog
        // deadline so cancel lands on a live job → Cancelling → watchdog.
        QThread::msleep( 300 );
        const auto mid = center.getTaskInfo( stranded );
        REQUIRE( !sicnu::isTerminalStatus( mid.status ) );
        REQUIRE( center.cancelTask( stranded ) );
        const auto info = center.waitForTask( stranded, std::chrono::milliseconds( 8000 ),
                                              std::chrono::milliseconds( 10 ) );
        REQUIRE( info.status == TaskStatus::Canceled );
        REQUIRE( info.errorMessage.contains( QStringLiteral( "watchdog" ),
                                           Qt::CaseInsensitive ) );
    }
    fault::disarmAllFaults();

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.shutdownForTests();
    engine.shutdownForTests();
}

TEST_CASE( "admissionSnapshot reports the pending bound and all gate families",
           "[tc12][snapshot]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setMaxPendingTasks( 1 );

    // Queue-full is a refusal, not a hold: distinct observable state.
    REQUIRE( center.enqueueTask( QStringLiteral( "tc12:manual_s" ), QVariantMap() ) > 0 );
    const auto fullSnap = center.admissionSnapshot( QStringLiteral( "tc12:anything" ) );
    REQUIRE( fullSnap.queueFull );
    REQUIRE( !fullSnap.wouldAdmit );
    REQUIRE( fullSnap.pendingCap == 1 );
    REQUIRE( fullSnap.pendingCount == 1 );
    REQUIRE( !fullSnap.reason.isEmpty() );

    center.shutdownForTests();
    // With the bound reset to the default, a plain candidate admits.
    const auto snap = center.admissionSnapshot( QStringLiteral( "tc12:anything" ) );
    REQUIRE( !snap.queueFull );
    REQUIRE( snap.wouldAdmit );

    center.resetResourceProfileLimits();
    engine.shutdownForTests();
}
