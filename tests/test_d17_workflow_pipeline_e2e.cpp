// tests/test_d17_workflow_pipeline_e2e.cpp — full-stack end-to-end (D17 Package I)
//
//   IR 2.0 -> DAG analyze -> optimize -> stream execute -> checkpoint/resume
//
// Ground truth: enumerated node counts / tier sizes of the synthetic
// fixtures, the enumerated CacheHit prefix after a 50 % abort, getrusage
// peak RSS as an external measurement, and the shipped lab corpus
// (data/labs/*.lab.json — 11 files, verified on master) enumerated by the
// filesystem itself.
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>

#if defined( Q_OS_WIN )
#include <windows.h>
#include <psapi.h>
#pragma comment( lib, "psapi.lib" )
#else
#include <sys/resource.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include "app/pipeline/labspec_workflow_lift.h"
#include "workflow/pipeline_run_coordinator.h"
#include "workflow/plan_optimizer.h"
#include "workflow/workflow_dag_analyzer.h"
#include "workflow/workflow_ir_v2.h"

using namespace sicnu::workflow;

namespace {

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        static int fake_argc = 1;
        static char fake_argv[] = "test_d17_workflow_pipeline_e2e";
        static char *fake_argv_ptr[] = { fake_argv };
        app = new QApplication( fake_argc, fake_argv_ptr );
    }
    return app;
}

QString scratchDir( const QString &tag )
{
    const QString dir = QDir::temp().filePath( QStringLiteral( "d17-e2e-%1-%2" ).arg( tag, QString::number( QCoreApplication::applicationPid() ) ) );
    QDir().mkpath( dir );
    return dir;
}

NodeFact node( const QString &id, int inputs = 1 )
{
    NodeFact n;
    n.nodeId = id;
    n.operatorId = QStringLiteral( "rs:step" );
    n.displayName = id;
    n.canvasPosition = QPointF( 0, 0 );
    if ( inputs > 0 )
        n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                   QStringLiteral( "None" ), 0, 0, 1, true } };
    n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                QStringLiteral( "None" ), 0, 0, 1, false } };
    return n;
}

EdgeFact edge( const QString &from, const QString &to, const QString &id = QString() )
{
    return EdgeFact{ id.isEmpty() ? QStringLiteral( "e_%1_%2" ).arg( from, to ) : id,
                     from, QStringLiteral( "output" ), to, QStringLiteral( "input" ) };
}

/// Chain document with fresh node objects — the test's own fixture builder.
WorkflowDocument chainDef( int steps )
{
    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-chain-%1" ).arg( steps );
    for ( int i = 1; i <= steps; ++i )
    {
        NodeFact n = node( QStringLiteral( "node_%1" ).arg( i ), i > 1 ? 1 : 0 );
        def.nodes.append( n );
        if ( i > 1 )
            def.edges.append( edge( QStringLiteral( "node_%1" ).arg( i - 1 ), QStringLiteral( "node_%1" ).arg( i ) ) );
    }
    return def;
}

bool waitForCompleted( PipelineRunCoordinator &coordinator, int timeoutMs = 60000 )
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

/// 10 layers x 10 nodes; every node >= 1 reads 2 predecessors from the
/// previous layer. Tier sizes 10 across the board — hand-verified shape.
WorkflowDocument layerCake100()
{
    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-scale-100" );
    auto id = []( int layer, int k ) {
        return QStringLiteral( "L%1_%2" ).arg( layer ).arg( k );
    };
    for ( int layer = 0; layer < 10; ++layer )
        for ( int k = 0; k < 10; ++k )
        {
            NodeFact n = node( id( layer, k ), layer == 0 ? 0 : 1 );
            if ( layer > 0 )
                n.inputPorts.append( PortFact{ QStringLiteral( "aux" ), QStringLiteral( "Raster" ),
                                               QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, true } );
            def.nodes.append( n );
        }
    for ( int layer = 1; layer < 10; ++layer )
        for ( int k = 0; k < 10; ++k )
        {
            def.edges.append( edge( id( layer - 1, k ), id( layer, k ) ) );
            // The second read targets a DIFFERENT input port (in-degree <= 1).
            def.edges.append( EdgeFact{ QStringLiteral( "e_%1_%2" ).arg( id( layer - 1, ( k + 1 ) % 10 ), id( layer, k ) ),
                                        id( layer - 1, ( k + 1 ) % 10 ), QStringLiteral( "output" ), id( layer, k ),
                                        QStringLiteral( "aux" ) } );
        }
    return def;
}

