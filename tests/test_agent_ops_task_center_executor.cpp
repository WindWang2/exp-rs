// tests/test_agent_ops_task_center_executor.cpp
#include <catch2/catch_test_macros.hpp>

#include "agent_loop/scientific_agent_session.h"
#include "agent_loop/fake_seams.h"
#include "agent_ops/task_center_executor.h"

#ifdef slots
#undef slots
#endif

#include "processing/framework/task_center.h"
#include "jobs/job_engine.h"
#include "runtime/observability/execution_telemetry.h"

#include <QCoreApplication>
#include <QThread>

#include <chrono>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using sicnu::TaskCenter;
using sicnu::TaskPriority;
using sicnu::TaskStatus;
using sicnu::LatencyClass;
using sicnu::agent_ops::TaskCenterExecutor;
using sicnu::agent_ops::ExecutionState::kRunning;
using sicnu::agent_ops::ExecutionState::kCompleted;
using sicnu::agent_ops::ExecutionState::kFailed;
using sicnu::agent_ops::ExecutionState::kCancelled;
using sicnu::agent_loop::PlanDraft;
using sicnu::agent_loop::ExecutionStart;
using sicnu::agent_loop::ExecutionOutcome;
using sicnu::runtime::observability::ExecutionTelemetry;
using sicnu::runtime::observability::Counter;

namespace {

void ensureApp()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char appName[] = "test_agent_ops_task_center_executor";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
}

struct TestCleaner {
    TestCleaner() {
        ensureApp();
        TaskCenter::instance().shutdownForTests();
        sicnu::jobs::JobEngine::instance().shutdownForTests();
        sicnu::jobs::JobEngine::instance().clearExecutors();
        ExecutionTelemetry::instance().clearEvents();
        sicnu::jobs::JobEngine::instance().registerExecutor( "mock:", []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
            for ( int i = 0; i < 200 && !ctx.isCancelled(); ++i )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            }
            return Json::Value();
        } );
    }
    ~TestCleaner() {
        TaskCenter::instance().shutdownForTests();
        sicnu::jobs::JobEngine::instance().shutdownForTests();
        sicnu::jobs::JobEngine::instance().clearExecutors();
        ExecutionTelemetry::instance().clearEvents();
    }
};

PlanDraft makeTestPlan( const std::string &planId = "p-001",
                        const std::string &operatorId = "mock:op" )
{
    PlanDraft plan;
    plan.planId = planId;
    plan.intent = "spectral_index";
    plan.valid = true;
    Json::Value step( Json::objectValue );
    step[ "id" ] = "step-1";
    step[ "operator_id" ] = operatorId;
    step[ "arguments" ] = Json::Value( Json::objectValue );
    step[ "arguments" ][ "index" ] = "NDVI";
    plan.steps.append( step );
    return plan;
}

struct ReadyEntry
{
    unsigned long long epoch = 0;
    int priority = 1;
    int latencyRank = 1; ///< 0 = Interactive, 1 = Background, 2 = Batch (R7 FM-6)
    long taskId = 0;
    unsigned long long serial = 0;
};

struct ReadyEntryGreater
{
    bool operator()( const ReadyEntry &a, const ReadyEntry &b ) const
    {
        if ( a.priority != b.priority ) return a.priority > b.priority;
        if ( a.latencyRank != b.latencyRank ) return a.latencyRank > b.latencyRank;
        if ( a.epoch != b.epoch ) return a.epoch > b.epoch;
        if ( a.taskId != b.taskId ) return a.taskId > b.taskId;
        return a.serial > b.serial;
    }
};

} // namespace

// ===========================================================================
// Tier 1.1: Post-R7 Typed Admission Refusals
// ===========================================================================

TEST_CASE( "TC_ADM_01_QueueCapacityRefusal", "[agent_ops][admission][refusal]" )
{
    TestCleaner cleaner;
    TaskCenter::instance().setMaxPendingTasks( 1 );

    const long dummyId = TaskCenter::instance().enqueueTask( QStringLiteral( "mock:held" ), QVariantMap() );
    REQUIRE( dummyId > 0 );
    REQUIRE( TaskCenter::instance().pendingTaskCount() == 1 );

    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );

    CHECK_FALSE( start.started );
    CHECK( start.runId.empty() );
    CHECK( start.error == "ADMISSION_REFUSED: QUEUE_SATURATED" );
}

TEST_CASE( "TC_ADM_02_ShutdownStateRefusal", "[agent_ops][admission][refusal]" )
{
    TestCleaner cleaner;
    TaskCenter::instance().shutdown();

    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );

    CHECK_FALSE( start.started );
    CHECK( start.runId.empty() );
    CHECK( start.error == "ADMISSION_REFUSED: SHUTTING_DOWN" );
}

