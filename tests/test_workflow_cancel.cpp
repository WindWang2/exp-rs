// tests/test_workflow_cancel.cpp
//
// Workflow runtime cooperative cancellation: requestCancel() must be observed
// by a long-running operator step mid-run through RSOperatorContext.
//
// Track 10 durability R4 extensions: the COORDINATOR cancel loop — cancelRun
// reaches a running step through TaskCenter, lands Canceled with the
// completed prefix committed, cannot resurrect a terminal run, and a run
// being canceled is refused to a concurrent resume until it is terminal.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_run_lock.h"
#include "workflow/workflow_runtime.h"

using namespace sicnu::workflow;
using namespace sicnu::operators;

namespace {

int &cancelAppArgc()
{
  static int argc = 1;
  return argc;
}
char cancelAppArgv0[] = "test_workflow_cancel";
char *cancelAppArgv[] = {cancelAppArgv0, nullptr};

void ensureQtApp()
{
  if ( !QCoreApplication::instance() )
    new QCoreApplication( cancelAppArgc(), cancelAppArgv );
}

/// Long-running operator that polls the cooperative cancel flag.
class SlowOperator : public RSOperator
{
public:
    std::string name() const override { return "test:slow"; }
    std::string displayName() const override { return "Slow"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Loops until cancelled."; }

    Json::Value schema() const override { return Json::Value( Json::objectValue ); }
    Json::Value metadata() const override
    {
        Json::Value meta( Json::objectValue );
        meta["supportsCancellation"] = true;
        return meta;
    }

    Json::Value run( const Json::Value &, RSOperatorContext &context ) override
    {
        for ( int i = 0; i < 1'000'000; ++i )
        {
            context.throwIfCancelled();
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        Json::Value result( Json::objectValue );
        result["result"] = "finished-unexpectedly";
        return result;
    }
};

} // namespace

TEST_CASE( "WorkflowRuntime::requestCancel aborts a running operator step", "[workflow][cancel]" )
{
    ensureQtApp();
    auto &reg = RSOperatorRegistry::instance();
    if ( !reg.hasOperator( "test:slow" ) )
    {
        reg.registerOperator( "test:slow", []() -> std::unique_ptr<RSOperator> {
            return std::make_unique<SlowOperator>();
        } );
    }

    WorkflowRuntime runtime( /*loadBuiltins=*/false );
    WorkflowDefinition def;
    def.id = "wf:cancel_test";
    def.title = "Cancel Test";
    StepDef step;
    step.id = "slow";
    step.title = "Slow";
    step.kind = StepKind::Operator;
    step.operatorId = "test:slow";
    def.steps.push_back( step );
    runtime.registerDefinition( def );

    const std::string sessionId = runtime.open( "wf:cancel_test" );
    REQUIRE_FALSE( sessionId.empty() );

    std::atomic<bool> started{ false };
    std::string runError;
    bool threw = false;

    std::thread runner( [&]() {
        started.store( true );
        try
        {
            runtime.runStep( sessionId, "slow" );
        }
        catch ( const std::exception &e )
        {
            threw = true;
            runError = e.what();
        }
    } );

    // Wait for the step to actually start, then cancel it.
    while ( !started.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    runtime.requestCancel( sessionId );

    runner.join();

    REQUIRE( threw );
    REQUIRE( runError.find( "cancelled" ) != std::string::npos );
}

namespace {

int &cancelCoordAppArgc()
{
    static int argc = 1;
    return argc;
}
char cancelCoordArgv0[] = "test_workflow_cancel_coord";
char *cancelCoordArgv[] = { cancelCoordArgv0, nullptr };

/// Bounded poll for content-based readiness (never a sleep race).
bool eventually( const std::function<bool()> &predicate, int timeoutMs = 15000 )
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while ( QDateTime::currentMSecsSinceEpoch() < deadline )
    {
        if ( predicate() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return predicate();
}

/// Coordinator-lane fixture: bounded JobEngine workers, a fresh checkpoint
/// directory per test, and a cancellable executor table.
struct CancelCoordinatorFixture
{
    QTemporaryDir scratch;

    CancelCoordinatorFixture()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( cancelCoordAppArgc(), cancelCoordArgv );
        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );
        WorkflowRunCoordinator::instance().setCheckpointDirectory( scratch.path() );
    }
    ~CancelCoordinatorFixture()
    {
        WorkflowRunCoordinator::instance().setCheckpointDirectory( QString() );
    }

    static WorkflowDefinition twoStepDefinition( const std::string &prefix )
    {
        WorkflowDefinition def;
        def.id = prefix + "_def";
        def.title = prefix;
        StepDef first;
        first.id = "first";
        first.operatorId = prefix + ":first";
        first.params["output"] = ( prefix + "_first.tif" );
        StepDef second;
        second.id = "second";
        second.operatorId = prefix + ":second";
        second.params["input"] = "$first.output";
        second.params["output"] = ( prefix + "_second.tif" );
        StepConnection conn;
        conn.fromStepId = "first";
        conn.fromPort = "output";
        conn.toPort = "input";
        second.inputs.push_back( conn );
        def.steps = { first, second };
        return def;
    }

    /// Registers the fast first-step executor (writes a real output file).
    static std::shared_ptr<std::atomic<int>> registerFastFirst( const std::string &prefix )
    {
        auto counter = std::make_shared<std::atomic<int>>( 0 );
        sicnu::jobs::JobEngine::instance().registerExecutor(
            prefix + ":first",
            [counter, prefix]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
                counter->fetch_add( 1 );
                QFile f( ( prefix + "_first.tif" ).c_str() );
                f.open( QIODevice::WriteOnly | QIODevice::Truncate );
                f.write( "first-bytes\n" );
                f.close();
                Json::Value payload( Json::objectValue );
                payload["output"] = prefix + "_first.tif";
                return payload;
            } );
        return counter;
    }

    /// Registers the cancellable second-step executor (loops on the
    /// cooperative cancel flag until cancelled or killed).
    static std::shared_ptr<std::atomic<int>> registerCancellableSecond( const std::string &prefix )
    {
        auto counter = std::make_shared<std::atomic<int>>( 0 );
        sicnu::jobs::JobEngine::instance().registerExecutor(
            prefix + ":second",
            [counter]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
                counter->fetch_add( 1 );
                for ( ;; )
                {
                    ctx.throwIfCancelled();
                    std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                }
                return Json::Value( Json::objectValue );
            } );
        return counter;
    }