qint64 peakRssBytes()
{
#if defined( Q_OS_WIN )
    PROCESS_MEMORY_COUNTERS counters;
    if ( ::GetProcessMemoryInfo( ::GetCurrentProcess(), &counters, sizeof( counters ) ) )
        return qint64( counters.PeakWorkingSetSize );
    return 0;
#elif defined( Q_OS_MACOS )
    rusage usage;
    ::getrusage( RUSAGE_SELF, &usage );
    return qint64( usage.ru_maxrss ); // macOS reports BYTES
#elif defined( Q_OS_LINUX )
    rusage usage;
    ::getrusage( RUSAGE_SELF, &usage );
    return qint64( usage.ru_maxrss ) * 1024; // Linux reports KiB
#else
    // Unknown platform: report no evidence rather than a wrong unit.
    return 0;
#endif
}

} // namespace

TEST_CASE( "Mini E2E: 3-node pipeline through IR, analyzer, optimizer and executor",
           "[d17][workflow][e2e][mini]" )
{
    ensureApp();
    // 1. Document via the IR seam (golden fixture).
    QFile golden( QString( "%1/tests/fixtures/workflow_ir_v2/linear_pipeline_v2.json" ).arg( CMAKE_SOURCE_DIR ) );
    REQUIRE( golden.open( QIODevice::ReadOnly ) );
    const QJsonDocument doc = QJsonDocument::fromJson( golden.readAll() );
    REQUIRE( !doc.isNull() );
    auto parsed = WorkflowIR::fromJson( doc.object() );
    REQUIRE( parsed.isSuccess() );

    // 2. DAG analysis: the 5-node linear chain has 5 tiers of width 1.
    const DagAnalysisResult dag = WorkflowDagAnalyzer::analyzeDag( parsed.value() );
    REQUIRE( dag.isAcyclic );
    REQUIRE( dag.executionTiers.size() == 5 );
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( dag.executionTiers ) == 1 );

    // 3. Optimization toward the final node is a no-op on a linear chain.
    const QString terminal = parsed.value().nodes.last().nodeId;
    OptimizationReport report;
    const WorkflowDocument optimized = WorkflowPlanOptimizer::optimizePlan(
        parsed.value(), QSet<QString>{ terminal }, &report );
    REQUIRE( report.deadNodesPruned == 0 );
    REQUIRE( report.commonSubexpressionsMerged == 0 );
    REQUIRE( optimized.nodes.size() == 5 );

    // 4. Execute; every artifact lands on disk.
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() ); // #1006: explicit binding, no implicit default
    const QString dir = scratchDir( QStringLiteral( "mini" ) );
    REQUIRE( coordinator.startRun( optimized, dir ) );
    REQUIRE( waitForCompleted( coordinator ) );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 5 );
    for ( const NodeStatusSnapshot &snapshot : statuses )
    {
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
        REQUIRE( QFile::exists( snapshot.outputArtifactPath ) );
    }
}

