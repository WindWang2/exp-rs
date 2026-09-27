// tests/test_workflow_crash_recovery_r4.cpp — Track 10 crash-recovery matrix
// (R4 deep edition). REAL-process injection: workflow_crash_helper runs the
// production coordinator/TaskCenter stack in a child process; the parent
// SIGKILLs it at the injection point and reconciles every assertion against
// the ON-DISK artifacts (checkpoint JSON read back as raw JSON), never
// against the recovery code's own view.
//
// Matrix anchors (charter §3.2.1):
//   IP-1  kill while holding the run lock (kernel releases flock; NoHolder
//         probe; no half state)
//   IP-2  kill mid-node-execution (committed set == disk journal set;
//         recovery reconciles; resume serves the committed set, never
//         replays it)
//   IP-3  torn checkpoint (torn-save mode; WP-B)
//   IP-4  journal commit windows (run-exit-at + crafted post-commit; WP-B)
//   IP-5  cancel propagation race (cancel-mid / cancel-done; WP-C)
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstring>

#include <json/json.h>

#include <sstream>

#include <atomic>
#include <algorithm>
#include <vector>
#include <chrono>
#include <functional>
#include <thread>

#include "jobs/job_engine.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_run_lock.h"
#include "workflow_crash_injector.h"

using namespace sicnu::workflow;

namespace {

int &crashAppArgc()
{
    static int argc = 1;
    return argc;
}
char crashAppArgv0[] = "test_workflow_crash_recovery_r4";
char *crashAppArgv[] = { crashAppArgv0, nullptr };

void ensureQtApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( crashAppArgc(), crashAppArgv );
}

/// Fresh singleton state per test: bounded workers, THIS test's checkpoint
/// dir, and a clean executor table (unique prefixes make counters exact).
struct CrashFixture
{
    QTemporaryDir scratch;
    CrashFixture()
    {
        ensureQtApp();
        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );
        WorkflowRunCoordinator::instance().setCheckpointDirectory( scratch.path() );
    }
    ~CrashFixture() { WorkflowRunCoordinator::instance().setCheckpointDirectory( QString() ); }

    QString outDir() const
    {
        const QString d = scratch.path() + QStringLiteral( "/out" );
        QDir().mkpath( d );
        return d;
    }
};

/// Bounded poll (5ms steps) for cross-process settle windows.
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

/// Waits until the run tracked for @a pipelineId reaches a terminal state.
std::shared_ptr<WorkflowRun> runToTerminal( WorkflowRunCoordinator &coordinator, long pipelineId )
{
    std::shared_ptr<WorkflowRun> snapshot;
    eventually(
        [&] {
            snapshot = coordinator.runForPipeline( pipelineId );
            return snapshot && isTerminalRunState( snapshot->state() );
        },
        30000 );
    return snapshot;
}

/// Registers a counting executor in THIS process for a child-killed run's
/// step; returns the counter so the test can prove (non-)replay.
std::shared_ptr<std::atomic<int>> registerCountingExecutor( const std::string &operatorId,
                                                            const QString &outDir,
                                                            const std::string &prefix, int step,
                                                            bool writeOutput )
{
    auto counter = std::make_shared<std::atomic<int>>( 0 );
    sicnu::jobs::JobEngine::instance().registerExecutor(
        operatorId, [counter, outDir, prefix, step, writeOutput](
                        const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
            counter->fetch_add( 1 );
            if ( writeOutput )
            {
                QFile f( outDir + QStringLiteral( "/%1_step%2.tif" ).arg( prefix.c_str() ).arg( step ) );
                f.open( QIODevice::WriteOnly | QIODevice::Truncate );
                f.write( "parent-resume-bytes\n" );
                f.close();
            }
            Json::Value payload( Json::objectValue );
            payload["output"] = ( outDir + QStringLiteral( "/%1_step%2.tif" )
                                              .arg( prefix.c_str() )
                                              .arg( step ) )
                                    .toStdString();
            return payload;
        } );
    return counter;
}