    /// Registers a completing (payload-only) second-step executor with a
    /// run counter — for resume legs where the step must visibly re-execute.
    static std::shared_ptr<std::atomic<int>> registerCountingCompletingSecond(
        const std::string &prefix )
    {
        auto counter = std::make_shared<std::atomic<int>>( 0 );
        sicnu::jobs::JobEngine::instance().registerExecutor(
            prefix + ":second",
            [counter]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
                counter->fetch_add( 1 );
                return Json::Value( Json::objectValue );
            } );
        return counter;
    }

    static std::shared_ptr<WorkflowRun> runToTerminal( WorkflowRunCoordinator &coordinator,
                                                       long pipelineId )
    {
        std::shared_ptr<WorkflowRun> snapshot;
        for ( int i = 0; i < 3000; ++i )
        {
            snapshot = coordinator.runForPipeline( pipelineId );
            if ( snapshot && isTerminalRunState( snapshot->state() ) )
                return snapshot;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        return snapshot;
    }
};

} // namespace

TEST_CASE( "cancelRun reaches a running step and lands Canceled with the prefix committed",
           "[workflow][cancel][r4][coordinator]" )
{
    CancelCoordinatorFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();
    const std::string prefix = "wf4cancel1";

    const auto firstRuns = CancelCoordinatorFixture::registerFastFirst( prefix );
    const auto secondRuns = CancelCoordinatorFixture::registerCancellableSecond( prefix );

    const long pipelineId =
        coordinator.startTrackedPipeline( CancelCoordinatorFixture::twoStepDefinition( prefix ),
                                          /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );

    // Cancel only once the second step is actually executing (content-based
    // readiness, not a sleep race).
    REQUIRE( eventually( [&] { return secondRuns->load() > 0; } ) );
    REQUIRE( coordinator.cancelRun( pipelineId ) );

    const auto snapshot = CancelCoordinatorFixture::runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Canceled );

    // The completed prefix is committed to the on-disk journal; the canceled
    // step is NOT recorded as Completed.
    const auto committed = [&] {
        QStringList ids;
        const QString path = fx.scratch.path() + QDir::separator()
                             + QStringLiteral( "checkpoint_%1.json" )
                                   .arg( QString::fromStdString( snapshot->runId() ) );
        REQUIRE( QFile::exists( path ) );
        // Reuse the checkpoint loader as the parse gate; statuses from the run.
        QString err;
        const auto run = WorkflowCheckpointManager().loadCheckpoint( path, &err );
        REQUIRE( run );
        for ( const auto &plan : run->stepPlans() )
            if ( plan.status == "Completed" )
                ids << QString::fromStdString( plan.stepId );
        return ids;
    }();
    REQUIRE( committed == QStringList{ QStringLiteral( "first" ) } );
    REQUIRE( firstRuns->load() == 1 );
    REQUIRE( secondRuns->load() == 1 ); // the cancel threw exactly once

    // Terminal Canceled releases the run lock: nobody owns it anymore.
    REQUIRE( WorkflowRunLock::probeOwner( WorkflowRunLock::lockPathForRun(
                 fx.scratch.path(), snapshot->runId() ) ).state
             == WorkflowRunLock::OwnerProbe::State::NoHolder );
}