TEST_CASE( "Crash consistency: abort at ~50%, resume reuses the prefix", "[d17][workflow][e2e][crash]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "crash" ) );
    QString checkpointFile;

    // Run 1: a hard abort after the 5th node of 10 finishes ("process kill"
    // simulation: the coordinator object is destroyed without completion).
    {
        PipelineRunCoordinator coordinator;
        coordinator.setExecutor( makeSyntheticNodeExecutor() );
        REQUIRE( coordinator.startRun( chainDef( 10 ), dir ) );
        QSignalSpy finishedSpy( &coordinator, &PipelineRunCoordinator::nodeFinished );
        QEventLoop loop;
        QObject::connect( &coordinator, &PipelineRunCoordinator::nodeFinished, &loop, [&]() {
            // fired AFTER each node terminal transition; 5 successes = 50 %
            int successes = 0;
            for ( int i = 0; i < finishedSpy.count(); ++i )
                if ( finishedSpy.at( i ).at( 1 ).toBool() )
                    ++successes;
            if ( successes >= 5 )
                loop.quit();
        } );
        QTimer::singleShot( 30000, &loop, &QEventLoop::quit );
        loop.exec();

        checkpointFile = coordinator.checkpointPath();
        REQUIRE( QFile::exists( checkpointFile ) );
        // Abandon the run: pending nodes never complete on this coordinator.
        coordinator.requestCancel();
    }

    // Run 2: a NEW coordinator replays the checkpoint. Nodes 1..5 were
    // Succeeded with intact artifacts and matching signatures -> CacheHit;
    // 6..10 recompute and finish.
    PipelineRunCoordinator resumeCoordinator;
    resumeCoordinator.setExecutor( makeSyntheticNodeExecutor() );
    QString error;
    REQUIRE( resumeCoordinator.resumeFromCheckpoint( checkpointFile, &error ) );
    REQUIRE( waitForCompleted( resumeCoordinator ) );

    const auto statuses = resumeCoordinator.getAllStatuses();
    REQUIRE( statuses.size() == 10 );
    int cacheHits = 0;
    for ( int i = 1; i <= 10; ++i )
    {
        const NodeStatusSnapshot snapshot = statuses.value( QStringLiteral( "node_%1" ).arg( i ) );
        REQUIRE( snapshot.state == ExecutionState::Succeeded );
        if ( i <= 5 )
        {
            INFO( "node_" << i );
            REQUIRE( snapshot.isCacheHit );
            ++cacheHits;
        }
        else
        {
            INFO( "node_" << i );
            REQUIRE_FALSE( snapshot.isCacheHit );
        }
    }
    REQUIRE( cacheHits == 5 );
}

TEST_CASE( "Full 100-node scale run under the 1.5 GiB RSS budget", "[d17][workflow][e2e][scale]" )
{
    ensureApp();
    const WorkflowDocument def = layerCake100();
    REQUIRE( def.nodes.size() == 100 );

    // Structural truth of the fixture: 10 tiers x width 10.
    const DagAnalysisResult dag = WorkflowDagAnalyzer::analyzeDag( def );
    REQUIRE( dag.isAcyclic );
    REQUIRE( dag.executionTiers.size() == 10 );
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( dag.executionTiers ) == 10 );
    REQUIRE( WorkflowIR::validateSemantics( def ) );

    const qint64 rssBefore = peakRssBytes();

    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeSyntheticNodeExecutor() );
    coordinator.setMaxParallelism( 4 );
    const QString dir = scratchDir( QStringLiteral( "scale" ) );
    const auto started = std::chrono::steady_clock::now();
    REQUIRE( coordinator.startRun( def, dir ) );
    REQUIRE( waitForCompleted( coordinator ) );
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started )
                               .count();

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 100 );
    for ( const auto &it : statuses.asKeyValueRange() )
    {
        INFO( it.first.toStdString() );
        REQUIRE( it.second.state == ExecutionState::Succeeded );
    }
    INFO( "elapsed: " << elapsedMs << " ms" );

    // External measurement: the process peak must stay under 1.5 GiB.
    INFO( "peak RSS: " << peakRssBytes() / ( 1024 * 1024 ) << " MiB" );
    REQUIRE( peakRssBytes() < qint64( 1536 ) * 1024 * 1024 );
    REQUIRE( rssBefore > 0 );
}

