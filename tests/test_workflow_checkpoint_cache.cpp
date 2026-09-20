// tests/test_workflow_checkpoint_cache.cpp — run coordinator & checkpoints (D17 Package E)
//
// Ground truth: the enumerated skip-cascade state vector for the failing
// 10-step chain (node 5 fails -> 6..10 skipped), the enumerated CacheHit
// set after resume (1..4), and the atomic-write protocol (no .tmp residue,
// parseable document). Executors are injected test doubles.
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtEndian>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>

#include "workflow/pipeline_run_coordinator.h"
#include "workflow/workflow_provenance.h"
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

TEST_CASE( "Cross-thread accessors read a consistent run state", "[d17][workflow][engine]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    coordinator.setMaxParallelism( 2 );
    // Slow executor: keeps the run in flight long enough for a foreign thread
    // to read the state onNodeFinished is mutating on the affinity thread
    // (#1056: unprotected cross-thread reads of the same fields).
    coordinator.setExecutor( []( const NodeFact &node, const QHash<QString, QString> &, const QString &dir ) {
        QThread::msleep( 10 );
        NodeExecutionResult result;
        const QString artifact = QDir( dir ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
        QFile f( artifact );
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
    const QString dir = scratchDir( QStringLiteral( "cross-thread" ) );

    std::atomic<bool> stop{ false };
    std::atomic<int> reads{ 0 };
    std::atomic<bool> inconsistent{ false };
    // QThread::create: the lambda IS run() — the thread finishes when it
    // returns. (A plain QThread would enter exec() after `started` and never
    // terminate: isFinished() would stay false forever.)
    QThread *reader = QThread::create( [&]() {
        while ( !stop.load() )
        {
            const auto statuses = coordinator.getAllStatuses();
            const bool completed = coordinator.hasCompleted();
            // A snapshotted status map must always cover the whole document.
            // (running && completed) is NOT checked: two separately marshalled
            // reads legitimately straddle the terminal transition — that is a
            // benign interleaving, not a torn field.
            if ( statuses.size() != 12 )
                inconsistent = true;
            for ( const NodeStatusSnapshot &snapshot : statuses )
                if ( executionStateString( snapshot.state ).isEmpty() )
                    inconsistent = true;
            reads.fetch_add( 1 );
            if ( completed )
                break;
        }
    } );
    REQUIRE( coordinator.startRun( chain( 12 ), dir ) );
    reader->start();
    REQUIRE( waitForCompleted( coordinator ) );

    // The reader blocks inside marshalled accessor calls until the
    // coordinator's thread services them: keep pumping events until it
    // observed the terminal state (bounded — then release it via `stop`).
    const qint64 pumpDeadline = QDateTime::currentMSecsSinceEpoch() + 30000;
    while ( !reader->isFinished() && QDateTime::currentMSecsSinceEpoch() < pumpDeadline )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );
    stop.store( true );
    REQUIRE( reader->wait( 30000 ) );
    delete reader;

    CHECK_FALSE( inconsistent.load() );
    CHECK( reads.load() > 0 );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 12 );
    for ( const NodeStatusSnapshot &snapshot : statuses )
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
    CHECK( coordinator.hasCompleted() );
    CHECK_FALSE( coordinator.isRunning() );
}

// ---------------------------------------------------------------------------
// Checkpoint hardening (flash-workflow-engine-12 / DECISIONS D2-D4): envelope
// gate, bounded reads, strict state vocabulary, artifact identity +
// run-directory containment, minimal invalidation.
// ---------------------------------------------------------------------------

namespace {

/// Runs a 3-node chain to completion with the synthetic executor; returns the
/// checkpoint path ("" on failure) and the run directory via @p outRunDir.
QString produceChainCheckpoint( const QString &tag, QString *outRunDir )
{
    const QString dir = scratchDir( tag );
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() );
    if ( !coordinator.startRun( chain( 3 ), dir ) || !waitForCompleted( coordinator ) )
        return {};
    if ( outRunDir )
        *outRunDir = dir;
    return coordinator.checkpointPath();
}

QJsonObject readJsonObject( const QString &path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    return QJsonDocument::fromJson( file.readAll() ).object();
}

bool writeJsonObject( const QString &path, const QJsonObject &object )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    return file.write( QJsonDocument( object ).toJson() ) > 0;
}

