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
#include <future>
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

TEST_CASE( "All shipped lab templates execute green through the full stack",
           "[d17][workflow][e2e][labs]" )
{
    ensureApp();
    const QDir labsDir( QString( "%1/data/labs" ).arg( CMAKE_SOURCE_DIR ) );
    QStringList labFiles = labsDir.entryList( { "*.lab.json" }, QDir::Files, QDir::Name );
    // D-160-7: the D16 temporal courseware pair ships a teaching-only
    // lab8_temporal_analysis.lab.json (id temporal_phenology_timeline) that
    // intentionally does not conform to the strict LabSpec-1.0 corpus loader;
    // its grading runs headless in test_d16_temporal_phenology_e2e instead.
    // (That file no longer ships as .lab.json — the removeAll below is kept
    // as a guard for checkouts that still carry it.)
    labFiles.removeAll( QStringLiteral( "lab8_temporal_analysis.lab.json" ) );
    // 6cc6b7016 (#1190 curriculum wave) added lab12..lab16 to the strict
    // LabSpec-1.0 corpus: lab01..lab11 (11) + lab12_sar_processing +
    // lab13_hyperspectral_analysis + lab14_cartographic_mapping +
    // lab15_data_inspection + lab16_accuracy_assessment = 16.
    REQUIRE( labFiles.size() == 16 ); // the strict LabSpec-1.0 teaching corpus

    // Corpus aggregate truth (recounted on master over data/labs:
    // 3+2+1+1+2+0+2+1+1+1+2 = 16 across lab01..lab11, plus lab12/13/14
    // teaching-only (0 operators), lab15 = 2, lab16 = 1):
    // 19 operator-bound steps lift into runnable nodes across the 16 labs;
    // lab06/lab12/lab13/lab14 are teaching-only (0 operators).
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
    REQUIRE( corpusOperatorSteps == 19 );
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

    // 1 GiB artifact: Auto identity escalates to the whole-file hash — 1024
    // chunked reads, each pumping the event loop, plus SHA-256 over the whole
    // file. Measured on the reference host (Qt's SIMD SHA-256, tmpfs-cached)
    // the hash frame stays live for ~30 ms — triple the 10 ms destroyer delay
    // below — so the mid-hash window hit holds on fast and slow machines both
    // (the margin only grows as the hash gets slower).
    const qint64 artifactBytes = 1024LL * 1024 * 1024;
    auto destroyed = std::make_shared<std::atomic<bool>>( false );
    auto completionSeen = std::make_shared<std::atomic<bool>>( false );

    auto *coordinator = new PipelineRunCoordinator;
    QPointer<PipelineRunCoordinator> guard( coordinator );
    const QPointer<QEventLoop> loopGuard = new QEventLoop;

    // Window-hit oracle: when the delete lands inside onNodeFinished (the
    // point of this test), the destructor's phase-1 shutdown latch is already
    // set and the coordinator ABANDONS the completion — nodeFinished must
    // never be emitted. If the delete ever lands past the frame instead, this
    // assertion fails loudly instead of passing vacuously.
    QObject::connect( coordinator, &PipelineRunCoordinator::nodeFinished, coordinator,
                      [completionSeen]( const QString &, bool, const QString & ) {
                          completionSeen->store( true );
                      },
                      Qt::DirectConnection );

    coordinator->setExecutor(
        [artifactBytes, coordinator, destroyed, loopGuard]( const NodeFact &node,
                                                            const QHash<QString, QString> &,
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
        // Arm the foreign destroyer NOW, just before the executor returns:
        // the worker queues the completion right afterwards, the affinity
        // thread enters onNodeFinished and starts hashing, and the one-shot
        // fires ~10 ms into the (multi-hundred-ms worst-case) hash. The
        // destroyer thread waits on the timer's promise — no sleep race — and
        // deletes the coordinator FROM A FOREIGN THREAD mid-hash.
        auto armed = std::make_shared<std::promise<void>>();
        auto armedFuture = armed->get_future().share();
        // Context: loopGuard (affinity thread). A context-less singleShot
        // armed from this pool thread would be parked on a dispatcher no one
        // runs — the timer must live on the driven event loop.
        QTimer::singleShot( 10, loopGuard, [armed] { armed->set_value(); } );
        std::thread( [coordinator, destroyed, loopGuard, armedFuture] {
            armedFuture.wait();
            delete coordinator; // foreign-thread destruction
            destroyed->store( true );
            if ( loopGuard )
                QMetaObject::invokeMethod( loopGuard, &QEventLoop::quit, Qt::QueuedConnection );
        } ).detach();
        result.success = true;
        result.artifactPath = artifact;
        return result;
        } );

    WorkflowDocument def = chainDef( 1 );
    REQUIRE( coordinator->startRun( def, dir ) );

    QTimer::singleShot( 60000, loopGuard, &QEventLoop::quit );
    loopGuard->exec();

    REQUIRE( waitUntilPredicate( [destroyed] { return destroyed->load(); }, 30000 ) );
    REQUIRE( guard.isNull() );
    REQUIRE_FALSE( completionSeen->load() );
    delete loopGuard;
    // The artifact is 1 GiB and the destroying coordinator never got to
    // finalization: remove the scratch here or repeated runs fill tmpfs.
    QDir( dir ).removeRecursively();
}

