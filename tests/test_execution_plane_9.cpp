// test_execution_plane_9.cpp — Execution / Concurrency / Lifecycle 9.0:
// structured task hierarchy, transient child admission, lifecycle-safe
// eventing, ghost-run closure.
//
// M0 coverage (#862 / #860 / #876) and M1 coverage (ownership edges, join
// rule I9, cancellation propagation). Every concurrency scenario is
// deterministic: gates + completion flags, no sleep-based luck. The
// regression property is "old code fails": the #862 reproducer times its
// bounded wait out on a pre-fix engine and fails the REQUIRE instead of
// hanging the suite forever.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "processing/algorithms/temporal/temporal_workspace.h"
#include "processing/framework/task_center.h"
#include "runtime/observability/fault_registry.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

using sicnu::TaskCenter;
using namespace sicnu::workflow;

namespace
{

/// RAII: no armed fault leaks into a later test.
struct AllDisarmedGuard
{
    ~AllDisarmedGuard() { sicnu::runtime::observability::fault::disarmAllFaults(); }
};


void ensureApp9()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char appName[] = "test_execution_plane_9";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
}

bool waitForCondition9( const std::function<bool()> &predicate, int attempts = 6000, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( predicate() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
    return predicate();
}

void waitForTerminalStatus9( long taskId, int attempts = 6000, int sleepMs = 5 )
{
    REQUIRE( waitForCondition9( [&] {
        return sicnu::isTerminalStatus( TaskCenter::instance().getTaskInfo( taskId ).status );
    }, attempts, sleepMs ) );
}

sicnu::jobs::JobRequest ep9Request( const char *algorithmId )
{
    sicnu::jobs::JobRequest r;
    r.algorithmId = algorithmId;
    r.source = "test";
    return r;
}

struct CoordinatorFixture9
{
    QTemporaryDir checkpointDir;
    WorkflowRunCoordinator &coordinator = WorkflowRunCoordinator::instance();

    CoordinatorFixture9()
    {
        ensureApp9();
        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );
        coordinator.setCheckpointDirectory( checkpointDir.path() );
    }
};

WorkflowDefinition twoStepDefinition9( const std::string &prefix )
{
    WorkflowDefinition def;
    def.id = prefix + "_def";
    def.title = "ep9 tracked pipeline";

    StepDef first;
    first.id = "first";
    first.title = "First";
    first.kind = StepKind::Operator;
    first.operatorId = prefix + ":first";
    first.params["output"] = "/tmp/" + prefix + "_first.tif";

    StepDef second;
    second.id = "second";
    second.title = "Second";
    second.kind = StepKind::Operator;
    second.operatorId = prefix + ":second";
    second.params["input"] = "$first.output";
    second.params["output"] = "/tmp/" + prefix + "_second.tif";
    StepConnection conn;
    conn.fromStepId = "first";
    conn.fromPort = "output";
    conn.toPort = "input";
    second.inputs.push_back( conn );

    def.steps.push_back( first );
    def.steps.push_back( second );
    return def;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// M0 #862 — transient child admission
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "worker-originated child is admitted beyond a saturated globalMax (#862)",
           "[ep9][admission][structured]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 2 );
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );
    center.setGlobalConcurrencyLimit( 1 ); // the single slot belongs to the parent

    engine.clearExecutors();
    static std::atomic<bool> releaseParent{ false };
    static std::atomic<bool> childCompleted{ false };
    static std::atomic<bool> parentSawWorkerThread{ false };
    static std::atomic<long> parentObservedChildId{ -1 };
    releaseParent.store( false );
    childCompleted.store( false );
    parentSawWorkerThread.store( false );
    parentObservedChildId.store( -1 );

    // Parent body: runs ON a JobEngine worker (its slot is the saturated
    // globalMax slot), submits a child from that worker and waits for it.
    // Pre-fix, the child sat in WaitingResource forever: the parent could
    // never finish, the single slot never freed — the pool deadlocked.
    engine.registerExecutor( "ep9:parent",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 parentSawWorkerThread.store(
                                     sicnu::jobs::JobEngine::isWorkerThread() );
                                 const long child = center.submitJob( ep9Request( "ep9:child" ) );
                                 parentObservedChildId.store( child );
                                 // Sanctioned worker-side wait: bounded poll on the
                                 // completion state (waitForTask is refused on
                                 // workers — see the next test).
                                 const bool finished =
                                     waitForCondition9( [&] {
                                         return sicnu::isTerminalStatus(
                                             center.getTaskInfo( child ).status );
                                     }, 2000 /* ~10s bounded: old code FAILS here */ );
                                 if ( !finished )
                                 {
                                     releaseParent.store( true ); // unwind so the suite survives
                                     Json::Value err( Json::objectValue );
                                     return err;
                                 }
                                 return Json::Value();
                             } );
    engine.registerExecutor( "ep9:child",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 childCompleted.store( true );
                                 return Json::Value();
                             } );

    const long parent = center.submitJob( ep9Request( "ep9:parent" ) );
    REQUIRE( parent > 0 );

    waitForTerminalStatus9( parent );

    REQUIRE( parentSawWorkerThread.load() );
    REQUIRE( childCompleted.load() );
    const auto childInfo = center.getTaskInfo( parentObservedChildId.load() );
    REQUIRE( childInfo.status == sicnu::TaskStatus::Completed );
    // Structured hierarchy stamps: the child is worker-originated and owned
    // by the submitting parent task.
    REQUIRE( childInfo.workerOriginated );
    REQUIRE( childInfo.ownerTaskId == parent );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