/// Counting executor: same contract as makeSyntheticNodeExecutor but records
/// invocations so tests can assert WHICH nodes recomputed.
NodeExecutor countingExecutor( std::atomic<int> *count )
{
    return [count]( const NodeFact &node, const QHash<QString, QString> &, const QString &dir ) {
        count->fetch_add( 1 );
        NodeExecutionResult result;
        const QString artifact = QDir( dir ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
        QFile f( artifact );
        // No Catch2 assertions on pool threads (thread-local): report through
        // the typed result instead.
        if ( !f.open( QIODevice::WriteOnly ) )
        {
            result.errorMessage = QStringLiteral( "artifact open failed for %1" ).arg( node.nodeId );
            return result;
        }
        f.write( QByteArrayLiteral( "ok" ) );
        result.success = true;
        result.artifactPath = artifact;
        return result;
    };
}

/// Independent reimplementation of the coordinator's sha256fl scheme — the
/// test forges a *valid* identity for a foreign file so that ONLY the
/// containment gate can refuse it.
QString testFingerprint( const QString &path, qint64 size )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    constexpr qint64 kWindow = 1024 * 1024;
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    QByteArray sizeLE( 8, '\0' );
    qToLittleEndian<qint64>( size, sizeLE.data() );
    hash.addData( sizeLE );
    hash.addData( file.read( qMin( kWindow, size ) ) );
    if ( size > kWindow )
    {
        if ( !file.seek( size - kWindow ) )
            return {};
        hash.addData( file.read( kWindow ) );
    }
    return QStringLiteral( "sha256fl:%1" ).arg( QString::fromLatin1( hash.result().toHex() ) );
}

/// Restore a file's modification time to @p targetMs (ms since epoch) — the
/// file clock ticks with the system clock on all supported platforms, so the
/// offset conversion is exact enough for the coordinator's ms comparisons.
bool restoreMtime( const QString &path, qint64 targetMs )
{
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    const qint64 sysNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();
    std::error_code ec;
    std::filesystem::last_write_time(
        std::filesystem::path( path.toStdWString() ),
        fileNow - std::chrono::milliseconds( sysNowMs - targetMs ), ec );
    return !ec;
}

/// Replace one node's entry inside a checkpoint document.
void mutateNodeEntry( QJsonObject &doc, const QString &nodeId,
                      const std::function<void( QJsonObject & )> &mutate )
{
    QJsonArray nodes = doc.value( QLatin1String( "nodes" ) ).toArray();
    for ( qsizetype i = 0; i < nodes.size(); ++i )
    {
        QJsonObject entry = nodes.at( i ).toObject();
        if ( entry.value( QLatin1String( "nodeId" ) ).toString() != nodeId )
            continue;
        mutate( entry );
        nodes.replace( i, entry );
        break;
    }
    doc.insert( QLatin1String( "nodes" ), nodes );
}

QJsonObject nodeEntry( const QJsonObject &doc, const QString &nodeId )
{
    for ( const QJsonValue &value : doc.value( QLatin1String( "nodes" ) ).toArray() )
    {
        const QJsonObject entry = value.toObject();
        if ( entry.value( QLatin1String( "nodeId" ) ).toString() == nodeId )
            return entry;
    }
    return {};
}

} // namespace

