// test_execution_plane_8.cpp — Execution Plane 8.0: admission scaling,
// worker containment, retry evidence, resume identity, cache identity.
//
// WP-A coverage (this file's first milestone):
//   - priority launch order through the indexed ready heap (1-slot control);
//   - cancel of a queued/admission-held task leaves no stranded work and no
//     stale heap launch (lazy invalidation);
//   - DAG chains drain in dependency order (parent promotion);
//   - transient auto-retry re-enters admission through the heap (no
//     stranding, gates re-engage);
//   - JobEngine exclusive drain order + priority pick with the bucketed
//     queue;
//   - short-job scaling: 2k vs 10k drain ratio stays ~linear (the pre-8.0
//     admission rescan was quadratic; ratio bound asserted generously),
//     absolute bounded runtime, no stranded tasks.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/task_center.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "data/execution_fingerprint.h"
#include "data/execution_identity_resolver.h"
#include "data/artifact_object_pool.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"
#include "processing/framework/local_worker_host.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#ifndef SICNU_WORKER_EXE
#define SICNU_WORKER_EXE "sicnu_worker"
#endif

#ifdef Q_OS_UNIX
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#endif

using sicnu::TaskCenter;
using namespace sicnu::workflow;

namespace
{

void ensureApp()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char appName[] = "test_execution_plane_8";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
}

void waitForTerminalStatus( long taskId, int attempts = 1200, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( sicnu::isTerminalStatus( TaskCenter::instance().getTaskInfo( taskId ).status ) )
            return;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
}

bool waitForCondition( const std::function<bool()> &predicate, int attempts = 1200, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( predicate() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
    return predicate();
}

sicnu::jobs::JobRequest ep8Request( const char *algorithmId )
{
    sicnu::jobs::JobRequest r;
    r.algorithmId = algorithmId;
    r.source = "test";
    return r;
}

/// Test executor registry id + registration helper.
constexpr const char *kStubAlgo = "ep8:stub";

/// The engine clamps pool sizes to JobEngine::kMinWorkers (2) — the smallest
/// deterministic pool this suite can request.
constexpr int kMinWorkersForTest = 2;

struct StartOrderRecorder
{
    std::mutex mutex;
    std::vector<int> order;
    void push( int value )
    {
        std::lock_guard<std::mutex> lock( mutex );
        order.push_back( value );
    }
    std::vector<int> snapshot()
    {
        std::lock_guard<std::mutex> lock( mutex );
        return order;
    }
};

} // namespace

TEST_CASE( "Priority order holds through the ready heap under a single slot",
           "[ep8][admission][priority]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );

    engine.clearExecutors();
    static StartOrderRecorder recorder;
    recorder.order.clear();
    static std::atomic<bool> releaseGate{ false };
    releaseGate.store( false );

    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext & ) {
        recorder.push( std::atoi( req.params["i"].asString().c_str() ) );
        // The first job is the slot holder: park until released so the
        // remaining tasks queue behind admission.
        while ( !releaseGate.load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        return Json::Value();
    } );

    center.setGlobalConcurrencyLimit( 1 );

    // Holder occupies the only slot (Normal priority, id smaller than the rest).
    const long holder = center.submitJob( ep8Request( "ep8:holder" ) );
    REQUIRE( holder > 0 );
    REQUIRE( waitForCondition( [&] {
        return center.getTaskInfo( holder ).status == sicnu::TaskStatus::Running;
    } ) );

    // Queue order of submission: LOW first, then NORMAL, then HIGH. With the
    // single slot busy, all three sit in the ready heap; on release they must
    // launch in priority order (HIGH, NORMAL, LOW) — the heap reproduces the
    // old full sort's (priority, taskId) order for fresh candidates.
    sicnu::jobs::JobRequest lowReq = ep8Request( "ep8:low" );
    lowReq.params["i"] = 1;
    const long low = center.submitJob( lowReq, {}, {}, true, sicnu::TaskPriority::Low );
    sicnu::jobs::JobRequest normalReq = ep8Request( "ep8:normal" );
    normalReq.params["i"] = 2;
    const long normal = center.submitJob( normalReq, {}, {}, true, sicnu::TaskPriority::Normal );
    sicnu::jobs::JobRequest highReq = ep8Request( "ep8:high" );
    highReq.params["i"] = 3;
    const long high = center.submitJob( highReq, {}, {}, true, sicnu::TaskPriority::High );
    REQUIRE( low > 0 );
    REQUIRE( normal > 0 );
    REQUIRE( high > 0 );

    // All three held for admission (one slot busy). The head candidate is
    // surfaced as WaitingResource; deep candidates stay queued.
    REQUIRE( waitForCondition( [&] {
        const auto st = center.getTaskInfo( high ).status;
        return st == sicnu::TaskStatus::WaitingResource
               || st == sicnu::TaskStatus::Dispatching;
    } ) );

    releaseGate.store( true );
    waitForTerminalStatus( holder );
    waitForTerminalStatus( normal );
    waitForTerminalStatus( high );
    waitForTerminalStatus( low );

    const auto order = recorder.snapshot();
    REQUIRE( order.size() == 4 );
    // holder (i=0) starts first; after release: HIGH(3), NORMAL(2), LOW(1).
    REQUIRE( order[0] == 0 );
    REQUIRE( order[1] == 3 );
    REQUIRE( order[2] == 2 );
    REQUIRE( order[3] == 1 );

    engine.clearExecutors();
    center.resetResourceProfileLimits(); // restores the global limit too
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