TEST_CASE( "waitForTask refuses to park a worker thread and returns a truthful snapshot (#862)",
           "[ep9][wait][structured]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 2 );
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );

    engine.clearExecutors();
    static std::atomic<bool> releasePark{ false };
    static std::atomic<bool> releaseWaiter{ false };
    static std::atomic<long> elapsedMs{ -1 };
    static std::atomic<int> refusedSnapshotStatus{ -1 };
    releasePark.store( false );
    releaseWaiter.store( false );
    elapsedMs.store( -1 );
    refusedSnapshotStatus.store( -1 );

    engine.registerExecutor( "ep9:park",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 while ( !releasePark.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 return Json::Value();
                             } );

    // The waiter runs on a worker and used to park on TaskCenter's wait
    // condition for the FULL default timeout (30 minutes) — occupying a
    // pool slot while doing so. The refusal returns the CURRENT snapshot
    // promptly (never a fabricated terminal record).
    engine.registerExecutor( "ep9:waiter",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 const long parked = center.submitJob( ep9Request( "ep9:park" ) );
                                 const auto started = std::chrono::steady_clock::now();
                                 const auto info = center.waitForTask( parked );
                                 elapsedMs.store( static_cast<long>(
                                     std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - started )
                                         .count() ) );
                                 refusedSnapshotStatus.store( static_cast<int>( info.status ) );
                                 releasePark.store( true );
                                 releaseWaiter.store( true );
                                 return Json::Value();
                             } );

    const long waiter = center.submitJob( ep9Request( "ep9:waiter" ) );
    REQUIRE( waiter > 0 );
    waitForTerminalStatus9( waiter );

    // The refusal must be near-instant: far under the 30-minute default
    // timeout, and the snapshot must be truthful (non-terminal park task).
    REQUIRE( elapsedMs.load() >= 0 );
    REQUIRE( elapsedMs.load() < 5000 );
    REQUIRE( refusedSnapshotStatus.load() >= 0 );
    REQUIRE( !sicnu::isTerminalStatus(
        static_cast<sicnu::TaskStatus>( refusedSnapshotStatus.load() ) ) );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