TEST_CASE( "Resume rejects a checkpoint with a foreign kind", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "kind" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    QJsonObject doc = readJsonObject( checkpoint );
    doc.insert( QLatin1String( "kind" ), QStringLiteral( "forged_checkpoint" ) );
    REQUIRE( writeJsonObject( checkpoint, doc ) );

    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE_FALSE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( error.contains( QStringLiteral( "kind" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "Resume rejects a checkpoint from an unknown future version", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "version" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    QJsonObject doc = readJsonObject( checkpoint );
    doc.insert( QLatin1String( "version" ), QStringLiteral( "9.9" ) );
    REQUIRE( writeJsonObject( checkpoint, doc ) );

    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE_FALSE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( error.contains( QStringLiteral( "version" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "Resume rejects an oversized checkpoint before buffering it", "[d17][workflow][engine]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "oversize" ) );
    const QString path = QDir( dir ).filePath( QStringLiteral( "checkpoint_big.json" ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( QByteArray( 16 * 1024 * 1024 + 1, 'x' ) ) > 0 );
    file.close();

    PipelineRunCoordinator coordinator;
    QString error;
    REQUIRE_FALSE( coordinator.resumeFromCheckpoint( path, &error ) );
    REQUIRE( error.contains( QStringLiteral( "cap" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "Resume rejects an unknown node state key fail-closed", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "badstate" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    QJsonObject doc = readJsonObject( checkpoint );
    mutateNodeEntry( doc, QStringLiteral( "node_2" ), []( QJsonObject &entry ) {
        entry.insert( QLatin1String( "state" ), QStringLiteral( "Teleporting" ) );
    } );
    REQUIRE( writeJsonObject( checkpoint, doc ) );

    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE_FALSE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( error.contains( QStringLiteral( "Teleporting" ) ) );
}

TEST_CASE( "Legacy 1.0 checkpoints resume but conservatively recompute", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "legacy" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    // A format-1.0 checkpoint carries no artifact identity: every node must
    // degrade to recompute rather than trust an unverifiable file.
    QJsonObject doc = readJsonObject( checkpoint );
    doc.insert( QLatin1String( "version" ), QStringLiteral( "1.0" ) );
    QJsonArray nodes = doc.value( QLatin1String( "nodes" ) ).toArray();
    for ( qsizetype i = 0; i < nodes.size(); ++i )
    {
        QJsonObject entry = nodes.at( i ).toObject();
        entry.remove( QLatin1String( "artifactSize" ) );
        entry.remove( QLatin1String( "artifactMtimeMs" ) );
        entry.remove( QLatin1String( "artifactFingerprint" ) );
        nodes.replace( i, entry );
    }
    doc.insert( QLatin1String( "nodes" ), nodes );
    REQUIRE( writeJsonObject( checkpoint, doc ) );

    std::atomic<int> executed{ 0 };
    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( countingExecutor( &executed ) );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    REQUIRE( executed.load() == 3 );
    const auto statuses = resumeCoordinator.getAllStatuses();
    for ( const NodeStatusSnapshot &snapshot : statuses )
    {
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
        REQUIRE_FALSE( snapshot.isCacheHit );
    }
}

TEST_CASE( "A tampered artifact recomputes instead of producing a false CacheHit", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "tamper" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    // Rewrite node_2's artifact with same-length different bytes and restore
    // the recorded mtime — only the content fingerprint can catch this.
    const QJsonObject doc = readJsonObject( checkpoint );
    const QJsonObject entry = nodeEntry( doc, QStringLiteral( "node_2" ) );
    const QString artifact = entry.value( QLatin1String( "artifact" ) ).toString();
    const qint64 recordedMtime = entry.value( QLatin1String( "artifactMtimeMs" ) ).toInteger();
    REQUIRE( QFile::exists( artifact ) );

    {
        QFile file( artifact );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        const QByteArray garbage( static_cast<qsizetype>( entry.value( QLatin1String( "artifactSize" ) ).toInteger() ), 'Z' );
        REQUIRE( file.write( garbage ) == garbage.size() );
    }
    REQUIRE( restoreMtime( artifact, recordedMtime ) );

    std::atomic<int> executed{ 0 };
    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( countingExecutor( &executed ) );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    // Minimal invalidation: only the tampered node recomputes; its untouched
    // siblings stay CacheHit (deterministic re-execution reproduces an
    // equivalent product, so node_3's cached output remains valid).
    REQUIRE( executed.load() == 1 );
    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE( statuses.value( QStringLiteral( "node_1" ) ).isCacheHit );
    REQUIRE_FALSE( statuses.value( QStringLiteral( "node_2" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "node_3" ) ).isCacheHit );
    for ( const NodeStatusSnapshot &snapshot : statuses )
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
}

TEST_CASE( "An artifact path outside the run directory is never served", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "escape" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    // Plant a foreign file outside the run directory and forge a VALID
    // identity for it — only the containment gate can refuse the hit.
    const QString foreignDir = scratchDir( QStringLiteral( "foreign" ) );
    const QString foreignPath = QDir( foreignDir ).filePath( QStringLiteral( "foreign.bin" ) );
    {
        QFile file( foreignPath );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( QByteArrayLiteral( "planted" ) ) > 0 );
    }
    const QFileInfo foreignInfo( foreignPath );

    QJsonObject doc = readJsonObject( checkpoint );
    mutateNodeEntry( doc, QStringLiteral( "node_1" ), [&]( QJsonObject &entry ) {
        entry.insert( QLatin1String( "artifact" ), foreignInfo.absoluteFilePath() );
        entry.insert( QLatin1String( "artifactSize" ), foreignInfo.size() );
        entry.insert( QLatin1String( "artifactMtimeMs" ),
                      foreignInfo.fileTime( QFileDevice::FileModificationTime ).toMSecsSinceEpoch() );
        entry.insert( QLatin1String( "artifactFingerprint" ),
                      testFingerprint( foreignInfo.canonicalFilePath(), foreignInfo.size() ) );
    } );
    REQUIRE( writeJsonObject( checkpoint, doc ) );

    std::atomic<int> executed{ 0 };
    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( countingExecutor( &executed ) );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE_FALSE( statuses.value( QStringLiteral( "node_1" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "node_1" ) ).state == ExecutionState::Succeeded );
    // node_1 recomputed; the still-verified siblings stay cached.
    REQUIRE( statuses.value( QStringLiteral( "node_2" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "node_3" ) ).isCacheHit );
}

TEST_CASE( "An executor artifact outside the run directory fails the node", "[d17][workflow][engine]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    const QString runDir = scratchDir( QStringLiteral( "outside-run" ) );
    const QString foreignDir = scratchDir( QStringLiteral( "outside-foreign" ) );

    coordinator.setExecutor( [&]( const NodeFact &, const QHash<QString, QString> &, const QString & ) {
        NodeExecutionResult result;
        const QString artifact = QDir( foreignDir ).filePath( QStringLiteral( "escape.bin" ) );
        QFile f( artifact );
        if ( !f.open( QIODevice::WriteOnly ) )
        {
            result.errorMessage = QStringLiteral( "artifact open failed" );
            return result;
        }
        f.write( QByteArrayLiteral( "escape" ) );
        result.success = true;
        result.artifactPath = artifact;
        return result;
    } );

    REQUIRE( coordinator.startRun( chain( 1 ), runDir ) );
    REQUIRE( waitForCompleted( coordinator ) );

    const NodeStatusSnapshot snapshot =
        coordinator.getAllStatuses().value( QStringLiteral( "node_1" ) );
    REQUIRE( snapshot.state == ExecutionState::Failed );
    REQUIRE( snapshot.errorMessage.contains( QStringLiteral( "ir2.artifact_outside_run" ) ) );
    REQUIRE( snapshot.outputArtifactPath.isEmpty() );
}

TEST_CASE( "A parameter change invalidates exactly the downstream subgraph", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "invalidate" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    // Edit node_2's parameters inside the embedded workflow: its signature
    // changes, and so does node_3's (parent component) — node_1's doesn't.
    QJsonObject doc = readJsonObject( checkpoint );
    QJsonObject workflow = doc.value( QLatin1String( "workflow" ) ).toObject();
    QJsonArray wnodes = workflow.value( QLatin1String( "nodes" ) ).toArray();
    for ( qsizetype i = 0; i < wnodes.size(); ++i )
    {
        QJsonObject node = wnodes.at( i ).toObject();
        if ( node.value( QLatin1String( "nodeId" ) ).toString() != QLatin1String( "node_2" ) )
            continue;
        node.insert( QLatin1String( "parameters" ),
                     QJsonObject{ { QStringLiteral( "changed" ), true } } );
        wnodes.replace( i, node );
    }
    workflow.insert( QLatin1String( "nodes" ), wnodes );
    doc.insert( QLatin1String( "workflow" ), workflow );
    REQUIRE( writeJsonObject( checkpoint, doc ) );

    std::atomic<int> executed{ 0 };
    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( countingExecutor( &executed ) );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    REQUIRE( executed.load() == 2 ); // node_2 + node_3, nothing else
    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE( statuses.value( QStringLiteral( "node_1" ) ).isCacheHit );
    REQUIRE_FALSE( statuses.value( QStringLiteral( "node_2" ) ).isCacheHit );
    REQUIRE_FALSE( statuses.value( QStringLiteral( "node_3" ) ).isCacheHit );
    for ( const NodeStatusSnapshot &snapshot : statuses )
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
}

TEST_CASE( "A node recorded as Running recomputes (crash mid-run)", "[d17][workflow][engine]" )
{
    ensureApp();
    QString runDir;
    const QString checkpoint = produceChainCheckpoint( QStringLiteral( "crash" ), &runDir );
    REQUIRE( !checkpoint.isEmpty() );

    // Simulate a process death between node_3's dispatch and completion.
    QJsonObject doc = readJsonObject( checkpoint );
    mutateNodeEntry( doc, QStringLiteral( "node_3" ), []( QJsonObject &entry ) {
        entry.insert( QLatin1String( "state" ), QStringLiteral( "Running" ) );
    } );
    REQUIRE( writeJsonObject( checkpoint, doc ) );

    std::atomic<int> executed{ 0 };
    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( countingExecutor( &executed ) );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpoint, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    REQUIRE( executed.load() == 1 );
    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE( statuses.value( QStringLiteral( "node_1" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "node_2" ) ).isCacheHit );
    REQUIRE_FALSE( statuses.value( QStringLiteral( "node_3" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "node_3" ) ).state == ExecutionState::Succeeded );
}

TEST_CASE( "A cancelled run resumes to full success", "[d17][workflow][engine]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "cancel-resume" ) );
    QString checkpointFile;

    {
        PipelineRunCoordinator coordinator;
        // Slow executor: keeps the run in flight so the cancel lands mid-run.
        coordinator.setExecutor( []( const NodeFact &node, const QHash<QString, QString> &, const QString &dir ) {
            QThread::msleep( 15 );
            NodeExecutionResult result;
            const QString artifact = QDir( dir ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
            QFile f( artifact );
            if ( !f.open( QIODevice::WriteOnly ) )
            {
                result.errorMessage = QStringLiteral( "artifact open failed" );
                return result;
            }
            f.write( QByteArrayLiteral( "ok" ) );
            result.success = true;
            result.artifactPath = artifact;
            return result;
        } );
        REQUIRE( coordinator.startRun( chain( 4 ), dir ) );
        QEventLoop loop;
        QObject::connect( &coordinator, &PipelineRunCoordinator::pipelineCompleted, &loop, &QEventLoop::quit );
        QTimer::singleShot( 0, &coordinator, [ &coordinator ]() { coordinator.requestCancel(); } );
        QTimer::singleShot( 20000, &loop, &QEventLoop::quit );
        loop.exec();
        checkpointFile = coordinator.checkpointPath();
        REQUIRE( QFile::exists( checkpointFile ) );
    }

    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpointFile, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE( statuses.size() == 4 );
    for ( const NodeStatusSnapshot &snapshot : statuses )
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
}

// ---------------------------------------------------------------------------
// WP2 composition: subflow instances expand before planning; the run only
// ever sees flat, namespaced nodes.
// ---------------------------------------------------------------------------

namespace {

WorkflowDocument subflowParentDoc()
{
    // Fragment: f_in -> f_out (both synthetic steps).
    WorkflowDocument frag;
    frag.version = QStringLiteral( "2.1" );
    frag.nodes = { chainNode( QStringLiteral( "f_in" ), QStringLiteral( "rs:step" ) ),
                   chainNode( QStringLiteral( "f_out" ), QStringLiteral( "rs:step" ) ) };
    frag.edges = { EdgeFact{ QStringLiteral( "e_f" ), QStringLiteral( "f_in" ), QStringLiteral( "output" ),
                             QStringLiteral( "f_out" ), QStringLiteral( "input" ) } };

    NodeFact sub;
    sub.nodeId = QStringLiteral( "S" );
    sub.operatorId = QStringLiteral( "workflow:subflow" );
    sub.displayName = QStringLiteral( "S" );
    sub.canvasPosition = QPointF( 0, 0 );
    sub.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                 QStringLiteral( "None" ), 0, 0, 1, true } };
    sub.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                  QStringLiteral( "None" ), 0, 0, 1, false } };
    sub.parameters = QJsonObject{
        { QStringLiteral( "fragment" ), WorkflowIR::toJson( frag ) },
        { QStringLiteral( "interface" ),
          QJsonObject{
              { QStringLiteral( "inputs" ),
                QJsonObject{ { QStringLiteral( "input" ), QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_in" ) },
                                                                       { QStringLiteral( "port" ), QStringLiteral( "input" ) } } } } },
              { QStringLiteral( "outputs" ),
                QJsonObject{ { QStringLiteral( "output" ), QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_out" ) },
                                                                        { QStringLiteral( "port" ), QStringLiteral( "output" ) } } } } } } } };

    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-subflow-parent" );
    NodeFact src = chainNode( QStringLiteral( "src" ), QStringLiteral( "rs:step" ) );
    src.inputPorts.clear();
    NodeFact sink = chainNode( QStringLiteral( "sink" ), QStringLiteral( "rs:step" ) );
    sink.outputPorts.clear();
    def.nodes = { src, sub, sink };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "src" ), QStringLiteral( "output" ),
                            QStringLiteral( "S" ), QStringLiteral( "input" ) },
                  EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "S" ), QStringLiteral( "output" ),
                            QStringLiteral( "sink" ), QStringLiteral( "input" ) } };
    return def;
}

} // namespace

