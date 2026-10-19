// tests/test_r14_C_mcp_workflow_cancel.cpp — R14-C FIX-1 regression.
//
// RED first: notifications/cancelled only consulted the rpc-id -> TASK map,
// which is populated exclusively for tools/call results carrying a top-level
// execution_id (execute_operator). run_workflow / resume_workflow return a
// pipeline_id instead, so a client abort (Pi bridge sends
// notifications/cancelled on AbortController) never stopped the dispatched
// pipeline: the run kept burning CPU for its whole budget while the client
// had already moved on (#R14-C WP02/WP09 cancel contract).
//
// The fixture drives the REAL protocol entry (McpServer::handleRequest) so the
// rpc-id bookkeeping path is exercised — calling handleRunWorkflow directly
// would skip the registration seam this regression pins.
#include <catch2/catch_test_macros.hpp>
#include <QJsonObject>
#include <QJsonDocument>
#include <QVariantMap>

#include <chrono>
#include <thread>

#include "agent/mcp_server.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <QTemporaryDir>
#include <QDir>
#include <QFile>

// The MCP sandbox is default-deny (unset SICNU_MCP_WORKSPACE = process CWD);
// this binary's fixtures live in QTemporaryDir, so opt into the widest root
// exactly like test_mcp_server does.
static const bool kFixtureWorkspaceInstalled = [] {
    if ( qEnvironmentVariableIsEmpty( "SICNU_MCP_WORKSPACE" ) )
        qputenv( "SICNU_MCP_WORKSPACE", QDir::rootPath().toUtf8() );
    return true;
}();

namespace {

/// Recording double (CountingServer pattern from test_surface_protocol_r4):
/// every send* is captured so tests can read tools/call result payloads that
/// rode content[0].text, while responses/errors/notification counts stay
/// observable for the protocol hygiene assertions.
class RecordingServer : public McpServer
{
public:
    struct Sent
    {
        QVariant id;
        QVariantMap result;
        bool isError = false;
        int errorCode = 0;
        QString errorMessage;
    };
    QList<Sent> responses;
    QList<Sent> errors;

    void request( const QVariantMap &req ) { handleRequest( req ); }

    void sendResponse( const QVariant &id, const QVariantMap &result ) override
    {
        if ( !id.isValid() )
            return;
        Sent s;
        s.id = id;
        s.result = result;
        s.isError = result.value( QStringLiteral( "isError" ) ).toBool();
        responses.append( s );
    }
    void sendError( const QVariant &id, int code, const QString &message ) override
    {
        sendError( id, code, message, QVariantMap() );
    }
    void sendError( const QVariant &id, int code, const QString &message,
                    const QVariantMap & ) override
    {
        if ( !id.isValid() )
            return;
        Sent s;
        s.id = id;
        s.errorCode = code;
        s.errorMessage = message;
        errors.append( s );
    }
    void sendNotification( const QString &, const QVariantMap & ) override {}

    void initialize( int id )
    {
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), id },
            { QStringLiteral( "method" ), QStringLiteral( "initialize" ) },
            { QStringLiteral( "params" ), QVariantMap{} } } );
    }

    void callTool( const QVariant &id, const QString &tool, const QVariantMap &arguments )
    {
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), id },
            { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "name" ), tool },
                           { QStringLiteral( "arguments" ), arguments } } } } );
    }

    void cancelNotification( const QVariant &requestId )
    {
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "method" ), QStringLiteral( "notifications/cancelled" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "requestId" ), requestId } } } } );
    }
};

QJsonObject payloadOf( const RecordingServer::Sent &sent )
{
    return QJsonDocument::fromJson(
               sent.result.value( QStringLiteral( "content" ) ).toList().value( 0 ).toMap()
                   .value( QStringLiteral( "text" ) ).toString().toUtf8() )
        .object();
}

/// Slice-sleeping operator honouring RSOperatorContext::isCancelled — the
/// surface_mcp_host_main pattern, under a track-private id.
class R14CSleepOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "rs:r14_c_sleep"; }
    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &ctx ) override
    {
        int slices = 600; // 60 s ceiling; every cancel path lands long before
        if ( params.isMember( "slices" ) && params["slices"].isInt() )
            slices = std::max( 1, std::min( 600, params["slices"].asInt() ) );
        for ( int i = 0; i < slices; ++i )
        {
            if ( ctx.isCancelled() )
            {
                Json::Value error( Json::objectValue );
                error["cancelled"] = true;
                return error;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            // Contract units: RSOperatorContext progress is a 0..1 fraction
            // (rs_operator_context.h), like every production operator.
            ctx.reportProgress( double( i + 1 ) / slices, "sleeping" );
        }
        Json::Value result( Json::objectValue );
        result["output"] = "/tmp/r14_c_sleep.tif";
        return result;
    }
};