// ─────────────────────────────────────────────────────────────────────────────
// M1 — structured ownership: join rule (I9) + cancellation propagation
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "owner reaching terminal cancels its orphaned owned children (I9 join)",
           "[ep9][structured][join]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 2 );
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );

    engine.clearExecutors();
    static std::atomic<bool> releaseChild{ false };
    releaseChild.store( false );

    // Parent submits the child and returns IMMEDIATELY — the child is
    // orphaned mid-run. The join rule requires the owner's terminal
    // transition to drive the child to a defined terminal state.
    engine.registerExecutor( "ep9:orphanowner",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 center.submitJob( ep9Request( "ep9:orphanchild" ) );
                                 return Json::Value();
                             } );
    engine.registerExecutor( "ep9:orphanchild",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext &ctx ) {
                                 // Cooperative cancellation: exit when the
                                 // join-rule cancel flag lands.
                                 while ( !releaseChild.load() && !ctx.isCancelled() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 return Json::Value();
                             } );

    const long owner = center.submitJob( ep9Request( "ep9:orphanowner" ) );
    REQUIRE( owner > 0 );
    waitForTerminalStatus9( owner );

    // The child sits in the transient-allowance set (default limits here, so
    // it was admitted as a plain running task); it must now be canceled by
    // the join rule — deterministically, without releasing its park gate.
    const auto childIds = center.allTasks();
    long childId = -1;
    for ( const auto &info : childIds )
    {
        if ( info.ownerTaskId == owner && info.algorithmId == QLatin1String( "ep9:orphanchild" ) )
            childId = info.taskId;
    }
    REQUIRE( childId > 0 );
    REQUIRE( waitForCondition9( [&] {
        return center.getTaskInfo( childId ).status == sicnu::TaskStatus::Canceled;
    } ) );
    // The typed reason must attribute the cancel to the structured join —
    // not to a user cancel and not to the engine's generic record.
    REQUIRE( center.getTaskInfo( childId ).cancelReason == sicnu::TaskCancelReason::StructuredJoin );

    releaseChild.store( true ); // let the (canceled-flag-armed) job body unwind
    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

TEST_CASE( "cancelling a running owner cancels its owned children immediately",
           "[ep9][structured][cancel-propagation]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 2 );
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );
    center.setGlobalConcurrencyLimit( 1 ); // child must ride the transient bypass

    engine.clearExecutors();
    static std::atomic<bool> releaseAll{ false };
    releaseAll.store( false );

    engine.registerExecutor( "ep9:owner",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 center.submitJob( ep9Request( "ep9:owned" ) );
                                 while ( !releaseAll.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 return Json::Value();
                             } );
    engine.registerExecutor( "ep9:owned",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 while ( !releaseAll.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 return Json::Value();
                             } );

    const long owner = center.submitJob( ep9Request( "ep9:owner" ) );
    REQUIRE( owner > 0 );

    long childId = -1;
    REQUIRE( waitForCondition9( [&] {
        for ( const auto &info : center.allTasks() )
        {
            if ( info.ownerTaskId == owner && info.algorithmId == QLatin1String( "ep9:owned" ) )
            {
                childId = info.taskId;
                return sicnu::isTerminalStatus( info.status )
                       || info.status == sicnu::TaskStatus::Running;
            }
        }
        return false;
    } ) );
    REQUIRE( childId > 0 );

    // Cancel the owner while BOTH are running: the ownership edge must
    // propagate the cancel to the owned child in the same pass (not only
    // after the owner's executor unwinds).
    REQUIRE( center.cancelTask( owner ) );
    REQUIRE( waitForCondition9( [&] {
        return center.getTaskInfo( childId ).status == sicnu::TaskStatus::Canceled
               || center.getTaskInfo( childId ).status == sicnu::TaskStatus::Cancelling;
    } ) );

    releaseAll.store( true );
    waitForTerminalStatus9( owner );
    waitForTerminalStatus9( childId );
    REQUIRE( center.getTaskInfo( childId ).status == sicnu::TaskStatus::Canceled );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