TEST_CASE( "All 11 shipped lab templates execute green through the full stack",
           "[d17][workflow][e2e][labs]" )
{
    ensureApp();
    const QDir labsDir( QString( "%1/data/labs" ).arg( CMAKE_SOURCE_DIR ) );
    QStringList labFiles = labsDir.entryList( { "*.lab.json" }, QDir::Files, QDir::Name );
    // D-160-7: the D16 temporal courseware pair ships a teaching-only
    // lab8_temporal_analysis.lab.json (id temporal_phenology_timeline) that
    // intentionally does not conform to the strict LabSpec-1.0 corpus loader;
    // its grading runs headless in test_d16_temporal_phenology_e2e instead.
    labFiles.removeAll( QStringLiteral( "lab8_temporal_analysis.lab.json" ) );
    REQUIRE( labFiles.size() == 11 ); // the strict LabSpec-1.0 teaching corpus

    // Corpus aggregate truth (hand-counted on master: 3+2+1+1+2+0+2+1+1+1+2):
    // 16 operator-bound steps lift into runnable nodes across the 11 labs;
    // lab06 is teaching-only (0 operators) and completes trivially.
    int corpusOperatorSteps = 0;

    // NOTE: deliberately ONE loop (no DYNAMIC_SECTION): Catch2 re-runs the
    // whole test case per section, which would reset the aggregate counter.
    for ( const QString &labFile : labFiles )
    {
        {
            // LabSpec 1.0 -> WorkflowDocument 2.0 via the shared lift.
            lab::LabSpecError loadError;
            const lab::LabSpec spec = lab::loadLabSpecFile( labsDir.filePath( labFile ), &loadError );
            REQUIRE( loadError.reason.isEmpty() );
            corpusOperatorSteps += static_cast<int>( std::count_if(
                spec.steps.cbegin(), spec.steps.cend(), []( const lab::LabStep &step ) { return step.hasOperator(); } ) );
            const WorkflowDocument def = sicnu::app::pipeline::liftLabSpecToWorkflow( spec );
            REQUIRE( WorkflowIR::validateSemantics( def ) );
            REQUIRE( WorkflowDagAnalyzer::analyzeDag( def ).isAcyclic );

            UNSCOPED_INFO( "running " << labFile.toStdString() << " (" << def.nodes.size() << " nodes)" );
            PipelineRunCoordinator coordinator;
            coordinator.setExecutor( makeSyntheticNodeExecutor() );
            const QString dir = scratchDir( spec.id );
            REQUIRE( coordinator.startRun( def, dir ) );
            REQUIRE( waitForCompleted( coordinator ) );

            const auto statuses = coordinator.getAllStatuses();
            REQUIRE( statuses.size() == def.nodes.size() );
            for ( const auto &it : statuses.asKeyValueRange() )
            {
                INFO( labFile.toStdString() << " / " << it.first.toStdString() );
                REQUIRE( it.second.state == ExecutionState::Succeeded );
            }
            // Hygiene: a green lab run leaves no scratch behind.
            QDir( dir ).removeRecursively();
        }
    }
    REQUIRE( corpusOperatorSteps == 16 );
}

namespace
{

bool waitUntilPredicate( const std::function<bool()> &predicate, int timeoutMs = 20000 )
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while ( QDateTime::currentMSecsSinceEpoch() < deadline )
    {
        if ( predicate() )
            return true;
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 10 );
    }
    return predicate();
}

QByteArray readFileBytes( const QString &path )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return {};
    return f.readAll();
}

} // namespace