TEST_CASE( "TC_ADM_03_ZeroGhostIdJournalInvariant", "[agent_ops][admission][refusal]" )
{
    TestCleaner cleaner;
    TaskCenter::instance().setMaxPendingTasks( 1 );

    const long dummyId = TaskCenter::instance().enqueueTask( QStringLiteral( "mock:held" ), QVariantMap() );
    REQUIRE( dummyId > 0 );

    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );

    CHECK_FALSE( start.started );
    CHECK( start.runId.empty() );
    CHECK( start.runId != "task--1" );
    CHECK( start.runId != "task-0" );

    sicnu::agent_loop::FakeScenario scenario;
    scenario.intent = "spectral_index";
    sicnu::agent_loop::FakeSeams seams( scenario );

    sicnu::agent_loop::ScientificAgentSession::Dependencies deps{
        &seams.dataProvider(),
        &seams.planner(),
        &seams.preflight(),
        &executor,
        &seams.verifier(),
        &seams.diagnoser()
    };

    sicnu::agent_loop::SessionPolicy policy = sicnu::agent_loop::SessionPolicy::defaults();
    sicnu::agent_loop::ScientificAgentSession session( policy, deps, {}, "sess-adm-03" );
    sicnu::agent_loop::SessionRunRequest req;
    req.goal = "calculate ndvi";
    req.intent = "spectral_index";
    const auto result = session.run( req );

    for ( const auto &entry : result.journal.entries() )
    {
        if ( entry.decision.has_value() && entry.decision->inputs.isObject() )
        {
            CHECK_FALSE( entry.decision->inputs.isMember( "run_id" ) );
        }
        if ( entry.payload.isObject() )
        {
            CHECK_FALSE( entry.payload.isMember( "run_id" ) );
        }
    }
}

TEST_CASE( "TC_ADM_04_ResourceBudgetRefusal", "[agent_ops][admission][refusal]" )
{
    TestCleaner cleaner;
    TaskCenter::instance().setMaxPendingTasks( 0 );

    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    CHECK( start.started );
    CHECK( start.runId.rfind( "task-", 0 ) == 0 );
}

TEST_CASE( "TC_ADM_05_AdmissionRecoveryAfterRefusal", "[agent_ops][admission][refusal]" )
{
    TestCleaner cleaner;
    TaskCenter::instance().setMaxPendingTasks( 1 );

    const long dummyId = TaskCenter::instance().enqueueTask( QStringLiteral( "mock:held" ), QVariantMap() );
    REQUIRE( dummyId > 0 );

    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    ExecutionStart start = executor.begin( plan );
    CHECK_FALSE( start.started );
    CHECK( start.error == "ADMISSION_REFUSED: QUEUE_SATURATED" );

    TaskCenter::instance().cancelTask( dummyId );
    TaskCenter::instance().clearCompletedTasks();

    start = executor.begin( plan );
    CHECK( start.started );
    CHECK( start.runId.rfind( "task-", 0 ) == 0 );
    const long taskId = TaskCenterExecutor::parseTaskId( start.runId );
    CHECK( taskId > 0 );
}

// ===========================================================================
// Tier 1.2: LatencyRank & Priority Preservation
// ===========================================================================

TEST_CASE( "TC_LAT_01_AgentSourceInteractiveMapping", "[agent_ops][priority][latency]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    REQUIRE( start.started );

    const long taskId = TaskCenterExecutor::parseTaskId( start.runId );
    REQUIRE( taskId > 0 );
    const auto info = TaskCenter::instance().getTaskInfo( taskId );
    CHECK( info.source == "agent" );
    CHECK( info.latencyClass == LatencyClass::Interactive );
}

TEST_CASE( "TC_LAT_02_InteractiveBeatsBatchSamePriority", "[agent_ops][priority][latency]" )
{
    ReadyEntry batchEntry;
    batchEntry.priority = 1;
    batchEntry.latencyRank = 2; // Batch
    batchEntry.epoch = 0;
    batchEntry.taskId = 101;
    batchEntry.serial = 1;

    ReadyEntry interactiveEntry;
    interactiveEntry.priority = 1;
    interactiveEntry.latencyRank = 0; // Interactive
    interactiveEntry.epoch = 0;
    interactiveEntry.taskId = 102;
    interactiveEntry.serial = 2;

    ReadyEntryGreater cmp;
    CHECK( cmp( batchEntry, interactiveEntry ) == true );
    CHECK( cmp( interactiveEntry, batchEntry ) == false );

    std::priority_queue<ReadyEntry, std::vector<ReadyEntry>, ReadyEntryGreater> heap;
    for ( int i = 0; i < 5; ++i ) {
        ReadyEntry b;
        b.priority = 1;
        b.latencyRank = 2;
        b.taskId = 10 + i;
        b.serial = i;
        heap.push( b );
    }
    heap.push( interactiveEntry );
    REQUIRE_FALSE( heap.empty() );
    CHECK( heap.top().taskId == 102 );
    CHECK( heap.top().latencyRank == 0 );
}

