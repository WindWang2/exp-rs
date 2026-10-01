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
//     queue. Ordering is asserted as a scheduler invariant (every job parks in
//     its executor, so a job can only start once a worker is free and the
//     recorded sequence is the engine's pick sequence) rather than as a
//     wall-clock start order, which two workers interleave under load;
//   - short-job scaling: 2k vs 10k drain ratio stays ~linear (the pre-8.0
//     admission rescan was quadratic; ratio bound asserted generously, min
//     over samples, and contaminated samples re-measured within a case
//     budget), absolute bounded runtime, no stranded tasks.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/execution_resource_bridge.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/task_center.h"
#include "jobs/job_engine.h"
#include "support/bounded_wait.h"
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
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
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
#include <QJsonObject>
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

/// Bounded waits in the bucketed-queue case state their budget explicitly
/// instead of inheriting the 6 s default: this suite runs on shared CI hosts
/// where a load spike can stall worker-thread start (and every gate-open
/// handshake below) for seconds. 6000 x 10 ms = 60 s, an order of magnitude
/// over what an idle host needs and still far inside the case's ctest timeout.
constexpr int kQueueWaitAttempts = 6000;
constexpr int kQueueWaitSleepMs = 10;

/// Opens a set of per-job start gates on destruction. Jobs that park inside
/// their executor hold a worker for the whole case, so any early exit (a failed
/// REQUIRE unwinds the case) must release them: the next case joins workers
/// inside shutdownForTests() and a body parked forever would hang it.
struct ParkGateGuard
{
    std::atomic<bool> *gates = nullptr;
    std::size_t count = 0;
    ~ParkGateGuard()
    {
        for ( std::size_t i = 0; i < count; ++i )
            gates[i].store( true );
    }
};

