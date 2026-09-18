// tests/test_pipeline_run_state_sync.cpp — #1056 run-state synchronization
//
//   All PipelineRunCoordinator public methods marshal onto the coordinator's
//   affinity thread. These tests exercise the FOREIGN-thread paths (the
//   same-thread inline path is what every existing coordinator test covers):
//   the coordinator is moved onto a dedicated QThread whose default event
//   loop plays the affinity role, and the Catch2 main thread then hammers
//   status/query/cancel concurrently with node completion. Before the fix,
//   these calls read and wrote QHash/bool run state racing the queued
//   onNodeFinished events.
#include <catch2/catch_test_macros.hpp>

#include "workflow/pipeline_run_coordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QIODevice>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace sicnu::workflow;

namespace {

/// Deterministic slow executor: sleeps, then writes the declared artifact.
NodeExecutor makeSlowSyntheticExecutor( int sleepMs )
{
    return [sleepMs]( const NodeFact &node, const QHash<QString, QString> &inputArtifacts,
                      const QString &runDirectory ) -> NodeExecutionResult {
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
        NodeExecutionResult result;
        const QString artifact =
            QDir( runDirectory ).filePath( QStringLiteral( "%1.artifact" ).arg( node.nodeId ) );
        QFile file( artifact );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        {
            result.errorMessage = QStringLiteral( "cannot write artifact for '%1'" ).arg( node.nodeId );
            return result;
        }
        file.write( QStringLiteral( "slow %1\n" ).arg( node.nodeId ).toUtf8() );
        file.close();
        result.success = true;
        result.artifactPath = artifact;
        Q_UNUSED( inputArtifacts );
        return result;
    };
}

WorkflowDocument linearDef( int steps )
{
    // Mirrors the e2e fixture shape: output port everywhere, input port when
    // the node has a predecessor — validateSemantics requires edges to
    // resolve to existing ports.
    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-state-sync-%1" ).arg( steps );
    for ( int i = 1; i <= steps; ++i )
    {
        NodeFact n;
        n.nodeId = QStringLiteral( "node_%1" ).arg( i );
        n.operatorId = QStringLiteral( "rs:step" );
        n.displayName = n.nodeId;
        n.canvasPosition = QPointF( 0, 0 );
        if ( i > 1 )
            n.inputPorts.append( PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ),
                                           QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, true } );
        n.outputPorts.append( PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ),
                                        QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, false } );
        def.nodes.append( n );
        if ( i > 1 )
        {
            EdgeFact edge;
            edge.edgeId = QStringLiteral( "e_%1" ).arg( i );
            edge.sourceNodeId = QStringLiteral( "node_%1" ).arg( i - 1 );
            edge.sourcePortName = QStringLiteral( "output" );
            edge.targetNodeId = QStringLiteral( "node_%1" ).arg( i );
            edge.targetPortName = QStringLiteral( "input" );
            def.edges.append( edge );
        }
    }
    return def;
}

QCoreApplication *ensureApp()
{
    // Qt's queued-invocation machinery (and thus the coordinator's marshal
    // and completion paths) requires a QCoreApplication instance.
    static QCoreApplication *app = nullptr;
    if ( !app )
    {
        static int fakeArgc = 1;
        static char fakeArgv[] = "test_pipeline_run_state_sync";
        static char *fakeArgvPtr[] = { fakeArgv };
        app = new QCoreApplication( fakeArgc, fakeArgvPtr );
    }
    return app;
}

/// Coordinator living on a dedicated QThread (its affinity thread, running a
/// real event loop) so foreign-thread calls marshal through a queue.
struct AffineCoordinator
{
    QThread thread;
    std::unique_ptr<PipelineRunCoordinator> coordinator;

    AffineCoordinator()
        : coordinator( std::make_unique<PipelineRunCoordinator>() )
    {
        coordinator->moveToThread( &thread );
        thread.start();
    }

    ~AffineCoordinator()
    {
        thread.quit();
        thread.wait( 30000 );
        // Affinity thread finished — deleting from this thread is legal.
        coordinator.reset();
    }
};

bool waitForCompleted( PipelineRunCoordinator &coordinator, int timeoutMs = 30000 )
{
    // Connect BEFORE checking hasCompleted: a completion landing between the
    // check and a late connect would otherwise stall the full timeout.
    QSignalSpy spy( &coordinator, &PipelineRunCoordinator::pipelineCompleted );
    QEventLoop loop;
    QObject::connect( &coordinator, &PipelineRunCoordinator::pipelineCompleted, &loop,
                      &QEventLoop::quit, Qt::QueuedConnection );
    QTimer::singleShot( timeoutMs, &loop, &QEventLoop::quit );
    if ( coordinator.hasCompleted() )
        return true;
    loop.exec();
    return spy.count() >= 1 || coordinator.hasCompleted();
}

} // namespace