// ─────────────────────────────────────────────────────────────────────────────
// M0 #860 — lifecycle-safe eventing (observer re-entrancy)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "observer slots may re-enter the coordinator from runStateChanged (#860)",
           "[ep9][coordinator][eventing]" )
{
    CoordinatorFixture9 fx;
    const std::string prefix = "ep9_reenter";

    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.registerExecutor( prefix + ":first",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 return Json::Value();
                             } );
    engine.registerExecutor( prefix + ":second",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 return Json::Value();
                             } );

    // The slot calls back into the coordinator SYNCHRONOUSLY from whatever
    // thread drains (Qt::DirectConnection: the test binary has no event
    // loop, so an auto/queued connection from a worker drain would never be
    // delivered). Pre-fix, runStateChanged was emitted with m_mutex held:
    // the queries below self-deadlocked the non-recursive mutex and hung
    // the suite. The slot only touches locked/atomic state, so it is safe
    // on any thread.
    static std::atomic<bool> reentered{ false };
    reentered.store( false );
    QObject slotContext;
    QObject::connect( &fx.coordinator, &WorkflowRunCoordinator::runStateChanged,
                      &slotContext,
                      [&]( const QString &, const QString &, const QString &,
                           qint64, qint64 ) {
                          const size_t runs = fx.coordinator.runs().size();
                          const auto run = fx.coordinator.runForPipeline( -1 );
                          const long pipe = fx.coordinator.pipelineIdForRun( "no-such-run" );
                          if ( runs > 0 && !run && pipe == -1 )
                              reentered.store( true );
                      },
                      Qt::DirectConnection );

    const long pipelineId = fx.coordinator.startTrackedPipeline( twoStepDefinition9( prefix ) );
    REQUIRE( pipelineId > 0 );

    std::shared_ptr<WorkflowRun> snapshot;
    REQUIRE( waitForCondition9( [&] {
        snapshot = fx.coordinator.runForPipeline( pipelineId );
        return snapshot && snapshot->state() == WorkflowRunState::Completed;
    } ) );
    // By terminal time at least one post-registration drain ran on some
    // thread; the slot's synchronous re-entrant queries must have completed.
    REQUIRE( reentered.load() );
}

