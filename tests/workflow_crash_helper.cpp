// tests/workflow_crash_helper.cpp — crash-injection helper for the Track 10
// durability lanes. One binary, several deterministic death windows; the
// parent test (test_workflow_crash_recovery_r4.cpp) spawns it, waits for
// barrier FILES in the run directory, then SIGKILLs it at the injection
// point. Every mode drives the PRODUCTION primitives (WorkflowRunLock,
// WorkflowRunCoordinator/TaskCenter/JobEngine, WorkflowCheckpointManager's
// tmp naming) — the helper only chooses where the process dies.
//
// Barrier protocol: modes announce progress by creating empty marker files
// (<dir>/barrier_<name>) — presence is atomic and cross-process visible;
// stdout carries the "RUN <runId> <pipelineId>" line for correlation.
//
// Modes:
//   hold-lock <dir> <runId>
//       Acquire the run's flock, barrier "locked", then spin until killed.
//   run-block-at <dir> <outDir> <prefix> <blockStep> <totalSteps>
//       Run a REAL tracked pipeline of totalSteps chained steps through
//       WorkflowRunCoordinator::startTrackedPipeline. Every step < blockStep
//       writes a real output file and completes (its fold commits the
//       checkpoint); step blockStep barriers "step<k>_running" and spins
//       until killed.
//   run-exit-at <dir> <outDir> <prefix> <exitStep> <totalSteps>
//       Same pipeline; step exitStep writes its output file, barriers, then
//       _exit(70) — hard death before its completion can be folded/committed.
//   cancel-mid <dir> <outDir> <prefix> <cancelStep> <totalSteps>
//       Steps up to cancelStep-1 complete; cancelStep runs cancellably.
//       Main thread cancels once the step is running, barriers
//       "cancel_persisted" (the Cancelling state is on disk), then spins
//       until killed — death DURING cancellation propagation.
//   cancel-done <dir> <outDir> <prefix> <cancelStep> <totalSteps>
//       Same, but waits for the run to reach a terminal state, barriers
//       "cancel_terminal" and _exit(70) — death AFTER cancel fully
//       propagated and was persisted.
//   torn-save <dir> <runId> <text>
//       Write a PARTIAL checkpoint tmp file using the exact production tmp
//       naming (checkpoint_<runId>.json.tmp.<pid>.<n>), barrier "staged",
//       then spin until killed — death between tmp write and rename.
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <json/json.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_run_lock.h"

using namespace sicnu::workflow;

namespace {

void barrier( const QString &dir, const std::string &name )
{
    QFile f( QDir( dir ).filePath( QStringLiteral( "barrier_%1" ).arg( name.c_str() ) ) );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        // A silently missing barrier would cost the parent a full timeout;
        // fail loudly on stderr instead.
        std::fprintf( stderr, "HELPER: barrier '%s' write failed: %s\n", name.c_str(),
                      f.errorString().toUtf8().constData() );
        std::fflush( stderr );
    }
    f.close();
}