void registerR14CFixtures()
{
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    registry.registerOperator( "rs:r14_c_sleep",
                               []() -> std::unique_ptr<sicnu::operators::RSOperator> {
                                   return std::make_unique<R14CSleepOperator>();
                               } );
    auto &adapters = sicnu::processing::AtomicAlgorithmRegistry::instance();
    if ( !adapters.findAdapter( "rs:r14_c_sleep" ) )
    {
        auto op = registry.create( "rs:r14_c_sleep" );
        if ( op )
            adapters.registerAdapter(
                std::make_shared<sicnu::processing::RsOperatorAdapter>( std::move( op ) ) );
    }
}

/// Session-scoped checkpoint family guard (CheckpointEnvGuard pattern from
/// test_mcp_server): run_workflow persists checkpoints + run locks, so they
/// must never land in the developer's real ~/.rs_studio/checkpoints.
struct CheckpointDirGuard
{
    QTemporaryDir dir;
    const QByteArray saved = qgetenv( "SICNU_CHECKPOINT_DIR" );
    const bool had = qEnvironmentVariableIsSet( "SICNU_CHECKPOINT_DIR" );
    CheckpointDirGuard()
    {
        REQUIRE( dir.isValid() );
        REQUIRE( qputenv( "SICNU_CHECKPOINT_DIR", dir.path().toUtf8() ) );
    }
    ~CheckpointDirGuard()
    {
        if ( had )
            qputenv( "SICNU_CHECKPOINT_DIR", saved );
        else
            qunsetenv( "SICNU_CHECKPOINT_DIR" );
    }
};

QString sleepPipelineJson( const char *workflowId )
{
    return QStringLiteral( R"({
        "id": "%1",
        "name": "r14_c cancel probe",
        "steps": [
            {"id": "s1", "title": "sleep", "operator": "rs:r14_c_sleep",
             "params": {"slices": 600}}
        ]
    })" )
        .arg( QLatin1String( workflowId ) );
}

/// Terminal run state via the coordinator (the same truth get_workflow_status
/// reports as run_state), polled with a deadline.
bool runReachedState( long pipelineId, sicnu::workflow::WorkflowRunState state,
                      std::chrono::milliseconds timeout )
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( std::chrono::steady_clock::now() < deadline )
    {
        if ( const auto run =
                 sicnu::workflow::WorkflowRunCoordinator::instance().runForPipeline( pipelineId ) )
        {
            if ( run->state() == state )
                return true;
            if ( sicnu::workflow::isTerminalRunState( run->state() ) )
                return false; // a different terminal won; stop waiting
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    return false;
}

} // namespace