// ─────────────────────────────────────────────────────────────────────────────
// M0 #876 — ghost-run closure on resume swap
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "resume swap closes the temporary run with a terminal broadcast (#876)",
           "[ep9][coordinator][resume][ghost]" )
{
    CoordinatorFixture9 fx;
    const std::string prefix = "ep9_ghost";
    const std::string origRunId = prefix + "_run";

    // Seed an Interrupted run: first step completed on disk, second stuck.
    const QString outputPath = fx.checkpointDir.path() + "/ep9_ghost_first.tif";
    {
        QFile f( outputPath );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "fake" );
    }
    WorkflowRun run;
    auto def = twoStepDefinition9( prefix );
    run.setDefinition( def );
    REQUIRE( run.setRunId( origRunId ) );
    run.forceSetState( WorkflowRunState::Running );
    StepPlan firstPlan;
    firstPlan.stepId = "first";
    firstPlan.operatorId = prefix + ":first";
    firstPlan.status = "Completed";
    firstPlan.outputLayerPath = outputPath.toStdString();
    {
        const QFileInfo info( outputPath );
        firstPlan.outputSizeBytes = info.size();
        firstPlan.outputMtimeMs = info.lastModified().toMSecsSinceEpoch();
    }
    StepPlan secondPlan;
    secondPlan.stepId = "second";
    secondPlan.operatorId = prefix + ":second";
    secondPlan.status = "Running";
    run.setStepPlans( { firstPlan, secondPlan } );
    WorkflowCheckpointManager checkpoints;
    REQUIRE( false == checkpoints.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );
    auto report = fx.coordinator.recoverAtStartup( /*autoResume=*/false );
    REQUIRE( report.interruptedRuns == 1 );

    auto &engine = sicnu::jobs::JobEngine::instance();
    static std::atomic<bool> releaseResumeStep{ false };
    releaseResumeStep.store( false );
    engine.registerExecutor( prefix + ":first",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 FAIL( "completed first step must not re-execute" );
                                 return Json::Value();
                             } );
    // The resumed (remaining) step parks so the swap sees a non-terminal
    // ghost run — the exact window in which the pre-fix code erased the
    // ghost without any terminal emission.
    engine.registerExecutor( prefix + ":second",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 while ( !releaseResumeStep.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 Json::Value r( Json::objectValue );
                                 r["output"] = "/tmp/ep9_ghost_second.tif";
                                 return r;
                             } );

    // Record every (runId → state) emission on every draining thread
    // (Qt::DirectConnection — no event loop in the binary; the lambda only
    // touches statics under a mutex, so it is safe on any thread).
    static std::mutex emissionMutex;
    static std::map<std::string, std::vector<std::string>> emissions;
    emissions.clear();
    QObject slotContext;
    QObject::connect( &fx.coordinator, &WorkflowRunCoordinator::runStateChanged,
                      &slotContext,
                      []( const QString &runId, const QString &, const QString &state,
                          qint64, qint64 ) {
                          std::lock_guard<std::mutex> lock( emissionMutex );
                          emissions[runId.toStdString()].push_back( state.toStdString() );
                      },
                      Qt::DirectConnection );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( origRunId, &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    // The temporary run was broadcast Running during the resume submission;
    // the swap must have closed it with a TERMINAL broadcast.
    QString ghostRunId;
    {
        std::lock_guard<std::mutex> lock( emissionMutex );
        for ( const auto &[runId, states] : emissions )
        {
            if ( runId == origRunId )
                continue;
            const bool sawRunning =
                std::find( states.begin(), states.end(), "Running" ) != states.end();
            const bool sawTerminal =
                std::find_if( states.begin(), states.end(), []( const std::string &s ) {
                    return s == "Canceled" || s == "Failed" || s == "Completed";
                } ) != states.end();
            if ( sawRunning )
                REQUIRE( sawTerminal ); // no ghost strand, even mid-test
            if ( sawRunning && sawTerminal
                 && std::find( states.begin(), states.end(), "Canceled" ) != states.end() )
                ghostRunId = QString::fromStdString( runId );
        }
    }
    REQUIRE( !ghostRunId.isEmpty() );

    // Unwind and let the resumed run finish.
    releaseResumeStep.store( true );
    std::shared_ptr<WorkflowRun> snapshot;
    REQUIRE( waitForCondition9( [&] {
        snapshot = fx.coordinator.runForPipeline( pipelineId );
        return snapshot
               && ( snapshot->state() == WorkflowRunState::Completed
                    || snapshot->state() == WorkflowRunState::Failed );
    } ) );

    // Final invariant sweep (I7): no runId was left non-terminal.
    {
        std::lock_guard<std::mutex> lock( emissionMutex );
        for ( const auto &[runId, states] : emissions )
        {
            (void)runId;
            REQUIRE( !states.empty() );
            REQUIRE( std::any_of( states.begin(), states.end(), []( const std::string &s ) {
                return s == "Canceled" || s == "Failed" || s == "Completed"
                       || s == "Interrupted";
            } ) );
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// M2 — snapshot contract: generation monotonicity under concurrent churn
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "catalog generation is strictly monotonic under concurrent reader churn (M2)",
           "[ep9][affinity][snapshot]" )
{
    ensureApp9();
    sicnu::data::DataManager dm;
    sicnu::temporal::setWorkspaceCatalog( &dm );

    sicnu::data::SourceDescriptor base;
    base.providerKey = QStringLiteral( "gdal" );
    base.canonicalSource = QStringLiteral( "/tmp/ep9_gen_base.tif" );
    REQUIRE( !dm.registerSource( sicnu::data::RegisterRequest{ base } ).assetId.isNull() );

    const quint64 generationAtStart = dm.catalogGeneration();
    REQUIRE( generationAtStart > 0 );

    std::atomic<bool> stop{ false };
    std::atomic<int> workerReads{ 0 };
    std::atomic<bool> workerSawMonotonic{ true };
    // Worker threads read the catalog concurrently (sanctioned by the M2
    // snapshot contract — the pre-9.0 affinity warning flagged exactly this).
    std::thread reader( [&] {
        quint64 last = generationAtStart;
        while ( !stop.load( std::memory_order_relaxed ) )
        {
            const auto snap = dm.findByPath( QStringLiteral( "/tmp/ep9_gen_base.tif" ) );
            if ( snap.has_value() )
                workerReads.fetch_add( 1, std::memory_order_relaxed );
            const quint64 gen = dm.catalogGeneration();
            if ( gen < last )
                workerSawMonotonic.store( false );
            last = gen;
            std::this_thread::yield();
        }
    } );

    for ( int i = 0; i < 100; ++i )
    {
        sicnu::data::SourceDescriptor src;
        src.providerKey = QStringLiteral( "gdal" );
        src.canonicalSource = QStringLiteral( "/tmp/ep9_gen_%1.tif" ).arg( i );
        dm.registerSource( sicnu::data::RegisterRequest{ src } );
    }
    stop.store( true, std::memory_order_relaxed );
    reader.join();

    REQUIRE( workerReads.load() > 0 );
    REQUIRE( workerSawMonotonic.load() );
    REQUIRE( dm.catalogGeneration() > generationAtStart );
    sicnu::temporal::setWorkspaceCatalog( nullptr );
}

// ─────────────────────────────────────────────────────────────────────────────
// M3 — typed pause refusals (engine tasks cannot fabricate Paused)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "pause/resume keep typed behavior on engine-dispatched tasks (#702)",
           "[ep9][pause][typed]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 2 );
    engine.clearExecutors();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );

    static std::atomic<bool> releasePark{ false };
    releasePark.store( false );
    engine.registerExecutor( "ep9:park3",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 while ( !releasePark.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 return Json::Value();
                             } );

    const long task = center.submitJob( ep9Request( "ep9:park3" ) );
    REQUIRE( task > 0 );
    REQUIRE( waitForCondition9( [&] {
        return center.getTaskInfo( task ).status == sicnu::TaskStatus::Running;
    } ) );

    // An engine-dispatched task has no QgsTask handle: pausing must be
    // REFUSED (a fabricated Paused would free an admission slot while the
    // worker keeps running — #702). Resuming a non-Paused task is refused.
    REQUIRE_FALSE( center.pauseTask( task ) );
    REQUIRE_FALSE( center.resumeTask( task ) );
    REQUIRE( center.getTaskInfo( task ).status == sicnu::TaskStatus::Running );

    releasePark.store( true );
    waitForTerminalStatus9( task );
    REQUIRE( center.getTaskInfo( task ).status == sicnu::TaskStatus::Completed );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

// ─────────────────────────────────────────────────────────────────────────────
// M7 — explain dumps: stuck-run evidence
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "explain dumps expose admission and run evidence (M7)",
           "[ep9][observability]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 2 );
    engine.clearExecutors();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );
    center.setGlobalConcurrencyLimit( 1 );

    static std::atomic<bool> releaseHolder{ false };
    releaseHolder.store( false );
    engine.registerExecutor( "ep9:holder7",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 while ( !releaseHolder.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 return Json::Value();
                             } );
    const long holder = center.submitJob( ep9Request( "ep9:holder7" ) );
    REQUIRE( holder > 0 );
    const long queued = center.submitJob( ep9Request( "ep9:holder7" ) );
    REQUIRE( queued > 0 );
    REQUIRE( waitForCondition9( [&] {
        return center.getTaskInfo( queued ).status == sicnu::TaskStatus::WaitingResource;
    } ) );
    // Deterministic dump: wait until the holder's worker actually started
    // (the engine's Running record), not merely Dispatching.
    REQUIRE( waitForCondition9( [&] {
        return center.getTaskInfo( holder ).status == sicnu::TaskStatus::Running;
    } ) );

    const QString dump = center.explainDump();
    REQUIRE( dump.contains( QLatin1String( "active: total=1/1" ) ) );
    REQUIRE( dump.contains( QLatin1String( "transientChildren: 0/8" ) ) );
    REQUIRE( dump.contains( QLatin1String( "tasks[Running]: 1" ) ) );
    REQUIRE( dump.contains( QLatin1String( "saturated" ) ) );

    releaseHolder.store( true );
    waitForTerminalStatus9( holder );
    waitForTerminalStatus9( queued );

    // Coordinator dumps.
    CoordinatorFixture9 fx;
    const std::string prefix = "ep9_explain";
    engine.registerExecutor( prefix + ":first",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) { return Json::Value(); } );
    engine.registerExecutor( prefix + ":second",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) { return Json::Value(); } );
    const long pipelineId = fx.coordinator.startTrackedPipeline( twoStepDefinition9( prefix ) );
    REQUIRE( pipelineId > 0 );
    const QString runDump = fx.coordinator.explainRun( pipelineId );
    REQUIRE( runDump.contains( QLatin1String( "runId: run-" ) ) );
    REQUIRE( runDump.contains( QLatin1String( "workflowId: ep9_explain_def" ) ) );
    REQUIRE( runDump.contains( QLatin1String( "step first" ) ) );
    REQUIRE( !fx.coordinator.explainRun( 987654 ).contains( QLatin1String( "state" ) ) );
    REQUIRE( fx.coordinator.explainDump().contains( QLatin1String( "pendingNotifications" ) ) );
    REQUIRE( waitForCondition9( [&] {
        const auto snap = fx.coordinator.runForPipeline( pipelineId );
        return snap && snap->state() == WorkflowRunState::Completed;
    } ) );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

