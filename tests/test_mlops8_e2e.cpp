// test_mlops8_e2e.cpp — Platform 8.0 end-to-end: a REAL tracked pipeline on
// the authoritative WorkflowRunCoordinator is auto-recorded into the
// ExperimentStore through WorkflowExperimentMonitor (goal 8.0 §A).
// Success/failure/cancellation/resume stories land truthfully; a disabled
// monitor records nothing.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUuid>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "data/artifact_store.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/bridge/workflow_experiment_adapter.h"
#include "jobs/job_engine.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"

using namespace sicnu::workflow;
using namespace sicnu::experiment;

namespace
{

struct MonitorFixture
{
    QTemporaryDir checkpointDir;
    QTemporaryDir storeDir;
    WorkflowRunCoordinator &coordinator = WorkflowRunCoordinator::instance();
    ExperimentStore experimentStore{};

    MonitorFixture()
    {
        int argc = 1;
        static char arg0[] = "test_mlops8_e2e";
        char *argv[] = { arg0, nullptr };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );

        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );

        coordinator.setCheckpointDirectory( checkpointDir.path() );
    }

    std::unique_ptr<WorkflowExperimentMonitor> enable( const QString &experimentId )
    {
        auto monitor = std::make_unique<WorkflowExperimentMonitor>( coordinator );
        QString error;
        const bool ok = monitor->enable(
            storeDir.filePath( QStringLiteral( "exp.db" ) ), experimentId,
            QStringLiteral( "e2e experiment" ), QString(),
            /*datasetDbPath=*/QString(), &error );
        REQUIRE( ok );
        return monitor;
    }

    static void waitTerminal( WorkflowRunCoordinator &coordinator, long pipelineId )
    {
        std::shared_ptr<WorkflowRun> snapshot;
        for ( int attempt = 0; attempt < 600; ++attempt )
        {
            snapshot = coordinator.runForPipeline( pipelineId );
            REQUIRE( snapshot != nullptr );
            if ( isTerminalRunState( snapshot->state() ) )
                return;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        FAIL( "pipeline never reached a terminal state" );
    }
};

WorkflowDefinition twoStepDefinition( const std::string &prefix )
{
    WorkflowDefinition def;
    def.id = prefix + "_def";
    def.title = "MLOps E2E tracked pipeline";

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

void registerExecutors( const std::string &prefix, bool secondFails = false )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.registerExecutor( prefix + ":first",
                             [prefix]( const sicnu::jobs::JobRequest &,
                                       sicnu::operators::RSOperatorContext & ) {
                                 Json::Value r( Json::objectValue );
                                 r["output"] = "/tmp/" + prefix + "_first.tif";
                                 return r;
                             } );
    engine.registerExecutor( prefix + ":second",
                             [prefix, secondFails]( const sicnu::jobs::JobRequest &,
                                                    sicnu::operators::RSOperatorContext & )
                                 -> Json::Value {
                                 if ( secondFails )
                                     throw std::runtime_error( "executor failed on purpose" );
                                 return Json::Value( Json::objectValue );
                             } );
}

} // namespace