TEST_CASE( "Cancelling an admission-held task leaves no stranded work",
           "[ep8][admission][cancel]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );

    engine.clearExecutors();
    static std::atomic<bool> releaseGate{ false };
    releaseGate.store( false );
    static std::atomic<int> runs{ 0 };

    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &,
                                         sicnu::operators::RSOperatorContext & ) {
        ++runs;
        while ( !releaseGate.load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        return Json::Value();
    } );

    center.setGlobalConcurrencyLimit( 1 );

    const long holder = center.submitJob( ep8Request( "ep8:holder" ) );
    REQUIRE( holder > 0 );
    REQUIRE( waitForCondition( [&] {
        return center.getTaskInfo( holder ).status == sicnu::TaskStatus::Running;
    } ) );

    // Two candidates queue on the single slot; cancel the FIRST one while
    // it sits in the ready heap (its heap entry must be lazily invalidated —
    // a stale launch would run a canceled job). With the slot busy, the
    // heap HEAD (the first candidate) surfaces as WaitingResource via the
    // global-hold parity flip; the deeper candidate stays Queued (truthful:
    // a bounded pass never examined it).
    const long canceled = center.submitJob( ep8Request( "ep8:canceled" ) );
    const long survivor = center.submitJob( ep8Request( "ep8:survivor" ) );
    REQUIRE( canceled > 0 );
    REQUIRE( survivor > 0 );
    REQUIRE( waitForCondition( [&] {
        return center.getTaskInfo( canceled ).status == sicnu::TaskStatus::WaitingResource;
    } ) );
    CHECK( sicnu::isTerminalStatus( center.getTaskInfo( survivor ).status ) == false );

    REQUIRE( center.cancelTask( canceled ) );
    REQUIRE( center.getTaskInfo( canceled ).status == sicnu::TaskStatus::Canceled );

    releaseGate.store( true );
    waitForTerminalStatus( holder );
    waitForTerminalStatus( survivor );

    // The canceled task never ran (holder + survivor only).
    CHECK( runs.load() == 2 );
    CHECK( center.getTaskInfo( survivor ).status == sicnu::TaskStatus::Completed );

    engine.clearExecutors();
    center.resetResourceProfileLimits(); // restores the global limit too
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

