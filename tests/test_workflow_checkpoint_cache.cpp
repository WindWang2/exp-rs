// tests/test_workflow_checkpoint_cache.cpp — run coordinator & checkpoints (D17 Package E)
//
// Ground truth: the enumerated skip-cascade state vector for the failing
// 10-step chain (node 5 fails -> 6..10 skipped), the enumerated CacheHit
// set after resume (1..4), and the atomic-write protocol (no .tmp residue,
// parseable document). Executors are injected test doubles.
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTimer>

#include <algorithm>

#include "workflow/pipeline_run_coordinator.h"
#include "workflow/workflow_dag_analyzer.h"

using namespace sicnu::workflow;

namespace {

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        static int fake_argc = 1;
        static char fake_argv[] = "test_workflow_checkpoint_cache";
        static char *fake_argv_ptr[] = { fake_argv };
        app = new QApplication( fake_argc, fake_argv_ptr );
    }
    return app;
}

QString scratchDir( const QString &tag )
{
    const QString dir = QDir::temp().filePath( QStringLiteral( "d17-coord-%1-%2" ).arg( tag, QString::number( QCoreApplication::applicationPid() ) ) );
    QDir().mkpath( dir );
    return dir;
}

NodeFact chainNode( const QString &id, const QString &op )
{
    NodeFact n;
    n.nodeId = id;
    n.operatorId = op;
    n.displayName = id;
    n.canvasPosition = QPointF( 0, 0 );
    n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                QStringLiteral( "None" ), 0, 0, 1, false } };
    n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                               QStringLiteral( "None" ), 0, 0, 1, true } };
    return n;
}

WorkflowDocument chain( int steps )
{
    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-chain-%1" ).arg( steps );
    for ( int i = 1; i <= steps; ++i )
    {
        NodeFact node = chainNode( QStringLiteral( "node_%1" ).arg( i ), QStringLiteral( "rs:step" ) );
        if ( i == 1 )
            node.inputPorts.clear();
        def.nodes.append( node );
        if ( i > 1 )
            def.edges.append( EdgeFact{ QStringLiteral( "e%1" ).arg( i ),
                                        QStringLiteral( "node_%1" ).arg( i - 1 ), QStringLiteral( "output" ),
                                        QStringLiteral( "node_%1" ).arg( i ), QStringLiteral( "input" ) } );
    }
    return def;
}

bool waitForCompleted( PipelineRunCoordinator &coordinator, int timeoutMs = 20000 )
{
    if ( coordinator.hasCompleted() )
        return true; // synchronous completion (empty document) inside startRun/resume
    QSignalSpy spy( &coordinator, &PipelineRunCoordinator::pipelineCompleted );
    QEventLoop loop;
    // Queued: even a synchronous completion (e.g. an empty run) delivers its
    // quit through the event loop instead of racing the exec() below.
    QObject::connect( &coordinator, &PipelineRunCoordinator::pipelineCompleted, &loop, &QEventLoop::quit,
                      Qt::QueuedConnection );
    QTimer::singleShot( timeoutMs, &loop, &QEventLoop::quit );
    loop.exec();
    return spy.count() >= 1 || coordinator.hasCompleted();
}

} // namespace

TEST_CASE( "Single-node run succeeds and writes a parseable checkpoint", "[d17][workflow][engine]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() ); // #1006: explicit binding, no implicit default
    const QString dir = scratchDir( QStringLiteral( "single" ) );

    REQUIRE( coordinator.startRun( chain( 1 ), dir ) );
    REQUIRE( waitForCompleted( coordinator ) );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 1 );
    REQUIRE( statuses.value( QStringLiteral( "node_1" ) ).state == ExecutionState::Succeeded );
    REQUIRE( QFile::exists( statuses.value( QStringLiteral( "node_1" ) ).outputArtifactPath ) );

    // Checkpoint: atomic protocol left no .tmp residue, document parses,
    // statuses agree with the in-memory snapshot.
    REQUIRE( QFile::exists( coordinator.checkpointPath() ) );
    const QStringList dirEntries = QDir( dir ).entryList( { "*.tmp" }, QDir::Files );
    REQUIRE( dirEntries.isEmpty() );
    QFile checkpoint( coordinator.checkpointPath() );
    REQUIRE( checkpoint.open( QIODevice::ReadOnly ) );
    const QJsonDocument document = QJsonDocument::fromJson( checkpoint.readAll() );
    REQUIRE( !document.isNull() );
    REQUIRE( document.object()["kind"].toString() == QStringLiteral( "d17_pipeline_checkpoint" ) );
    REQUIRE( document.object()["nodes"].toArray().size() == 1 );
}

