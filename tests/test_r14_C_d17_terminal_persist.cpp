// tests/test_r14_C_d17_terminal_persist.cpp — R14-C FIX-2 regression (#1448,
// Engine-2 / D17 lane).
//
// RED first: PipelineRunCoordinator::finalizeIfDone released the cross-process
// run lock UNCONDITIONALLY after the terminal persistCheckpoint call, but
// persistCheckpoint swallowed the atomicWriteJson failure (it returns {} on a
// fault or sidecar refusal and only skipped the checkpointPersisted signal).
// A peer could then flock the run whose terminal state was never durable —
// exactly the #1448 scenario the Engine-1 coordinator fixed for its lane
// (d5ab7bc0b, "synchronize terminal persistence and run ownership release"),
// leaving the D17 lane as the unfixed half.
//
// Fix contract pinned here: when the TERMINAL checkpoint publish fails, the
// ownership flock stays held for the coordinator's lifetime, so a peer resume
// is refused ("owned by a live process") instead of double-owning a run whose
// on-disk checkpoint still shows a mid-run state.
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTimer>
#include <QEventLoop>

#include <atomic>

#include "workflow/pipeline_run_coordinator.h"
#include "runtime/observability/fault_registry.h"

using namespace sicnu::workflow;

namespace {

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        static int fake_argc = 1;
        static char fake_argv[] = "test_r14_C_d17_terminal_persist";
        static char *fake_argv_ptr[] = { fake_argv };
        app = new QApplication( fake_argc, fake_argv_ptr );
    }
    return app;
}

QString scratchDir( const QString &tag )
{
    const QString dir = QDir::temp().filePath(
        QStringLiteral( "r14c-d17-%1-%2" ).arg( tag, QString::number( QCoreApplication::applicationPid() ) ) );
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
    n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ),
                                QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, false } };
    n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ),
                               QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, true } };
    return n;
}

WorkflowDocument chain( int steps )
{
    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-r14c-%1" ).arg( steps );
    for ( int i = 1; i <= steps; ++i )
    {
        NodeFact node = chainNode( QStringLiteral( "node_%1" ).arg( i ), QStringLiteral( "rs:step" ) );
        if ( i == 1 )
            node.inputPorts.clear();
        def.nodes.append( node );
        if ( i > 1 )
            def.edges.append( EdgeFact{ QStringLiteral( "e%1" ).arg( i ),
                                         QStringLiteral( "node_%1" ).arg( i - 1 ),
                                         QStringLiteral( "output" ),
                                         QStringLiteral( "node_%1" ).arg( i ),
                                         QStringLiteral( "input" ) } );
    }
    return def;
}

bool waitForCompleted( PipelineRunCoordinator &coordinator, int timeoutMs = 20000 )
{
    ensureApp();
    if ( coordinator.hasCompleted() )
        return true;
    QSignalSpy spy( &coordinator, &PipelineRunCoordinator::pipelineCompleted );
    QEventLoop loop;
    QObject::connect( &coordinator, &PipelineRunCoordinator::pipelineCompleted, &loop,
                      &QEventLoop::quit, Qt::QueuedConnection );
    QTimer::singleShot( timeoutMs, &loop, &QEventLoop::quit );
    loop.exec();
    return spy.count() >= 1 || coordinator.hasCompleted();
}

NodeExecutor countingExecutor( std::atomic<int> *count )
{
    return [count]( const NodeFact &node, const QHash<QString, QString> &, const QString &runDir,
                    const std::atomic<bool> * ) -> NodeExecutionResult {
        count->fetch_add( 1 );
        return makeSyntheticNodeExecutor()( node, QHash<QString, QString>{}, runDir, nullptr );
    };
}

} // namespace

TEST_CASE( "a failed TERMINAL checkpoint publish keeps the D17 run lock owned (#1448, R14-C FIX-2)",
           "[d17][workflow][recovery][r14_C]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "terminal-persist" ) );

    // Producer: an unfaulted run writes a good, complete checkpoint and
    // releases its ownership at the durable terminal persist.
    PipelineRunCoordinator producer;
    producer.setExecutor( makeSyntheticNodeExecutor() );
    REQUIRE( producer.startRun( chain( 2 ), dir ) );
    REQUIRE( waitForCompleted( producer ) );
    const QString checkpoint = producer.checkpointPath();
    REQUIRE( QFile::exists( checkpoint ) );

    // Resumer: every checkpoint publish now faults — including the TERMINAL
    // one after the all-cache-hit finalize. The resume itself must still
    // complete (a lost persist is a lost recovery aid, never a failed run).
    std::atomic<int> executed{ 0 };
    sicnu::runtime::observability::fault::ArmedFault armed{
        sicnu::runtime::observability::fault::FaultAction{
            "d17_checkpoint.publish", sicnu::runtime::observability::fault::Mode::Always, 64, "" }
    };
    PipelineRunCoordinator resumer;
    resumer.setExecutor( countingExecutor( &executed ) );
    QString resumeError;
    REQUIRE( resumer.resumeFromCheckpoint( checkpoint, &resumeError ) );
    REQUIRE( waitForCompleted( resumer ) );
    REQUIRE( executed.load() == 0 ); // both nodes stayed verified cache hits

    // The run is terminal in memory but its terminal state was NEVER
    // published. Ownership must therefore still be held: a peer resume is
    // refused by the flock instead of double-owning a run whose on-disk
    // checkpoint still shows the pre-resume (mid-run) generation.
    PipelineRunCoordinator peer;
    peer.setExecutor( makeSyntheticNodeExecutor() );
    QString peerError;
    REQUIRE_FALSE( peer.resumeFromCheckpoint( checkpoint, &peerError ) );
    INFO( "peer resume error: " << peerError.toStdString() );
    CHECK( peerError.contains( QStringLiteral( "owned by a live process" ) ) );
}

TEST_CASE( "a DURABLE terminal publish still releases the D17 run lock (R14-C FIX-2 control)",
           "[d17][workflow][recovery][r14_C]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "terminal-persist-control" ) );

    PipelineRunCoordinator producer;
    producer.setExecutor( makeSyntheticNodeExecutor() );
    REQUIRE( producer.startRun( chain( 2 ), dir ) );
    REQUIRE( waitForCompleted( producer ) );
    const QString checkpoint = producer.checkpointPath();
    REQUIRE( QFile::exists( checkpoint ) );

    // No fault: the terminal publish succeeds, ownership is released, and a
    // peer resume proceeds past the flock (it may still refuse later for
    // content reasons, but never with "owned by a live process").
    PipelineRunCoordinator peer;
    peer.setExecutor( makeSyntheticNodeExecutor() );
    QString peerError;
    const bool peerResumed = peer.resumeFromCheckpoint( checkpoint, &peerError );
    INFO( "peer resume error: " << peerError.toStdString() );
    if ( peerResumed )
        REQUIRE( waitForCompleted( peer ) );
    else
        CHECK_FALSE( peerError.contains( QStringLiteral( "owned by a live process" ) ) );
}
