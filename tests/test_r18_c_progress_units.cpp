// tests/test_r18_c_progress_units.cpp — R18-C C03: the TaskCenter progress
// contract is a 0..1 fraction (rs_operator_context.h "progress reaches 0/1",
// completion stamps 1.0, every production operator reports fractions). The
// notifications/progress relay consumed it as a 0..100 percentage: the wire
// value divided by 100 (a completed task emitted progress 0.01 with
// total 1.0) and the 5-point rate limiter never saw an intermediate step
// cross its threshold (fractions cannot move 5.0), so intermediate progress
// was silently suppressed. This file pins the true-scale relay.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <chrono>
#include <cmath>
#include <thread>

#include "agent/mcp_server.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_id.h"
#include "processing/framework/task_center.h"

// The MCP sandbox is default-deny; fixtures here live under /tmp, so this
// binary opts into the widest root (same rationale as test_surface_protocol).
static const bool kFixtureWorkspaceInstalled = [] {
    if ( qEnvironmentVariableIsEmpty( "SICNU_MCP_WORKSPACE" ) )
        qputenv( "SICNU_MCP_WORKSPACE", "/" );
    return true;
}();

namespace
{

class ProtocolServer : public McpServer
{
public:
    QVariantMap lastResponseResult;
    int lastErrorCode = 0;
    QString lastErrorMessage;
    QVariantList progressNotifications;

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
    void sendNotification( const QString &method, const QVariantMap &params ) override
    {
        if ( method == QLatin1String( "notifications/progress" ) )
            progressNotifications.append( params );
    }

    QVariantMap call( const QString &tool, const QVariantMap &args,
                      const QVariant &progressToken = {} )
    {
        QVariantMap params;
        params[QStringLiteral( "name" )] = tool;
        params[QStringLiteral( "arguments" )] = args;
        if ( progressToken.isValid() )
        {
            QVariantMap meta;
            meta[QStringLiteral( "progressToken" )] = progressToken;
            params[QStringLiteral( "_meta" )] = meta;
        }
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 1 },
            { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
            { QStringLiteral( "params" ), params } } );
        return lastResponseResult;
    }
};

/// Short slice-sleeping operator honouring isCancelled; reports the CONTRACT
/// units (0..1 fractions) like every production operator.
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

ProtocolServer &server()
{
    static ProtocolServer *instance = [] {
        static int argc = 1;
        static char arg0[] = "test_r18_c_progress_units";
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
        ProtocolServer *s = new ProtocolServer();
        s->request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 0 },
            { QStringLiteral( "method" ), QStringLiteral( "initialize" ) },
            { QStringLiteral( "params" ), QVariantMap{} } } );
        return s;
    }();
    return *instance;
}

QJsonObject payloadOf( const QVariantMap &callResult )
{
    return QJsonDocument::fromJson(
               callResult.value( QStringLiteral( "content" ) ).toList().value( 0 ).toMap()
                   .value( QStringLiteral( "text" ) ).toString().toUtf8() )
        .object();
}

double progressAt( const ProtocolServer &s, int index )
{
    return s.progressNotifications.at( index ).toMap()
        .value( QStringLiteral( "progress" ) )
        .toDouble();
}

} // namespace

TEST_CASE( "notifications/progress relays TaskCenter fractions at true scale",
           "[agent][mcp][progress][r18c]" )
{
    ProtocolServer &s = server();
    const int before = s.progressNotifications.size();

    // Arm a subscription with a task that stays alive while we fabricate
    // updates (the queued REAL signals are only delivered on processEvents,
    // which this test never pumps before its assertions).
    const QVariantMap result =
        s.call( QStringLiteral( "execute_operator" ),
                QVariantMap{ { QStringLiteral( "operator_id" ), QStringLiteral( "rs:r18_c_sleep" ) },
                             { QStringLiteral( "arguments" ),
                               QVariantMap{ { QStringLiteral( "slices" ), 30 } } } },
                QVariant( QStringLiteral( "tok-r18c" ) ) );
    REQUIRE( result.value( QStringLiteral( "isError" ) ).toBool() == false );
    const QString execId = payloadOf( result ).value( QStringLiteral( "execution_id" ) ).toString();
    REQUIRE_FALSE( execId.isEmpty() );
    const auto executionId = sicnu::processing::ExecutionId::fromWire( execId );
    REQUIRE( executionId );
    const long taskId = executionId->taskId();

    sicnu::AlgorithmTaskInfo info;
    info.taskId = taskId;
    info.algorithmId = QStringLiteral( "rs:r18_c_sleep" );

    // onTaskUpdated is a private slot; drive it through the moc table like
    // the queued TaskCenter signal would (same metacall path, synchronous).
    const auto pump = [ &s ]( const sicnu::AlgorithmTaskInfo &taskInfo ) {
        QMetaObject::invokeMethod( &s, "onTaskUpdated", Qt::DirectConnection,
                                   Q_ARG( sicnu::AlgorithmTaskInfo, taskInfo ) );
    };

    // First intermediate step (0.50 fraction): must be emitted at true scale.
    info.status = sicnu::TaskStatus::Running;
    info.progressPercentage = 0.50;
    pump( info );
    REQUIRE( s.progressNotifications.size() == before + 1 );
    CHECK( std::abs( progressAt( s, before ) - 0.50 ) < 1e-9 );
    CHECK( s.progressNotifications.at( before ).toMap()
              .value( QStringLiteral( "progressToken" ) )
              .toString() == QStringLiteral( "tok-r18c" ) );
    CHECK( s.progressNotifications.at( before ).toMap()
              .value( QStringLiteral( "total" ) )
              .toDouble() == 1.0 );

    // A small move (0.54: under the 0.05 delta) is rate-limited away.
    info.progressPercentage = 0.54;
    pump( info );
    CHECK( s.progressNotifications.size() == before + 1 );

    // Crossing the delta threshold (0.50 → 0.60) emits at true scale.
    info.progressPercentage = 0.60;
    pump( info );
    REQUIRE( s.progressNotifications.size() == before + 2 );
    CHECK( std::abs( progressAt( s, before + 1 ) - 0.60 ) < 1e-9 );

    // Terminal completion (fraction 1.0) emits 1.0 — not 0.01 — exactly once.
    info.status = sicnu::TaskStatus::Completed;
    info.progressPercentage = 1.0;
    pump( info );
    REQUIRE( s.progressNotifications.size() == before + 3 );
    CHECK( std::abs( progressAt( s, before + 2 ) - 1.0 ) < 1e-9 );
    pump( info );
    CHECK( s.progressNotifications.size() == before + 3 );

    // Clean up the real task so the binary can exit promptly.
    sicnu::TaskCenter::instance().cancelTask( taskId );
}