TEST_CASE( "A checkpoint mid-resume is owned by one process at a time", "[d17][e2e][resume][ownership]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "ownership" ) );

    // Gate executor: writes every node's artifact but parks "node_2" inside
    // the worker until the test releases it — the stand-in for a long-running
    // operator in the owning process.
    auto gate = std::make_shared<std::atomic<bool>>( false );
    PipelineRunCoordinator owner;
    owner.setExecutor( [gate]( const NodeFact &node, const QHash<QString, QString> &,
                               const QString &runDirectory,
                               const std::atomic<bool> *cancel ) -> NodeExecutionResult {
        NodeExecutionResult result;
        const QString artifact =
            QDir( runDirectory ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
        {
            QFile f( artifact );
            if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
            {
                result.errorMessage = QStringLiteral( "cannot write artifact" );
                return result;
            }
            f.write( "x" );
        }
        if ( node.nodeId == QLatin1String( "node_2" ) )
        {
            while ( !gate->load() && !( cancel && cancel->load() ) )
                QThread::msleep( 10 );
        }
        result.success = true;
        result.artifactPath = artifact;
        return result;
    } );
    REQUIRE( owner.startRun( chainDef( 3 ), dir ) );
    REQUIRE( waitUntilPredicate( [&owner] {
        return owner.getAllStatuses().value( QStringLiteral( "node_2" ) ).state
               == ExecutionState::Running;
    } ) );
    // The initial checkpoint records node_1 Succeeded, node_2 Running.
    const QByteArray checkpointBefore = readFileBytes( owner.checkpointPath() );
    REQUIRE( !checkpointBefore.isEmpty() );

    // The double-execution oracle: while the owner holds the run, a second
    // coordinator resuming the same checkpoint must be REFUSED. The pre-fix
    // coordinator accepted it — both processes then executed node_2/node_3
    // against the same artifact paths and overwrote each other's checkpoints.
    {
        PipelineRunCoordinator peer;
        peer.setExecutor( makeSyntheticNodeExecutor() );
        QString err;
        REQUIRE_FALSE( peer.resumeFromCheckpoint( owner.checkpointPath(), &err ) );
        REQUIRE( err.contains( QLatin1String( "live process" ) ) );
        // A refused resume is a no-op on disk: the owner's checkpoint is
        // exactly the bytes the peer read, never a peer-state rewrite.
        REQUIRE( readFileBytes( owner.checkpointPath() ) == checkpointBefore );
    }

    // Release the gate: the owner finishes and finalizes, which drops the
    // ownership lock — the checkpoint becomes verifiable by peers again.
    gate->store( true );
    REQUIRE( waitForCompleted( owner ) );

    PipelineRunCoordinator verifier;
    verifier.setExecutor( makeSyntheticNodeExecutor() );
    QString verifyErr;
    REQUIRE( verifier.resumeFromCheckpoint( owner.checkpointPath(), &verifyErr ) );
    REQUIRE( waitForCompleted( verifier ) );
    const QMap<QString, NodeStatusSnapshot> statuses = verifier.getAllStatuses();
    REQUIRE( statuses.value( QStringLiteral( "node_1" ) ).isCacheHit );
    REQUIRE( statuses.value( QStringLiteral( "node_3" ) ).isCacheHit );
}

TEST_CASE( "Two coordinators cannot resume the same checkpoint concurrently",
           "[d17][e2e][resume][ownership][double-resume]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "double-resume" ) );

    // Produce a terminal checkpoint, then invalidate one artifact so the
    // resume has real work (a node reverts to Pending and would re-execute).
    PipelineRunCoordinator first;
    first.setExecutor( makeSyntheticNodeExecutor() );
    REQUIRE( first.startRun( chainDef( 2 ), dir ) );
    REQUIRE( waitForCompleted( first ) );
    const QString checkpoint = first.checkpointPath();
    REQUIRE( QFile::remove( QDir( dir ).filePath( QStringLiteral( "node_2.artifact" ) ) ) );

    // Resume A parks node_1 (still Succeeded? no — node_1's artifact is
    // intact, it CacheHits; node_2 re-executes). Park INSIDE node_2's
    // re-execution via a gate executor bound to the resuming coordinator.
    auto gate = std::make_shared<std::atomic<bool>>( false );
    auto resumeA = std::make_unique<PipelineRunCoordinator>();
    resumeA->setExecutor( [gate]( const NodeFact &node, const QHash<QString, QString> &,
                                  const QString &runDirectory,
                                  const std::atomic<bool> *cancel ) -> NodeExecutionResult {
        while ( !gate->load() && !( cancel && cancel->load() ) )
            QThread::msleep( 10 );
        NodeExecutionResult result;
        const QString artifact =
            QDir( runDirectory ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
        QFile f( artifact );
        f.open( QIODevice::WriteOnly | QIODevice::Truncate );
        f.write( "y" );
        f.close();
        result.success = true;
        result.artifactPath = artifact;
        return result;
    } );
    REQUIRE( resumeA->resumeFromCheckpoint( checkpoint ) );
    REQUIRE( waitUntilPredicate( [&resumeA] {
        return resumeA->getAllStatuses().value( QStringLiteral( "node_2" ) ).state
               == ExecutionState::Running;
    } ) );

    // Resume B of the SAME checkpoint while A holds it: refused, no work.
    PipelineRunCoordinator resumeB;
    resumeB.setExecutor( makeSyntheticNodeExecutor() );
    QString err;
    REQUIRE_FALSE( resumeB.resumeFromCheckpoint( checkpoint, &err ) );
    REQUIRE( err.contains( QLatin1String( "live process" ) ) );

    gate->store( true );
    REQUIRE( waitForCompleted( *resumeA ) );
}

TEST_CASE( "Foreign-thread destruction during a whole-file hash never frees live state",
           "[d17][e2e][destroy][uaf]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "destroy-hash" ) );

    // 32 MiB artifact: Auto identity escalates to the whole-file hash, which
    // pumps the event loop once per 1 MiB chunk — dozens of pump windows in
    // which a foreign destruction can land re-entrantly inside onNodeFinished.
    const qint64 artifactBytes = 32LL * 1024 * 1024;
    auto destroyed = std::make_shared<std::atomic<bool>>( false );
    auto *coordinator = new PipelineRunCoordinator;
    QPointer<PipelineRunCoordinator> guard( coordinator );
    const QPointer<QEventLoop> loopGuard = new QEventLoop;

    coordinator->setExecutor(
        [artifactBytes]( const NodeFact &node, const QHash<QString, QString> &,
                         const QString &runDirectory,
                         const std::atomic<bool> * ) -> NodeExecutionResult {
            NodeExecutionResult result;
            const QString artifact =
                QDir( runDirectory ).filePath( node.nodeId + QStringLiteral( ".artifact" ) );
            {
                QFile f( artifact );
                if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
                {
                    result.errorMessage = QStringLiteral( "cannot write artifact" );
                    return result;
                }
                const QByteArray chunk( 1024 * 1024, 'x' );
                for ( qint64 written = 0; written < artifactBytes; written += chunk.size() )
                    f.write( chunk );
            }
            result.success = true;
            result.artifactPath = artifact;
            return result;
        } );

    WorkflowDocument def = chainDef( 1 );
    REQUIRE( coordinator->startRun( def, dir ) );

    // Arm a 0 ms timer that deletes the coordinator from a FOREIGN thread.
    // The timer is created on this (affinity) thread via a queued closure, so
    // it first fires while the event loop is pumping INSIDE onNodeFinished's
    // whole-file hash — exactly the re-entrancy window the destructor drain
    // must survive.
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [coordinator, destroyed, loopGuard] {
            QTimer *killer = new QTimer( QCoreApplication::instance() );
            killer->setSingleShot( true );
            QObject::connect( killer, &QTimer::timeout, QCoreApplication::instance(),
                              [coordinator, destroyed, loopGuard] {
                                  std::thread( [coordinator, destroyed, loopGuard] {
                                      delete coordinator; // foreign-thread destruction
                                      destroyed->store( true );
                                      if ( loopGuard )
                                          QMetaObject::invokeMethod(
                                              loopGuard, &QEventLoop::quit,
                                              Qt::QueuedConnection );
                                  } ).detach();
                              },
                              Qt::DirectConnection );
            killer->start( 0 );
        },
        Qt::QueuedConnection );

    // Drive the affinity loop: worker completion -> onNodeFinished (hash,
    // pumps) -> killer timer -> foreign delete -> destructor drain.
    QTimer::singleShot( 60000, loopGuard, &QEventLoop::quit );
    loopGuard->exec();

    REQUIRE( waitUntilPredicate( [destroyed] { return destroyed->load(); }, 30000 ) );
    // Surviving with the coordinator fully destroyed IS the assertion: the
    // pre-fix destructor freed m_state while the affinity thread was still
    // inside onNodeFinished's hash, corrupting the heap.
    REQUIRE( guard.isNull() );
    delete loopGuard;
}