bool waitBarrier( const QString &dir, const std::string &name, int timeoutMs )
{
    const QString path = QDir( dir ).filePath( QStringLiteral( "barrier_%1" ).arg( name.c_str() ) );
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while ( QDateTime::currentMSecsSinceEpoch() < deadline )
    {
        QCoreApplication::processEvents();
        if ( QFile::exists( path ) )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return false;
}

void spinForever()
{
    // Pump the (core) event loop while spinning: TaskCenter drives its
    // completion-notification flush paths from the event loop, and this
    // helper's main thread owns the QCoreApplication. Without the pump the
    // journal would silently lag the real task states and the parent's
    // settle polls would chase a ghost.
    for ( ;; )
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
}

/// Sequential chain of totalSteps operator steps with per-step executors.
/// kind of step k (1-based) is decided by the mode:
///   k < holdPoint  → completes (writes a real output file + returns payload)
///   k == holdPoint → mode behavior (block / exit / cancellable)
///   k > holdPoint  → cancellable for cancel modes, block for run modes
Json::Value makeOutputPayload( const QString &outDir, const std::string &prefix, int step )
{
    Json::Value payload( Json::objectValue );
    payload["output"] = ( outDir + QStringLiteral( "/%1_step%2.tif" )
                              .arg( prefix.c_str() )
                              .arg( step ) )
                             .toStdString();
    return payload;
}

void writeRealOutput( const QString &outDir, const std::string &prefix, int step )
{
    const QString path = outDir + QStringLiteral( "/%1_step%2.tif" ).arg( prefix.c_str() ).arg( step );
    QFile f( path );
    f.open( QIODevice::WriteOnly | QIODevice::Truncate );
    f.write( QStringLiteral( "bytes-of-%1-step%2\n" ).arg( prefix.c_str() ).arg( step ).toUtf8() );
    f.close();
}

/// Content-based commit wait (the helper-side twin of the parent's settle
/// poll): blocks until the run's checkpoint on disk records @p stepId as
/// Completed, or @p timeoutMs elapses. Used by the exit mode so the hard
/// death cannot race ahead of the PREVIOUS step's journal commit — the
/// injected window is "step N died before ITS commit", not "step N-1's
/// commit was lost too".
void waitCommitted( const QString &dir, const std::string &stepId, int timeoutMs )
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    for ( ;; )
    {
        const QStringList checkpoints = QDir( dir ).entryList(
            QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files );
        for ( const QString &entry : checkpoints )
        {
            QFile file( QDir( dir ).filePath( entry ) );
            if ( !file.open( QIODevice::ReadOnly ) )
                continue;
            const QByteArray data = file.readAll();
            file.close();
            Json::CharReaderBuilder builder;
            Json::Value root;
            std::string errs;
            std::istringstream stream( data.toStdString() );
            if ( !Json::parseFromStream( builder, stream, &root, &errs ) )
                continue;
            for ( const Json::Value &plan : root["stepPlans"] )
            {
                if ( plan["stepId"].asString() == stepId
                     && plan["status"].asString() == "Completed" )
                    return;
            }
        }
        if ( QDateTime::currentMSecsSinceEpoch() >= deadline )
            return; // parent's settle poll will observe the truth either way
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
}

enum class StepMode { Complete, Block, Exit, Cancellable };

WorkflowDefinition chainDefinition( const QString &dir, const QString &outDir,
                                    const std::string &prefix, int totalSteps, StepMode mode,
                                    int holdPoint )
{
    WorkflowDefinition def;
    def.id = prefix + "_wf";
    def.title = prefix + " crash window";
    for ( int k = 1; k <= totalSteps; ++k )
    {
        const std::string stepId = "step" + std::to_string( k );
        StepDef step;
        step.id = stepId;
        step.title = stepId;
        step.kind = StepKind::Operator;
        step.operatorId = prefix + ":step" + std::to_string( k );
        step.params["output"] = makeOutputPayload( outDir, prefix, k )["output"];
        if ( k > 1 )
        {
            StepConnection conn;
            conn.fromStepId = "step" + std::to_string( k - 1 );
            conn.fromPort = "output";
            conn.toPort = "input";
            step.inputs.push_back( conn );
        }
        def.steps.push_back( step );

        const int stepNo = k;
        StepMode effective = mode;
        if ( stepNo < holdPoint )
            effective = StepMode::Complete;
        else if ( stepNo > holdPoint && mode != StepMode::Cancellable )
            effective = StepMode::Block;

        sicnu::jobs::JobEngine::instance().registerExecutor(
            step.operatorId, [dir, outDir, prefix, stepNo, effective](
                                 const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
                barrier( dir, "step" + std::to_string( stepNo ) + "_running" );
                switch ( effective )
                {
                    case StepMode::Complete:
                        writeRealOutput( outDir, prefix, stepNo );
                        return makeOutputPayload( outDir, prefix, stepNo );
                    case StepMode::Block:
                        spinForever(); // killed by the parent test
                        break;
                    case StepMode::Exit:
                        writeRealOutput( outDir, prefix, stepNo );
                        if ( stepNo > 1 )
                            waitCommitted( dir, "step" + std::to_string( stepNo - 1 ), 10000 );
                        std::fflush( stdout );
                        std::_Exit( 70 ); // journal commit for this step never happens
                    case StepMode::Cancellable:
                        // Poll the cooperative cancel flag: TaskCenter cancel
                        // reaches the executor, the step ends Canceled.
                        for ( ;; )
                        {
                            ctx.throwIfCancelled();
                            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                        }
                }
                return Json::Value( Json::objectValue );
            } );
    }
    return def;
}

int runPipelineMode( const QString &dir, const QString &outDir, const std::string &prefix,
                     int totalSteps, StepMode mode, int holdPoint, bool cancelAfterBarrier )
{
    QDir().mkpath( outDir ); // step outputs live here; the parent creates only the scratch root
    WorkflowDefinition def = chainDefinition( dir, outDir, prefix, totalSteps, mode, holdPoint );
    auto &coordinator = WorkflowRunCoordinator::instance();
    coordinator.setCheckpointDirectory( dir );
    const long pipelineId = coordinator.startTrackedPipeline( def, /*autoLoad=*/false );
    if ( pipelineId < 0 )
        return 3;

    // The runId is generated inside startTrackedPipeline; report it via the
    // tracked run (the parent also reconciles from disk, so this line is a
    // convenience, not the contract).
    std::string runId;
    for ( int i = 0; i < 100; ++i )
    {
        const auto runs = coordinator.runs();
        if ( !runs.empty() )
        {
            runId = runs.front()->runId();
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    std::printf( "RUN %s %ld\n", runId.c_str(), pipelineId );
    std::fflush( stdout );

    // Plain run modes (block / exit): the parent kills while a node runs.
    if ( mode == StepMode::Block || mode == StepMode::Exit )
        spinForever();

    // Cancel modes: wait until the hold-point step is actually running, then
    // cancel through the coordinator (persists Cancelling BEFORE TaskCenter
    // propagation — checkpoint first, propagation second).
    const std::string barrierName = "step" + std::to_string( holdPoint ) + "_running";
    if ( !waitBarrier( dir, barrierName, 30000 ) )
        return 4;
    std::fprintf( stderr, "HELPER: calling cancelRun\n" );
    std::fflush( stderr );
    if ( !coordinator.cancelRun( pipelineId ) )
        return 5;
    std::fprintf( stderr, "HELPER: cancelRun returned\n" );
    std::fflush( stderr );
    barrier( dir, cancelAfterBarrier ? "cancel_propagated" : "cancel_persisted" );

    if ( !cancelAfterBarrier )
        spinForever(); // parent kills DURING propagation

    // cancel-done: wait for the terminal roll-up, then die hard so the
    // parent inherits the fully-persisted terminal world.
    for ( int i = 0; i < 6000; ++i )
    {
        const auto run = coordinator.runForPipeline( pipelineId );
        if ( run && ( run->state() == WorkflowRunState::Canceled
                      || run->state() == WorkflowRunState::Failed
                      || run->state() == WorkflowRunState::Completed ) )
        {
            barrier( dir, "cancel_terminal" );
            std::fflush( stdout );
            std::_Exit( 70 );
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    return 6; // never reached terminal
}

int holdLockMode( const QString &dir, const std::string &runId )
{
    QDir().mkpath( dir );
    WorkflowRunLock lock( WorkflowRunLock::lockPathForRun( dir, runId ) );
    QString heldByPid;
    if ( lock.tryAcquire( &heldByPid ) != WorkflowRunLock::TryResult::Acquired )
        return 3;
    barrier( dir, "locked" );
    spinForever(); // kernel holds the flock until SIGKILL lands
    return 0;
}

int tornSaveMode( const QString &dir, const std::string &runId, const std::string &text )
{
    QDir().mkpath( dir );
    // EXACT production tmp naming (workflow_checkpoint.cpp::saveCheckpoint):
    // checkpoint_<runId>.json.tmp.<pid>.<counter> — the name the recovery
    // sweep's glob and lock-probe arithmetic both rely on.
    const QString finalPath = QDir( dir ).filePath(
        QStringLiteral( "checkpoint_%1.json" ).arg( runId.c_str() ) );
    const QString tmpPath = finalPath + QStringLiteral( ".tmp.%1.%2" )
                                .arg( QCoreApplication::applicationPid() )
                                .arg( 1 );
    QFile tmp( tmpPath );
    if ( !tmp.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
        return 3;
    tmp.write( text.data(), static_cast<qint64>( text.size() ) ); // partial, no rename, no fsync
    tmp.close();
    barrier( dir, "staged" );
    spinForever(); // killed BEFORE the rename that would commit it
    return 0;
}

} // namespace

int main( int argc, char **argv )
{
    // QCoreApplication (not QApplication): the coordinator/TaskCenter stack
    // needs an event-loop-less core app with a real pid for lock metadata.
    QCoreApplication app( argc, argv );
    if ( argc < 2 )
        return 2;
    const std::string mode = argv[1];

    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.setMaxWorkers( 2 );

    if ( mode == "hold-lock" && argc == 4 )
        return holdLockMode( argv[2], argv[3] );
    if ( mode == "torn-save" && argc == 5 )
        return tornSaveMode( argv[2], argv[3], argv[4] );
    if ( mode == "run-block-at" && argc == 7 )
        return runPipelineMode( argv[2], argv[3], argv[4], std::atoi( argv[6] ),
                                StepMode::Block, std::atoi( argv[5] ), false );
    if ( mode == "run-exit-at" && argc == 7 )
        return runPipelineMode( argv[2], argv[3], argv[4], std::atoi( argv[6] ),
                                StepMode::Exit, std::atoi( argv[5] ), false );
    if ( mode == "cancel-mid" && argc == 7 )
        return runPipelineMode( argv[2], argv[3], argv[4], std::atoi( argv[6] ),
                                StepMode::Cancellable, std::atoi( argv[5] ), false );
    if ( mode == "cancel-done" && argc == 7 )
        return runPipelineMode( argv[2], argv[3], argv[4], std::atoi( argv[6] ),
                                StepMode::Cancellable, std::atoi( argv[5] ), true );
    return 2;
}