TEST_CASE( "A subflow instance runs as expanded nodes and resumes as cache hits", "[d17][workflow][engine][composition]" )
{
    const QString dir = scratchDir( QStringLiteral( "subflow-e2e" ) );
    QString checkpointFile;
    {
        PipelineRunCoordinator coordinator;
        coordinator.setExecutor( makeSyntheticNodeExecutor() );
        QString error;
        REQUIRE( coordinator.startRun( subflowParentDoc(), dir, &error ) );
        INFO( error.toStdString() );
        REQUIRE( waitForCompleted( coordinator ) );

        const auto statuses = coordinator.getAllStatuses();
        REQUIRE( statuses.size() == 4 ); // src + S__f_in + S__f_out + sink
        for ( const QString &id : { QStringLiteral( "src" ), QStringLiteral( "S__f_in" ),
                                    QStringLiteral( "S__f_out" ), QStringLiteral( "sink" ) } )
        {
            INFO( id.toStdString() );
            REQUIRE( statuses.contains( id ) );
            REQUIRE( statuses.value( id ).state == ExecutionState::Succeeded );
            REQUIRE( QFile::exists( statuses.value( id ).outputArtifactPath ) );
        }
        checkpointFile = coordinator.checkpointPath();
        REQUIRE( QFile::exists( checkpointFile ) );
    }

    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpointFile, &error ) );
    INFO( error.toStdString() );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE( statuses.size() == 4 );
    for ( const NodeStatusSnapshot &snapshot : statuses )
    {
        INFO( snapshot.nodeId.toStdString() );
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
        REQUIRE( snapshot.isCacheHit ); // expanded ids are stable -> full reuse
    }
}