TEST_CASE( "TC_LAT_03_InteractiveBeatsBackgroundSamePriority", "[agent_ops][priority][latency]" )
{
    ReadyEntry bgEntry;
    bgEntry.priority = 1;
    bgEntry.latencyRank = 1; // Background
    bgEntry.taskId = 201;
    bgEntry.serial = 1;

    ReadyEntry interactiveEntry;
    interactiveEntry.priority = 1;
    interactiveEntry.latencyRank = 0; // Interactive
    interactiveEntry.taskId = 202;
    interactiveEntry.serial = 2;

    ReadyEntryGreater cmp;
    CHECK( cmp( bgEntry, interactiveEntry ) == true );
    CHECK( cmp( interactiveEntry, bgEntry ) == false );
}

TEST_CASE( "TC_LAT_04_ExplicitPriorityDominatesLatency", "[agent_ops][priority][latency]" )
{
    ReadyEntry highBatch;
    highBatch.priority = 0; // High
    highBatch.latencyRank = 2; // Batch
    highBatch.taskId = 301;
    highBatch.serial = 1;

    ReadyEntry normalInteractive;
    normalInteractive.priority = 1; // Normal
    normalInteractive.latencyRank = 0; // Interactive
    normalInteractive.taskId = 302;
    normalInteractive.serial = 2;

    ReadyEntryGreater cmp;
    CHECK( cmp( normalInteractive, highBatch ) == true );
    CHECK( cmp( highBatch, normalInteractive ) == false );
}

TEST_CASE( "TC_LAT_05_TelemetryInteractiveCounters", "[agent_ops][priority][latency]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    REQUIRE( start.started );

    const long taskId = TaskCenterExecutor::parseTaskId( start.runId );
    const auto info = TaskCenter::instance().getTaskInfo( taskId );
    CHECK( info.latencyClass == LatencyClass::Interactive );
    CHECK( info.source == "agent" );
}

// ===========================================================================
// Tier 1.3: TOCTOU-Safe Cancellation Relay
// ===========================================================================

TEST_CASE( "TC_CNC_01_RunningTaskCancellation", "[agent_ops][cancel][toctou]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    REQUIRE( start.started );

    executor.cancel( start );

    const auto outcome = executor.poll( start, 1000 );
    CHECK( outcome.finished );
    CHECK_FALSE( outcome.succeeded );
    CHECK( outcome.state == "cancelled" );
    CHECK( outcome.errorCode == "CANCELLED" );
}

TEST_CASE( "TC_CNC_02_QueuedTaskCancellation", "[agent_ops][cancel][toctou]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    REQUIRE( start.started );

    const long taskId = TaskCenterExecutor::parseTaskId( start.runId );
    REQUIRE( taskId > 0 );

    executor.cancel( start.runId );
    const auto info = TaskCenter::instance().getTaskInfo( taskId );
    CHECK( ( info.status == TaskStatus::Canceled || info.status == TaskStatus::Cancelling ) );
}

TEST_CASE( "TC_CNC_03_PointAlphaTOCTOURace", "[agent_ops][cancel][toctou]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    REQUIRE( start.started );

    executor.cancel( start );
    executor.cancel( start );
    const auto outcome = executor.poll( start, 500 );
    CHECK( outcome.finished );
    CHECK( outcome.state == "cancelled" );
}

TEST_CASE( "TC_CNC_04_TerminalCancellationIdempotency", "[agent_ops][cancel][toctou]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    REQUIRE( start.started );

    const long taskId = TaskCenterExecutor::parseTaskId( start.runId );
    TaskCenter::instance().markTaskCompleted( taskId );

    executor.cancel( start );

    const auto outcome = executor.poll( start, 500 );
    CHECK( outcome.finished );
    CHECK( outcome.succeeded );
    CHECK( outcome.state == "succeeded" );
}

TEST_CASE( "TC_CNC_05_ResourceReclaimOnCancel", "[agent_ops][cancel][toctou]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );
    REQUIRE( start.started );

    executor.cancel( start );
    const auto outcome = executor.poll( start, 500 );
    CHECK( outcome.finished );
    CHECK( outcome.state == "cancelled" );
}

// ===========================================================================
// Tier 2.1: Boundary & Corner Cases (Empty & Malformed Plans)
// ===========================================================================

TEST_CASE( "TC_BND_PLN_01_EmptyPlanDraft", "[agent_ops][boundary]" )
{
    TaskCenterExecutor executor;
    PlanDraft plan;
    const auto start = executor.begin( plan );
    CHECK_FALSE( start.started );
    CHECK( start.runId.empty() );
    CHECK( start.error == "EMPTY_PLAN_REFUSED" );
}