TEST_CASE( "notifications/cancelled stops a run_workflow pipeline server-side (R14-C FIX-1)",
           "[agent][mcp][workflow][r14_C]" )
{
    registerR14CFixtures();
    CheckpointDirGuard checkpointGuard;
    RecordingServer server;
    server.initialize( 1 );
    REQUIRE( server.responses.size() == 1 );
    server.responses.clear();

    // Submit a slow pipeline through the real protocol entry. The response
    // must carry pipeline_id (the async submission contract).
    server.callTool( QVariant( 7 ), QStringLiteral( "run_workflow" ),
                     QVariantMap{ { QStringLiteral( "pipeline" ),
                                    sleepPipelineJson( "r14_c_cancel_probe" ) } } );
    REQUIRE( server.errors.isEmpty() );
    REQUIRE( server.responses.size() == 1 );
    REQUIRE( !server.responses.first().isError );
    const long pipelineId =
        payloadOf( server.responses.first() ).value( QStringLiteral( "pipeline_id" ) ).toInteger();
    REQUIRE( pipelineId >= 0 );

    // Give the pipeline a moment to confirm it is genuinely live, then cancel
    // the REQUEST (client abort semantics — the Pi bridge path).
    REQUIRE( runReachedState( pipelineId, sicnu::workflow::WorkflowRunState::Running,
                              std::chrono::milliseconds( 5000 ) ) );
    server.cancelNotification( QVariant( 7 ) );

    // The run must fold to Canceled — not run its 60 s sleep to completion.
    REQUIRE( runReachedState( pipelineId, sicnu::workflow::WorkflowRunState::Canceled,
                              std::chrono::milliseconds( 15000 ) ) );

    // Drain the coordinator's terminal persist before teardown (the pipeline
    // status flips before the fold's terminal checkpoint is durable).
    bool durableCanceled = false;
    for ( int attempt = 0; attempt < 1200 && !durableCanceled; ++attempt )
    {
        const QStringList families = { QString(), QStringLiteral( "history" ) };
        for ( const QString &family : families )
        {
            const QDir dir( checkpointGuard.dir.filePath( family ) );
            for ( const QString &entry :
                  dir.entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files ) )
            {
                QFile f( dir.filePath( entry ) );
                if ( !f.open( QIODevice::ReadOnly ) )
                    continue;
                const QJsonDocument doc = QJsonDocument::fromJson( f.readAll() );
                f.close();
                if ( doc.object().value( QStringLiteral( "workflowId" ) ).toString()
                         == QStringLiteral( "r14_c_cancel_probe" )
                     && doc.object().value( QStringLiteral( "state" ) ).toString()
                            == QStringLiteral( "Canceled" ) )
                    durableCanceled = true;
            }
        }
        if ( !durableCanceled )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    CHECK( durableCanceled );

    // The cancel notification itself never answered anything (it is a
    // notification), and a later ping proves the loop is still healthy.
    server.cancelNotification( QVariant( 999 ) ); // unknown id: consumed no-op
    server.request( QVariantMap{
        { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
        { QStringLiteral( "id" ), 8 },
        { QStringLiteral( "method" ), QStringLiteral( "ping" ) },
        { QStringLiteral( "params" ), QVariantMap{} } } );
    REQUIRE( server.responses.size() == 2 ); // run_workflow + ping
    REQUIRE( server.responses.last().id.toInt() == 8 );
}

TEST_CASE( "a get_workflow_status poll never arms pipeline cancellation (R14-C FIX-1 hygiene)",
           "[agent][mcp][workflow][r14_C]" )
{
    registerR14CFixtures();
    CheckpointDirGuard checkpointGuard;
    RecordingServer server;
    server.initialize( 1 );
    server.responses.clear();

    // Slow pipeline, submitted as rpc 11.
    server.callTool( QVariant( 11 ), QStringLiteral( "run_workflow" ),
                     QVariantMap{ { QStringLiteral( "pipeline" ),
                                    sleepPipelineJson( "r14_c_hygiene_probe" ) } } );
    REQUIRE( server.responses.size() == 1 );
    const long pipelineId =
        payloadOf( server.responses.first() ).value( QStringLiteral( "pipeline_id" ) ).toInteger();
    REQUIRE( pipelineId >= 0 );
    REQUIRE( runReachedState( pipelineId, sicnu::workflow::WorkflowRunState::Running,
                              std::chrono::milliseconds( 5000 ) ) );

    // A STATUS poll over the same pipeline (rpc 12) also returns pipeline_id
    // in its result. Aborting that poll (rpc 12) must NOT cancel the run —
    // cancellation is armed by the SUBMITTING call, never by a read-only
    // status probe.
    server.responses.clear();
    server.callTool( QVariant( 12 ), QStringLiteral( "get_workflow_status" ),
                     QVariantMap{ { QStringLiteral( "pipeline_id" ),
                                    static_cast<qlonglong>( pipelineId ) } } );
    REQUIRE( server.responses.size() == 1 );
    REQUIRE( payloadOf( server.responses.first() )
                 .contains( QStringLiteral( "pipeline_id" ) ) );
    server.cancelNotification( QVariant( 12 ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    if ( const auto run =
             sicnu::workflow::WorkflowRunCoordinator::instance().runForPipeline( pipelineId ) )
        REQUIRE( !sicnu::workflow::isTerminalRunState( run->state() ) ); // still live

    // The submitting rpc id DOES cancel.
    server.cancelNotification( QVariant( 11 ) );
    REQUIRE( runReachedState( pipelineId, sicnu::workflow::WorkflowRunState::Canceled,
                              std::chrono::milliseconds( 15000 ) ) );
}