QStringList injector_read_plans( const QString &dir, const std::string &runId )
{
    QFile f( QDir( dir ).filePath(
        QStringLiteral( "checkpoint_%1.json" ).arg( QString::fromStdString( runId ) ) ) );
    QStringList ids;
    if ( !f.open( QIODevice::ReadOnly ) )
        return ids;
    const QByteArray data = f.readAll();
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::istringstream stream( data.toStdString() );
    if ( !Json::parseFromStream( builder, stream, &root, &errs ) )
        return ids;
    for ( const Json::Value &plan : root["stepPlans"] )
        ids << QString::fromStdString( plan["stepId"].asString() );
    return ids;
}

QStringList normalize( QStringList list )
{
    std::sort( list.begin(), list.end() );
    return list;
}

/// Raw checkpoint "state" reader for runs whose ids the test derived itself.
QString injector_read_state( const QString &dir, const std::string &runId )
{
    QFile f( QDir( dir ).filePath(
        QStringLiteral( "checkpoint_%1.json" ).arg( QString::fromStdString( runId ) ) ) );
    if ( !f.open( QIODevice::ReadOnly ) )
        return QString();
    const QByteArray data = f.readAll();
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::istringstream stream( data.toStdString() );
    if ( !Json::parseFromStream( builder, stream, &root, &errs ) )
        return QString();
    return root["state"].isString() ? QString::fromStdString( root["state"].asString() )
                                    : QString();
}

} // namespace