TEST_CASE( "a rejected empty lineage refuses resume and never escalates Failed to Completed",
           "[workflow][cancel][r5][coordinator][recovery]" )
{
    CancelCoordinatorFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();

    // A zero-step submission is refused up front (Track 10): -1 to the
    // caller, a persisted Failed run, and no lock was ever taken.
    WorkflowDefinition empty;
    empty.id = "wf5empty_def";
    empty.title = "Empty";
    REQUIRE( coordinator.startTrackedPipeline( empty, /*autoLoad=*/false ) == -1 );

    // The rejection persists a Failed checkpoint under a generated runId —
    // the identity the runs index (WorkspaceService) surfaces to users.
    const QStringList checkpoints = QDir( fx.scratch.path() ).entryList(
        QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files );
    REQUIRE( checkpoints.size() == 1 );
    const QString entry = checkpoints.first();
    const std::string runId =
        entry.mid( QStringLiteral( "checkpoint_" ).size(),
                   entry.size() - QStringLiteral( "checkpoint_" ).size()
                       - QStringLiteral( ".json" ).size() )
            .toStdString();
    const QString path = fx.scratch.path() + QDir::separator() + entry;
    const QByteArray before = [&] {
        QFile f( path );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    // Resume refuses: a zero-step lineage has nothing to execute, and the
    // #1078a empty-remaining derivation must not fabricate a Completed
    // verdict over a Failed one with zero execution.
    QString err;
    REQUIRE( coordinator.resumeRun( runId, &err ) == -1 );
    INFO( err.toStdString() );
    REQUIRE( err.contains( QStringLiteral( "no steps" ) ) );

    // The on-disk verdict is untouched: still Failed, byte-identical
    // envelope/lineage/provenance (a refused resume persists nothing).
    const QByteArray after = [&] {
        QFile f( path );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();
    REQUIRE( after == before );
    QString loadErr;
    const auto run = WorkflowCheckpointManager().loadCheckpoint( path, &loadErr );
    REQUIRE( run );
    REQUIRE( run->state() == WorkflowRunState::Failed );
    REQUIRE( WorkflowRunLock::probeOwner( WorkflowRunLock::lockPathForRun(
                 fx.scratch.path(), runId ) ).state
             == WorkflowRunLock::OwnerProbe::State::NoHolder );
}

TEST_CASE( "a legacy ACTIVE zero-step lineage converges to Interrupted on refused resume",
           "[workflow][cancel][r5][coordinator][recovery]" )
{
    // Pre-Track-10 builds could persist a zero-step run in an ACTIVE state
    // (the wedge the Track 10 refusal replaced). The resume gate sits AFTER
    // the inline reconcile, so such a legacy checkpoint still converges —
    // Interrupted, persisted, then refused — instead of being re-elected by
    // every recoverAtStartup pass forever.
    CancelCoordinatorFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();

    WorkflowDefinition empty;
    empty.id = "wf5legacy_def";
    empty.title = "legacy empty";
    auto run = WorkflowRun::createFromDefinition( empty, "wf5legacy_empty" );
    REQUIRE( run );
    run->transitionTo( WorkflowRunState::Planning );
    run->transitionTo( WorkflowRunState::Ready );
    run->transitionTo( WorkflowRunState::Running );
    REQUIRE_FALSE( WorkflowCheckpointManager().saveCheckpoint( *run, fx.scratch.path() ).isEmpty() );

    QString err;
    REQUIRE( coordinator.resumeRun( "wf5legacy_empty", &err ) == -1 );
    INFO( err.toStdString() );
    REQUIRE( err.contains( QStringLiteral( "no steps" ) ) );

    // Converged, not stranded: the on-disk state is the recovery-terminal
    // Interrupted (a non-candidate for the next pass), and the lock is free.
    const QString path = fx.scratch.path() + QDir::separator()
                         + QStringLiteral( "checkpoint_wf5legacy_empty.json" );
    QString loadErr;
    const auto loaded = WorkflowCheckpointManager().loadCheckpoint( path, &loadErr );
    REQUIRE( loaded );
    REQUIRE( loaded->state() == WorkflowRunState::Interrupted );
    REQUIRE( WorkflowRunLock::probeOwner( WorkflowRunLock::lockPathForRun(
                 fx.scratch.path(), "wf5legacy_empty" ) ).state
             == WorkflowRunLock::OwnerProbe::State::NoHolder );
}

TEST_CASE( "resume state matrix: terminal verdicts are refused with their specific reason",
           "[workflow][cancel][r5][coordinator][recovery]" )
{
    CancelCoordinatorFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();
    const std::string prefix = "wf5matrix";

    sicnu::jobs::JobEngine::instance().registerExecutor(
        prefix + ":first",
        []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
            return Json::Value( Json::objectValue );
        } );
    sicnu::jobs::JobEngine::instance().registerExecutor(
        prefix + ":second",
        []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
            return Json::Value( Json::objectValue );
        } );

    // --- Completed lineage -------------------------------------------------
    const long completedPipeline =
        coordinator.startTrackedPipeline( CancelCoordinatorFixture::twoStepDefinition( prefix ),
                                          /*autoLoad=*/false );
    REQUIRE( completedPipeline > 0 );
    const auto completedSnapshot =
        CancelCoordinatorFixture::runToTerminal( coordinator, completedPipeline );
    REQUIRE( completedSnapshot );
    REQUIRE( completedSnapshot->state() == WorkflowRunState::Completed );
    const std::string completedRunId = completedSnapshot->runId();

    // A Completed lineage is not resumable. The terminal checkpoint may
    // already be in history/ (the finalize sweep archives Completed runs
    // concurrently with this call), so the typed refusal is EITHER the
    // state wall naming Completed OR the no-checkpoint refusal — never a
    // successful resume and never a re-execution.
    QString err;
    REQUIRE( coordinator.resumeRun( completedRunId, &err ) == -1 );
    INFO( err.toStdString() );
    REQUIRE( ( err.contains( QStringLiteral( "only interrupted/failed/canceled runs resume" ) )
               || err.contains( QStringLiteral( "No checkpoint for run" ) ) ) );

    // The durable verdict is untouched wherever it lives (live dir or
    // history/ archive): still Completed.
    const QString completedPath = [&] {
        const QString live = fx.scratch.path() + QDir::separator()
                             + QStringLiteral( "checkpoint_%1.json" )
                                   .arg( QString::fromStdString( completedRunId ) );
        if ( QFile::exists( live ) )
            return live;
        return fx.scratch.path() + QDir::separator() + QStringLiteral( "history" )
               + QDir::separator() + QStringLiteral( "checkpoint_%1.json" )
                     .arg( QString::fromStdString( completedRunId ) );
    }();
    REQUIRE( QFile::exists( completedPath ) );
    QString loadErr;
    const auto completedRun =
        WorkflowCheckpointManager().loadCheckpoint( completedPath, &loadErr );
    REQUIRE( completedRun );
    REQUIRE( completedRun->state() == WorkflowRunState::Completed );

    // --- Completed checkpoint crafted on disk ------------------------------
    // A Completed lineage is RESUME-REFUSED with the exact state-wall text.
    // Written directly (no coordinator finalize) so the checkpoint stays
    // LIVE — no archive race in the message assertion. Failed/Canceled/
    // Interrupted are the resumable states (covered by the other cases in
    // this file); Completed is the terminal wall of the resume matrix.
    auto completedOnDisk = WorkflowRun::createFromDefinition(
        CancelCoordinatorFixture::twoStepDefinition( prefix ), "wf5matrix_completed" );
    REQUIRE( completedOnDisk );
    completedOnDisk->transitionTo( WorkflowRunState::Planning );
    completedOnDisk->transitionTo( WorkflowRunState::Ready );
    completedOnDisk->transitionTo( WorkflowRunState::Running );
    completedOnDisk->transitionTo( WorkflowRunState::Completed );
    REQUIRE_FALSE( WorkflowCheckpointManager()
                       .saveCheckpoint( *completedOnDisk, fx.scratch.path() )
                       .isEmpty() );

    QString wallErr;
    REQUIRE( coordinator.resumeRun( completedOnDisk->runId(), &wallErr ) == -1 );
    INFO( wallErr.toStdString() );
    REQUIRE( wallErr.contains( QStringLiteral( "only interrupted/failed/canceled runs resume" ) ) );
    REQUIRE( wallErr.contains( QStringLiteral( "Completed" ) ) );

    // A refused resume never rewrites the verdict on disk.
    const QString wallPath = fx.scratch.path() + QDir::separator()
                             + QStringLiteral( "checkpoint_%1.json" )
                                   .arg( QString::fromStdString( completedOnDisk->runId() ) );
    const auto wallRun = WorkflowCheckpointManager().loadCheckpoint( wallPath, &loadErr );
    REQUIRE( wallRun );
    REQUIRE( wallRun->state() == WorkflowRunState::Completed );
}