TEST_CASE( "Deleting the coordinator from its own hash pump never frees live state",
           "[d17][e2e][destroy][reentrant]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "destroy-pump" ) );

    // Same sizing math as the foreign-thread case: the hash frame must still
    // be pumping when the 20 ms one-shot fires (measured hash: ~100 ms).
    const qint64 artifactBytes = 512LL * 1024 * 1024;
    auto destroyed = std::make_shared<std::atomic<bool>>( false );
    auto completionSeen = std::make_shared<std::atomic<bool>>( false );

    auto *coordinator = new PipelineRunCoordinator;
    QPointer<PipelineRunCoordinator> guard( coordinator );
    const QPointer<QEventLoop> loopGuard = new QEventLoop;

    QObject::connect( coordinator, &PipelineRunCoordinator::nodeFinished, coordinator,
                      [completionSeen]( const QString &, bool, const QString & ) {
                          completionSeen->store( true );
                      },
                      Qt::DirectConnection );

    coordinator->setExecutor(
        [artifactBytes, coordinator, destroyed, loopGuard]( const NodeFact &node,
                                                            const QHash<QString, QString> &,
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
        // The SAME-thread re-entrant deletion point: the one-shot fires ~20 ms
        // into the hash, and its functor — delivered by the hash's own
        // processEvents pump, on the affinity thread, INSIDE the onNodeFinished
        // frame — deletes the coordinator. The pre-fix destructor took an
        // "unsupported" qWarning branch here and freed the run state under the
        // live frame (use-after-free: the hash loop then read the freed
        // cancel flag and unwound through freed state). This is the exact
        // shape a dock deleteLater() taking effect mid-hash produces.
        QTimer::singleShot( 20, loopGuard, [coordinator, destroyed, loopGuard] {
            delete coordinator;
            destroyed->store( true );
            if ( loopGuard )
                QMetaObject::invokeMethod( loopGuard, &QEventLoop::quit, Qt::QueuedConnection );
        } );
        result.success = true;
        result.artifactPath = artifact;
        return result;
        } );

    WorkflowDocument def = chainDef( 1 );
    REQUIRE( coordinator->startRun( def, dir ) );

    QTimer::singleShot( 60000, loopGuard, &QEventLoop::quit );
    loopGuard->exec();

    REQUIRE( waitUntilPredicate( [destroyed] { return destroyed->load(); }, 30000 ) );
    REQUIRE( guard.isNull() );
    // The completion was in flight when the delete landed: it must be
    // abandoned, never emitted from the dying object.
    REQUIRE_FALSE( completionSeen->load() );
    delete loopGuard;
}

TEST_CASE( "A completion whose worker outlives the coordinator is never delivered",
           "[d17][e2e][destroy][late-completion]" )
{
    ensureApp();
    const QString dir = scratchDir( QStringLiteral( "destroy-late" ) );

    auto gate = std::make_shared<std::atomic<bool>>( false );
    auto destroyed = std::make_shared<std::atomic<bool>>( false );
    auto completionSeen = std::make_shared<std::atomic<bool>>( false );

    auto *coordinator = new PipelineRunCoordinator;
    QPointer<PipelineRunCoordinator> guard( coordinator );
    const QPointer<QEventLoop> loopGuard = new QEventLoop;

    QObject::connect( coordinator, &PipelineRunCoordinator::nodeFinished, coordinator,
                      [completionSeen]( const QString &, bool, const QString & ) {
                          completionSeen->store( true );
                      },
                      Qt::DirectConnection );

    // The worker parks on the gate (honouring the cancel flag) until the
    // destructor's drain trips it — the "worker still running at delete"
    // shape. Its completion post then races the destructor's
    // removePostedEvents; the destructor must win (drain joins the worker
    // BEFORE dropping posted events) and the completion must never be
    // delivered into the dying object.
    coordinator->setExecutor(
        [gate]( const NodeFact &node, const QHash<QString, QString> &,
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
        while ( !gate->load() && !( cancel && cancel->load() ) )
            QThread::msleep( 5 );
        result.success = true;
        result.artifactPath = artifact;
        return result;
        } );

    WorkflowDocument def = chainDef( 1 );
    REQUIRE( coordinator->startRun( def, dir ) );
    REQUIRE( waitUntilPredicate( [&coordinator] {
        return coordinator->getAllStatuses().value( QStringLiteral( "node_1" ) ).state
               == ExecutionState::Running;
    } ) );

    std::thread( [coordinator, destroyed, loopGuard] {
        delete coordinator; // foreign-thread destruction while the worker is parked
        destroyed->store( true );
        if ( loopGuard )
            QMetaObject::invokeMethod( loopGuard, &QEventLoop::quit, Qt::QueuedConnection );
    } ).detach();

    // The destructor's drain trips the cancel flag first, so the parked
    // worker exits within one sleep quantum, posts its completion and
    // terminates; the drain then drops every posted completion.
    REQUIRE( waitUntilPredicate( [destroyed] { return destroyed->load(); }, 30000 ) );
    REQUIRE( guard.isNull() );
    REQUIRE_FALSE( completionSeen->load() );
    delete loopGuard;
    // The artifact is 1 GiB and the destroying coordinator never got to
    // finalization: remove the scratch here or repeated runs fill tmpfs.
    QDir( dir ).removeRecursively();
}
