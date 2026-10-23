// tests/test_r18_c_rpc_cancel_id_namespace.cpp — R18-C C05: JSON-RPC ids are
// numbers OR strings (#644), and the cancel bookkeeping maps armed them via
// id.toString() — so numeric id 5 and string id "5" collapsed onto one map
// key. A notifications/cancelled for one request then cancelled the OTHER
// request's task and consumed its map entry (one-shot removal), leaving the
// legitimately cancelled request un-cancellable. The map key must carry the
// id's type.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <chrono>
#include <thread>

#include "agent/mcp_server.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_id.h"
#include "processing/framework/task_center.h"

static const bool kFixtureWorkspaceInstalled = [] {
    if ( qEnvironmentVariableIsEmpty( "SICNU_MCP_WORKSPACE" ) )
        qputenv( "SICNU_MCP_WORKSPACE", "/" );
    return true;
}();

namespace
{

class RecordingServer : public McpServer
{
public:
    QVariantMap lastResponseResult;
    int lastErrorCode = 0;
    QString lastErrorMessage;

    void request( const QVariantMap &req ) { handleRequest( req ); }

    void sendResponse( const QVariant &, const QVariantMap &result ) override
    {
        lastResponseResult = result;
        lastErrorCode = 0;
    }
    void sendError( const QVariant &, int code, const QString &message ) override
    {
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendError( const QVariant &, int code, const QString &message,
                    const QVariantMap & ) override
    {
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendNotification( const QString &, const QVariantMap & ) override {}

    QVariantMap callTool( const QVariant &id, const QString &tool, const QVariantMap &arguments )
    {
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), id },
            { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "name" ), tool },
                           { QStringLiteral( "arguments" ), arguments } } } } );
        return lastResponseResult;
    }

    void cancelNotification( const QVariant &requestId )
    {
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "method" ), QStringLiteral( "notifications/cancelled" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "requestId" ), requestId } } } } );
    }

    QString statusOf( const QString &executionId )
    {
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 99 },
            { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
            { QStringLiteral( "params" ),
              QVariantMap{
                  { QStringLiteral( "name" ), QStringLiteral( "get_execution_status" ) },
                  { QStringLiteral( "arguments" ),
                    QVariantMap{ { QStringLiteral( "execution_id" ), executionId } } } } } } );
        if ( lastErrorCode != 0 )
            return QStringLiteral( "error" );
        const QJsonObject payload = QJsonDocument::fromJson(
                                        lastResponseResult.value( QStringLiteral( "content" ) )
                                            .toList()
                                            .value( 0 )
                                            .toMap()
                                            .value( QStringLiteral( "text" ) )
                                            .toString()
                                            .toUtf8() )
                                        .object();
        return payload.value( QStringLiteral( "status" ) ).toString();
    }
};

/// Slice-sleeping operator honouring isCancelled (r14_C pattern,
/// track-private id), reporting contract units (0..1 fractions).
class R18CSleepOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "rs:r18_c_sleep"; }
    Json::Value run( const Json::Value &params,
                     sicnu::operators::RSOperatorContext &ctx ) override
    {
        const int slices = params.isMember( "slices" ) && params["slices"].isInt()
                               ? std::max( 1, std::min( 600, params["slices"].asInt() ) )
                               : 30;
        for ( int i = 0; i < slices; ++i )
        {
            if ( ctx.isCancelled() )
            {
                Json::Value error( Json::objectValue );
                error["cancelled"] = true;
                return error;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            ctx.reportProgress( double( i + 1 ) / slices, "sleeping" );
        }
        Json::Value result( Json::objectValue );
        result["output"] = "/tmp/r18_c_sleep.tif";
        return result;
    }
};

RecordingServer &server()
{
    static RecordingServer *instance = [] {
        static int argc = 1;
        static char arg0[] = "test_r18_c_rpc_cancel_id_namespace";
        static char *argv[] = { arg0 };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );
        auto &registry = sicnu::operators::RSOperatorRegistry::instance();
        registry.registerOperator( "rs:r18_c_sleep",
                                   []() -> std::unique_ptr<sicnu::operators::RSOperator> {
                                       return std::make_unique<R18CSleepOperator>();
                                   } );
        auto &adapters = sicnu::processing::AtomicAlgorithmRegistry::instance();
        if ( !adapters.findAdapter( "rs:r18_c_sleep" ) )
        {
            auto op = registry.create( "rs:r18_c_sleep" );
            if ( op )
                adapters.registerAdapter(
                    std::make_shared<sicnu::processing::RsOperatorAdapter>( std::move( op ) ) );
        }
        RecordingServer *s = new RecordingServer();
        s->request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 0 },
            { QStringLiteral( "method" ), QStringLiteral( "initialize" ) },
            { QStringLiteral( "params" ), QVariantMap{} } } );
        return s;
    }();
    return *instance;
}

QString executionIdOf( const QVariantMap &callResult )
{
    return QJsonDocument::fromJson(
               callResult.value( QStringLiteral( "content" ) ).toList().value( 0 ).toMap()
                   .value( QStringLiteral( "text" ) ).toString().toUtf8() )
        .object()
        .value( QStringLiteral( "execution_id" ) )
        .toString();
}

