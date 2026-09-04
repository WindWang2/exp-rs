// tests/test_workflow_run_coordinator.cpp — production wiring of Workflow v2
// (#697/#668): tracked pipelines persist per-transition checkpoints, recover
// interrupted runs at startup, resume without re-executing completed steps,
// and sweep intermediates via ArtifactGC on completion.
//
// #727 regression coverage: resume DAG reconstruction is independent of the
// definition's declaration order, resume placeholder resolution is port-aware
// and shared with fresh dispatch, resolvedParams reflect what actually ran,
// and a run owned by a live process cannot be resumed from another owner.
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

/// Registers a no-op executor for @a algorithmId that records the parameters
/// it was dispatched with (post placeholder substitution) and optionally
/// flags that it ran. The returned Json carries @a resultPorts.
void registerCapturingExecutor( const std::string &algorithmId, std::atomic_bool *ran,
                                Json::Value resultPorts = Json::Value( Json::objectValue ) )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.registerExecutor( algorithmId,
                             [ran, resultPorts]( const sicnu::jobs::JobRequest &,
                                                 sicnu::operators::RSOperatorContext & ) {
                                 if ( ran )
                                     ran->store( true );
                                 return resultPorts;
                             } );
}

/// Waits until the run tracked for @a pipelineId reaches a terminal state.
std::shared_ptr<WorkflowRun> runToTerminal( WorkflowRunCoordinator &coordinator, long pipelineId )
{
    std::shared_ptr<WorkflowRun> snapshot;
    for ( int attempt = 0; attempt < 600; ++attempt )
    {
        snapshot = coordinator.runForPipeline( pipelineId );
        REQUIRE( snapshot != nullptr );
        if ( snapshot->state() == WorkflowRunState::Completed
             || snapshot->state() == WorkflowRunState::Failed
             || snapshot->state() == WorkflowRunState::Canceled )
            return snapshot;
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    return snapshot;
}

/// Persists @a run as an Interrupted checkpoint in the fixture directory —
/// the on-disk picture a crashed process leaves behind.
void saveInterruptedCheckpoint( CoordinatorFixture &fx, WorkflowRun &run )
{
    run.forceSetState( WorkflowRunState::Interrupted );
    WorkflowCheckpointManager checkpoints;
    REQUIRE( false == checkpoints.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );
}

/// Creates a placeholder output file (the recorded artifact of a pre-crash
/// completed step must exist for the step to be reusable).
void touchFile( const QString &path )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "artifact" );
}

QVariantMap executedParams( const std::shared_ptr<WorkflowRun> &run, const std::string &stepId )
{
    const auto plan = run->stepPlan( stepId );
    REQUIRE( plan.has_value() );
    REQUIRE( plan->taskId > 0 );
    return sicnu::TaskCenter::instance().getTaskInfo( plan->taskId ).parameterMap;
}

StepPlan makePlan( const std::string &stepId, const std::string &status,
                   const std::string &outputPath = {},
                   const Json::Value &payload = Json::Value( Json::objectValue ),
                   const std::string &operatorId = {} )
{
    StepPlan plan;
    plan.stepId = stepId;
    plan.status = status;
    plan.outputLayerPath = outputPath;
    plan.resultPayload = payload;
    plan.operatorId = operatorId;
    return plan;
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
        if ( snapshot->state() == WorkflowRunState::Completed
             || snapshot->state() == WorkflowRunState::Failed
             || snapshot->state() == WorkflowRunState::Canceled )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( firstRan.load() );
    REQUIRE( secondRan.load() );

    // Both step plans landed Completed with recorded outputs.
    const auto first = snapshot->stepPlan( "first" );
    REQUIRE( first.has_value() );
    REQUIRE( first->status == "Completed" );
    REQUIRE( first->outputLayerPath == "/tmp/coord_ok_first.tif" );

    // The checkpoint of a COMPLETED run is removed after the GC sweep (the
    // committed outputs live on as assets; the run is done).
    const QString checkpointPath = fx.checkpointDir.path() + QDir::separator()
                                   + QString::fromStdString( snapshot->runId() ) + ".json";
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
        if ( snapshot->state() == WorkflowRunState::Completed
             || snapshot->state() == WorkflowRunState::Failed
             || snapshot->state() == WorkflowRunState::Canceled )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( snapshot->runId() == "coord_resume_crashed" ); // same lineage thread
    REQUIRE( secondRan.load() );
    REQUIRE_FALSE( firstRan.load() ); // completed step served from its output
}