// ─────────────────────────────────────────────────────────────────────────────
// M8 — scale / fault matrix
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "deep DAG chain (300 steps) drains in order without stranding",
           "[ep9][scale][dag]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 4 );
    engine.clearExecutors();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );
    center.setGlobalConcurrencyLimit( 4 );

    static std::atomic<int> completedCount{ 0 };
    completedCount.store( 0 );
    engine.registerExecutor( "ep9:chain",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 completedCount.fetch_add( 1 );
                                 return Json::Value();
                             } );

    // 300-step linear chain: each step depends on the previous one.
    constexpr int kChainLength = 300;
    long previous = -1;
    std::vector<long> chain;
    for ( int i = 0; i < kChainLength; ++i )
    {
        QList<long> parents;
        if ( previous > 0 )
            parents.append( previous );
        previous = center.enqueueTask( QStringLiteral( "ep9:chain" ), {}, true,
                                       sicnu::TaskPriority::Normal, parents, true );
        REQUIRE( previous > 0 );
        chain.push_back( previous );
    }
    REQUIRE( waitForCondition9( [&] {
        return completedCount.load() >= kChainLength;
    }, 60000 /* 300s ceiling */, 5 ) );
    for ( long id : chain )
        REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

TEST_CASE( "cancel storm: cancelling half a running drain converges with no stranding",
           "[ep9][fault][cancel-storm]" )
{
    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 4 );
    engine.clearExecutors();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );
    center.setGlobalConcurrencyLimit( 4 );

    static std::atomic<bool> releaseAll{ false };
    releaseAll.store( false );
    engine.registerExecutor( "ep9:storm",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 while ( !releaseAll.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                                 return Json::Value();
                             } );

    constexpr int kTasks = 60;
    std::vector<long> ids;
    for ( int i = 0; i < kTasks; ++i )
    {
        const long id = center.submitJob( ep9Request( "ep9:storm" ) );
        REQUIRE( id > 0 );
        ids.push_back( id );
    }
    // Cancel every other task while all of them are queued/running.
    for ( size_t i = 0; i < ids.size(); i += 2 )
        center.cancelTask( ids[i] );

    releaseAll.store( true );
    for ( long id : ids )
    {
        waitForTerminalStatus9( id );
        REQUIRE( sicnu::isTerminalStatus( center.getTaskInfo( id ).status ) );
    }

    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