/// Waits until the task reaches one of @p statuses (or returns the last
/// observed status when the deadline passes).
QString waitForStatus( RecordingServer &s, const QString &executionId,
                       const QStringList &statuses, int timeoutMs = 15000 )
{
    QString status;
    for ( int i = 0; i < timeoutMs / 25; ++i )
    {
        status = s.statusOf( executionId );
        if ( statuses.contains( status ) )
            return status;
        QCoreApplication::processEvents();
        std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
    }
    return status;
}

} // namespace

TEST_CASE( "numeric and string rpc ids keep disjoint cancel bookkeeping",
           "[agent][mcp][cancel][r18c]" )
{
    RecordingServer &s = server();

    // Two concurrent executions armed under DIFFERENT rpc id types that
    // stringify identically: numeric 5 and string "5".
    const QVariantMap numericResult =
        s.callTool( QVariant( qlonglong( 5 ) ), QStringLiteral( "execute_operator" ),
                    QVariantMap{ { QStringLiteral( "operator_id" ),
                                   QStringLiteral( "rs:r18_c_sleep" ) },
                                 { QStringLiteral( "arguments" ),
                                   QVariantMap{ { QStringLiteral( "slices" ), 120 } } } } );
    const QString numericExec = executionIdOf( numericResult );
    REQUIRE_FALSE( numericExec.isEmpty() );

    const QVariantMap stringResult =
        s.callTool( QVariant( QStringLiteral( "5" ) ), QStringLiteral( "execute_operator" ),
                    QVariantMap{ { QStringLiteral( "operator_id" ),
                                   QStringLiteral( "rs:r18_c_sleep" ) },
                                 { QStringLiteral( "arguments" ),
                                   QVariantMap{ { QStringLiteral( "slices" ), 120 } } } } );
    const QString stringExec = executionIdOf( stringResult );
    REQUIRE_FALSE( stringExec.isEmpty() );
    REQUIRE( numericExec != stringExec );

    // Give both tasks a moment to actually start running.
    waitForStatus( s, numericExec, { QStringLiteral( "running" ) }, 5000 );
    waitForStatus( s, stringExec, { QStringLiteral( "running" ) }, 5000 );

    // Cancel ONLY the numeric request: notifications/cancelled {requestId: 5}.
    s.cancelNotification( QVariant( qlonglong( 5 ) ) );

    // The numeric task must fold to canceled...
    const QString numericStatus =
        waitForStatus( s, numericExec, { QStringLiteral( "canceled" ) }, 15000 );
    CHECK( numericStatus == QStringLiteral( "canceled" ) );

    // ...while the string-id task keeps running: the numeric cancel must not
    // have consumed its map entry or cancelled its task.
    const QString stringStatus = s.statusOf( stringExec );
    CHECK( stringStatus == QStringLiteral( "running" ) );

    // The string-id cancel still works afterwards (its entry was not eaten).
    s.cancelNotification( QVariant( QStringLiteral( "5" ) ) );
    const QString stringStatusAfter =
        waitForStatus( s, stringExec, { QStringLiteral( "canceled" ) }, 15000 );
    CHECK( stringStatusAfter == QStringLiteral( "canceled" ) );
}

TEST_CASE( "aborting a read-only status poll never cancels the task it reports",
           "[agent][mcp][cancel][r21c]" )
{
    // R21-C F1: m_cancelledRequestTasks was armed by EVERY tools/call whose
    // result carried an execution_id — including get_execution_status polls
    // (they always echo it). A UI abort or bridge 10-minute timeout on the
    // POLL itself then cancelled the long-running execution the poll was
    // merely reporting on. The pipeline map got the submitting-call gate in
    // R14-C FIX-1; the task map must mirror it (RED on master: the poll id
    // 99 armed the map, so cancelling 99 kills the task).
    RecordingServer &s = server();

    const QVariantMap submitted = s.callTool(
        QVariant( qlonglong( 9001 ) ), QStringLiteral( "execute_algorithm" ),
        QVariantMap{ { QStringLiteral( "algorithm_id" ), QStringLiteral( "rs:r18_c_sleep" ) },
                     { QStringLiteral( "parameters" ),
                       QVariantMap{ { QStringLiteral( "slices" ), 120 } } } } );
    const QString exec = executionIdOf( submitted );
    REQUIRE_FALSE( exec.isEmpty() );
    REQUIRE( waitForStatus( s, exec, { QStringLiteral( "running" ) }, 15000 )
             == QStringLiteral( "running" ) );

    // Cancel the READ-ONLY poll's rpc id (99 = the harness statusOf id).
    s.cancelNotification( QVariant( qlonglong( 99 ) ) );
    for ( int i = 0; i < 8; ++i )
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    CHECK( s.statusOf( exec ) == QStringLiteral( "running" ) );

    // The SUBMITTING request's own id stays the real cancel channel.
    s.cancelNotification( QVariant( qlonglong( 9001 ) ) );
    const QString finalStatus =
        waitForStatus( s, exec, { QStringLiteral( "canceled" ) }, 15000 );
    CHECK( finalStatus == QStringLiteral( "canceled" ) );
}