TEST_CASE( "DAG chains drain in dependency order through parent promotion",
           "[ep8][admission][dag]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );

    engine.clearExecutors();
    static StartOrderRecorder recorder;
    recorder.order.clear();
    static std::atomic<bool> releaseGate{ false };
    releaseGate.store( false );

    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext & ) {
        recorder.push( std::atoi( req.params["i"].asString().c_str() ) );
        // Only the ROOT parks; children complete immediately once launched.
        if ( std::atoi( req.params["i"].asString().c_str() ) == 0 )
        {
            while ( !releaseGate.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        return Json::Value();
    } );

    center.setGlobalConcurrencyLimit( 8 );

    // Chain of 4: root parks until released; each child must not launch
    // before its parent completes.
    // The root's request carries no "i" param: the executor records 0 and
    // parks (that is the park key below).
    long previous = center.submitJob( ep8Request( "ep8:root" ) );
    REQUIRE( previous > 0 );
    QList<long> parents{ previous };
    for ( int i = 1; i < 4; ++i )
    {
        sicnu::jobs::JobRequest req = ep8Request( "ep8:child" );
        req.params["i"] = i;
        const long child = center.submitJob( req, {}, {}, true,
                                             sicnu::TaskPriority::Normal, parents );
        REQUIRE( child > 0 );
        parents = { child };
    }

    // Let the chain drain fully.
    releaseGate.store( true );
    waitForTerminalStatus( parents.front() );

    const auto order = recorder.snapshot();
    REQUIRE( order.size() == 4 );
    // Dependency order: 0 before 1 before 2 before 3.
    for ( int i = 0; i < 3; ++i )
    {
        const auto posOf = [&]( int v ) {
            return std::find( order.begin(), order.end(), v ) - order.begin();
        };
        REQUIRE( posOf( i ) < posOf( i + 1 ) );
    }

    engine.clearExecutors();
    center.resetResourceProfileLimits(); // restores the global limit too
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

TEST_CASE( "Transient auto-retry re-enters admission through the ready heap",
           "[ep8][retry][admission]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 1 );

    engine.clearExecutors();
    static std::atomic<int> flakyRuns{ 0 };
    flakyRuns.store( 0 );

    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &,
                                         sicnu::operators::RSOperatorContext & ) {
        // First attempt dies with a TRANSIENT infrastructure class; the
        // retry completes. The task must be resurrected through the heap
        // (its old heap entries were consumed at staging) and re-dispatch.
        if ( flakyRuns.fetch_add( 1 ) == 0 )
            throw std::runtime_error( "worker crashed: injected for test" );
        return Json::Value();
    } );

    const long task = center.submitJob( ep8Request( "ep8:flaky" ) );
    REQUIRE( task > 0 );
    waitForTerminalStatus( task );

    CHECK( center.getTaskInfo( task ).status == sicnu::TaskStatus::Completed );
    CHECK( flakyRuns.load() == 2 ); // one crash + one successful retry

    engine.clearExecutors();
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

TEST_CASE( "Exhausted retry budget records evidence and fails permanently",
           "[ep8][retry][evidence]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 1 );

    engine.clearExecutors();
    static std::atomic<int> runs{ 0 };
    runs.store( 0 );
    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &,
                                         sicnu::operators::RSOperatorContext & )
                                 -> Json::Value {
        // Transient-class failure EVERY time: attempt 1 → auto-retry →
        // attempt 2 → budget exhausted → permanent Failed.
        ++runs;
        throw std::runtime_error( "worker crashed: always transient" );
    } );

    const long task = center.submitJob( ep8Request( "ep8:always-flaky" ) );
    REQUIRE( task > 0 );
    waitForTerminalStatus( task );

    const auto info = center.getTaskInfo( task );
    CHECK( info.status == sicnu::TaskStatus::Failed );
    CHECK( info.autoRetryAttempts == 1 );
    CHECK( runs.load() == 2 );
    // WP-D evidence: the record must say WHY the task was retried and why it
    // stopped being retried.
    bool sawRetry = false;
    bool sawExhausted = false;
    for ( const QString &line : info.logBuffer )
    {
        if ( line.contains( QStringLiteral( "auto-retry 1/1" ) ) )
            sawRetry = true;
        if ( line.contains( QStringLiteral( "Auto-retry budget exhausted" ) ) )
            sawExhausted = true;
    }
    CHECK( sawRetry );
    CHECK( sawExhausted );

    engine.clearExecutors();
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