TEST_CASE( "a resume storm racing a completing run never resurrects a Completed lineage",
           "[workflow][cancel][r5][coordinator][recovery]" )
{
    CancelCoordinatorFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();
    const std::string prefix = "wf5matrixrace";

    const auto firstRuns = CancelCoordinatorFixture::registerFastFirst( prefix );
    // Second step completes instantly (payload only) so the whole run
    // finalizes within milliseconds — the resume race has to win the window
    // between the in-memory terminal flip and the durable checkpoint, not a
    // wide open door.
    sicnu::jobs::JobEngine::instance().registerExecutor(
        prefix + ":second",
        []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
            return Json::Value( Json::objectValue );
        } );

    const long pipelineId =
        coordinator.startTrackedPipeline( CancelCoordinatorFixture::twoStepDefinition( prefix ),
                                          /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    const auto liveRun = coordinator.runForPipeline( pipelineId );
    REQUIRE( liveRun );
    const std::string runId = liveRun->runId();

    // Hammer resumeRun from a second thread across the run's whole lifetime:
    // live (ownership flock), finalizing (lock held until the terminal
    // checkpoint is durable), and terminal (state wall / archived checkpoint).
    // A Completed lineage is NEVER resumable, so every attempt must refuse.
    std::atomic<bool> stop{ false };
    std::atomic<int> successes{ 0 };
    QString stormError;
    std::thread storm( [&]() {
        while ( !stop.load() )
        {
            QString err;
            const long resumed = coordinator.resumeRun( runId, &err );
            if ( resumed >= 0 )
                successes.fetch_add( 1 );
            stormError = err;
        }
    } );

    const auto snapshot = CancelCoordinatorFixture::runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    stop.store( true );
    storm.join();

    INFO( stormError.toStdString() );
    REQUIRE( successes.load() == 0 );  // no resume resurrected a Completed run
    REQUIRE( firstRuns->load() == 1 ); // and the first step never re-executed
}