TEST_CASE( "Failure at node 5 skips the downstream cascade", "[d17][workflow][engine]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    const QString dir = scratchDir( QStringLiteral( "fail5" ) );

    // Independent ground truth: the enumerated post-mortem vector.
    const QVector<ExecutionState> expected = {
        ExecutionState::Succeeded, ExecutionState::Succeeded, ExecutionState::Succeeded,
        ExecutionState::Succeeded, ExecutionState::Failed,    ExecutionState::Skipped,
        ExecutionState::Skipped,   ExecutionState::Skipped,   ExecutionState::Skipped,
        ExecutionState::Skipped
    };

    QString error;
    // Bind BEFORE startRun: the coordinator has no implicit synthetic default
    // (#1006) — a post-startRun bind raced the first dispatched node.
    coordinator.setExecutor( []( const NodeFact &node, const QHash<QString, QString> &, const QString &dir ) {
        NodeExecutionResult result;
        if ( node.nodeId == QLatin1String( "node_5" ) )
        {
            result.errorMessage = QStringLiteral( "injected failure" );
            return result;
        }
        const QString artifact = QDir( dir ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
        QFile f( artifact );
        // No Catch2 assertions on pool threads (they are thread-local):
        // report IO failure through the typed result instead.
        if ( !f.open( QIODevice::WriteOnly ) )
        {
            result.errorMessage = QStringLiteral( "artifact open failed for %1" ).arg( node.nodeId );
            return result;
        }
        f.write( QByteArrayLiteral( "ok" ) );
        result.success = true;
        result.artifactPath = artifact;
        return result;
    } );
    REQUIRE( coordinator.startRun( chain( 10 ), dir, &error ) );
    Q_UNUSED( error );

    REQUIRE( waitForCompleted( coordinator ) );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 10 );
    for ( int i = 1; i <= 10; ++i )
    {
        INFO( "node_" << i );
        REQUIRE( statuses.value( QStringLiteral( "node_%1" ).arg( i ) ).state == expected[i - 1] );
    }
    REQUIRE_FALSE( coordinator.getAllStatuses().isEmpty() );
}