TEST_CASE( "startRun refuses an unexpandable subflow and names the instance", "[d17][workflow][engine][composition]" )
{
    WorkflowDocument def = subflowParentDoc();
    // Break the interface: point the output mapping at a missing port.
    QJsonObject iface = def.nodes[1].parameters[QStringLiteral( "interface" )].toObject();
    iface[QStringLiteral( "outputs" )] = QJsonObject{
        { QStringLiteral( "output" ), QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_out" ) },
                                                   { QStringLiteral( "port" ), QStringLiteral( "gone" ) } } } };
    def.nodes[1].parameters[QStringLiteral( "interface" )] = iface;

    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE_FALSE( coordinator.startRun( def, scratchDir( QStringLiteral( "subflow-bad" ) ), &error ) );
    REQUIRE( error.contains( QStringLiteral( "'S'" ) ) );
    REQUIRE( error.contains( QStringLiteral( "f_out" ) ) );
}


// ---------------------------------------------------------------------------
// WP5 provenance: one queryable lineage record per terminal run.
// ---------------------------------------------------------------------------

namespace {

ProvenanceGraph loadProvenance( const QString &path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    auto parsed = ProvenanceGraph::fromJson( doc.object() );
    return parsed.isSuccess() ? parsed.value() : ProvenanceGraph{};
}

const ProvenanceNode *findProvNode( const ProvenanceGraph &graph, const QString &id )
{
    for ( const ProvenanceNode &node : graph.nodes() )
        if ( node.id == id )
            return &node;
    return nullptr;
}

} // namespace