TEST_CASE( "JobEngine bucketed queue: priority pick and exclusive drain order",
           "[ep8][engine][queue]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    const int defaultWorkers = engine.maxWorkers();
    // The engine clamps pool sizes to kMinWorkers (= 2), so the smallest
    // observable pool is two workers: BOTH are kept busy by park jobs, which
    // makes every subsequent pick deterministic.
    engine.setMaxWorkers( kMinWorkersForTest );

    engine.clearExecutors();
    static StartOrderRecorder recorder;
    recorder.order.clear();
    static std::atomic<bool> releaseGate{ false };
    releaseGate.store( false );

    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext & ) {
        recorder.push( std::atoi( req.params["i"].asString().c_str() ) );
        if ( req.params["park"].asBool() )
        {
            while ( !releaseGate.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        return Json::Value();
    } );

    // Two park jobs occupy the whole pool; the test continues only when BOTH
    // are running (their executors recorded the starts).
    sicnu::jobs::JobRequest parkA = ep8Request( "ep8:parkA" );
    parkA.params["i"] = 0;
    parkA.params["park"] = true;
    const std::string parkAId = engine.submit( parkA );
    sicnu::jobs::JobRequest parkB = ep8Request( "ep8:parkB" );
    parkB.params["i"] = 6;
    parkB.params["park"] = true;
    const std::string parkBId = engine.submit( parkB );
    REQUIRE( waitForCondition( [&] { return recorder.snapshot().size() == 2; } ) );

    sicnu::jobs::JobRequest lowA = ep8Request( "ep8:lowA" );
    lowA.priority = 2;
    lowA.params["i"] = 1;
    const std::string lowAId = engine.submit( lowA );

    sicnu::jobs::JobRequest normalB = ep8Request( "ep8:normalB" );
    normalB.priority = 1;
    normalB.params["i"] = 2;
    const std::string normalBId = engine.submit( normalB );

    sicnu::jobs::JobRequest highC = ep8Request( "ep8:highC" );
    highC.priority = 0;
    highC.params["i"] = 3;
    const std::string highCId = engine.submit( highC );

    sicnu::jobs::JobRequest lowD = ep8Request( "ep8:lowD" );
    lowD.priority = 2;
    lowD.params["i"] = 4;
    const std::string lowDId = engine.submit( lowD );

    // An exclusive job queued while non-exclusive work is in flight follows
    // the drain-then-exclusive contract: it must not start until in-flight
    // work finished — and once the pool is idle it runs ALONE before queued
    // non-exclusive work (the historical pick order, preserved exactly).
    sicnu::jobs::JobRequest exclusive = ep8Request( "ep8:exclusive" );
    exclusive.exclusive = true;
    exclusive.params["i"] = 5;
    const std::string exclusiveId = engine.submit( exclusive );

    // Launch order once the parks release: both parks drain, then the
    // exclusive runs ALONE on the idle pool, then HIGH(3), NORMAL(2),
    // LOW(1), LOW(4) in strict priority order.
    releaseGate.store( true );
    // Wait for EVERY job (the exclusive finishing only unblocks the queued
    // non-exclusive work — snapshotting earlier races the scheduler).
    REQUIRE( engine.waitForJob( exclusiveId, 60000 ) );
    REQUIRE( engine.waitForJob( parkAId, 60000 ) );
    REQUIRE( engine.waitForJob( parkBId, 60000 ) );
    REQUIRE( engine.waitForJob( highCId, 60000 ) );
    REQUIRE( engine.waitForJob( normalBId, 60000 ) );
    REQUIRE( engine.waitForJob( lowAId, 60000 ) );
    REQUIRE( engine.waitForJob( lowDId, 60000 ) );

    const auto order = recorder.snapshot();
    {
        std::string dump;
        for ( int v : order )
            dump += std::to_string( v ) + ",";
        INFO( "order: " << dump );
    }
    REQUIRE( order.size() == 7 );
    // The two parks started first (either order).
    REQUIRE( ( ( order[0] == 0 && order[1] == 6 )
               || ( order[0] == 6 && order[1] == 0 ) ) );
    REQUIRE( order[2] == 5 ); // exclusive ran alone right after the drain
    // PICK order is strictly priority-serial (3, 2, 1, 4); with two workers
    // the STARTS of same-... adjacent picks can land in either order, so
    // assert the scheduler guarantees: high+normal occupy the next two slots
    // (either order), and the two lows follow FIFO (pick order is serial).
    const bool highThenNormal = ( order[3] == 3 && order[4] == 2 );
    const bool normalThenHigh = ( order[3] == 2 && order[4] == 3 );
    // Adjacent picks may interleave their starts on the two workers.
    REQUIRE( ( highThenNormal || normalThenHigh ) );
    // The two lows occupy the last two slots (either start order — adjacent
    // picks interleave on two workers; their PICK order is serial).
    const bool lowThenLow = ( order[5] == 1 && order[6] == 4 )
                            || ( order[5] == 4 && order[6] == 1 );
    REQUIRE( lowThenLow );

    engine.clearExecutors();
    engine.setMaxWorkers( defaultWorkers );
    engine.shutdownForTests();
}