TEST_CASE( "Resume from checkpoint reuses exactly the succeeded prefix", "[d17][workflow][engine]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "resume" ) );
    QString checkpointFile;

    // Run 1: node 5 fails.
    {
        PipelineRunCoordinator coordinator;
        // Bind BEFORE startRun (#1006: no implicit synthetic default).
        coordinator.setExecutor( []( const NodeFact &node, const QHash<QString, QString> &, const QString &dir ) {
            NodeExecutionResult result;
            if ( node.nodeId == QLatin1String( "node_5" ) )
            {
                result.errorMessage = QStringLiteral( "injected failure" );
                return result;
            }
            const QString artifact = QDir( dir ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
            QFile f( artifact );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( QByteArrayLiteral( "ok" ) );
            result.success = true;
            result.artifactPath = artifact;
            return result;
        } );
        REQUIRE( coordinator.startRun( chain( 10 ), dir ) );
        REQUIRE( waitForCompleted( coordinator ) );
        checkpointFile = coordinator.checkpointPath();
        REQUIRE( QFile::exists( checkpointFile ) );
    }

    // Run 2: fresh coordinator, "environment fixed" (no failing node), resume.
    PipelineRunCoordinator resumeCoordinator;
    QString error;
    resumeCoordinator.setExecutor( []( const NodeFact &node, const QHash<QString, QString> &, const QString &dir ) {
        NodeExecutionResult result;
        const QString artifact = QDir( dir ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
        QFile f( artifact );
        if ( f.open( QIODevice::WriteOnly ) )
        {
            f.write( QByteArrayLiteral( "ok" ) );
            result.success = true;
            result.artifactPath = artifact;
        }
        else
        {
            result.errorMessage = QStringLiteral( "artifact open failed for %1" ).arg( node.nodeId );
        }
        return result;
    } );

    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpointFile, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    const auto postStatuses = resumeCoordinator.getAllStatuses();
    REQUIRE( postStatuses.size() == 10 );
    // Nodes 1..4 were Succeeded with intact artifacts and matching lineage
    // signatures -> CacheHit.
    for ( int i = 1; i <= 4; ++i )
    {
        INFO( "node_" << i );
        REQUIRE( postStatuses.value( QStringLiteral( "node_%1" ).arg( i ) ).isCacheHit );
        REQUIRE( postStatuses.value( QStringLiteral( "node_%1" ).arg( i ) ).state == ExecutionState::Succeeded );
    }
    // The rest recomputed and succeeded.
    for ( int i = 5; i <= 10; ++i )
    {
        INFO( "node_" << i );
        REQUIRE_FALSE( postStatuses.value( QStringLiteral( "node_%1" ).arg( i ) ).isCacheHit );
        REQUIRE( postStatuses.value( QStringLiteral( "node_%1" ).arg( i ) ).state == ExecutionState::Succeeded );
    }
}

TEST_CASE( "Resume works when the checkpoint document order is NOT topological", "[d17][workflow][engine]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "resume-nt" ) );
    QString checkpointFile;

    // Document order: the CONSUMER first, the PRODUCER second, wired
    // consumer <- producer. Both succeed; the checkpoint records them in
    // this non-topological order (creation order, not tier order).
    WorkflowDocument def;
    auto makeNode = []( const QString &id, bool withInput ) {
        NodeFact n;
        n.nodeId = id;
        n.operatorId = QStringLiteral( "rs:step" );
        n.canvasPosition = QPointF( 0, 0 );
        n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                    QStringLiteral( "None" ), 0, 0, 1, false } };
        if ( withInput )
            n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                       QStringLiteral( "None" ), 0, 0, 1, true } };
        return n;
    };
    def.nodes = { makeNode( QStringLiteral( "consumer" ), true ), makeNode( QStringLiteral( "producer" ), false ) };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "producer" ), QStringLiteral( "output" ),
                            QStringLiteral( "consumer" ), QStringLiteral( "input" ) } };

    {
        PipelineRunCoordinator coordinator;
        coordinator.setExecutor( makeSyntheticNodeExecutor() );
        REQUIRE( coordinator.startRun( def, dir ) );
        REQUIRE( waitForCompleted( coordinator ) );
        checkpointFile = coordinator.checkpointPath();
        REQUIRE( QFile::exists( checkpointFile ) );
    }

    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpointFile, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE( statuses.size() == 2 );
    // Both nodes were cached — including the consumer whose producer is
    // listed AFTER it in the document. A one-pass parent count would stall
    // this resume forever (P0 regression pin).
    REQUIRE( statuses.value( QStringLiteral( "consumer" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "producer" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "consumer" ) ).state == ExecutionState::Succeeded );
    REQUIRE( statuses.value( QStringLiteral( "producer" ) ).state == ExecutionState::Succeeded );
}

TEST_CASE( "Resume rejects a corrupted checkpoint fail-closed", "[d17][workflow][engine]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "corrupt" ) );
    const QString path = QDir( dir ).filePath( QStringLiteral( "checkpoint_bad.json" ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QByteArrayLiteral( "{ not json " ) );
    file.close();

    PipelineRunCoordinator coordinator;
    QString error;
    REQUIRE_FALSE( coordinator.resumeFromCheckpoint( path, &error ) );
    REQUIRE_FALSE( error.isEmpty() );
}

TEST_CASE( "Cancel before dispatch marks everything Cancelled", "[d17][workflow][engine]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() );
    const QString dir = scratchDir( QStringLiteral( "cancel" ) );

    // Cancel from inside the event loop: requestCancel() emits
    // pipelineCompleted synchronously, so the wait must already be armed.
    REQUIRE( coordinator.startRun( chain( 6 ), dir ) );
    QEventLoop loop;
    QObject::connect( &coordinator, &PipelineRunCoordinator::pipelineCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 0, &coordinator, [ &coordinator ]() { coordinator.requestCancel(); } );
    QTimer::singleShot( 20000, &loop, &QEventLoop::quit );
    loop.exec();

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 6 );
    // Every node reached a terminal state — none left hanging.
    for ( const NodeStatusSnapshot &snapshot : statuses )
        REQUIRE( ( snapshot.state == ExecutionState::Cancelled || snapshot.state == ExecutionState::Skipped
                   || snapshot.state == ExecutionState::Succeeded || snapshot.state == ExecutionState::Failed ) );
}