TEST_CASE( "cancelRun cannot resurrect or re-terminal a completed run",
           "[workflow][cancel][r4][coordinator]" )
{
    CancelCoordinatorFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();
    const std::string prefix = "wf4cancel2";

    const auto firstRuns = CancelCoordinatorFixture::registerFastFirst( prefix );
    // Second step completes instantly too (payload only, no file needed for
    // the aggregate roll-up — the checkpoint just records the outcome).
    sicnu::jobs::JobEngine::instance().registerExecutor(
        prefix + ":second",
        []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
            return Json::Value( Json::objectValue );
        } );

    const long pipelineId =
        coordinator.startTrackedPipeline( CancelCoordinatorFixture::twoStepDefinition( prefix ),
                                          /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    const auto snapshot = CancelCoordinatorFixture::runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( firstRuns->load() == 1 );

    // A late cancel must not flip the terminal run to Cancelling/Canceled
    // (the guarded transition table refuses Running→… from a terminal state;
    // an unknown pipeline is a plain false).
    REQUIRE_FALSE( coordinator.cancelRun( pipelineId ) );
    REQUIRE_FALSE( coordinator.cancelRun( pipelineId + 12345 ) );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
}

TEST_CASE( "a run being canceled is refused to a concurrent resume until terminal, then resumes "
           "from its committed prefix",
           "[workflow][cancel][r4][coordinator][recovery]" )
{
    CancelCoordinatorFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();
    const std::string prefix = "wf4cancel3";

    const auto firstRuns = CancelCoordinatorFixture::registerFastFirst( prefix );
    const auto secondRuns = CancelCoordinatorFixture::registerCancellableSecond( prefix );

    const long pipelineId =
        coordinator.startTrackedPipeline( CancelCoordinatorFixture::twoStepDefinition( prefix ),
                                          /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    REQUIRE( eventually( [&] { return secondRuns->load() > 0; } ) );

    // While the run is live, a resume of the SAME runId is refused — not
    // blocked (the ownership refusal is immediate), with an explanatory error.
    const std::string runId = [&] {
        const auto run = coordinator.runForPipeline( pipelineId );
        REQUIRE( run );
        return run->runId();
    }();
    QString refusal;
    REQUIRE( coordinator.resumeRun( runId, &refusal ) == -1 );
    // The refusal is specifically the OWNERSHIP gate (the live coordinator
    // holds the run's flock — same process, second descriptor), never a
    // silent block and never the weaker tracked-gate message.
    INFO( refusal.toStdString() );
    REQUIRE( refusal.contains( QStringLiteral( "owned by a live process" ) ) );

    REQUIRE( coordinator.cancelRun( pipelineId ) );
    const auto snapshot = CancelCoordinatorFixture::runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Canceled );

    // After the terminal, the same lineage resumes: the committed first step
    // is served (its executor does not run again); the canceled second step
    // re-executes in THIS submission and the run completes.
    sicnu::jobs::JobEngine::instance().clearExecutors();
    const auto resumedFirst = CancelCoordinatorFixture::registerFastFirst( prefix );
    const auto resumedSecond =
        CancelCoordinatorFixture::registerCountingCompletingSecond( prefix );

    QString err;
    const long resumePipeline = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    REQUIRE( resumePipeline > 0 );
    const auto resumed = CancelCoordinatorFixture::runToTerminal( coordinator, resumePipeline );
    REQUIRE( resumed );
    REQUIRE( resumed->state() == WorkflowRunState::Completed );
    REQUIRE( resumed->runId() == runId ); // one lineage thread across the resume
    REQUIRE( resumedFirst->load() == 0 ); // committed prefix served, not replayed
    REQUIRE( resumedSecond->load() >= 1 ); // the canceled step visibly re-executed
}
