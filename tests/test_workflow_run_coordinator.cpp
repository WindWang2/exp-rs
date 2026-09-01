// tests/test_workflow_run_coordinator.cpp — production wiring of Workflow v2
// (#697/#668): tracked pipelines persist per-transition checkpoints, recover
// interrupted runs at startup, resume without re-executing completed steps,
// and sweep intermediates via ArtifactGC on completion.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <thread>

#include "jobs/job_engine.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"

using namespace sicnu::workflow;

namespace {

struct CoordinatorFixture
{
    QTemporaryDir checkpointDir;
    WorkflowRunCoordinator &coordinator = WorkflowRunCoordinator::instance();

    CoordinatorFixture()
    {
        int argc = 1;
        static char arg0[] = "test_workflow_run_coordinator";
        char *argv[] = { arg0, nullptr };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );

        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );

        coordinator.setCheckpointDirectory( checkpointDir.path() );
    }
};

WorkflowDefinition twoStepDefinition( const std::string &prefix )
{
    WorkflowDefinition def;
    def.id = prefix + "_def";
    def.title = "Tracked pipeline";

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

void registerTwoStepExecutors( const std::string &prefix, std::atomic_bool *firstRan = nullptr,
                               std::atomic_bool *secondRan = nullptr )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.registerExecutor( prefix + ":first",
                             [firstRan, prefix]( const sicnu::jobs::JobRequest &,
                                                 sicnu::operators::RSOperatorContext & ) {
                                 if ( firstRan )
                                     firstRan->store( true );
                                 Json::Value r( Json::objectValue );
                                 r["output"] = "/tmp/" + prefix + "_first.tif";
                                 return r;
                             } );
    engine.registerExecutor( prefix + ":second",
                             [secondRan]( const sicnu::jobs::JobRequest &,
                                          sicnu::operators::RSOperatorContext & ) {
                                 if ( secondRan )
                                     secondRan->store( true );
                                 return Json::Value( Json::objectValue );
                             } );
}

} // namespace

TEST_CASE( "tracked pipeline persists checkpoints and completes (#697)", "[workflow][coordinator]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_ok";
    std::atomic_bool firstRan{ false }, secondRan{ false };
    registerTwoStepExecutors( prefix, &firstRan, &secondRan );

    const long pipelineId = fx.coordinator.startTrackedPipeline( twoStepDefinition( prefix ),
                                                                 /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );

    // Wait for the run to reach a terminal aggregate state.
    std::shared_ptr<WorkflowRun> snapshot;
    for ( int attempt = 0; attempt < 600; ++attempt )
    {
        snapshot = fx.coordinator.runForPipeline( pipelineId );
        REQUIRE( snapshot != nullptr );
        if ( snapshot.state() == WorkflowRunState::Completed
             || snapshot.state() == WorkflowRunState::Failed
             || snapshot.state() == WorkflowRunState::Canceled )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( snapshot.state() == WorkflowRunState::Completed );
    REQUIRE( firstRan.load() );
    REQUIRE( secondRan.load() );

    // Both step plans landed Completed with recorded outputs.
    const auto first = snapshot.stepPlan( "first" );
    REQUIRE( first.has_value() );
    REQUIRE( first->status == "Completed" );
    REQUIRE( first->outputLayerPath == "/tmp/coord_ok_first.tif" );

    // The checkpoint of a COMPLETED run is removed after the GC sweep (the
    // committed outputs live on as assets; the run is done).
    const QString checkpointPath = fx.checkpointDir.path() + QDir::separator()
                                   + QString::fromStdString( snapshot.runId() ) + ".json";
    REQUIRE_FALSE( QFile::exists( checkpointPath ) );
}

TEST_CASE( "interrupted run recovers at startup and resumes remaining steps (#697/#668)",
           "[workflow][coordinator][recovery]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_resume";

    // Hand-craft the interrupted on-disk state: first step completed with a
    // real output file, second step stuck Running (as after a crash).
    const QString outputPath = fx.checkpointDir.path() + "/coord_resume_first.tif";
    {
        QFile f( outputPath );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "fake" );
    }

    WorkflowRun run;
    auto def = twoStepDefinition( prefix );
    run.setDefinition( def );
    REQUIRE( run.setRunId( "coord_resume_crashed" ) );
    run.forceSetState( WorkflowRunState::Running );
    StepPlan firstPlan;
    firstPlan.stepId = "first";
    firstPlan.operatorId = prefix + ":first";
    firstPlan.status = "Completed";
    firstPlan.outputLayerPath = outputPath.toStdString();
    StepPlan secondPlan;
    secondPlan.stepId = "second";
    secondPlan.operatorId = prefix + ":second";
    secondPlan.status = "Running";
    run.setStepPlans( { firstPlan, secondPlan } );

    WorkflowCheckpointManager checkpoints;
    REQUIRE( false == checkpoints.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );

    // Startup recovery marks the run Interrupted (nothing re-executes yet).
    auto report = fx.coordinator.recoverAtStartup( /*autoResume=*/false );
    REQUIRE( report.interruptedRuns == 1 );
    REQUIRE( report.resumedPipelines == 0 );

    // Resume: the completed first step is NOT re-executed; only "second" runs.
    std::atomic_bool firstRan{ false }, secondRan{ false };
    registerTwoStepExecutors( prefix, &firstRan, &secondRan );
    QString err;
    const long pipelineId = fx.coordinator.resumeRun( "coord_resume_crashed", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    std::shared_ptr<WorkflowRun> snapshot;
    for ( int attempt = 0; attempt < 600; ++attempt )
    {
        snapshot = fx.coordinator.runForPipeline( pipelineId );
        REQUIRE( snapshot != nullptr );
        if ( snapshot.state() == WorkflowRunState::Completed
             || snapshot.state() == WorkflowRunState::Failed
             || snapshot.state() == WorkflowRunState::Canceled )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( snapshot.state() == WorkflowRunState::Completed );
    REQUIRE( snapshot.runId() == "coord_resume_crashed" ); // same lineage thread
    REQUIRE( secondRan.load() );
    REQUIRE_FALSE( firstRan.load() ); // completed step served from its output
}