TEST_CASE( "IP-1: SIGKILL of the lock holder releases the flock with no half state",
           "[workflow][crash][r4][lock]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );
    REQUIRE( QFile::exists( WorkflowCrashInjector::helperPath() ) );

    const std::string runId = "wf4ip1_holder";
    REQUIRE( injector.spawnUntilBarrier(
        { QStringLiteral( "hold-lock" ), fx.scratch.path(), QString::fromStdString( runId ) },
        /*barrier=*/"locked" ) );

    // While the holder lives, the lock is a live owner's — seen from THIS
    // process (cross-process evidence, not an in-thread simulation).
    const QString lockPath = WorkflowRunLock::lockPathForRun( fx.scratch.path(), runId );
    {
        const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner( lockPath );
        REQUIRE( probe.state == WorkflowRunLock::OwnerProbe::State::LiveOwner );
        REQUIRE( probe.pid > 0 );
    }
    {
        WorkflowRunLock contender( lockPath );
        QString heldByPid;
        REQUIRE( contender.tryAcquire( &heldByPid ) == WorkflowRunLock::TryResult::HeldByLiveOwner );
        REQUIRE( heldByPid.toLongLong() == WorkflowRunLock::probeOwner( lockPath ).pid );
    }

    // The injection point: SIGKILL the holder. The header contract is that
    // the kernel releases the flock on ANY process death — no stale logic.
    injector.killChild();
    REQUIRE( eventually( [&] {
        return WorkflowRunLock::probeOwner( lockPath ).state == WorkflowRunLock::OwnerProbe::State::NoHolder;
    } ) );

    // No half state: the death happened before any run existed, so the
    // directory holds the lock file (never unlinked by design) but NO
    // checkpoint and NO tmp residue.
    REQUIRE( QDir( fx.scratch.path() )
                 .entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files )
                 .isEmpty() );
    REQUIRE( injector.tmpResidueCount() == 0 );

    // The next owner acquires cleanly; release is idempotent (double release
    // must not throw or corrupt anything).
    {
        WorkflowRunLock next( lockPath );
        REQUIRE( next.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
        next.release();
        next.release();
        REQUIRE_FALSE( next.isHeld() );
        // ...and the released lock is acquirable again (release is real).
        WorkflowRunLock again( lockPath );
        REQUIRE( again.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
    }
}

TEST_CASE( "IP-2: SIGKILL mid-node commits exactly the journal set and resume never replays it",
           "[workflow][crash][r4][recovery]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );

    const std::string prefix = "wf4ip2";
    REQUIRE( injector.spawnUntilBarrier(
        { QStringLiteral( "run-block-at" ), fx.scratch.path(), fx.outDir(),
          QString::fromStdString( prefix ), QStringLiteral( "2" ), QStringLiteral( "2" ) },
        /*barrier=*/"step2_running" ) );
    const std::string runId = injector.resolveRunId().toStdString();
    REQUIRE_FALSE( runId.empty() );

    // Settle window: the first node's completion fold managed its atomic
    // publish before the kill (bounded poll — content, not timing, asserts).
    REQUIRE( eventually( [&] {
        return injector.committedStepsOnDisk( runId ).contains( QStringLiteral( "step1" ) );
    } ) );

    // Injection point 2: kill while the second node executes.
    injector.killChild();

    // Journal truth read back as RAW JSON (independent of the resume code):
    // exactly step1 is committed; the run never reached a terminal state;
    // the per-fold atomic publishes left no torn tmp behind.
    REQUIRE( normalize( injector.committedStepsOnDisk( runId ) )
             == normalize( { QStringLiteral( "step1" ) } ) );
    REQUIRE( injector.tmpResidueCount() == 0 );
    REQUIRE( injector.checkpointLoads( runId ) ); // the parse gate recovery uses

    // Startup recovery: the orphan is adopted (reconciled Interrupted + the
    // flock proves the previous owner is gone).
    auto &coordinator = WorkflowRunCoordinator::instance();
    const auto report = coordinator.recoverAtStartup( /*autoResume=*/false );
    REQUIRE( report.interruptedRuns == 1 );
    REQUIRE( report.runIds.contains( QString::fromStdString( runId ) ) );

    // Resume in THIS process: step1 must be SERVED from its committed
    // journal entry (never re-executed); step2 re-executes here.
    const auto step1Runs = registerCountingExecutor( prefix + ":step1", fx.outDir(), prefix, 1,
                                                     /*writeOutput=*/false );
    const auto step2Runs = registerCountingExecutor( prefix + ":step2", fx.outDir(), prefix, 2,
                                                     /*writeOutput=*/true );
    QString err;
    const long pipelineId = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( step1Runs->load() == 0 ); // post-commit: a committed step is never replayed
    REQUIRE( step2Runs->load() >= 1 ); // uncommitted work re-executes exactly here
}

TEST_CASE( "IP-2b: kill at a later commit boundary keeps the longer committed prefix",
           "[workflow][crash][r4][recovery]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );

    const std::string prefix = "wf4ip2b";
    REQUIRE( injector.spawnUntilBarrier(
        { QStringLiteral( "run-block-at" ), fx.scratch.path(), fx.outDir(),
          QString::fromStdString( prefix ), QStringLiteral( "3" ), QStringLiteral( "3" ) },
        /*barrier=*/"step3_running" ) );
    const std::string runId = injector.resolveRunId().toStdString();
    REQUIRE_FALSE( runId.empty() );
    REQUIRE( eventually( [&] {
        return injector.committedStepsOnDisk( runId ).contains( QStringLiteral( "step1" ) );
    } ) );

    injector.killChild();

    // The journal is a best-effort lagging view by design (a failed/slow save
    // never aborts the pipeline): whatever it committed by kill time is the
    // truth resume must honor — read it back and derive the expectations
    // from the DISK, never from a schedule assumption.
    const QStringList committedAtKill = injector.committedStepsOnDisk( runId );
    INFO( "committed at kill: " << committedAtKill.join( QLatin1Char( ',' ) ).toStdString() );
    REQUIRE( committedAtKill.contains( QStringLiteral( "step1" ) ) );
    REQUIRE_FALSE( committedAtKill.contains( QStringLiteral( "step3" ) ) ); // blocked, never done
    REQUIRE( injector.tmpResidueCount() == 0 );

    auto &coordinator = WorkflowRunCoordinator::instance();
    const auto report = coordinator.recoverAtStartup( false );
    REQUIRE( report.interruptedRuns == 1 );

    const auto step1Runs = registerCountingExecutor( prefix + ":step1", fx.outDir(), prefix, 1,
                                                     false );
    const auto step2Runs = registerCountingExecutor( prefix + ":step2", fx.outDir(), prefix, 2,
                                                     false );
    const auto step3Runs = registerCountingExecutor( prefix + ":step3", fx.outDir(), prefix, 3,
                                                     true );
    QString err;
    const long pipelineId = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( step1Runs->load() == 0 ); // always committed: never replayed
    // Exactly the UNCOMMITTED steps re-execute, per the journal read above.
    REQUIRE( step2Runs->load() == ( committedAtKill.contains( QStringLiteral( "step2" ) ) ? 0 : 1 ) );
    REQUIRE( step3Runs->load() >= 1 );
}

TEST_CASE( "IP-3: death between tmp write and rename leaves the previous checkpoint intact",
           "[workflow][crash][r4][checkpoint]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );

    // The "previous consistent version": a real checkpoint written by the
    // production writer, then hashed as raw bytes.
    const std::string runId = "wf4ip3_torn";
    auto &coordinator = WorkflowRunCoordinator::instance();
    {
        WorkflowRun run;
        WorkflowDefinition def;
        def.id = "wf-" + runId;
        StepDef step;
        step.id = "step1";
        step.operatorId = "wf4:step1";
        def.steps.push_back( step );
        run.setDefinition( def );
        REQUIRE( run.setRunId( runId ) );
        run.forceSetState( WorkflowRunState::Running );
        StepPlan plan;
        plan.stepId = "step1";
        plan.operatorId = "wf4:step1";
        plan.status = "Completed";
        run.setStepPlans( { plan } );
        REQUIRE_FALSE( WorkflowCheckpointManager().saveCheckpoint( run, fx.scratch.path() )
                           .isEmpty() );
    }
    const QString checkpointPath = injector.checkpointPath( runId );
    const QByteArray intactBytes = [&] {
        QFile f( checkpointPath );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    // The injection point: a REAL process dies holding a partial tmp in the
    // EXACT production naming, before any rename could commit it.
    REQUIRE( injector.spawnUntilBarrier(
        { QStringLiteral( "torn-save" ), fx.scratch.path(), QString::fromStdString( runId ),
          QStringLiteral( "{ \"version\": 2, \"runId\": \"wf4ip3_torn\", TRUNCATED" ) },
        /*barrier=*/"staged" ) );
    injector.killChild();

    REQUIRE( injector.tmpResidueCount() == 1 );
    REQUIRE( injector.committedStepsOnDisk( runId ) == QStringList{ QStringLiteral( "step1" ) } );
    {
        QFile after( checkpointPath );
        REQUIRE( after.open( QIODevice::ReadOnly ) );
        REQUIRE( after.readAll() == intactBytes ); // byte-identical: nothing torn was promoted
    }

    // Recovery sweeps the tmp orphan (the run is unowned) and the previous
    // version keeps serving — a failed save is a lost recovery aid, never a
    // lost run.
    const auto report = coordinator.recoverAtStartup( false );
    REQUIRE( report.interruptedRuns == 1 );
    REQUIRE( report.runIds.contains( QString::fromStdString( runId ) ) );
    REQUIRE( injector.tmpResidueCount() == 0 );
    REQUIRE( injector.checkpointLoads( runId ) );
    REQUIRE( injector.committedStepsOnDisk( runId ) == QStringList{ QStringLiteral( "step1" ) } );
}

TEST_CASE( "IP-4a: hard death before a step's commit re-executes it and never serves stale bytes",
           "[workflow][crash][r4][recovery]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );

    const std::string prefix = "wf4ip4a";
    // step2 writes its output file, then _exit(70): the bytes exist on disk
    // but the journal never committed step2.
    REQUIRE( injector.spawnUntilBarrier(
        { QStringLiteral( "run-exit-at" ), fx.scratch.path(), fx.outDir(),
          QString::fromStdString( prefix ), QStringLiteral( "2" ), QStringLiteral( "2" ) },
        /*barrier=*/"step2_running" ) );
    const std::string runId = injector.resolveRunId().toStdString();
    REQUIRE_FALSE( runId.empty() );
    REQUIRE( eventually( [&] {
        return injector.committedStepsOnDisk( runId ).contains( QStringLiteral( "step1" ) );
    } ) );

    // The child died by its own hard exit; reap it.
    injector.killChild();
    REQUIRE( injector.childRunning() == false );

    // The stale step2 output exists, but the journal's committed set is
    // step1 only: pre-commit death — the uncommitted result is not trusted.
    REQUIRE( QFile::exists( fx.outDir() + QStringLiteral( "/%1_step2.tif" ).arg( prefix.c_str() ) ) );
    REQUIRE( normalize( injector.committedStepsOnDisk( runId ) )
             == normalize( { QStringLiteral( "step1" ) } ) );

    auto &coordinator = WorkflowRunCoordinator::instance();
    const auto report = coordinator.recoverAtStartup( false );
    REQUIRE( report.interruptedRuns == 1 );

    const auto step1Runs = registerCountingExecutor( prefix + ":step1", fx.outDir(), prefix, 1,
                                                     false );
    const auto step2Runs = registerCountingExecutor( prefix + ":step2", fx.outDir(), prefix, 2,
                                                     true );
    QString err;
    const long pipelineId = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( step1Runs->load() == 0 );
    REQUIRE( step2Runs->load() >= 1 ); // the stale bytes were re-earned, not served
}

TEST_CASE( "IP-4b: death after the last commit but before finalize completes the run without "
           "re-executing anything",
           "[workflow][crash][r4][recovery]" )
{
    CrashFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();

    // The on-disk picture of a death between the last step's commit persist
    // and the finalize persist: every step Completed, run state still
    // Running. Crafted through the PRODUCTION writer, then resumed through
    // the PRODUCTION recovery path.
    WorkflowCrashInjector injector( fx.scratch.path() );
    const std::string runId = "wf4ip4b_all_committed";
    const QString out1 = fx.outDir() + QStringLiteral( "/%1_s1.tif" ).arg( runId.c_str() );
    const QString out2 = fx.outDir() + QStringLiteral( "/%1_s2.tif" ).arg( runId.c_str() );
    for ( const QString &path : { out1, out2 } )
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( "committed-output\n" );
        f.close();
    }
    {
        WorkflowRun run;
        WorkflowDefinition def;
        def.id = "wf-" + runId;
        for ( const auto &stepId : { "s1", "s2" } )
        {
            StepDef step;
            step.id = stepId;
            step.operatorId = std::string( "wf4:" ) + stepId;
            def.steps.push_back( step );
        }
        run.setDefinition( def );
        REQUIRE( run.setRunId( runId ) );
        run.forceSetState( WorkflowRunState::Running );
        std::vector<StepPlan> plans;
        int n = 1;
        for ( const QString &path : { out1, out2 } )
        {
            StepPlan plan;
            plan.stepId = QStringLiteral( "s%1" ).arg( n ).toStdString();
            plan.operatorId = std::string( "wf4:s" ) + std::to_string( n );
            plan.status = "Completed";
            plan.outputLayerPath = path.toStdString();
            const QFileInfo info( path );
            plan.outputSizeBytes = info.size();
            plan.outputMtimeMs = info.lastModified().toMSecsSinceEpoch();
            plans.push_back( plan );
            ++n;
        }
        run.setStepPlans( plans );
        REQUIRE_FALSE(
            WorkflowCheckpointManager().saveCheckpoint( run, fx.scratch.path() ).isEmpty() );
    }

    const auto step1Runs = registerCountingExecutor( "wf4:s1", fx.outDir(), runId, 1, false );
    const auto step2Runs = registerCountingExecutor( "wf4:s2", fx.outDir(), runId, 2, false );

    const auto report = coordinator.recoverAtStartup( false );
    REQUIRE( report.interruptedRuns == 1 );

    QString err;
    const long resumeOutcome = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    // Nothing left to execute: the run finalizes from its committed set
    // (#1078a) instead of refusing forever. resumeRun returns 0 for that
    // path; -1 would mean the gate refused to serve a committed step.
    REQUIRE( resumeOutcome == 0 );
    REQUIRE( step1Runs->load() == 0 );
    REQUIRE( step2Runs->load() == 0 );

    // The reconciled world is terminal-Completed with the committed set intact.
    const QString path = injector.checkpointPath( runId );
    if ( QFile::exists( path ) )
    {
        REQUIRE( injector.checkpointStateOnDisk( runId ) == QStringLiteral( "Completed" ) );
        REQUIRE( normalize( injector.committedStepsOnDisk( runId ) )
                 == normalize( { QStringLiteral( "s1" ), QStringLiteral( "s2" ) } ) );
    }
    else
    {
        // Completed runs archive to history/ (GC sweep) — also terminal.
        REQUIRE( QFile::exists( injector.archivedCheckpointPath( runId ) ) );
    }
}

TEST_CASE( "IP-5a: death during cancel propagation keeps the completed prefix and the run resumable",
           "[workflow][crash][r4][cancel]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );

    const std::string prefix = "wf4ip5a";
    // step1 completes; step2 runs cancellably; the child cancels through the
    // coordinator (Cancelling is persisted BEFORE propagation) and the
    // parent kills it in or around that window.
    REQUIRE( injector.spawnUntilBarrier(
        { QStringLiteral( "cancel-mid" ), fx.scratch.path(), fx.outDir(),
          QString::fromStdString( prefix ), QStringLiteral( "2" ), QStringLiteral( "2" ) },
        /*barrier=*/"cancel_persisted" ) );
    const std::string runId = injector.resolveRunId().toStdString();
    REQUIRE_FALSE( runId.empty() );
    REQUIRE( eventually( [&] {
        return injector.committedStepsOnDisk( runId ).contains( QStringLiteral( "step1" ) );
    } ) );

    // Positive pin of the ordering contract: at the barrier, cancelRun has
    // returned, so the Cancelling (or later) verdict is already on disk —
    // checkpoint first, propagation second is the production order.
    const QString stateAtBarrier = injector.checkpointStateOnDisk( runId );
    INFO( "state at barrier: " << stateAtBarrier.toStdString() );
    REQUIRE( ( stateAtBarrier == QStringLiteral( "Cancelling" )
               || stateAtBarrier == QStringLiteral( "Canceled" ) ) );

    injector.killChild();

    // Whatever the race landed (still Cancelling on disk, or the terminal
    // Canceled roll-up won before the kill), the completed prefix is
    // committed and the world is consistent.
    REQUIRE( normalize( injector.committedStepsOnDisk( runId ) )
             == normalize( { QStringLiteral( "step1" ) } ) );
    const QString diskState = injector.checkpointStateOnDisk( runId );
    INFO( "disk state after the kill: " << diskState.toStdString() );
    REQUIRE( ( diskState == QStringLiteral( "Running" ) || diskState == QStringLiteral( "Cancelling" )
               || diskState == QStringLiteral( "Canceled" ) ) );

    auto &coordinator = WorkflowRunCoordinator::instance();
    const auto report = coordinator.recoverAtStartup( false );
    if ( diskState != QStringLiteral( "Canceled" ) )
    {
        // The non-terminal pictures reconcile to Interrupted and resumable.
        REQUIRE( report.interruptedRuns == 1 );
        REQUIRE( injector.checkpointStateOnDisk( runId ) == QStringLiteral( "Interrupted" ) );
    }
    else
    {
        // A terminal world is inert for recovery (nothing to adopt).
        REQUIRE( report.interruptedRuns == 0 );
    }

    // 取消是粘性的: the cancel never vanished — the run needed an explicit
    // resume, and after it the completed step1 is still not replayed.
    const auto step1Runs = registerCountingExecutor( prefix + ":step1", fx.outDir(), prefix, 1,
                                                     false );
    const auto step2Runs = registerCountingExecutor( prefix + ":step2", fx.outDir(), prefix, 2,
                                                     true );
    QString err;
    const long pipelineId = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( step1Runs->load() == 0 );
    REQUIRE( step2Runs->load() >= 1 );
}

TEST_CASE( "IP-5b: death after cancel fully propagated leaves a terminal, unowned, resumable world",
           "[workflow][crash][r4][cancel]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );

    const std::string prefix = "wf4ip5b";
    REQUIRE( injector.spawnUntilBarrier(
        { QStringLiteral( "cancel-done" ), fx.scratch.path(), fx.outDir(),
          QString::fromStdString( prefix ), QStringLiteral( "2" ), QStringLiteral( "3" ) },
        /*barrier=*/"cancel_terminal" ) );
    const std::string runId = injector.resolveRunId().toStdString();
    REQUIRE_FALSE( runId.empty() );

    // The child hard-exited after the terminal persist; reap it.
    injector.killChild();

    REQUIRE( injector.checkpointStateOnDisk( runId ) == QStringLiteral( "Canceled" ) );
    REQUIRE( normalize( injector.committedStepsOnDisk( runId ) )
             == normalize( { QStringLiteral( "step1" ) } ) );

    // Finalize released the run lock BEFORE the death: nobody holds it.
    const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner(
        WorkflowRunLock::lockPathForRun( fx.scratch.path(), runId ) );
    REQUIRE( probe.state == WorkflowRunLock::OwnerProbe::State::NoHolder );

    // Terminal runs are inert for recovery adoption...
    auto &coordinator = WorkflowRunCoordinator::instance();
    REQUIRE( coordinator.recoverAtStartup( false ).interruptedRuns == 0 );

    // ...and an explicit resume still serves the committed prefix.
    const auto step1Runs = registerCountingExecutor( prefix + ":step1", fx.outDir(), prefix, 1,
                                                     false );
    const auto step2Runs = registerCountingExecutor( prefix + ":step2", fx.outDir(), prefix, 2,
                                                     true );
    const auto step3Runs = registerCountingExecutor( prefix + ":step3", fx.outDir(), prefix, 3,
                                                     false );
    QString err;
    const long pipelineId = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );
    const auto snapshot = runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( step1Runs->load() == 0 );
    REQUIRE( step2Runs->load() >= 1 ); // was cancelled: must re-execute
    REQUIRE( step3Runs->load() >= 1 ); // never ran: must re-execute — no silent skips
}