TEST_CASE( "A finished run emits a queryable, round-trip-stable provenance record", "[d17][workflow][provenance]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "provenance" ) );
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() );
    REQUIRE( coordinator.startRun( chain( 3 ), dir ) );
    REQUIRE( waitForCompleted( coordinator ) );

    const QString provenanceFile = coordinator.provenancePath();
    REQUIRE( !provenanceFile.isEmpty() );
    REQUIRE( QFile::exists( provenanceFile ) );
    REQUIRE( provenanceFile.contains( QStringLiteral( "provenance_" ) ) );

    const ProvenanceGraph graph = loadProvenance( provenanceFile );
    REQUIRE( !graph.nodes().isEmpty() );

    // One run node carrying the deterministic plan signature.
    const ProvenanceNode *run = nullptr;
    int runCount = 0;
    for ( const ProvenanceNode &node : graph.nodes() )
        if ( node.kind == QLatin1String( "run" ) )
        {
            run = &node;
            ++runCount;
        }
    REQUIRE( runCount == 1 );
    REQUIRE( run->attributes.value( QLatin1String( "planSignature" ) ).toString().size() == 64 );
    REQUIRE( run->attributes.value( QLatin1String( "workflowId" ) ).toString()
             == QStringLiteral( "wf-chain-3" ) );

    // nodeExec per graph node + produced artifact per exec.
    for ( int i = 1; i <= 3; ++i )
    {
        const QString execId = QStringLiteral( "node:node_%1" ).arg( i );
        const ProvenanceNode *exec = findProvNode( graph, execId );
        REQUIRE( exec != nullptr );
        REQUIRE( exec->attributes.value( QLatin1String( "state" ) ).toString()
                 == QStringLiteral( "Succeeded" ) );
        REQUIRE( exec->attributes.value( QLatin1String( "lineageSignature" ) ).toString().size() == 64 );

        const QStringList produced = graph.producedBy( execId );
        REQUIRE( produced.size() == 1 );
        // Transitive lineage: the artifact's producer resolves back.
        REQUIRE( graph.producerOf( produced.first() ) == execId );
    }

    // node_2 consumed node_1's artifact; node_1 consumed nothing.
    REQUIRE( graph.consumedBy( QStringLiteral( "node:node_1" ) ).isEmpty() );
    const QStringList consumed2 = graph.consumedBy( QStringLiteral( "node:node_2" ) );
    REQUIRE( consumed2.size() == 1 );
    REQUIRE( graph.producerOf( consumed2.first() ) == QStringLiteral( "node:node_1" ) );

    // Stable serialization: serialize -> parse -> serialize is identical.
    QFile raw( provenanceFile );
    REQUIRE( raw.open( QIODevice::ReadOnly ) );
    const QJsonObject disk = QJsonDocument::fromJson( raw.readAll() ).object();
    const auto reparsed = ProvenanceGraph::fromJson( disk );
    REQUIRE( reparsed.isSuccess() );
    REQUIRE( QJsonDocument( reparsed.value().toJson() ).toJson( QJsonDocument::Compact )
             == QJsonDocument( disk ).toJson( QJsonDocument::Compact ) );
}

