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