TEST_CASE( "resume keeps parent edges when the child is declared before its parent (#727)",
           "[workflow][coordinator][recovery][dag]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_dag_ooo";

    // Legal definition with OUT-OF-ORDER declaration: "child" first, its
    // dependency "parent" last. Nothing completed (crash before dispatch) —
    // both steps are resubmitted and the parent→child edge MUST survive.
    WorkflowDefinition def;
    def.id = prefix + "_def";
    def.title = "Out-of-order declaration";

    StepDef child;
    child.id = "child";
    child.kind = StepKind::Operator;
    child.operatorId = prefix + ":child";
    child.params["input"] = "$parent.output";
    child.params["output"] = "/tmp/" + prefix + "_child.tif";
    child.inputs.push_back( StepConnection{ "parent", "output", "input" } );

    StepDef parent;
    parent.id = "parent";
    parent.kind = StepKind::Operator;
    parent.operatorId = prefix + ":parent";
    parent.params["output"] = "/tmp/" + prefix + "_parent.tif";

    def.steps.push_back( child );  // declared FIRST
    def.steps.push_back( parent ); // declared LAST

    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.setStepPlans( { makePlan( "child", "Pending", {}, Json::Value(), prefix + ":child" ),
                        makePlan( "parent", "Pending", {}, Json::Value(), prefix + ":parent" ) } );
    saveInterruptedCheckpoint( fx, run );

    std::atomic_bool childRan{ false }, parentRan{ false };
    // The parent's executor provides a real "output" port payload so the
    // dispatch substitution resolves the child's $parent.output.
    Json::Value parentPorts( Json::objectValue );
    parentPorts["output"] = "/tmp/" + prefix + "_parent.tif";
    registerCapturingExecutor( prefix + ":child", &childRan );
    registerCapturingExecutor( prefix + ":parent", &parentRan, parentPorts );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( prefix + "_run", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( fx.coordinator, pipelineId );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( parentRan.load() );
    REQUIRE( childRan.load() );

    // The edge survived: the child's dispatched input is the parent's raster
    // output, not the literal "$parent.output".
    const QVariantMap childParams = executedParams( snapshot, "child" );
    REQUIRE( childParams["input"].toString() == "/tmp/" + prefix + "_parent.tif" );
    // ... and the parent wiring exists on the child task.
    const auto childPlan = snapshot->stepPlan( "child" );
    const auto parentPlan = snapshot->stepPlan( "parent" );
    REQUIRE( childPlan.has_value() );
    REQUIRE( parentPlan.has_value() );
    const auto childTask = sicnu::TaskCenter::instance().getTaskInfo( childPlan->taskId );
    REQUIRE( childTask.parentTaskIds.contains( parentPlan->taskId ) );
}

TEST_CASE( "resume resolves pre-crash parents port-aware from their result payloads (#727)",
           "[workflow][coordinator][recovery][ports]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_ports";

    // "infer" completed pre-crash; its payload carries a non-output string
    // port ("model") besides the raster output. "consume" (declared first!)
    // references three different ports.
    const QString rasterPath = fx.checkpointDir.path() + QStringLiteral("/%1_raster.tif").arg( QString::fromStdString( prefix ) );
    touchFile( rasterPath );

    WorkflowDefinition def;
    def.id = prefix + "_def";

    StepDef consume;
    consume.id = "consume";
    consume.kind = StepKind::Operator;
    consume.operatorId = prefix + ":consume";
    consume.params["model"] = "$infer.model";
    consume.params["input"] = "$infer.output";
    consume.params["mask"] = "$infer.mask"; // port absent from payload → canonical fallback
    consume.params["output"] = "/tmp/" + prefix + "_out.tif";
    // Nested JSON object placeholder: substitution recurses string leaves
    // inside objects (#727 port shapes).
    Json::Value nested( Json::Value( Json::objectValue ) );
    nested["raster"] = "$infer.output";
    nested["model"] = "$infer.model";
    nested["ghost"] = "$ghoststep.output"; // dangling step ref — stays literal
    consume.params["nested"] = nested;
    // Array element placeholder: substitution recurses into arrays too.
    Json::Value list( Json::Value( Json::arrayValue ) );
    list.append( "$infer.output" );
    list.append( "$infer.model" );
    consume.params["list"] = list;
    // Dangling port reference (step id never declared): parse succeeds, no
    // parent and no completed step matches — the placeholder stays literal
    // through resume AND dispatch.
    consume.params["dangling"] = "$ghoststep.output";
    consume.inputs.push_back( StepConnection{ "infer", "output", "input" } );

    StepDef infer;
    infer.id = "infer";
    infer.kind = StepKind::Operator;
    infer.operatorId = prefix + ":infer";
    infer.params["output"] = rasterPath.toStdString();

    def.steps.push_back( consume ); // child declared first
    def.steps.push_back( infer );

    Json::Value payload( Json::objectValue );
    payload["output"] = rasterPath.toStdString();
    payload["model"] = "seg_best.onnx";
    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.setStepPlans( { makePlan( "consume", "Pending" ),
                        makePlan( "infer", "Completed", rasterPath.toStdString(), payload,
                                  prefix + ":infer" ) } );
    saveInterruptedCheckpoint( fx, run );

    std::atomic_bool inferRan{ false }, consumeRan{ false };
    registerCapturingExecutor( prefix + ":infer", &inferRan, payload );
    registerCapturingExecutor( prefix + ":consume", &consumeRan );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( prefix + "_run", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( fx.coordinator, pipelineId );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE_FALSE( inferRan.load() );  // checkpoint-served
    REQUIRE( consumeRan.load() );

    const QVariantMap params = executedParams( snapshot, "consume" );
    REQUIRE( params["model"].toString() == "seg_best.onnx" ); // named port, NOT the raster path
    REQUIRE( params["input"].toString() == rasterPath );      // output port
    // A port MISSING from the payload falls back to the canonical output —
    // fallback order (2) of the shared resolver, identical to fresh dispatch
    // (applyPlaceholdersForTask returns the parent's outputLayerPath when the
    // exact port is absent). This is the defined shared semantics: what
    // resume must never do is shadow a port that EXISTS in the payload.
    REQUIRE( params["mask"].toString() == rasterPath );
    // Nested object: placeholder inside a nested JSON object is substituted
    // (string-leaf recursion), the dangling ref inside stays literal.
    const QVariantMap nestedMap = params["nested"].toMap();
    REQUIRE( nestedMap["raster"].toString() == rasterPath );
    REQUIRE( nestedMap["model"].toString() == "seg_best.onnx" );
    REQUIRE( nestedMap["ghost"].toString() == "$ghoststep.output" );
    // Array: every string element is substituted.
    const QVariantList listOut = params["list"].toList();
    REQUIRE( listOut.size() == 2 );
    REQUIRE( listOut[0].toString() == rasterPath );
    REQUIRE( listOut[1].toString() == "seg_best.onnx" );
    // Dangling port reference (no such step, completed or live): stays
    // literal end-to-end — nothing invents a value for it.
    REQUIRE( params["dangling"].toString() == "$ghoststep.output" );

    // resolvedParams persisted for BOTH sides of the crash boundary: the
    // resubmitted step carries the substituted set...
    const auto consumePlan = snapshot->stepPlan( "consume" );
    REQUIRE( consumePlan.has_value() );
    REQUIRE( consumePlan->resolvedParams["model"].asString() == "seg_best.onnx" );
    REQUIRE( consumePlan->resolvedParams["input"].asString() == rasterPath.toStdString() );
    REQUIRE( consumePlan->resolvedParams["mask"].asString() == rasterPath.toStdString() );
    // ... and the nested/array/dangling shapes persist exactly as executed.
    REQUIRE( consumePlan->resolvedParams["nested"]["raster"].asString() == rasterPath.toStdString() );
    REQUIRE( consumePlan->resolvedParams["nested"]["model"].asString() == "seg_best.onnx" );
    REQUIRE( consumePlan->resolvedParams["nested"]["ghost"].asString() == "$ghoststep.output" );
    REQUIRE( consumePlan->resolvedParams["list"][0].asString() == rasterPath.toStdString() );
    REQUIRE( consumePlan->resolvedParams["list"][1].asString() == "seg_best.onnx" );
    REQUIRE( consumePlan->resolvedParams["dangling"].asString() == "$ghoststep.output" );
    // ... and the checkpoint-served step's persisted params are untouched
    // here (they carried no placeholders — the mid-chain substitution case is
    // covered by test_workflow_resume_provenance).
    const auto inferPlan = snapshot->stepPlan( "infer" );
    REQUIRE( inferPlan.has_value() );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
}

TEST_CASE( "resume reconstructs a partially completed diamond with scrambled "
           "declaration order and multi-parent edges (#727)",
           "[workflow][coordinator][recovery][dag][diamond]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_diamond";

    // Diamond: a → (b, c) → d, with a completed pre-crash and declaration
    // order deliberately anti-topological: [d, c, b, a].
    const QString aPath = fx.checkpointDir.path() + QStringLiteral("/%1_a.tif").arg( QString::fromStdString( prefix ) );
    touchFile( aPath );

    WorkflowDefinition def;
    def.id = prefix + "_def";

    const auto edge = []( const char *from ) {
        return StepConnection{ from, "output", "input" };
    };

    StepDef d;
    d.id = "d";
    d.kind = StepKind::Operator;
    d.operatorId = prefix + ":d";
    d.params["fromB"] = "$b.output";
    d.params["fromC"] = "$c.output";
    d.params["output"] = "/tmp/" + prefix + "_d.tif";
    d.inputs.push_back( edge( "b" ) );
    d.inputs.push_back( edge( "c" ) );

    StepDef c;
    c.id = "c";
    c.kind = StepKind::Operator;
    c.operatorId = prefix + ":c";
    c.params["input"] = "$a.output";
    c.params["output"] = "/tmp/" + prefix + "_c.tif";
    c.inputs.push_back( edge( "a" ) );

    StepDef b;
    b.id = "b";
    b.kind = StepKind::Operator;
    b.operatorId = prefix + ":b";
    b.params["input"] = "$a.output";
    b.params["output"] = "/tmp/" + prefix + "_b.tif";
    b.inputs.push_back( edge( "a" ) );

    StepDef a;
    a.id = "a";
    a.kind = StepKind::Operator;
    a.operatorId = prefix + ":a";
    a.params["output"] = aPath.toStdString();

    def.steps = { d, c, b, a }; // scrambled on purpose

    Json::Value aPayload( Json::objectValue );
    aPayload["output"] = aPath.toStdString();
    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.setStepPlans( { makePlan( "d", "Pending" ),
                        makePlan( "c", "Pending" ),
                        makePlan( "b", "Pending" ),
                        makePlan( "a", "Completed", aPath.toStdString(), aPayload,
                                  prefix + ":a" ) } );
    saveInterruptedCheckpoint( fx, run );

    std::atomic_bool ranA{ false }, ranB{ false }, ranC{ false }, ranD{ false };
    Json::Value ports( Json::objectValue );
    ports["output"] = "unused.tif";
    registerCapturingExecutor( prefix + ":a", &ranA, ports );
    registerCapturingExecutor( prefix + ":b", &ranB, ports );
    registerCapturingExecutor( prefix + ":c", &ranC, ports );
    registerCapturingExecutor( prefix + ":d", &ranD, ports );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( prefix + "_run", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( fx.coordinator, pipelineId );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE_FALSE( ranA.load() );
    REQUIRE( ranB.load() );
    REQUIRE( ranC.load() );
    REQUIRE( ranD.load() );

    // Both live edges into d survived the scrambled declaration order: d's
    // task carries b AND c as parents (multi-parent).
    const auto dPlan = snapshot->stepPlan( "d" );
    const auto bPlan = snapshot->stepPlan( "b" );
    const auto cPlan = snapshot->stepPlan( "c" );
    REQUIRE( dPlan.has_value() );
    const auto dTask = sicnu::TaskCenter::instance().getTaskInfo( dPlan->taskId );
    REQUIRE( dTask.parentTaskIds.size() == 2 );
    REQUIRE( dTask.parentTaskIds.contains( bPlan->taskId ) );
    REQUIRE( dTask.parentTaskIds.contains( cPlan->taskId ) );
    // Reusable parent's output flowed into both resubmitted children.
    REQUIRE( executedParams( snapshot, "b" )["input"].toString() == aPath );
    REQUIRE( executedParams( snapshot, "c" )["input"].toString() == aPath );
}

TEST_CASE( "resume handles multiple completed parents and a disconnected "
           "resubmitted chain in one scrambled definition (#727)",
           "[workflow][coordinator][recovery][dag][disconnected]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_discon";

    // Two INDEPENDENT chains in one definition, scrambled declaration:
    //   chain 1: a1, a2 → d      (both parents completed pre-crash)
    //   chain 2: s → q           (declared [s, q]: child before parent,
    //                             both resubmitted)
    //   plus a completed standalone step with no dependents (reusable tail).
    const QString a1Path = fx.checkpointDir.path() + QStringLiteral("/%1_a1.tif").arg( QString::fromStdString( prefix ) );
    const QString a2Path = fx.checkpointDir.path() + QStringLiteral("/%1_a2.tif").arg( QString::fromStdString( prefix ) );
    const QString tailPath = fx.checkpointDir.path() + QStringLiteral("/%1_tail.tif").arg( QString::fromStdString( prefix ) );
    touchFile( a1Path );
    touchFile( a2Path );
    touchFile( tailPath );

    WorkflowDefinition def;
    def.id = prefix + "_def";

    StepDef d;
    d.id = "d";
    d.kind = StepKind::Operator;
    d.operatorId = prefix + ":d";
    d.params["in1"] = "$a1.output";
    d.params["in2"] = "$a2.output";
    d.params["output"] = "/tmp/" + prefix + "_d.tif";
    d.inputs.push_back( StepConnection{ "a1", "output", "input" } );
    d.inputs.push_back( StepConnection{ "a2", "output", "input" } );

    StepDef s; // disconnected chain, child declared before parent
    s.id = "s";
    s.kind = StepKind::Operator;
    s.operatorId = prefix + ":s";
    s.params["input"] = "$q.output";
    s.params["output"] = "/tmp/" + prefix + "_s.tif";
    s.inputs.push_back( StepConnection{ "q", "output", "input" } );

    StepDef q;
    q.id = "q";
    q.kind = StepKind::Operator;
    q.operatorId = prefix + ":q";
    q.params["output"] = "/tmp/" + prefix + "_q.tif";

    StepDef a1;
    a1.id = "a1";
    a1.kind = StepKind::Operator;
    a1.operatorId = prefix + ":a1";
    a1.params["output"] = a1Path.toStdString();

    StepDef a2;
    a2.id = "a2";
    a2.kind = StepKind::Operator;
    a2.operatorId = prefix + ":a2";
    a2.params["output"] = a2Path.toStdString();

    StepDef tail;
    tail.id = "tail";
    tail.kind = StepKind::Operator;
    tail.operatorId = prefix + ":tail";
    tail.params["output"] = tailPath.toStdString();

    def.steps = { s, d, a1, q, tail, a2 }; // fully scrambled

    Json::Value a1Payload( Json::objectValue );
    a1Payload["output"] = a1Path.toStdString();
    Json::Value a2Payload( Json::objectValue );
    a2Payload["output"] = a2Path.toStdString();
    Json::Value tailPayload( Json::objectValue );
    tailPayload["output"] = tailPath.toStdString();

    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.setStepPlans( { makePlan( "s", "Pending" ),
                        makePlan( "d", "Pending" ),
                        makePlan( "a1", "Completed", a1Path.toStdString(), a1Payload,
                                  prefix + ":a1" ),
                        makePlan( "q", "Pending" ),
                        makePlan( "tail", "Completed", tailPath.toStdString(), tailPayload,
                                  prefix + ":tail" ),
                        makePlan( "a2", "Completed", a2Path.toStdString(), a2Payload,
                                  prefix + ":a2" ) } );
    saveInterruptedCheckpoint( fx, run );

    std::atomic_bool ranD{ false }, ranS{ false }, ranQ{ false }, ranA1{ false },
        ranA2{ false }, ranTail{ false };
    Json::Value ports( Json::objectValue );
    ports["output"] = "unused.tif";
    registerCapturingExecutor( prefix + ":d", &ranD, ports );
    registerCapturingExecutor( prefix + ":s", &ranS, ports );
    registerCapturingExecutor( prefix + ":q", &ranQ, ports );
    registerCapturingExecutor( prefix + ":a1", &ranA1, ports );
    registerCapturingExecutor( prefix + ":a2", &ranA2, ports );
    registerCapturingExecutor( prefix + ":tail", &ranTail, ports );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( prefix + "_run", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( fx.coordinator, pipelineId );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( ranD.load() );
    REQUIRE( ranS.load() );
    REQUIRE( ranQ.load() );
    REQUIRE_FALSE( ranA1.load() );
    REQUIRE_FALSE( ranA2.load() );
    REQUIRE_FALSE( ranTail.load() );

    // Multiple completed parents: both substituted into d, no live edges.
    const QVariantMap dParams = executedParams( snapshot, "d" );
    REQUIRE( dParams["in1"].toString() == a1Path );
    REQUIRE( dParams["in2"].toString() == a2Path );
    const auto dPlan = snapshot->stepPlan( "d" );
    REQUIRE( dPlan.has_value() );
    REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( dPlan->taskId ).parentTaskIds.isEmpty() );

    // Disconnected legal chain survives: q (resubmitted parent, declared
    // AFTER its child s) stays wired.
    const auto sPlan = snapshot->stepPlan( "s" );
    const auto qPlan = snapshot->stepPlan( "q" );
    REQUIRE( sPlan.has_value() );
    REQUIRE( qPlan.has_value() );
    const auto sTask = sicnu::TaskCenter::instance().getTaskInfo( sPlan->taskId );
    REQUIRE( sTask.parentTaskIds.contains( qPlan->taskId ) );
}

TEST_CASE( "resume is refused while another owner holds the run lock (#727)",
           "[workflow][coordinator][recovery][ownership]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_own";

    WorkflowDefinition def;
    def.id = prefix + "_def";
    StepDef only;
    only.id = "only";
    only.kind = StepKind::Operator;
    only.operatorId = prefix + ":only";
    only.params["output"] = "/tmp/" + prefix + "_out.tif";
    def.steps.push_back( only );

    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.setStepPlans( { makePlan( "only", "Pending" ) } );
    saveInterruptedCheckpoint( fx, run );

    // A foreign live owner holds the lock (as an executing process would).
    WorkflowRunLock ownerLock(
        WorkflowRunLock::lockPathForRun( fx.checkpointDir.path(), prefix + "_run" ) );
    REQUIRE( ownerLock.tryAcquire() == WorkflowRunLock::TryResult::Acquired );

    std::atomic_bool ran{ false };
    registerCapturingExecutor( prefix + ":only", &ran );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( prefix + "_run", &err );
    REQUIRE( pipelineId < 0 );
    REQUIRE( err.contains( QStringLiteral( "owned by a live process" ) ) );
    REQUIRE_FALSE( ran.load() );

    // The owner releases (here: explicitly; in production: process death) —
    // the run becomes resumable again.
    ownerLock.release();
    const long pipelineId2 = fx.coordinator.resumeRun( prefix + "_run", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId2 > 0 );
    const auto snapshot = runToTerminal( fx.coordinator, pipelineId2 );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( ran.load() );
}

TEST_CASE( "resumeRun reconciles a still-Running checkpoint inline when it holds "
           "the lock (MCP surface has no recovery pre-pass) (#727)",
           "[workflow][coordinator][recovery][ownership]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_inline";

    WorkflowDefinition def;
    def.id = prefix + "_def";
    StepDef only;
    only.id = "only";
    only.kind = StepKind::Operator;
    only.operatorId = prefix + ":only";
    only.params["output"] = "/tmp/" + prefix + "_out.tif";
    def.steps.push_back( only );

    // A crashed process left the checkpoint RUNNING (not yet reconciled):
    // exactly what the MCP resume_workflow tool sees — it calls resumeRun
    // directly, without a recoverAtStartup pre-pass.
    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.forceSetState( WorkflowRunState::Running );
    StepPlan stuck = makePlan( "only", "Running", {}, Json::Value(), prefix + ":only" );
    run.setStepPlans( { stuck } );
    WorkflowCheckpointManager checkpoints;
    REQUIRE( false == checkpoints.saveCheckpoint( run, fx.checkpointDir.path() ).isEmpty() );

    std::atomic_bool ran{ false };
    registerCapturingExecutor( prefix + ":only", &ran );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( prefix + "_run", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );
    const auto snapshot = runToTerminal( fx.coordinator, pipelineId );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE( ran.load() );
}

TEST_CASE( "resume substitutes a step with mixed completed and live parents "
           "(static at resume + dispatch-time via TaskCenter) (#727)",
           "[workflow][coordinator][recovery][ports][dag][mixed]" )
{
    CoordinatorFixture fx;
    const std::string prefix = "coord_mixed";

    // combo (declared FIRST) references TWO parents: "done" completed
    // pre-crash (substituted statically at resume from its checkpoint
    // payload) and "live" resubmitted (substituted at dispatch from the
    // live task's payload once it completed). Nested + list shapes ride
    // along so both substitution paths are exercised inside containers.
    const QString donePath = fx.checkpointDir.path()
                             + QStringLiteral("/%1_done.tif").arg( QString::fromStdString( prefix ) );
    touchFile( donePath );
    const std::string livePath = "/tmp/" + prefix + "_live.tif";
    const std::string comboPath = "/tmp/" + prefix + "_combo.tif";

    WorkflowDefinition def;
    def.id = prefix + "_def";

    StepDef combo;
    combo.id = "combo";
    combo.kind = StepKind::Operator;
    combo.operatorId = prefix + ":combo";
    combo.params["inDone"] = "$done.output";
    combo.params["inLive"] = "$live.output";
    Json::Value nested( Json::Value( Json::objectValue ) );
    nested["fromDone"] = "$done.output";
    nested["fromLive"] = "$live.output";
    combo.params["nested"] = nested;
    Json::Value list( Json::Value( Json::arrayValue ) );
    list.append( "$done.output" );
    list.append( "$live.output" );
    combo.params["list"] = list;
    combo.params["output"] = comboPath;
    combo.inputs.push_back( StepConnection{ "done", "output", "input" } );
    combo.inputs.push_back( StepConnection{ "live", "output", "input" } );

    StepDef done;
    done.id = "done";
    done.kind = StepKind::Operator;
    done.operatorId = prefix + ":done";
    done.params["output"] = donePath.toStdString();

    StepDef live;
    live.id = "live";
    live.kind = StepKind::Operator;
    live.operatorId = prefix + ":live";
    live.params["output"] = livePath;

    def.steps.push_back( combo ); // declared before BOTH parents
    def.steps.push_back( done );
    def.steps.push_back( live );

    Json::Value donePayload( Json::Value( Json::objectValue ) );
    donePayload["output"] = donePath.toStdString();
    WorkflowRun run;
    run.setDefinition( def );
    REQUIRE( run.setRunId( prefix + "_run" ) );
    run.setStepPlans( { makePlan( "combo", "Pending" ),
                        makePlan( "done", "Completed", donePath.toStdString(), donePayload,
                                  prefix + ":done" ),
                        makePlan( "live", "Pending" ) } );
    saveInterruptedCheckpoint( fx, run );

    std::atomic_bool doneRan{ false }, liveRan{ false }, comboRan{ false };
    // The live parent's executor provides a real "output" port payload so
    // the dispatch-time substitution resolves combo's $live.output.
    Json::Value livePorts( Json::Value( Json::objectValue ) );
    livePorts["output"] = livePath;
    registerCapturingExecutor( prefix + ":done", &doneRan, donePayload );
    registerCapturingExecutor( prefix + ":live", &liveRan, livePorts );
    registerCapturingExecutor( prefix + ":combo", &comboRan );

    QString err;
    const long pipelineId = fx.coordinator.resumeRun( prefix + "_run", &err );
    INFO( err.toStdString() );
    REQUIRE( pipelineId > 0 );

    const auto snapshot = runToTerminal( fx.coordinator, pipelineId );
    REQUIRE( snapshot->state() == WorkflowRunState::Completed );
    REQUIRE_FALSE( doneRan.load() ); // checkpoint-served, never re-executed
    REQUIRE( liveRan.load() );
    REQUIRE( comboRan.load() );

    // Only the LIVE parent is wired as a task parent: the completed one was
    // consumed by the static substitution.
    const auto comboPlan = snapshot->stepPlan( "combo" );
    const auto livePlan = snapshot->stepPlan( "live" );
    REQUIRE( comboPlan.has_value() );
    REQUIRE( livePlan.has_value() );
    const auto comboTask = sicnu::TaskCenter::instance().getTaskInfo( comboPlan->taskId );
    REQUIRE( comboTask.parentTaskIds.size() == 1 );
    REQUIRE( comboTask.parentTaskIds.contains( livePlan->taskId ) );

    const QVariantMap params = executedParams( snapshot, "combo" );
    // Completed parent: substituted statically at resume time.
    REQUIRE( params["inDone"].toString() == donePath );
    // Live parent: substituted at dispatch from the live task's payload.
    REQUIRE( params["inLive"].toString() == livePath );
    // Both substitution paths work inside nested containers too.
    const QVariantMap nestedMap = params["nested"].toMap();
    REQUIRE( nestedMap["fromDone"].toString() == donePath );
    REQUIRE( nestedMap["fromLive"].toString() == livePath );
    const QVariantList listOut = params["list"].toList();
    REQUIRE( listOut.size() == 2 );
    REQUIRE( listOut[0].toString() == donePath );
    REQUIRE( listOut[1].toString() == livePath );

    // The persisted plan carries the fully substituted set (raw $refs gone).
    REQUIRE( comboPlan->resolvedParams["inDone"].asString() == donePath.toStdString() );
    REQUIRE( comboPlan->resolvedParams["inLive"].asString() == livePath );
    REQUIRE( comboPlan->resolvedParams["nested"]["fromLive"].asString() == livePath );
    REQUIRE( comboPlan->resolvedParams["list"][1].asString() == livePath );
}