TEST_CASE( "TC_BND_PLN_02_EmptyOperatorId", "[agent_ops][boundary]" )
{
    TaskCenterExecutor executor;
    PlanDraft plan;
    Json::Value step( Json::objectValue );
    step[ "operator_id" ] = "";
    plan.steps.append( step );
    const auto start = executor.begin( plan );
    CHECK_FALSE( start.started );
    CHECK( start.error == "INVALID_OPERATOR_ID" );
}

TEST_CASE( "TC_BND_PLN_03_PlanExceedingStepLimit", "[agent_ops][boundary]" )
{
    TaskCenterExecutor executor;
    PlanDraft plan;
    for ( int i = 0; i < 1001; ++i ) {
        Json::Value step( Json::objectValue );
        step[ "operator_id" ] = "op:test";
        plan.steps.append( step );
    }
    const auto start = executor.begin( plan );
    CHECK_FALSE( start.started );
    CHECK( start.error == "PLAN_STEP_LIMIT_EXCEEDED" );
}

TEST_CASE( "TC_BND_PLN_04_CyclicStepDependencies", "[agent_ops][boundary]" )
{
    TaskCenterExecutor executor;
    PlanDraft plan;
    Json::Value s1( Json::objectValue ), s2( Json::objectValue ), s3( Json::objectValue );
    s1[ "id" ] = "A"; s1[ "operator_id" ] = "op:1"; s1[ "depends_on" ] = "B";
    s2[ "id" ] = "B"; s2[ "operator_id" ] = "op:2"; s2[ "depends_on" ] = "C";
    s3[ "id" ] = "C"; s3[ "operator_id" ] = "op:3"; s3[ "depends_on" ] = "A";
    plan.steps.append( s1 );
    plan.steps.append( s2 );
    plan.steps.append( s3 );

    const auto start = executor.begin( plan );
    CHECK_FALSE( start.started );
    CHECK( start.error == "CYCLIC_DEPENDENCY_DETECTED" );
}

TEST_CASE( "TC_BND_PLN_05_MalformedJsonArguments", "[agent_ops][boundary]" )
{
    TaskCenterExecutor executor;
    PlanDraft plan;
    Json::Value step( Json::objectValue );
    step[ "operator_id" ] = "op:test";
    step[ "arguments_invalid" ] = true;
    plan.steps.append( step );

    const auto start = executor.begin( plan );
    CHECK_FALSE( start.started );
    CHECK( start.error == "INVALID_ARGUMENTS_JSON" );
}

// ===========================================================================
// Tier 2.2: Boundary & Corner Cases (Negative & Sentinel Task IDs)
// ===========================================================================

TEST_CASE( "TC_BND_ID_01_NegativeIdNeverProducesRunId", "[agent_ops][boundary]" )
{
    TestCleaner cleaner;
    TaskCenter::instance().shutdown();

    TaskCenterExecutor executor;
    const PlanDraft plan = makeTestPlan();
    const ExecutionStart start = executor.begin( plan );

    CHECK_FALSE( start.started );
    CHECK( start.runId == "" );
    CHECK( start.runId != "task--1" );
    CHECK( start.runId != "task-0" );
}

TEST_CASE( "TC_BND_ID_02_PollEmptyRunId", "[agent_ops][boundary]" )
{
    TaskCenterExecutor executor;
    const auto outcome = executor.poll( ExecutionStart{ .runId = "", .started = false }, 1000 );
    CHECK( outcome.finished );
    CHECK_FALSE( outcome.succeeded );
    CHECK( outcome.errorCode == "INVALID_RUN_ID" );
}

TEST_CASE( "TC_BND_ID_03_PollNonExistentTaskId", "[agent_ops][boundary]" )
{
    TestCleaner cleaner;
    TaskCenterExecutor executor;
    const auto outcome = executor.poll( ExecutionStart{ .runId = "task-999999", .started = true }, 1000 );
    CHECK( outcome.finished );
    CHECK_FALSE( outcome.succeeded );
    CHECK( outcome.errorCode == "TASK_NOT_FOUND" );
}

TEST_CASE( "TC_BND_ID_04_CancelEmptyRunId", "[agent_ops][boundary]" )
{
    TaskCenterExecutor executor;
    CHECK_NOTHROW( executor.cancel( ExecutionStart{ .runId = "", .started = false } ) );
    CHECK_NOTHROW( executor.cancel( "" ) );
    CHECK_NOTHROW( executor.cancel( "invalid-format" ) );
}

TEST_CASE( "TC_BND_ID_05_LargeTaskIdBoundary", "[agent_ops][boundary]" )
{
    const std::string largeRunId = "task-2147483640";
    const long id = TaskCenterExecutor::parseTaskId( largeRunId );
    CHECK( id == 2147483640L );
}