TEST_CASE( "Foreign-thread status queries stay consistent during a run",
           "[d17][run_state][1056]" )
{
    ensureApp();
    AffineCoordinator fixture;
    PipelineRunCoordinator &coordinator = *fixture.coordinator;

    QTemporaryDir runDir;
    REQUIRE( runDir.isValid() );
    coordinator.setExecutor( makeSlowSyntheticExecutor( 10 ) );
    QString startError;
    const bool started = coordinator.startRun( linearDef( 12 ), runDir.path(), &startError );
    if ( !started )
        FAIL( "startRun failed: " << startError.toStdString() );
    REQUIRE( coordinator.isRunning() );

    // Hammer the public state API from a foreign thread while the affinity
    // thread processes node completions.
    std::atomic<bool> stopQueries{ false };
    std::atomic<bool> sawConsistentSnapshots{ true };
    std::thread hammer( [&coordinator, &stopQueries, &sawConsistentSnapshots] {
        while ( !stopQueries.load( std::memory_order_relaxed ) )
        {
            const auto statuses = coordinator.getAllStatuses();
            // A snapshot must be internally consistent: every recorded
            // terminal Succeeded node carries its artifact.
            for ( const NodeStatusSnapshot &snapshot : statuses )
                if ( snapshot.state == ExecutionState::Succeeded
                     && snapshot.outputArtifactPath.isEmpty() )
                    sawConsistentSnapshots = false;
            ( void )coordinator.isRunning();
            ( void )coordinator.hasCompleted();
            ( void )coordinator.checkpointPath();
            std::this_thread::yield(); // CI politeness: do not saturate a core
        }
    } );

    const bool completed = waitForCompleted( coordinator );
    stopQueries = true;
    hammer.join();
    REQUIRE( completed );
    REQUIRE( sawConsistentSnapshots );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 12 );
    for ( const NodeStatusSnapshot &snapshot : statuses )
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
    REQUIRE( coordinator.hasCompleted() );
    REQUIRE_FALSE( coordinator.isRunning() );
}

TEST_CASE( "Foreign-thread requestCancel is serialized with node completion",
           "[d17][run_state][1056]" )
{
    ensureApp();
    AffineCoordinator fixture;
    PipelineRunCoordinator &coordinator = *fixture.coordinator;

    QTemporaryDir runDir;
    REQUIRE( runDir.isValid() );
    coordinator.setExecutor( makeSlowSyntheticExecutor( 120 ) );

    // Direct-connection lambda: records the completion verdict atomically
    // from the emitting (affinity) thread — Catch2 assertions stay on main.
    std::atomic<bool> completedSuccess{ true };
    bool connected = QObject::connect(
        &coordinator, &PipelineRunCoordinator::pipelineCompleted,
        [&completedSuccess]( bool success, const QString & ) { completedSuccess = success; } );
    REQUIRE( connected );

    REQUIRE( coordinator.startRun( linearDef( 6 ), runDir.path() ) );

    // Cancel from the foreign thread while nodes are still running. The
    // blocking marshal guarantees the cancel has been APPLIED to the queued
    // frontier (not merely requested) when the call returns.
    std::thread canceller( [&coordinator] { coordinator.requestCancel(); } );
    canceller.join();

    // In-flight workers still drain after the cancel request; the run
    // terminalizes once the last one lands.
    REQUIRE( waitForCompleted( coordinator, 30000 ) );
    REQUIRE_FALSE( completedSuccess );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 6 );
    for ( const NodeStatusSnapshot &snapshot : statuses )
    {
        INFO( snapshot.nodeId.toStdString() << " -> " << executionStateString( snapshot.state ).toStdString() );
        REQUIRE( ( snapshot.state == ExecutionState::Cancelled
                   || snapshot.state == ExecutionState::Succeeded ) );
    }
    REQUIRE( !coordinator.checkpointPath().isEmpty() );
}

TEST_CASE( "startRun on a foreign thread returns after the run state exists",
           "[d17][run_state][1056]" )
{
    ensureApp();
    AffineCoordinator fixture;
    PipelineRunCoordinator &coordinator = *fixture.coordinator;

    QTemporaryDir runDir;
    REQUIRE( runDir.isValid() );
    coordinator.setExecutor( makeSyntheticNodeExecutor() );

    // startRun is called from the foreign (main) thread; the blocking marshal
    // means the initial checkpoint and the scheduled frontier both exist by
    // the time the call returns to the starter thread.
    std::atomic<bool> started{ false };
    std::thread starter( [&coordinator, &runDir, &started] {
        QString error;
        started = coordinator.startRun( linearDef( 3 ), runDir.path(), &error );
    } );
    starter.join();
    REQUIRE( started );

    REQUIRE( waitForCompleted( coordinator ) );
    REQUIRE( !coordinator.checkpointPath().isEmpty() );
    REQUIRE( coordinator.hasCompleted() );
}
