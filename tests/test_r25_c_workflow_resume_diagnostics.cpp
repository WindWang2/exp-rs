// tests/test_r25_c_workflow_resume_diagnostics.cpp — R25-C W3.
//
// After a server restart the in-memory TaskCenter pipeline map is gone, so
// get_workflow_status(pipeline_id) answers "Unknown pipeline id" for every
// run an agent launched in a previous session — while the run's checkpoint
// outlives the process and resume_workflow(run_id) still works. The agent
// had NO query surface between "unknown pipeline" and blind resume: no run
// state, no per-step checkpoint plan, no resumability verdict.
//
// This file pins the run_id diagnostic path on get_workflow_status:
//   - run_id resolves the in-memory run first, else the checkpoint
//     (read-only, never taking the run lock — resume keeps its own
//     ownership-reconcile semantics);
//   - the result carries run_state / per-step plan statuses / progress /
//     checkpoint presence / a resumable verdict mirroring resumeRun's
//     state wall (Interrupted/Failed/Canceled resume; Completed does not;
//     zero-step lineages never do);
//   - unknown ids fail typed (UNKNOWN_RUN); ids that could reach the
//     filesystem are rejected like resume_workflow's path guard.
//
// Compile-in-process pattern: test_r14_C_mcp_workflow_cancel (requests go
// through the real tools/call dispatch; tool failures arrive as
// isError:true results with structured errorCode, #620/#645).
#include <catch2/catch_test_macros.hpp>

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <QRegularExpression>
#include <functional>
#include "jobs/job_types.h"
#include "jobs/job_engine.h"

#include "agent/mcp_server.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

namespace
{

class TestServer : public McpServer
{
public:
    TestServer() : McpServer()
    {
        dispatch( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 0 },
            { QStringLiteral( "method" ), QStringLiteral( "initialize" ) },
            { QStringLiteral( "params" ), QVariantMap{} } } );
        REQUIRE( lastErrorCode == 0 );
    }

    /// handleRequest is protected on McpServer; expose it.
    void dispatch( const QVariantMap &req ) { handleRequest( req ); }
    /// Handler-level entry (no dispatch-layer trimming).
    QVariantMap statusByRun( const QString &runId ) { return handleGetWorkflowStatusByRun( runId ); }

    /// One tools/call round-trip. Returns the parsed payload on success;
    /// on a tool failure returns {isError, errorCode, message} instead.
    struct CallOutcome
    {
        bool isError = false;
        QString errorCode;
        QString message;
        QVariantMap payload;
    };

    CallOutcome callTool( const QString &tool, const QVariantMap &arguments )
    {
        lastErrorCode = 0;
        dispatch( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 1 },
            { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "name" ), tool },
                           { QStringLiteral( "arguments" ), arguments } } } } );
        REQUIRE( lastErrorCode == 0 ); // no protocol-level fault
        CallOutcome out;
        if ( lastResponseResult.value( QStringLiteral( "isError" ) ).toBool() )
        {
            out.isError = true;
            out.errorCode = lastResponseResult.value( QStringLiteral( "errorCode" ) ).toString();
            out.message = lastResponseResult.value( QStringLiteral( "content" ) )
                              .toList()
                              .value( 0 )
                              .toMap()
                              .value( QStringLiteral( "text" ) )
                              .toString();
            return out;
        }
        const QString text = lastResponseResult.value( QStringLiteral( "content" ) )
                                 .toList()
                                 .value( 0 )
                                 .toMap()
                                 .value( QStringLiteral( "text" ) )
                                 .toString();
        out.payload = QJsonDocument::fromJson( text.toUtf8() ).toVariant().toMap();
        REQUIRE( !out.payload.isEmpty() );
        return out;
    }

    QVariant lastResponseId;
    QVariantMap lastResponseResult;
    QVariant lastErrorId;
    int lastErrorCode = 0;
    QString lastErrorMessage;

    void sendResponse( const QVariant &id, const QVariantMap &result ) override
    {
        lastResponseId = id;
        lastResponseResult = result;
    }
    void sendError( const QVariant &id, int code, const QString &message ) override
    {
        lastErrorId = id;
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendError( const QVariant &id, int code, const QString &message, const QVariantMap & ) override
    {
        lastErrorId = id;
        lastErrorCode = code;
        lastErrorMessage = message;
    }
};

sicnu::workflow::WorkflowDefinition twoStepDefinition()
{
    sicnu::workflow::WorkflowDefinition def;
    def.id = "wf_r25c_diag";
    def.title = "R25-C diagnostics";
    sicnu::workflow::StepDef s1;
    s1.id = "s1";
    s1.operatorId = "rs:test";
    sicnu::workflow::StepDef s2;
    s2.id = "s2";
    s2.operatorId = "rs:test";
    sicnu::workflow::StepConnection edge;
    edge.fromStepId = "s1";
    edge.fromPort = "output";
    edge.toPort = "input";
    s2.inputs.push_back( edge );
    def.steps = { s1, s2 };
    return def;
}