TEST_CASE( "admission structures stay bounded and correct at 100k logical tasks",
           "[ep9][scale][logical]" )
{
    // Logical scale: 100k tasks live in the admission structures (heap,
    // counts, indexes) with a parked slot holder — the data-structure
    // contract is exercised without executing 100k real jobs. Set
    // SICNU_EP9_STRESS=1 to opt in (keeps the default suite bounded).
    if ( !qEnvironmentVariableIsSet( "SICNU_EP9_STRESS" ) )
        return;

    ensureApp9();
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.setMaxWorkers( 2 );
    engine.clearExecutors();
    auto &center = TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setMaxAutoRetries( 0 );
    center.setGlobalConcurrencyLimit( 1 );

    static std::atomic<bool> releaseHolder{ false };
    releaseHolder.store( false );
    engine.registerExecutor( "ep9:wall",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 while ( !releaseHolder.load() )
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
                                 return Json::Value();
                             } );
    const long holder = center.submitJob( ep9Request( "ep9:wall" ) );
    REQUIRE( holder > 0 );
    REQUIRE( waitForCondition9( [&] {
        return center.getTaskInfo( holder ).status == sicnu::TaskStatus::Running;
    } ) );

    constexpr int kScale = 100000;
    for ( int i = 0; i < kScale; ++i )
    {
        REQUIRE( center.submitJob( ep9Request( "ep9:wall" ) ) > 0 );
    }

    // The dump reflects the logical scale; admission counters remain exact.
    const QString dump = center.explainDump();
    REQUIRE( dump.contains( QLatin1String( "active: total=1/1" ) ) );
    REQUIRE( center.getTaskInfo( holder ).status == sicnu::TaskStatus::Running );

    releaseHolder.store( true );
    REQUIRE( waitForCondition9( [&] {
        return sicnu::isTerminalStatus( center.getTaskInfo( holder ).status );
    } ) );
    engine.clearExecutors();
    center.resetResourceProfileLimits();
    center.clearCompletedTasks();
}