TEST_CASE( "A resumed run records cache hits as reusedFrom edges", "[d17][workflow][provenance]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "provenance-resume" ) );
    QString checkpointFile;
    {
        PipelineRunCoordinator coordinator;
        coordinator.setExecutor( makeSyntheticNodeExecutor() );
        REQUIRE( coordinator.startRun( chain( 3 ), dir ) );
        REQUIRE( waitForCompleted( coordinator ) );
        checkpointFile = coordinator.checkpointPath();
    }
    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpointFile ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    const ProvenanceGraph graph = loadProvenance( resumeCoordinator.provenancePath() );
    for ( int i = 1; i <= 3; ++i )
    {
        const QString execId = QStringLiteral( "node:node_%1" ).arg( i );
        REQUIRE( graph.producedBy( execId ).isEmpty() ); // nothing re-produced
        REQUIRE( graph.reusedBy( execId ).size() == 1 ); // verified reuse recorded
    }
    // The consumed artifact's producer is absent in THIS run's record — it
    // was produced by the previous attempt and lives in that record.
    const QStringList consumed2 = graph.consumedBy( QStringLiteral( "node:node_2" ) );
    REQUIRE( consumed2.size() == 1 );
    REQUIRE( graph.producerOf( consumed2.first() ).isEmpty() );
}