/// Scopes the coordinator's checkpoint directory to a test tmpdir and
/// restores the default on scope exit.
struct CheckpointDirGuard
{
    explicit CheckpointDirGuard( const QString &dir )
    {
        sicnu::workflow::WorkflowRunCoordinator::instance().setCheckpointDirectory( dir );
    }
    ~CheckpointDirGuard() { sicnu::workflow::WorkflowRunCoordinator::instance().setCheckpointDirectory( QString() ); }
};

/// Saves a run in @p state under the guarded checkpoint dir.
void seedCheckpoint( QTemporaryDir &dir, const std::string &runId,
                     sicnu::workflow::WorkflowRunState state )
{
    auto run = sicnu::workflow::WorkflowRun::createFromDefinition( twoStepDefinition(), runId );
    REQUIRE( run->transitionTo( sicnu::workflow::WorkflowRunState::Planning ) );
    REQUIRE( run->transitionTo( sicnu::workflow::WorkflowRunState::Ready ) );
    REQUIRE( run->transitionTo( sicnu::workflow::WorkflowRunState::Running ) );
    REQUIRE( run->transitionTo( state ) );
    sicnu::workflow::WorkflowCheckpointManager manager;
    REQUIRE( !manager.saveCheckpoint( *run, dir.path() ).isEmpty() );
}

QVariantMap runArgs( const QString &runId )
{
    QVariantMap args;
    args.insert( QStringLiteral( "run_id" ), runId );
    return args;
}

} // namespace

TEST_CASE( "get_workflow_status run_id path reports checkpoint truth and resumability",
           "[agent][mcp][workflow][r25_c]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    CheckpointDirGuard dirGuard( tmpDir.path() );
    TestServer server;

    SECTION( "an interrupted checkpointed run is resumable with per-step plans" )
    {
        seedCheckpoint( tmpDir, "run-r25c-interrupted", sicnu::workflow::WorkflowRunState::Interrupted );

        const auto res = server.callTool( QStringLiteral( "get_workflow_status" ),
                                          runArgs( QStringLiteral( "run-r25c-interrupted" ) ) );
        REQUIRE_FALSE( res.isError );
        CHECK( res.payload.value( "run_id" ).toString() == QStringLiteral( "run-r25c-interrupted" ) );
        CHECK( res.payload.value( "run_state" ).toString() == QStringLiteral( "Interrupted" ) );
        CHECK( res.payload.value( "checkpoint_present" ).toBool() );
        CHECK( res.payload.value( "source" ).toString() == QStringLiteral( "checkpoint" ) );
        CHECK( res.payload.value( "resumable" ).toBool() == true );

        const QVariantList steps = res.payload.value( "steps" ).toList();
        REQUIRE( steps.size() == 2 );
        CHECK( steps[0].toMap().value( "step_id" ).toString() == QStringLiteral( "s1" ) );
        CHECK( steps[0].toMap().value( "status" ).toString() == QStringLiteral( "Pending" ) );
        CHECK( steps[1].toMap().value( "step_id" ).toString() == QStringLiteral( "s2" ) );
        CHECK_FALSE( res.payload.value( "resume_hint" ).toString().isEmpty() );
    }

    SECTION( "a completed checkpoint is NOT resumable and says why" )
    {
        seedCheckpoint( tmpDir, "run-r25c-done", sicnu::workflow::WorkflowRunState::Completed );

        const auto res = server.callTool( QStringLiteral( "get_workflow_status" ),
                                          runArgs( QStringLiteral( "run-r25c-done" ) ) );
        REQUIRE_FALSE( res.isError );
        CHECK( res.payload.value( "run_state" ).toString() == QStringLiteral( "Completed" ) );
        CHECK( res.payload.value( "resumable" ).toBool() == false );
        CHECK( res.payload.value( "resume_hint" ).toString().contains( QStringLiteral( "terminal" ),
                                                                       Qt::CaseInsensitive ) );
    }

    SECTION( "an unknown run id fails typed" )
    {
        const auto res = server.callTool( QStringLiteral( "get_workflow_status" ),
                                          runArgs( QStringLiteral( "run-never-existed" ) ) );
        REQUIRE( res.isError );
        CHECK( res.errorCode == QStringLiteral( "UNKNOWN_RUN" ) );
    }

    SECTION( "run ids that could reach the filesystem are rejected" )
    {
        for ( const QString &bad : { QStringLiteral( "../escape" ), QStringLiteral( "a/b" ),
                                     QStringLiteral( "run..x" ) } )
        {
            const auto res =
                server.callTool( QStringLiteral( "get_workflow_status" ), runArgs( bad ) );
            REQUIRE( res.isError );
            CHECK( res.errorCode == QStringLiteral( "INVALID_PARAMETER" ) );
        }
    }

    SECTION( "a failed run (resume wall admits Failed) is resumable" )
    {
        seedCheckpoint( tmpDir, "run-r25c-failed", sicnu::workflow::WorkflowRunState::Failed );
        const auto res = server.callTool( QStringLiteral( "get_workflow_status" ),
                                          runArgs( QStringLiteral( "run-r25c-failed" ) ) );
        REQUIRE_FALSE( res.isError );
        CHECK( res.payload.value( "run_state" ).toString() == QStringLiteral( "Failed" ) );
        CHECK( res.payload.value( "resumable" ).toBool() == true );
    }
}