// ─────────────────────────────────────────────────────────────────────────────
// M6 — corrupt checkpoint: typed refusal, no partial load
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "corrupt checkpoint is refused with a typed error and no partial load (M6)",
           "[ep9][checkpoint][corrupt]" )
{
    CoordinatorFixture9 fx;
    const std::string runId = "ep9_corrupt_run";

    QFile cpFile( fx.checkpointDir.path() + "/checkpoint_" + QString::fromStdString( runId )
                  + ".json" );
    REQUIRE( cpFile.open( QIODevice::WriteOnly | QIODevice::Text ) );
    cpFile.write( "{ this is not json" );
    cpFile.close();

    QString error;
    const long pipelineId = fx.coordinator.resumeRun( runId, &error );
    REQUIRE( pipelineId == -1 );
    // Typed refusal: the error names the parse failure, not a generic miss.
    REQUIRE( error.contains( QLatin1String( "Checkpoint rejected" ) ) );
    REQUIRE( error.contains( QLatin1String( "parse" ) ) );
    // The corrupt checkpoint stays on disk (quarantine is recovery's job);
    // resume must not have deleted evidence.
    REQUIRE( cpFile.exists() );
}

TEST_CASE( "checkpoint publish fault point: crash between write and rename leaves no partial state (M6)",
           "[ep9][checkpoint][fault]" )
{
    CoordinatorFixture9 fx;
    const std::string prefix = "ep9_fault";
    auto def = twoStepDefinition9( prefix );
    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.forceSetState( WorkflowRunState::Running );

    using namespace sicnu::runtime::observability::fault;
    AllDisarmedGuard disarmGuard;

    // Injected rename failure — the crash window between the durable temp
    // write and the atomic publish: the previous checkpoint must survive
    // (or, with none, no partial file may appear) and the failure is typed.
    WorkflowCheckpointManager checkpoints;
    REQUIRE( false == checkpoints.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );
    const QString goodCheckpoint = fx.checkpointDir.path() + "/checkpoint_ep9_fault_run.json";
    REQUIRE( QFile::exists( goodCheckpoint ) );

    armFault( { "workflow_checkpoint.publish", Mode::NextN, 1, {} } );
    REQUIRE( checkpoints.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );
    // The pre-crash checkpoint is intact — no truncated payload, no tmp leak.
    REQUIRE( QFile::exists( goodCheckpoint ) );
    QDir checkpointDir( fx.checkpointDir.path() );
    REQUIRE( checkpointDir.entryList( QStringList{ QStringLiteral( "*.tmp*" ) }, QDir::Files )
                 .isEmpty() );
    // The surviving checkpoint still parses.
    QString loadError;
    REQUIRE( checkpoints.loadCheckpoint( goodCheckpoint, &loadError ) != nullptr );

    disarmFault( "workflow_checkpoint.publish" );
    // Recovery: the next save publishes normally.
    REQUIRE( false == checkpoints.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );
}