TEST_CASE( "Short-job drain scales without the pre-8.0 admission cliff",
           "[ep8][stress][scaling]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();

    engine.clearExecutors();
    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext & ) {
        Json::Value result;
        result["i"] = req.params["i"];
        return result;
    } );

    // Measures the drain of n trivial jobs through the FULL admission path
    // (submit → heap → gates → engine). The pre-8.0 pass re-scanned the whole
    // task map per transition: the 10k drain ratio vs 2k was ~quadratic.
    // Bound: ratio < 15 (linear ≈ 5; quadratic ≥ 25) and an absolute cap.
    const auto drainJobs = [&]( int n ) {
        const auto start = std::chrono::steady_clock::now();
        std::vector<long> ids;
        ids.reserve( n );
        for ( int i = 0; i < n; ++i )
        {
            sicnu::jobs::JobRequest r = ep8Request( "ep8:tiny" );
            r.params["i"] = i;
            ids.push_back( center.submitJob( r ) );
        }
        REQUIRE( ids.size() == static_cast<size_t>( n ) );

        // The TIMED window ends at engine idle (submission + drain): an
        // O(n) allTasks() poll loop inside the window inflated the 10k ratio
        // with test-side cost. The verification snapshot below is NOT timed.
        engine.waitUntilIdleForTests( 600000 );

        const auto drainDeadline = std::chrono::steady_clock::now()
                                   + std::chrono::seconds( 60 );
        size_t terminal = 0;
        size_t completed = 0;
        std::unordered_set<long> idSet( ids.begin(), ids.end() );
        do
        {
            terminal = 0;
            completed = 0;
            const QList<sicnu::AlgorithmTaskInfo> snapshot = center.allTasks();
            for ( const auto &t : snapshot )
            {
                if ( !idSet.count( t.taskId ) )
                    continue;
                if ( sicnu::isTerminalStatus( t.status ) )
                    ++terminal;
                if ( t.status == sicnu::TaskStatus::Completed )
                    ++completed;
            }
            if ( terminal < ids.size() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        } while ( terminal < ids.size()
                  && std::chrono::steady_clock::now() < drainDeadline );

        const auto elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start ).count();
        INFO( "drained " << n << " jobs in " << elapsedMs << " ms" );
        REQUIRE( terminal == ids.size() ); // no stranded tasks
        REQUIRE( completed == ids.size() ); // and none failed/canceled
        center.clearCompletedTasks();
        return elapsedMs;
    };

    // Best-of-3 per size: a transient machine-load spike must not fail the
    // complexity check (the min sample is the machine's honest capability).
    qint64 smallMs = 0;
    qint64 largeMs = 0;
    for ( int round = 0; round < 3; ++round )
    {
        const qint64 ms = drainJobs( 2000 );
        smallMs = ( round == 0 || ms < smallMs ) ? ms : smallMs;
    }
    for ( int round = 0; round < 3; ++round )
    {
        const qint64 ms = drainJobs( 10000 );
        largeMs = ( round == 0 || ms < largeMs ) ? ms : largeMs;
    }

    INFO( "scaling: 2000→" << smallMs << " ms, 10000→" << largeMs
                           << " ms, ratio=" << ( double( largeMs ) / std::max( qint64( 1 ), smallMs ) ) );
    // ALWAYS-ON complexity check (review P2): linear scaling is ~5×; the
    // removed quadratic admission was >= 25×. The denominator is
    // floor-clamped so a fast machine cannot silently disable the assertion
    // (100 ms floor: linear passes comfortably, quadratic cannot).
    REQUIRE( largeMs < 10 * std::max( qint64( 100 ), smallMs ) );
    REQUIRE( largeMs < 600000 );

    engine.clearExecutors();
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

// ---------------------------------------------------------------------------
// WP-B: dynamic resource availability
// ---------------------------------------------------------------------------

TEST_CASE( "Raising a resource limit admits held work without a task transition",
           "[ep8][admission][dynamic]" )
{
    ensureApp();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );

    engine.clearExecutors();
    static std::atomic<bool> releaseGate{ false };
    releaseGate.store( false );
    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &,
                                         sicnu::operators::RSOperatorContext & ) {
        while ( !releaseGate.load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        return Json::Value();
    } );

    center.setGlobalConcurrencyLimit( 1 );
    const long holder = center.submitJob( ep8Request( "ep8:holder" ) );
    REQUIRE( holder > 0 );
    REQUIRE( waitForCondition( [&] {
        return center.getTaskInfo( holder ).status == sicnu::TaskStatus::Running;
    } ) );

    const long held = center.submitJob( ep8Request( "ep8:held" ) );
    REQUIRE( held > 0 );
    // One slot busy: the held candidate never launches while the limit stays.
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    const auto heldBefore = center.getTaskInfo( held ).status;
    REQUIRE( ( heldBefore == sicnu::TaskStatus::Queued
               || heldBefore == sicnu::TaskStatus::WaitingResource ) );

    // Dynamic availability (8.0 WP-B): raising the limit must re-run
    // admission immediately — no task transition happens here.
    center.setGlobalConcurrencyLimit( 2 );
    REQUIRE( waitForCondition( [&] {
        return center.getTaskInfo( held ).status == sicnu::TaskStatus::Running;
    } ) );

    releaseGate.store( true );
    waitForTerminalStatus( holder );
    waitForTerminalStatus( held );

    engine.clearExecutors();
    center.resetResourceProfileLimits(); // restores the global limit too
    center.clearCompletedTasks();
    engine.shutdownForTests();
}