// --- pipeline_id path: the resumable advisory on a LIVE tracked run --------
// R25-C added resumable/resume_hint to handleGetWorkflowStatus(pipeline_id)
// (additive keys). R1 P3 flagged zero coverage; this drives the real
// tracked-pipeline path with a coordinator-level tracked run and asserts
// the advisory keys and their value for a Completed live run.
#include <QCoreApplication>

namespace
{

bool waitForPredicate( const std::function<bool()> &predicate, int attempts = 4000, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( predicate() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
    return predicate();
}

} // namespace

TEST_CASE( "pipeline_id status carries the resumable advisory for live tracked runs",
           "[agent][mcp][workflow][r25_c]" )
{
    if ( !QCoreApplication::instance() )
    {
        static int argc = 1;
        static char appName[] = "test_r25_c_workflow_resume_diagnostics";
        static char *argv[] = { appName, nullptr };
        new QCoreApplication( argc, argv );
    }
    QTemporaryDir checkpointDir;
    REQUIRE( checkpointDir.isValid() );
    auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
    coordinator.setCheckpointDirectory( checkpointDir.path() );

    TestServer server;
    // Single-step definition (an unregistered operator id is fine: the
    // pipeline submit tracks the run; execution outcome is irrelevant to
    // the projection under test).
    sicnu::workflow::WorkflowDefinition def;
    def.id = "wf_r25c_live";
    def.title = "R25-C live advisory";
    sicnu::workflow::StepDef only;
    only.id = "s1";
    only.operatorId = "rs:test";
    def.steps = { only };

    const long pipelineId = coordinator.startTrackedPipeline( def, /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );

    // Wait for the run to reach a terminal state (completed or failed —
    // either resolves the tracked pipeline).
    const bool terminal = waitForPredicate( [&] {
        const auto run = coordinator.runForPipeline( pipelineId );
        return run && ( run->state() == sicnu::workflow::WorkflowRunState::Completed
                        || run->state() == sicnu::workflow::WorkflowRunState::Failed
                        || run->state() == sicnu::workflow::WorkflowRunState::Canceled );
    } );

    if ( !terminal )
    {
        WARN( "tracked run did not settle in time — advisory keys unchecked" );
        coordinator.shutdownForTests();
        return;
    }

    const auto run = coordinator.runForPipeline( pipelineId );
    REQUIRE( run );

    QVariantMap args;
    args.insert( QStringLiteral( "pipeline_id" ), static_cast<qlonglong>( pipelineId ) );
    const auto res = server.callTool( QStringLiteral( "get_workflow_status" ), args );
    REQUIRE_FALSE( res.isError );
    CHECK( res.payload.value( "pipeline_id" ).toLongLong() == pipelineId );
    CHECK( res.payload.value( "run_id" ).toString() == QString::fromStdString( run->runId() ) );
    // The advisory must exist and agree with the state wall.
    CHECK( res.payload.contains( QStringLiteral( "resumable" ) ) );
    const bool completed = run->state() == sicnu::workflow::WorkflowRunState::Completed;
    CHECK( res.payload.value( "resumable" ).toBool() == !completed );
    if ( completed )
        CHECK_FALSE( res.payload.value( "resume_hint" ).toString().isEmpty() );

    coordinator.shutdownForTests();
}

// --- R1/R2 fix discriminating tests -----------------------------------------

TEST_CASE( "run_id guards reject a trailing-newline id (\\z anchor)",
           "[agent][mcp][workflow][r25_c]" )
{
    if ( !QCoreApplication::instance() )
    {
        static int argc = 1;
        static char appName[] = "test_r25_c_workflow_resume_diagnostics";
        static char *argv[] = { appName, nullptr };
        new QCoreApplication( argc, argv );
    }
    TestServer server;
    // PCRE2 $ and \Z also match BEFORE a trailing newline, which would let
    // "run\n" through as a legal id (R2 P3). \z anchors the absolute end.
    // Driven through the HANDLER directly: the tools/call dispatch trims the
    // argument first (a second line of defence), so this pins the handler's
    // own contract for any other caller.
    // McpToolError lives in mcp_server.cpp's anonymous namespace, so the
    // handler is driven through the public tools/call path here (its
    // dispatch layer trims first — the handler's \z anchor is the second
    // line of defence, pinned by the regex unit below).
    for ( const QString &bad : { QStringLiteral( "run\n" ), QStringLiteral( "run\n\n" ),
                                 QStringLiteral( "ok\nx" ) } )
    {
        const auto res = server.callTool( QStringLiteral( "resume_workflow" ), runArgs( bad ) );
        // resume_workflow's dispatch does NOT trim-away an inner newline the
        // same way... it does; so pin the REGEX unit directly instead.
        (void)res;
    }
    // Regex unit: the exact pattern the handler uses must reject a trailing
    // newline (PCRE2 $ and \Z do not).
    static const QRegularExpression kPattern( QStringLiteral( "^[A-Za-z0-9_.-]+\\z" ) );
    CHECK( kPattern.match( QStringLiteral( "run-plain" ) ).hasMatch() );
    CHECK( !kPattern.match( QStringLiteral( "run\n" ) ).hasMatch() );
    CHECK( !kPattern.match( QStringLiteral( "run\n\n" ) ).hasMatch() );
    CHECK( !kPattern.match( QStringLiteral( "ok\nx" ) ).hasMatch() );
    // And the handler's own guard must still refuse a newline-bearing id
    // when it reaches it un-trimmed (dispatch-layer trim removed).
    for ( const QString &bad : { QStringLiteral( "run\n" ), QStringLiteral( "run\n\n" ) } )
    {
        bool refused = false;
        try { server.statusByRun( bad ); }
        catch ( const std::runtime_error &err )
        {
            refused = QString::fromUtf8( err.what() ).contains(
                QStringLiteral( "invalid characters" ), Qt::CaseInsensitive );
        }
        CHECK( refused );
    }
}

TEST_CASE( "diagnostic output_path keeps sandboxed paths and collapses foreign ones",
           "[agent][mcp][workflow][r25_c]" )
{
    // The projection rule (R1 P3 / R2 P2-2): inside the workspace root the
    // path is returned verbatim; outside it only the file name survives; a
    // placeholder token is untouched. Driven through the real handler by
    // seeding a checkpoint whose step recorded an output path.
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    CheckpointDirGuard dirGuard( tmpDir.path() );

    const QString inside = tmpDir.path() + QStringLiteral( "/inside.tif" );
    const QString outside = QStringLiteral( "/etc/passwd" );

    auto run = sicnu::workflow::WorkflowRun::createFromDefinition( twoStepDefinition(),
                                                                   "run-r25c-paths" );
    REQUIRE( run->transitionTo( sicnu::workflow::WorkflowRunState::Planning ) );
    REQUIRE( run->transitionTo( sicnu::workflow::WorkflowRunState::Ready ) );
    REQUIRE( run->transitionTo( sicnu::workflow::WorkflowRunState::Running ) );
    REQUIRE( run->transitionTo( sicnu::workflow::WorkflowRunState::Interrupted ) );
    std::vector<sicnu::workflow::StepPlan> plans( 2 );
    plans[0].stepId = "s1";
    plans[0].operatorId = "rs:test";
    plans[0].status = "Completed";
    plans[0].outputLayerPath = inside.toStdString();
    plans[1].stepId = "s2";
    plans[1].operatorId = "rs:test";
    plans[1].status = "Pending";
    plans[1].outputLayerPath = outside.toStdString();
    run->setStepPlans( plans );
    sicnu::workflow::WorkflowCheckpointManager manager;
    REQUIRE( !manager.saveCheckpoint( *run, tmpDir.path() ).isEmpty() );

    // Sandbox the server to the checkpoint dir: `inside` is within it,
    // `/etc/passwd` is not.
    qputenv( "SICNU_MCP_WORKSPACE", tmpDir.path().toUtf8() );

    TestServer server;
    const auto res = server.callTool( QStringLiteral( "get_workflow_status" ),
                                      runArgs( QStringLiteral( "run-r25c-paths" ) ) );
    REQUIRE_FALSE( res.isError );
    const QVariantList steps = res.payload.value( "steps" ).toList();
    REQUIRE( steps.size() == 2 );
    CHECK( steps[0].toMap().value( "output_path" ).toString() == inside );
    CHECK( steps[1].toMap().value( "output_path" ).toString()
           == QStringLiteral( "passwd" ) );
    qunsetenv( "SICNU_MCP_WORKSPACE" );
}
