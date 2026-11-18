// test_r33_t04_retry_identity.cpp — R33-T04 / WP02 (#2123):
//
//   TaskCenter::retryTask submitted the retried task FIRST and bound its
//   pipeline/step identity AFTER; submission flushes taskUpdated inside
//   enqueueTask/submitJobImpl, so a retried task that reaches a terminal
//   state before the binding emitted ONLY identity-less updates — which
//   WorkflowRunCoordinator::onTaskUpdated drops — and the tracked run then
//   never converged (stayed Failed while the pipeline task was Completed).
//   The binding also overwrote the pipeline step status with a stale Queued.
//
//   These cases pin the contract on a real TaskCenter + real
//   WorkflowRunCoordinator wiring:
//
//   * a retried tracked step converges the run to its actual outcome;
//   * the pipeline step status mirrors the retried task's real state;
//   * a task that fails again keeps the run Failed (no false convergence).
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run_coordinator.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace
{
bool pollUntil( const std::function<bool()> &pred, int timeoutMs )
{
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds( timeoutMs );
    while ( std::chrono::steady_clock::now() < deadline )
    {
        if ( pred() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return pred();
}

void ensureQCoreApplication()
{
    if ( !QCoreApplication::instance() )
    {
        static int argc = 1;
        static char appName[] = "test_r33_t04_retry_identity";
        static char *argv[] = { appName, nullptr };
        new QCoreApplication( argc, argv );
    }
}

sicnu::workflow::WorkflowDefinition singleStepDefinition( const std::string &operatorId )
{
    sicnu::workflow::WorkflowDefinition def;
    def.id = "wf_r33t04_retry";
    def.title = "R33-T04 retry identity";
    sicnu::workflow::StepDef only;
    only.id = "s1";
    only.title = "flaky";
    only.kind = sicnu::workflow::StepKind::Operator;
    only.operatorId = operatorId;
    def.steps = { only };
    return def;
}
} // namespace

TEST_CASE( "#2123 retry of a tracked failed step converges the run and mirrors real step status",
           "[processing][task_center][retry][r33t04]" )
{
    ensureQCoreApplication();
    QTemporaryDir checkpointDir;
    REQUIRE( checkpointDir.isValid() );
    auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
    coordinator.setCheckpointDirectory( checkpointDir.path() );
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers( 4 );
    auto &center = sicnu::TaskCenter::instance();

    // Fails on the first execution, completes instantly on every retry —
    // the retried task routinely reaches Completed BEFORE retryTask's
    // identity binding runs (the exact #2123 race).
    std::atomic<int> calls{ 0 };
    engine.registerExecutor( "r33t04:flaky",
                             [&calls]( const sicnu::jobs::JobRequest &,
                                       sicnu::operators::RSOperatorContext & ) -> Json::Value
    {
        if ( calls.fetch_add( 1 ) == 0 )
            throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::ComputationError, "first attempt fails" );
        Json::Value r( Json::objectValue );
        r["output"] = "/tmp/r33t04_retry_identity_ok.tif";
        return r;
    } );

    const long pipelineId = coordinator.startTrackedPipeline( singleStepDefinition( "r33t04:flaky" ),
                                                              /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    REQUIRE( pollUntil( [&]
                        {
                            const auto run = coordinator.runForPipeline( pipelineId );
                            return run
                                   && run->state() == sicnu::workflow::WorkflowRunState::Failed;
                        },
                        15000 ) );

    auto pipe = center.getPipelineInfo( pipelineId );
    const long failedTask = pipe.stepToTaskId.value( "s1" );
    REQUIRE( failedTask > 0 );
    REQUIRE( pollUntil( [&]
                        {
                            return center.getTaskInfo( failedTask ).status
                                   == sicnu::TaskStatus::Failed;
                        },
                        5000 ) );

    const long retried = center.retryTask( failedTask );
    REQUIRE( retried > 0 );
    REQUIRE( retried != failedTask );

    // The retried task itself completes (fast executor).
    REQUIRE( pollUntil( [&]
                        {
                            return center.getTaskInfo( retried ).status
                                   == sicnu::TaskStatus::Completed;
                        },
                        10000 ) );

    // The tracked run must converge to the retried outcome: previously the
    // only Completed update was identity-less (dropped by onTaskUpdated)
    // and the run stayed Failed forever.
    REQUIRE( pollUntil( [&]
                        {
                            const auto run = coordinator.runForPipeline( pipelineId );
                            return run
                                   && run->state() == sicnu::workflow::WorkflowRunState::Completed;
                        },
                        10000 ) );

    // The pipeline step status mirrors the retried task's ACTUAL state —
    // not the stale Queued the old post-submission binding wrote over a
    // fast completion.
    pipe = center.getPipelineInfo( pipelineId );
    CHECK( pipe.stepToTaskId.value( "s1" ) == retried );
    CHECK( pipe.stepStatuses.value( "s1" ) == sicnu::TaskStatus::Completed );
    CHECK( pipe.isCompleted );
    CHECK_FALSE( pipe.isFailed );

    engine.clearExecutors();
    coordinator.shutdownForTests();
}

TEST_CASE( "#2123 retry that fails again keeps the run Failed with no stale success",
           "[processing][task_center][retry][r33t04]" )
{
    ensureQCoreApplication();
    QTemporaryDir checkpointDir;
    REQUIRE( checkpointDir.isValid() );
    auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
    coordinator.setCheckpointDirectory( checkpointDir.path() );
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers( 4 );
    auto &center = sicnu::TaskCenter::instance();

    std::atomic<int> calls{ 0 };
    engine.registerExecutor( "r33t04:always-fails",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) -> Json::Value
    {
        throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::ComputationError, "always fails" );
    } );
    (void)calls;

    const long pipelineId =
        coordinator.startTrackedPipeline( singleStepDefinition( "r33t04:always-fails" ),
                                          /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    REQUIRE( pollUntil( [&]
                        {
                            const auto run = coordinator.runForPipeline( pipelineId );
                            return run
                                   && run->state() == sicnu::workflow::WorkflowRunState::Failed;
                        },
                        15000 ) );

    auto pipe = center.getPipelineInfo( pipelineId );
    const long failedTask = pipe.stepToTaskId.value( "s1" );
    const long retried = center.retryTask( failedTask );
    REQUIRE( retried > 0 );

    REQUIRE( pollUntil( [&]
                        {
                            return center.getTaskInfo( retried ).status
                                   == sicnu::TaskStatus::Failed;
                        },
                        10000 ) );
    // The retried failure keeps the run Failed — the fresh identity-bound
    // update must fold the real outcome, never a phantom success.
    REQUIRE( pollUntil( [&]
                        {
                            const auto run = coordinator.runForPipeline( pipelineId );
                            return run
                                   && run->state() == sicnu::workflow::WorkflowRunState::Failed;
                        },
                        10000 ) );
    pipe = center.getPipelineInfo( pipelineId );
    CHECK( pipe.stepStatuses.value( "s1" ) == sicnu::TaskStatus::Failed );
    CHECK( pipe.isFailed );

    engine.clearExecutors();
    coordinator.shutdownForTests();
}