// --- Track 10 WP-D: boundary second pass ------------------------------------

TEST_CASE( "edge: an empty DAG is refused with a Failed on-disk run and no lock left behind",
           "[workflow][r4][edge][coordinator]" )
{
    CrashFixture fx;
    auto &coordinator = WorkflowRunCoordinator::instance();

    WorkflowDefinition def;
    def.id = "wf4_edge_empty";
    def.title = "empty";
    const long pipelineId = coordinator.startTrackedPipeline( def, /*autoLoad=*/false );
    // Found by this edge probe (pre-fix behavior: dispatched as an empty
    // pipeline, then wedged Running forever with the flock held — the
    // allTerminal roll-up requires non-empty plans). The coordinator now
    // refuses the degenerate document up front.
    REQUIRE( pipelineId == -1 );

    // The refusal persisted a terminal, explainable run — never a silent
    // half state — and the run lock was never taken.
    QDir dir( fx.scratch.path() );
    const QStringList checkpoints = dir.entryList(
        QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files );
    REQUIRE( checkpoints.size() == 1 );
    const QString stem = checkpoints.front().chopped( strlen( ".json" ) );
    const std::string runId = stem.mid( strlen( "checkpoint_" ) ).toStdString();
    REQUIRE( injector_read_state( fx.scratch.path(), runId ) == "Failed" );
    REQUIRE( injector_read_plans( fx.scratch.path(), runId ).isEmpty() );

    const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner(
        WorkflowRunLock::lockPathForRun( fx.scratch.path(), runId ) );
    REQUIRE( probe.state == WorkflowRunLock::OwnerProbe::State::NoHolder );

    // The resumable-cycle contract stays consistent for the degenerate
    // lineage: resume of the Failed run finalizes it Completed from an empty
    // remaining set (#1078a), re-executing nothing.
    QString err;
    const long resumeOutcome = coordinator.resumeRun( runId, &err );
    INFO( err.toStdString() );
    REQUIRE( resumeOutcome == 0 );
    // Completed runs archive to history/ — check both locations.
    const QString stateAfter = injector_read_state( fx.scratch.path(), runId ).isEmpty()
                                   ? injector_read_state(
                                       QDir( fx.scratch.path() ).filePath( QStringLiteral( "history" ) ),
                                       runId )
                                   : injector_read_state( fx.scratch.path(), runId );
    REQUIRE( stateAfter == "Completed" );
}

TEST_CASE( "edge: a single-node DAG completes, archives, and leaves the world terminal",
           "[workflow][r4][edge][coordinator]" )
{
    CrashFixture fx;
    WorkflowCrashInjector injector( fx.scratch.path() );
    auto &coordinator = WorkflowRunCoordinator::instance();

    const std::string prefix = "wf4_edge_single";
    const auto stepRuns = registerCountingExecutor( prefix + ":only", fx.outDir(), prefix, 1,
                                                    true );
    WorkflowDefinition def;
    def.id = prefix + "_def";
    StepDef step;
    step.id = "only";
    step.operatorId = prefix + ":only";
    step.params["output"] = ( fx.outDir() + QStringLiteral( "/%1_only.tif" ).arg( prefix.c_str() ) )
                                .toStdString();
    def.steps.push_back( step );

    const long pipelineId = coordinator.startTrackedPipeline( def, /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    const auto snapshot = runToTerminal( coordinator, pipelineId );
    REQUIRE( snapshot );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( stepRuns->load() == 1 );
    // Completed runs archive to history/ — the durable terminal world.
    REQUIRE( QFile::exists( injector.archivedCheckpointPath( snapshot->runId() ) ) );
}