// ---------------------------------------------------------------------------
// WP-E: resume identity 3.0 — operator implementation gate + moved output
// ---------------------------------------------------------------------------

namespace
{

/// Fake registered operator with a STABLE schema so the implementation
/// identity recipe is exercisable end-to-end.
class Ep8ResumeOperator : public sicnu::operators::RSOperator
{
  public:
    static std::atomic<int> &runCount()
    {
        static std::atomic<int> count{ 0 };
        return count;
    }
    std::string name() const override { return "ep8resume:op"; }
    std::string displayName() const override { return "Ep8Resume"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "resume identity stub"; }
    Json::Value schema() const override
    {
        Json::Value params( Json::objectValue );
        params["output"] = sicnu::operators::schema::makeOutputParam( "output", "out", "tif" );
        return sicnu::operators::schema::makeRootSchema( displayName(), description(), params,
                                                         Json::Value( Json::objectValue ) );
    }
    Json::Value run( const Json::Value &, sicnu::operators::RSOperatorContext & ) override
    {
        ++runCount();
        Json::Value payload( Json::objectValue );
        payload["output"] = "ep8resume-produced";
        return payload;
    }
};

/// The implementation identity of @p op exactly as the coordinator computes
/// it (schema text + determinism grade through makeImplementationIdentity).
std::string identityOf( sicnu::operators::RSOperator &op )
{
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const std::string schemaText = Json::writeString( writer, op.schema() );
    return sicnu::data::makeImplementationIdentity(
               schemaText + "|grade=" + op.determinismGrade() ).toStdString();
}

const std::string kWrongStamp( 64, '0' );

} // namespace