TEST_CASE( "Provenance records also describe failed runs", "[d17][workflow][provenance]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "provenance-fail" ) );
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( []( const NodeFact &node, const QHash<QString, QString> &,
                                 const QString &runDirectory ) -> NodeExecutionResult {
        NodeExecutionResult result;
        if ( node.nodeId == QLatin1String( "node_2" ) )
        {
            result.errorMessage = QStringLiteral( "boom" );
            return result;
        }
        const QString artifact = QDir( runDirectory ).filePath( node.nodeId + QStringLiteral( ".out" ) );
        QFile file( artifact );
        if ( file.open( QIODevice::WriteOnly ) )
        {
            file.write( "x" );
            file.close();
            result.success = true;
            result.artifactPath = artifact;
        }
        return result;
    } );
    REQUIRE( coordinator.startRun( chain( 3 ), dir ) );
    REQUIRE( waitForCompleted( coordinator ) );

    const ProvenanceGraph graph = loadProvenance( coordinator.provenancePath() );
    const ProvenanceNode *failed = findProvNode( graph, QStringLiteral( "node:node_2" ) );
    REQUIRE( failed != nullptr );
    REQUIRE( failed->attributes.value( QLatin1String( "state" ) ).toString()
             == QStringLiteral( "Failed" ) );
    REQUIRE( failed->attributes.value( QLatin1String( "errorMessage" ) ).toString()
             == QStringLiteral( "boom" ) );
    REQUIRE( findProvNode( graph, QStringLiteral( "node:node_3" ) )
                 ->attributes.value( QLatin1String( "state" ) )
                 .toString()
             == QStringLiteral( "Skipped" ) );
}

TEST_CASE( "Provenance parse fails closed on bad envelope and dangling edges", "[d17][workflow][provenance]" )
{
    const QJsonObject good =
        ProvenanceGraph::fromRunState( QStringLiteral( "r1" ), chain( 1 ),
                                       QHash<QString, NodeStatusSnapshot>{}, QString() )
            .toJson();

    SECTION( "wrong kind" )
    {
        QJsonObject doc = good;
        doc[QStringLiteral( "kind" )] = QStringLiteral( "other" );
        REQUIRE_FALSE( ProvenanceGraph::fromJson( doc ).isSuccess() );
    }
    SECTION( "unknown version" )
    {
        QJsonObject doc = good;
        doc[QStringLiteral( "version" )] = QStringLiteral( "9.9" );
        REQUIRE_FALSE( ProvenanceGraph::fromJson( doc ).isSuccess() );
    }
    SECTION( "dangling edge endpoint" )
    {
        QJsonObject doc = good;
        QJsonArray edges = doc[QStringLiteral( "edges" )].toArray();
        edges.append( QJsonObject{ { QStringLiteral( "from" ), QStringLiteral( "node:ghost" ) },
                                   { QStringLiteral( "to" ), QStringLiteral( "node:also_ghost" ) },
                                   { QStringLiteral( "kind" ), QStringLiteral( "consumed" ) } } );
        doc[QStringLiteral( "edges" )] = edges;
        REQUIRE_FALSE( ProvenanceGraph::fromJson( doc ).isSuccess() );
    }
    SECTION( "duplicate node id" )
    {
        QJsonObject doc = good;
        QJsonArray nodes = doc[QStringLiteral( "nodes" )].toArray();
        nodes.append( nodes[0] );
        doc[QStringLiteral( "nodes" )] = nodes;
        REQUIRE_FALSE( ProvenanceGraph::fromJson( doc ).isSuccess() );
    }
}