/// True when exactly one job is Running and it is @p jobId — the engine's
/// "drain then exclusive" run-alone invariant read from engine state, which is
/// a statement about the scheduler rather than a thread-scheduling artifact.
bool onlyRunningJob( const sicnu::jobs::JobEngine &engine, const std::string &jobId )
{
    bool saw = false;
    for ( const sicnu::jobs::JobRecord &rec : engine.list() )
    {
        if ( rec.state != sicnu::jobs::JobState::Running )
            continue;
        if ( saw || rec.id != jobId )
            return false;
        saw = true;
    }
    return saw;
}

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
        // Deadline-bounded (#1392): an assertion between submit and release
        // would leave this executor parked on a gate the case never opens, so
        // the executor releases itself and the case's own assertions report
        // the real state instead of spinning into the harness timeout.
        sicnu_test::waitUntil( [&] { return releaseGate.load(); }, 60000,
                               [] { return "ep8 gate never released"; } );
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
        // Deadline-bounded (#1392): an assertion between submit and release
        // would leave this executor parked on a gate the case never opens, so
        // the executor releases itself and the case's own assertions report
        // the real state instead of spinning into the harness timeout.
        sicnu_test::waitUntil( [&] { return releaseGate.load(); }, 60000,
                               [] { return "ep8 gate never released"; } );
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
            // Deadline-bounded (#1392): a closed gate must not strand the
            // executor past the harness timeout.
            sicnu_test::waitUntil( [&] { return releaseGate.load(); }, 60000,
                                   [] { return "ep8 root holder never released"; } );
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

    // ── Ordering without the race ────────────────────────────────────────
    // Pick order is a property of the engine's queue, but START order is
    // whatever the pool's threads happen to record first. With two workers,
    // adjacent picks interleave, and under load a job that finishes instantly
    // can be overtaken by a job picked AFTER it (a descheduled worker records
    // its start late) — the load flake this suite used to fail with. So every
    // job here HOLDS its worker (parks in its executor) until the case opens
    // that job's start gate: a job can only start once a worker is free, so
    // the recorded sequence is the engine's PICK sequence. The job's "i" param
    // (0..6) is also its gate index.
    constexpr int kGateCount = 7;
    static std::array<std::atomic<bool>, kGateCount> startGate;
    for ( int i = 0; i < kGateCount; ++i )
        startGate[static_cast<std::size_t>( i )].store( false );
    // Any early exit (a failed REQUIRE unwinds the case) must release the
    // parked jobs: the next case joins workers inside shutdownForTests() and a
    // body parked forever would hang it.
    ParkGateGuard openAllGates{ startGate.data(), startGate.size() };

    engine.registerExecutor( "ep8:", []( const sicnu::jobs::JobRequest &req,
                                         sicnu::operators::RSOperatorContext & ) {
        const int i = std::atoi( req.params["i"].asString().c_str() );
        recorder.push( i );
        // A request without an "i" in 0..6 must not index the gate array
        // (there is no gate to park on): it completes at once and the case's
        // id set remains the truth.
        if ( i < 0 || i >= kGateCount )
            return Json::Value();
        while ( !startGate[static_cast<std::size_t>( i )].load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        return Json::Value();
    } );

    // Two park jobs occupy the whole pool; the case continues only when BOTH
    // are running (their executors recorded the starts).
    sicnu::jobs::JobRequest parkA = ep8Request( "ep8:parkA" );
    parkA.params["i"] = 0;
    const std::string parkAId = engine.submit( parkA );
    sicnu::jobs::JobRequest parkB = ep8Request( "ep8:parkB" );
    parkB.params["i"] = 6;
    const std::string parkBId = engine.submit( parkB );
    REQUIRE( waitForCondition( [&] { return recorder.snapshot().size() == 2; },
                               kQueueWaitAttempts, kQueueWaitSleepMs ) );

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

    // ── Drain-then-exclusive, asserted as an invariant ────────────────────
    // While the parks hold both workers, the queued exclusive must not start
    // and must not admit non-exclusive work next to it. Every job parks, so
    // "nothing new started" is NOT a timing race: no code path can add a start
    // here without violating the drain contract. The quiet window below only
    // gives a violation that needs a scheduling cycle a chance to show itself.
    REQUIRE( recorder.snapshot().size() == 2 );
    const auto exclusiveSnapshot = engine.snapshot( exclusiveId );
    REQUIRE( exclusiveSnapshot.has_value() );
    REQUIRE( exclusiveSnapshot->state == sicnu::jobs::JobState::Queued );
    std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
    REQUIRE( recorder.snapshot().size() == 2 );
    const auto exclusiveStillQueued = engine.snapshot( exclusiveId );
    REQUIRE( exclusiveStillQueued.has_value() );
    REQUIRE( exclusiveStillQueued->state == sicnu::jobs::JobState::Queued );

    // Release the parks: both drain, and only once the pool is idle does the
    // exclusive start.
    startGate[0].store( true );
    startGate[6].store( true );
    REQUIRE( waitForCondition( [&] { return recorder.snapshot().size() >= 3; },
                               kQueueWaitAttempts, kQueueWaitSleepMs ) );
    // ... and it runs ALONE. Everything else is terminal or queued behind the
    // exclusive, so this holds for as long as the exclusive is parked — again a
    // scheduler invariant rather than a snapshot race.
    REQUIRE( waitForCondition( [&] { return onlyRunningJob( engine, exclusiveId ); },
                               kQueueWaitAttempts, kQueueWaitSleepMs ) );

    // Release the exclusive: both workers go idle and pick back to back, so
    // the next two picks are the best-paying buckets, HIGH(3) then NORMAL(2).
    // Which worker RECORDS first is not observable; the SET of the next two
    // starts is fixed, because those two jobs hold both workers and no
    // lower-priority pick can overtake them.
    startGate[5].store( true );
    REQUIRE( waitForCondition( [&] { return recorder.snapshot().size() >= 5; },
                               kQueueWaitAttempts, kQueueWaitSleepMs ) );
    const auto midOrder = recorder.snapshot();
    REQUIRE( midOrder.size() == 5 );
    REQUIRE( ( ( midOrder[3] == 3 && midOrder[4] == 2 )
               || ( midOrder[3] == 2 && midOrder[4] == 3 ) ) );

    // Free ONE worker: the pick that follows is the lowest-priority bucket's
    // FIFO head (lowA, submitted before lowD), because the other worker is
    // still parked. This is the one point where a single pick can be observed
    // in isolation, so FIFO order inside a bucket is asserted HERE instead of
    // being inferred from two instant jobs racing on two workers.
    startGate[static_cast<std::size_t>( midOrder[3] )].store( true );
    REQUIRE( waitForCondition( [&] { return recorder.snapshot().size() >= 6; },
                               kQueueWaitAttempts, kQueueWaitSleepMs ) );
    startGate[static_cast<std::size_t>( midOrder[4] )].store( true );
    REQUIRE( waitForCondition( [&] { return recorder.snapshot().size() >= 7; },
                               kQueueWaitAttempts, kQueueWaitSleepMs ) );

    // Every gate is open, so nothing is parked any more and the remaining work
    // only completes: wait for the engine's own terminal state per job instead
    // of a fixed sleep.
    const std::vector<std::string> allIds{ parkAId, parkBId, lowAId, normalBId,
                                           highCId, lowDId, exclusiveId };
    REQUIRE( waitForCondition( [&] {
        for ( const std::string &id : allIds )
        {
            const auto rec = engine.snapshot( id );
            if ( !rec.has_value()
                 || ( rec->state != sicnu::jobs::JobState::Succeeded
                      && rec->state != sicnu::jobs::JobState::Failed
                      && rec->state != sicnu::jobs::JobState::Cancelled ) )
                return false;
        }
        return true;
    }, kQueueWaitAttempts, kQueueWaitSleepMs ) );

    const auto order = recorder.snapshot();
    {
        std::string dump;
        for ( int v : order )
            dump += std::to_string( v ) + ",";
        INFO( "start order: " << dump );
    }
    REQUIRE( order.size() == 7 );
    // The two parks started first (either order).
    REQUIRE( ( ( order[0] == 0 && order[1] == 6 )
               || ( order[0] == 6 && order[1] == 0 ) ) );
    // The exclusive started third, alone: the drain finished first and no
    // non-exclusive work started next to it.
    REQUIRE( order[2] == 5 );
    // HIGH(3) and NORMAL(2) fill the next two slots (either start order — two
    // workers pick them back to back, so which thread records first is not
    // observable). No LOW can overtake them: both workers are parked.
    REQUIRE( ( ( order[3] == 3 && order[4] == 2 )
               || ( order[3] == 2 && order[4] == 3 ) ) );
    // Then the LOW bucket drains FIFO: lowA(1) before lowD(4), each observed as
    // an isolated single-worker pick.
    REQUIRE( order[5] == 1 );
    REQUIRE( order[6] == 4 );

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
    // 12.0: this is a drain-throughput benchmark, not an admission-control
    // test — the 10k submit burst would trip the 4096-task pending bound
    // (refused with -1) and distort the drain measurement it exists to take.
    center.setMaxPendingTasks( 0 );

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
            const long id = center.submitJob( r );
            REQUIRE( id > 0 ); // fail fast if admission ever refuses (12.0 bound)
            ids.push_back( id );
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

    // ── Measurement retries — never test retries ──────────────────────────
    // The ratio is the point of this case, so it stays a wall-clock ratio.
    // But a sample taken on a shared CI host measures the host's load as much
    // as the engine's capability: a single spike landing inside the 10k rounds
    // adds a constant to largeMs only, and the old best-of-3 ratio check could
    // not tell that from a quadratic admission cliff. So each size is sampled
    // kMeasureAttempts times (min kept), and a SUSPECT sample — one that
    // overruns the size's per-round budget, or one far worse than that size's
    // best sample so far — is RE-MEASURED. Only the measurement is retried;
    // the assertions below are evaluated once, on the min sample.
    constexpr int kMeasureAttempts = 3;
    constexpr int kMaxMeasureAttempts = 6;
    // Per-round budgets: an idle 2k / 10k drain on the reference CI lane is a
    // few seconds, so anything an order of magnitude past that is carrying
    // load rather than measuring the engine.
    constexpr qint64 kSmallRoundBudgetMs = 20000;
    constexpr qint64 kLargeRoundBudgetMs = 45000;
    // Hard ceiling on the retries: this case stays well inside its ctest
    // timeout even when every sample is suspect.
    constexpr qint64 kCaseBudgetMs = 150000;

    const auto caseStarted = std::chrono::steady_clock::now();
    auto caseSpentMs = [&caseStarted] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - caseStarted ).count();
    };

    auto measureDrain = [&]( int n, qint64 roundBudgetMs ) {
        qint64 bestMs = 0;
        for ( int attempt = 1; attempt <= kMaxMeasureAttempts; ++attempt )
        {
            const qint64 ms = drainJobs( n );
            if ( bestMs == 0 || ms < bestMs )
                bestMs = ms;
            const bool suspect = ms > roundBudgetMs
                                 || ( ms != bestMs && ms > 2 * bestMs );
            if ( !suspect && attempt >= kMeasureAttempts )
                return bestMs; // best-of-N with a clean sample
            if ( caseSpentMs() >= kCaseBudgetMs )
                return bestMs; // out of budget: the min sample is the answer
        }
        return bestMs;
    };

    const qint64 smallMs = measureDrain( 2000, kSmallRoundBudgetMs );
    const qint64 largeMs = measureDrain( 10000, kLargeRoundBudgetMs );

    INFO( "scaling: 2000→" << smallMs << " ms, 10000→" << largeMs
                           << " ms, ratio=" << ( double( largeMs ) / std::max( qint64( 1 ), smallMs ) ) );
    // ALWAYS-ON complexity check (review P2): linear scaling is ~5×; the
    // removed quadratic admission was >= 25×. The denominator is
    // floor-clamped so a fast machine cannot silently disable the assertion
    // (100 ms floor: linear passes comfortably, quadratic cannot).
    //
    // 15× is the documented mid-point between the two regimes (the ratio this
    // check exists to separate: 5× linear vs >= 25× quadratic). It is 3× the
    // linear expectation and still 1.67× below the quadratic floor, so it
    // absorbs the additive cost of a load spike landing inside the 10k rounds
    // (the CI flake this suite was failing with) without normalising a real
    // admission cliff. The min-of-3 sampling above is what keeps the spike out
    // of the denominator; the budget and ratio together bound the check.
    REQUIRE( largeMs < 15 * std::max( qint64( 100 ), smallMs ) );
    REQUIRE( largeMs < 600000 );

    engine.clearExecutors();
    center.clearCompletedTasks();
    center.resetResourceProfileLimits(); // restore the 12.0 pending bound
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
        // Deadline-bounded (#1392): an assertion between submit and release
        // would leave this executor parked on a gate the case never opens, so
        // the executor releases itself and the case's own assertions report
        // the real state instead of spinning into the harness timeout.
        sicnu_test::waitUntil( [&] { return releaseGate.load(); }, 60000,
                               [] { return "ep8 gate never released"; } );
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
    // The production resume gate stamps identities WITH the environment pins
    // the coordinator installs at construction (execution_resource_bridge).
    // Install the same pins BEFORE computing the expected stamp, or the two
    // hashes diverge by the pin string and the fail-closed gate demotes every
    // served step (observed as a deterministic re-execution wherever
    // GDALVersionInfo is non-empty at the coordinator's first use).
    sicnu::processing::installExecutionEnvironmentPins();
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