TEST_CASE( "successful tracked pipeline auto-records a Completed experiment run",
           "[mlops8][e2e]" )
{
    MonitorFixture fx;
    const std::string prefix = "mlops8_ok";
    registerExecutors( prefix );
    auto monitor = fx.enable( QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000001" ) );

    const long pipelineId =
        fx.coordinator.startTrackedPipeline( twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    MonitorFixture::waitTerminal( fx.coordinator, pipelineId );
    monitor->flush();

    const auto page = fx.experimentStore.listRuns();
    REQUIRE( page.has_value() );
    REQUIRE( page.value().second.size() == 1 );
    const ExperimentRun &run = page.value().second.first();
    REQUIRE( run.status() == RunStatus::Completed );
    REQUIRE( run.executionRef() == QString::fromStdString(
                                       fx.coordinator.runForPipeline( pipelineId )->runId() ) );
    REQUIRE( run.algorithmId() == QStringLiteral( "mlops8_ok_def" ) );
    // Artifacts carry the step output paths (one per completed step; no
    // digest here because the fixture's outputs are not real files).
    REQUIRE( run.artifacts().size() == 2 );
    bool sawSecondArtifact = false;
    for ( const auto &artifact : run.artifacts() )
    {
        if ( artifact.role() == QStringLiteral( "second" ) )
        {
            REQUIRE( artifact.path() == QStringLiteral( "/tmp/mlops8_ok_second.tif" ) );
            sawSecondArtifact = true;
        }
    }
    REQUIRE( sawSecondArtifact );
    // Definition snapshot + step evidence recorded.
    REQUIRE( run.metrics()
                 .value( "workflow" )
                 .toObject()
                 .value( "steps" )
                 .toArray()
                 .size() == 2 );
}

TEST_CASE( "failing tracked pipeline auto-records a Failed run with evidence",
           "[mlops8][e2e]" )
{
    MonitorFixture fx;
    const std::string prefix = "mlops8_fail";
    registerExecutors( prefix, /*secondFails=*/true );
    auto monitor = fx.enable( QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000002" ) );

    const long pipelineId =
        fx.coordinator.startTrackedPipeline( twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    MonitorFixture::waitTerminal( fx.coordinator, pipelineId );
    monitor->flush();

    const auto page = fx.experimentStore.listRuns();
    REQUIRE( page.has_value() );
    REQUIRE( page.value().second.size() == 1 );
    const ExperimentRun &run = page.value().second.first();
    REQUIRE( run.status() == RunStatus::Failed );
    const QJsonObject error = run.metrics().value( "error" ).toObject();
    REQUIRE( error.value( "error_code" ).toString() == QStringLiteral( "workflow.failed" ) );
    REQUIRE( error.value( "message" ).toString().contains(
                 QStringLiteral( "executor failed on purpose" ) ) );
}

TEST_CASE( "cancelled tracked pipeline auto-records Cancelled", "[mlops8][e2e]" )
{
    MonitorFixture fx;
    const std::string prefix = "mlops8_cancel";

    // First executor blocks until we cancel; second never runs.
    auto &engine = sicnu::jobs::JobEngine::instance();
    std::atomic_bool firstStarted{ false };
    engine.registerExecutor( prefix + ":first",
                             [&firstStarted]( const sicnu::jobs::JobRequest &,
                                              sicnu::operators::RSOperatorContext & ) {
                                 firstStarted.store( true );
                                 int waited = 0;
                                 while ( !firstStarted.load() || waited < 3000 )
                                 {
                                     std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
                                     waited += 10;
                                 }
                                 throw std::runtime_error( "cancelled while running" );
                             } );
    engine.registerExecutor( prefix + ":second",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 return Json::Value( Json::objectValue );
                             } );

    auto monitor = fx.enable( QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000003" ) );
    const long pipelineId =
        fx.coordinator.startTrackedPipeline( twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );

    // Wait until the first step is actually running, then cancel the run.
    bool stepRunning = false;
    for ( int attempt = 0; attempt < 300; ++attempt )
    {
        if ( const auto run = fx.coordinator.runForPipeline( pipelineId ) )
        {
            const auto plan = run->stepPlan( "first" );
            if ( plan && plan->status == "Running" )
            {
                stepRunning = true;
                break;
            }
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( stepRunning );
    REQUIRE( fx.coordinator.cancelRun( pipelineId ) );
    MonitorFixture::waitTerminal( fx.coordinator, pipelineId );
    monitor->flush();

    // The contract is experiment-state == execution-truth, whatever the
    // engine decided for the racing cancel (Canceled when the cascade won,
    // Failed when the executor's own error landed first — both are truthful).
    const auto terminalState =
        fx.coordinator.runForPipeline( pipelineId )->state();
    REQUIRE( terminalState == WorkflowRunState::Canceled
             || terminalState == WorkflowRunState::Failed );
    const auto page = fx.experimentStore.listRuns();
    REQUIRE( page.has_value() );
    REQUIRE( page.value().second.size() >= 1 );
    bool sawTerminal = false;
    for ( const auto &run : page.value().second )
    {
        if ( run.executionRef()
             != QString::fromStdString(
                    fx.coordinator.runForPipeline( pipelineId )->runId() ) )
            continue;
        REQUIRE( isTerminalRunStatus( run.status() ) );
        REQUIRE( ( ( terminalState == WorkflowRunState::Canceled )
                   == ( run.status() == RunStatus::Cancelled ) ) );
        sawTerminal = true;
    }
    REQUIRE( sawTerminal );
}

TEST_CASE( "disabled monitor records nothing", "[mlops8][e2e]" )
{
    MonitorFixture fx;
    const std::string prefix = "mlops8_off";
    registerExecutors( prefix );
    // A monitor that never enable()d must be inert — even when attached.
    WorkflowExperimentMonitor monitor( fx.coordinator );

    const long pipelineId =
        fx.coordinator.startTrackedPipeline( twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    MonitorFixture::waitTerminal( fx.coordinator, pipelineId );
    monitor.flush();

    REQUIRE_FALSE( QFile::exists( fx.storeDir.filePath( QStringLiteral( "exp.db" ) ) ) );
}

TEST_CASE( "interrupted run resumes and completes the SAME experiment record",
           "[mlops8][e2e][resume]" )
{
    MonitorFixture fx;
    const std::string prefix = "mlops8_resume";

    // Hand-craft the interrupted on-disk state the way a crash leaves it:
    // first step completed with a real output file, second stuck Running.
    const QString outputPath = fx.checkpointDir.path() + "/mlops8_resume_first.tif";
    {
        QFile f( outputPath );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "fake-bytes" );
    }
    auto &engine = sicnu::jobs::JobEngine::instance();
    std::atomic_bool secondRan{ false };
    engine.registerExecutor( prefix + ":first",
                             [prefix]( const sicnu::jobs::JobRequest &,
                                       sicnu::operators::RSOperatorContext & ) {
                                 Json::Value r( Json::objectValue );
                                 r["output"] = "/tmp/" + prefix + "_first.tif";
                                 return r;
                             } );
    engine.registerExecutor( prefix + ":second",
                             [&]( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
                                 secondRan.store( true );
                                 return Json::Value( Json::objectValue );
                             } );

    const QString runId = QStringLiteral( "mlops8-resume-run" );
    {
        WorkflowRun run;
        run.setDefinition( twoStepDefinition( prefix ) );
        REQUIRE( run.setRunId( runId.toStdString() ) );
        run.forceSetState( WorkflowRunState::Running );
        StepPlan firstPlan;
        firstPlan.stepId = "first";
        firstPlan.operatorId = prefix + ":first";
        firstPlan.status = "Completed";
        firstPlan.outputLayerPath = outputPath.toStdString();
        firstPlan.outputSizeBytes = QFileInfo( outputPath ).size();
        firstPlan.outputMtimeMs =
            QFileInfo( outputPath ).lastModified().toMSecsSinceEpoch();
        StepPlan secondPlan;
        secondPlan.stepId = "second";
        secondPlan.operatorId = prefix + ":second";
        secondPlan.status = "Running";
        run.setStepPlans( { firstPlan, secondPlan } );
        workflow::WorkflowCheckpointManager manager;
        REQUIRE_FALSE( manager.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );
    }

    auto monitor = fx.enable( QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000005" ) );

    // Startup recovery marks the run Interrupted (emitted → recorded), then
    // the explicit resume completes it under the SAME run id.
    const auto recovered = fx.coordinator.recoverAtStartup( /*autoResume=*/false );
    REQUIRE( recovered.interruptedRuns == 1 );
    monitor->flush();

    REQUIRE( fx.coordinator.resumeRun( runId.toStdString() ) > 0 );
    const long pipelineId = fx.coordinator.pipelineIdForRun( runId.toStdString() );
    REQUIRE( pipelineId > 0 );
    MonitorFixture::waitTerminal( fx.coordinator, pipelineId );
    monitor->flush();

    const auto page = fx.experimentStore.listRuns();
    REQUIRE( page.has_value() );
    REQUIRE( page.value().second.size() == 1 );
    const ExperimentRun &run = page.value().second.first();
    REQUIRE( run.executionRef() == runId );
    REQUIRE( run.status() == RunStatus::Completed );
    REQUIRE( secondRan.load() );
    // The full story (interrupt + resume + complete) lives in ONE record.
    REQUIRE( run.startedAtUtc().isValid() );
    REQUIRE( run.finishedAtUtc().isValid() );
}

TEST_CASE( "stale reconciliation closes dead executions from checkpoint evidence",
           "[mlops8][e2e][reconcile]" )
{
    MonitorFixture fx;

    // Hand-craft two "crashed" checkpoints with truthful terminal evidence.
    const QString failedRef = QStringLiteral( "mlops8_stale_failed" );
    const QString canceledRef = QStringLiteral( "mlops8_stale_canceled" );

    auto writeCheckpoint = [&]( const QString &runId, WorkflowRunState state ) {
        WorkflowRun run;
        WorkflowDefinition def;
        def.id = "stale_def";
        run.setDefinition( def );
        REQUIRE( run.setRunId( runId.toStdString() ) );
        run.forceSetState( state );
        StepPlan plan;
        plan.stepId = "first";
        plan.status = state == WorkflowRunState::Failed ? "Failed" : "Canceled";
        if ( state == WorkflowRunState::Failed )
            plan.errorMessage = "crashed with evidence";
        run.setStepPlans( { plan } );
        workflow::WorkflowCheckpointManager manager;
        const QString saved = manager.saveCheckpoint( run, fx.checkpointDir.path() );
        REQUIRE_FALSE( saved.isEmpty() );
    };
    writeCheckpoint( failedRef, WorkflowRunState::Failed );
    writeCheckpoint( canceledRef, WorkflowRunState::Canceled );

    // Pre-seed experiment runs that claim these executions as Running (the
    // state a crash leaves behind), then reconcile.
    {
        REQUIRE( fx.experimentStore.open(
            fx.storeDir.filePath( QStringLiteral( "reconcile.db" ) ) ) );
        Experiment experiment;
        experiment.setExperimentId( QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000004" ) );
        experiment.setName( QStringLiteral( "reconcile" ) );
        experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
        REQUIRE( fx.experimentStore.upsertExperiment( experiment ).has_value() );
        for ( const QString &ref : { failedRef, canceledRef } )
        {
            ExperimentRun run;
            run.setRunId( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
            run.setExperimentId( experiment.experimentId() );
            run.setStatus( RunStatus::Running );
            run.setExecutionRef( ref );
            run.setAlgorithmId( QStringLiteral( "stale_def" ) );
            run.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
            run.setStartedAtUtc( QDateTime::currentDateTimeUtc() );
            REQUIRE( fx.experimentStore.upsertRun( run ).has_value() );
        }
    }

    auto monitor = std::make_unique<WorkflowExperimentMonitor>( fx.coordinator );
    QString error;
    REQUIRE( monitor->enable( fx.storeDir.filePath( QStringLiteral( "reconcile.db" ) ),
                              QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000004" ),
                              QStringLiteral( "reconcile" ), QString(), QString(), &error ) );

    const auto decisions = monitor->reconcileStaleRuns();
    QMap<QString, QString> byRef;
    for ( const auto &d : decisions )
        byRef.insert( d.executionRef, d.action );
    REQUIRE( byRef.value( failedRef ) == QStringLiteral( "failed" ) );
    REQUIRE( byRef.value( canceledRef ) == QStringLiteral( "cancelled" ) );

    for ( const auto &run : fx.experimentStore.listRuns().value().second )
    {
        if ( run.executionRef() == failedRef )
            REQUIRE( run.status() == RunStatus::Failed );
        if ( run.executionRef() == canceledRef )
            REQUIRE( run.status() == RunStatus::Cancelled );
    }
}