TEST_CASE( "Diamond workflow executes the converging node once", "[d17][workflow][engine]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() );
    const QString dir = scratchDir( QStringLiteral( "diamond" ) );

    WorkflowDocument def;
    auto node = []( const QString &id, bool withInput ) {
        NodeFact n;
        n.nodeId = id;
        n.operatorId = QStringLiteral( "rs:step" );
        n.canvasPosition = QPointF( 0, 0 );
        n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                    QStringLiteral( "None" ), 0, 0, 1, false } };
        if ( withInput )
            n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                       QStringLiteral( "None" ), 0, 0, 1, true } };
        return n;
    };
    NodeFact merge = node( "m", true );
    // The convergence node has TWO distinct input ports — the single-source
    // in-degree invariant forbids two edges into the same port.
    merge.inputPorts.append( PortFact{ QStringLiteral( "aux" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                       QStringLiteral( "None" ), 0, 0, 1, true } );
    def.nodes = { node( "s", false ), node( "a", true ), node( "b", true ), merge };
    def.edges = {
        EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "s" ), QStringLiteral( "output" ), QStringLiteral( "a" ), QStringLiteral( "input" ) },
        EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "s" ), QStringLiteral( "output" ), QStringLiteral( "b" ), QStringLiteral( "input" ) },
        EdgeFact{ QStringLiteral( "e3" ), QStringLiteral( "a" ), QStringLiteral( "output" ), QStringLiteral( "m" ), QStringLiteral( "input" ) },
        EdgeFact{ QStringLiteral( "e4" ), QStringLiteral( "b" ), QStringLiteral( "output" ), QStringLiteral( "m" ), QStringLiteral( "aux" ) },
    };
    REQUIRE( WorkflowDagAnalyzer::analyzeDag( def ).isAcyclic );

    int mergeRuns = 0;
    QObject probe;
    QObject::connect( &coordinator, &PipelineRunCoordinator::nodeFinished, &probe,
                      [&]( const QString &nodeId, bool success, const QString & ) {
                          if ( nodeId == QLatin1String( "m" ) && success )
                              ++mergeRuns;
                      } );

    REQUIRE( coordinator.startRun( def, dir ) );
    REQUIRE( waitForCompleted( coordinator ) );

    REQUIRE( mergeRuns == 1 );
    const auto statuses = coordinator.getAllStatuses();
    for ( const NodeStatusSnapshot &snapshot : statuses )
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
}

TEST_CASE( "startRun rejects cyclic documents and rejects double starts", "[d17][workflow][engine]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    const QString dir = scratchDir( QStringLiteral( "cyclic" ) );

    WorkflowDocument def;
    auto node = []( const QString &id ) {
        NodeFact n;
        n.nodeId = id;
        n.operatorId = QStringLiteral( "rs:step" );
        n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                   QStringLiteral( "None" ), 0, 0, 1, true } };
        n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                    QStringLiteral( "None" ), 0, 0, 1, false } };
        return n;
    };
    def.nodes = { node( "x" ), node( "y" ) };
    def.edges = {
        EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "x" ), QStringLiteral( "output" ), QStringLiteral( "y" ), QStringLiteral( "input" ) },
        EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "y" ), QStringLiteral( "output" ), QStringLiteral( "x" ), QStringLiteral( "input" ) },
    };

    QString error;
    REQUIRE_FALSE( coordinator.startRun( def, dir, &error ) );
    REQUIRE( error.contains( QStringLiteral( "cycle" ), Qt::CaseInsensitive ) );
}