TEST_CASE( "Resume re-executes a served step only when the operator implementation changed",
           "[ep8][resume][identity]" )
{
    ensureApp();
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    registry.registerOperator( "ep8resume:op", [] {
        return std::make_unique<Ep8ResumeOperator>();
    } );
    Ep8ResumeOperator prototype;
    const std::string correctStamp = identityOf( prototype );

    QTemporaryDir checkpointDir;
    WorkflowRunCoordinator &coordinator = WorkflowRunCoordinator::instance();
    coordinator.setCheckpointDirectory( checkpointDir.path() );

    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors(); // dispatch must resolve through the REGISTRY

    const QString artifactPath = checkpointDir.path() + "/ep8_resume_first.tif";
    {
        QFile f( artifactPath );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "identity-artifact" );
    }

    auto seedRun = [&]( const std::string &runId, const std::string &stamp ) {
        WorkflowRun run;
        sicnu::workflow::WorkflowDefinition def;
        def.id = "ep8_resume_def";
        sicnu::workflow::StepDef first;
        first.id = "first";
        first.kind = sicnu::workflow::StepKind::Operator;
        first.operatorId = "ep8resume:op";
        first.params["output"] = artifactPath.toStdString();
        sicnu::workflow::StepDef second;
        second.id = "second";
        second.kind = sicnu::workflow::StepKind::Operator;
        second.operatorId = "ep8resume:op";
        second.params["output"] = ( checkpointDir.path() + "/ep8_resume_second.tif" ).toStdString();
        sicnu::workflow::StepConnection conn;
        conn.fromStepId = "first";
        conn.fromPort = "output";
        conn.toPort = "input";
        second.inputs.push_back( conn );
        def.steps.push_back( first );
        def.steps.push_back( second );
        run.setDefinition( def );
        REQUIRE( run.setRunId( runId ) );
        run.forceSetState( WorkflowRunState::Running );

        sicnu::workflow::StepPlan firstPlan;
        firstPlan.stepId = "first";
        firstPlan.operatorId = "ep8resume:op";
        firstPlan.status = "Completed";
        firstPlan.outputLayerPath = artifactPath.toStdString();
        firstPlan.resultPayload["output"] = artifactPath.toStdString();
        const QFileInfo info( artifactPath );
        firstPlan.outputSizeBytes = info.size();
        firstPlan.outputMtimeMs = info.lastModified().toMSecsSinceEpoch();
        firstPlan.operatorImplStamp = stamp;
        sicnu::workflow::StepPlan secondPlan;
        secondPlan.stepId = "second";
        secondPlan.operatorId = "ep8resume:op";
        secondPlan.status = "Pending";
        run.setStepPlans( { firstPlan, secondPlan } );

        WorkflowCheckpointManager checkpoints;
        REQUIRE( false == checkpoints.saveCheckpoint( run, checkpointDir.path() ).isEmpty() );
    };

    // Coordinator singletons hold task state between the two scenarios; the
    // recovery path marks the run Interrupted on resumeRun directly.

    // Scenario 1: matching stamp ⇒ step served (operator NOT executed for it).
    {
        Ep8ResumeOperator::runCount().store( 0 );
        seedRun( "ep8_identity_match", correctStamp );
        QString err;
        const long pipelineId = coordinator.resumeRun( "ep8_identity_match", &err );
        INFO( err.toStdString() );
        REQUIRE( pipelineId > 0 );
        // Wait for the resubmitted pipeline to finish.
        std::shared_ptr<WorkflowRun> snapshot;
        for ( int attempt = 0; attempt < 600; ++attempt )
        {
            snapshot = coordinator.runForPipeline( pipelineId );
            REQUIRE( snapshot != nullptr );
            if ( snapshot->state() == WorkflowRunState::Completed
                 || snapshot->state() == WorkflowRunState::Failed
                 || snapshot->state() == WorkflowRunState::Canceled )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        // Only the SECOND step dispatched to the (counting) operator: the
        // served first step never runs.
        CHECK( snapshot->state() == WorkflowRunState::Completed );
        CHECK( Ep8ResumeOperator::runCount().load() == 1 );
    }

    engine.shutdownForTests();

    // Scenario 2: wrong stamp ⇒ the operator "changed" ⇒ the step re-executes.
    {
        // Fresh artifact + checkpoint: the gate compares identity before
        // bytes, so reuse the same on-disk artifact through a new run id.
        Ep8ResumeOperator::runCount().store( 0 );
        seedRun( "ep8_identity_mismatch", kWrongStamp );
        QString err;
        const long pipelineId = coordinator.resumeRun( "ep8_identity_mismatch", &err );
        INFO( err.toStdString() );
        REQUIRE( pipelineId > 0 );
        std::shared_ptr<WorkflowRun> snapshot;
        for ( int attempt = 0; attempt < 600; ++attempt )
        {
            snapshot = coordinator.runForPipeline( pipelineId );
            REQUIRE( snapshot != nullptr );
            if ( snapshot->state() == WorkflowRunState::Completed
                 || snapshot->state() == WorkflowRunState::Failed
                 || snapshot->state() == WorkflowRunState::Canceled )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        // BOTH steps re-ran: the mismatch demotes the completed step.
        CHECK( snapshot->state() == WorkflowRunState::Completed );
        CHECK( Ep8ResumeOperator::runCount().load() == 2 );
    }

    engine.clearExecutors();
    engine.shutdownForTests();
}

TEST_CASE( "StepPlan operator identity stamp round-trips through checkpoint JSON",
           "[ep8][resume][identity]" )
{
    sicnu::workflow::StepPlan plan;
    plan.stepId = "s";
    plan.operatorImplStamp = "abc123";
    const Json::Value json = plan.toJson();
    REQUIRE( json["operatorImplStamp"].asString() == "abc123" );
    const auto back = sicnu::workflow::StepPlan::fromJson( json );
    CHECK( back.operatorImplStamp == "abc123" );

    // Legacy plan (no stamp): the field stays absent in JSON — old readers
    // see no change — and empty after the round-trip.
    sicnu::workflow::StepPlan legacy;
    legacy.stepId = "legacy";
    const Json::Value legacyJson = legacy.toJson();
    CHECK_FALSE( legacyJson.isMember( "operatorImplStamp" ) );
    CHECK( sicnu::workflow::StepPlan::fromJson( legacyJson ).operatorImplStamp.empty() );
}

// ---------------------------------------------------------------------------
// WP-F: remote identity in the execution fingerprint
// ---------------------------------------------------------------------------

TEST_CASE( "Remote identity token participates in the execution fingerprint",
           "[ep8][cache][identity]" )
{
    using sicnu::data::TaggedDerivationInput;
    auto input = []( const QString &token ) {
        TaggedDerivationInput in;
        in.revision = sicnu::data::AssetRevision::initial();
        in.toPort = QStringLiteral( "input" );
        in.valueDomain = QStringLiteral( "remote" );
        in.remoteIdentity = token;
        return in;
    };

    QJsonObject params;
    params["output"] = QStringLiteral( "/tmp/ep8_remote.tif" );
    const auto base = sicnu::data::makeExecutionFingerprintV2(
        QStringLiteral( "rs:whatever" ), QStringLiteral( "1.0" ), params, {} );

    // Same strong ETag ⇒ same identity (cacheable).
    const auto withToken = sicnu::data::makeExecutionFingerprintV2(
        QStringLiteral( "rs:whatever" ), QStringLiteral( "1.0" ), params,
        { input( QStringLiteral( "etag:\"stable-strong-1\"" ) ) } );
    const auto withTokenAgain = sicnu::data::makeExecutionFingerprintV2(
        QStringLiteral( "rs:whatever" ), QStringLiteral( "1.0" ), params,
        { input( QStringLiteral( "etag:\"stable-strong-1\"" ) ) } );
    REQUIRE( withToken.isValid() );
    REQUIRE( withToken == withTokenAgain );

    // Server-side change (new ETag) ⇒ different identity (guaranteed miss).
    const auto changed = sicnu::data::makeExecutionFingerprintV2(
        QStringLiteral( "rs:whatever" ), QStringLiteral( "1.0" ), params,
        { input( QStringLiteral( "etag:\"rotated-strong-2\"" ) ) } );
    CHECK( changed != withToken );

    // No remote input ⇒ different from any remote-identified variant.
    CHECK( base != withToken );
}

TEST_CASE( "TaskCenter installs the remote identity resolver by default and the default fails closed locally",
           "[ep8][cache][identity]" )
{
    ensureApp();
    auto &center = TaskCenter::instance(); // constructor installs the default resolver
    Q_UNUSED( center );
    const auto resolver = sicnu::data::executionIdentityResolver();
    REQUIRE( resolver != nullptr );
    // Local paths are NOT the remote resolver's business: empty token (the
    // collector keeps its local handling; a remote probe is never attempted).
    CHECK( ( *resolver )( QStringLiteral( "/tmp/some/local/file.tif" ) ).isEmpty() );
    CHECK( ( *resolver )( QStringLiteral( "relative/path.tif" ) ).isEmpty() );
}

// ---------------------------------------------------------------------------
// WP-C: process-tree containment (POSIX-runnable; Windows logic compiled on
// Windows lanes only)
// ---------------------------------------------------------------------------

#ifndef Q_OS_WIN
TEST_CASE( "Worker cancel escalation reaps SIGTERM-immune helper processes",
           "[ep8][worker][containment]" )
{
    ensureApp();
    QTemporaryDir workDir;
    const QString helperPidFile = workDir.path() + "/helper.pid";

    // Runs one "__spawn_helper__" job on a REAL isolated worker. The job
    // parks ignoring cancellation and the helper ignores SIGTERM — the only
    // way both can die is the guard's group-wide SIGKILL sweep. (The worker
    // thread records its outcome; Catch2 assertions stay on the test thread.)
    std::atomic<bool> cancelRequest{ false };
    std::string workerOutcome;
    std::thread runThread( [&] {
        try
        {
            Json::Value helperParams( Json::objectValue );
            helperParams["helperPidFile"] = helperPidFile.toStdString();
            ( void )sicnu::processing::runInLocalWorker(
                QStringLiteral( SICNU_WORKER_EXE ), "__spawn_helper__", helperParams,
                [&] { return cancelRequest.load(); },
                std::chrono::minutes( 1 ),           // job timeout
                std::chrono::milliseconds( 1000 ) ); // cancel grace
            workerOutcome = "returned-a-result";
        }
        catch ( const std::exception &e )
        {
            workerOutcome = e.what();
        }
    } );

    // Wait for the helper to publish its pid, then request cancellation.
    qint64 helperPid = 0;
    for ( int i = 0; i < 1200 && helperPid <= 0; ++i )
    {
        QFile f( helperPidFile );
        if ( f.open( QIODevice::ReadOnly ) )
        {
            helperPid = f.readAll().toLongLong();
        }
        if ( helperPid <= 0 )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    REQUIRE( helperPid > 0 );
    cancelRequest.store( true );
    runThread.join();
    INFO( "worker outcome: " << workerOutcome );
    REQUIRE( workerOutcome.find( "worker cancelled" ) != std::string::npos );

    // The helper ignored SIGTERM and its parent (the worker) is dead: only
    // the group-wide SIGKILL sweep can account for it. Poll until the pid is
    // gone (ESRCH — reaped after reparenting; a zombie would still resolve).
    bool gone = false;
    for ( int i = 0; i < 2000; ++i )
    {
        if ( ::kill( static_cast<pid_t>( helperPid ), 0 ) == -1 && errno == ESRCH )
        {
            gone = true;
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    REQUIRE( gone );
}
#endif
