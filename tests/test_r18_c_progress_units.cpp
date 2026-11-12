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
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <cmath>
#include <thread>

#include "agent/mcp_server.h"
#include "jobs/job_engine.h"
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

// The worker advances only when the test supplies the next progress value.
// This exercises real adapter -> TaskCenter -> queued MCP delivery without
// races against a clock-driven stream of unrelated progress ticks.
struct ProgressScript
{
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<double> pending;
    int reported = 0;
    bool started = false;
};
ProgressScript script;

class R18CProgressAdapter : public sicnu::processing::AtomicAlgorithmAdapter
{
public:
    std::string algorithmId() const override { return "rs:r18_c_progress"; }
    sicnu::processing::AlgorithmDescriptor descriptor() const override
    {
        sicnu::processing::AlgorithmDescriptor desc;
        desc.id = algorithmId();
        return desc;
    }
    Json::Value execute( const Json::Value &, sicnu::processing::ProgressCallback progress,
                         std::function<bool()> ) override
    {
        {
            std::lock_guard lock( script.mutex );
            script.started = true;
            script.changed.notify_all();
        }
        for ( ;; )
        {
            std::unique_lock lock( script.mutex );
            if ( !script.changed.wait_for( lock, std::chrono::seconds( 5 ), [] { return !script.pending.empty(); } ) )
                throw std::runtime_error( "progress script timed out" );
            const double value = script.pending.front();
            script.pending.pop_front();
            lock.unlock();
            // 1.0 ends the task; TaskCenter supplies the terminal progress.
            if ( progress && value < 1.0 )
                progress( int( std::lround( value * 100.0 ) ), "controlled progress" );
            lock.lock();
            ++script.reported;
            script.changed.notify_all();
            if ( value == 1.0 )
                return Json::Value( Json::objectValue );
        }
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
        auto &adapters = sicnu::processing::AtomicAlgorithmRegistry::instance();
        const auto adapter = std::make_shared<R18CProgressAdapter>();
        adapters.registerAdapter( adapter );
        sicnu::jobs::JobEngine::instance().registerExecutor( adapter->algorithmId(),
            [adapter]( const sicnu::jobs::JobRequest &request, sicnu::operators::RSOperatorContext &ctx ) {
                return adapter->execute( request.params,
                    [&ctx]( int percent, const std::string &message ) {
                        ctx.reportProgress( percent / 100.0, message );
                    }, [&ctx] { return ctx.isCancelled(); } );
            } );
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

    // Arm a subscription before allowing the real worker to report progress.
    const QVariantMap result =
        s.call( QStringLiteral( "execute_operator" ),
                QVariantMap{ { QStringLiteral( "operator_id" ), QStringLiteral( "rs:r18_c_progress" ) },
                             { QStringLiteral( "parameters" ),
                               QVariantMap{} } },
                QVariant( QStringLiteral( "tok-r18c" ) ) );
    INFO( "tool result: " << QJsonDocument::fromVariant( result ).toJson( QJsonDocument::Compact ).toStdString() );
    REQUIRE( result.value( QStringLiteral( "isError" ) ).toBool() == false );
    const QString execId = payloadOf( result ).value( QStringLiteral( "execution_id" ) ).toString();
    REQUIRE_FALSE( execId.isEmpty() );
    const auto executionId = sicnu::processing::ExecutionId::fromWire( execId );
    REQUIRE( executionId );
    const long taskId = executionId->taskId();

    const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    bool started = false;
    while ( !started && std::chrono::steady_clock::now() < startDeadline )
    {
        QCoreApplication::processEvents();
        {
            std::lock_guard lock( script.mutex );
            started = script.started;
        }
        std::this_thread::yield();
    }
    INFO( "task status: " << int( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status ) );
    INFO( "task error: " << sicnu::TaskCenter::instance().getTaskInfo( taskId ).errorMessage.toStdString() );
    REQUIRE( started );
    QCoreApplication::processEvents();
    const int before = s.progressNotifications.size();
    int sent = 0;
    const auto pump = [&]( double value ) {
        const int notificationsBefore = s.progressNotifications.size();
        {
            std::unique_lock lock( script.mutex );
            script.pending.push_back( value );
            script.changed.notify_all();
            ++sent;
            REQUIRE( script.changed.wait_for( lock, std::chrono::seconds( 5 ), [&] {
                return script.reported >= sent;
            } ) );
        }
        // Worker delivery is queued: nothing arrives until the event loop runs.
        CHECK( s.progressNotifications.size() == notificationsBefore );
        QCoreApplication::processEvents();
    };

    // First intermediate step (0.50 fraction): must be emitted at true scale.
    pump( 0.50 );
    REQUIRE( s.progressNotifications.size() == before + 1 );
    CHECK( std::abs( progressAt( s, before ) - 0.50 ) < 1e-9 );
    CHECK( s.progressNotifications.at( before ).toMap()
              .value( QStringLiteral( "progressToken" ) )
              .toString() == QStringLiteral( "tok-r18c" ) );
    CHECK( s.progressNotifications.at( before ).toMap()
              .value( QStringLiteral( "total" ) )
              .toDouble() == 1.0 );

    // A small move (0.54: under the 0.05 delta) is rate-limited away.
    pump( 0.54 );
    CHECK( s.progressNotifications.size() == before + 1 );

    // Crossing the delta threshold (0.50 → 0.60) emits at true scale.
    pump( 0.60 );
    REQUIRE( s.progressNotifications.size() == before + 2 );
    CHECK( std::abs( progressAt( s, before + 1 ) - 0.60 ) < 1e-9 );

    // Complete through the real engine. It may publish a running 100% tick
    // before the terminal Completed record; both must carry 1.0, not 0.01.
    pump( 1.0 );
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    while ( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status != sicnu::TaskStatus::Completed
            && std::chrono::steady_clock::now() < deadline )
    {
        QCoreApplication::processEvents();
        std::this_thread::yield();
    }
    CHECK( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status == sicnu::TaskStatus::Completed );
    QCoreApplication::processEvents();
    const int after = s.progressNotifications.size();
    REQUIRE( after >= before + 3 );
    REQUIRE( after <= before + 4 );
    for ( int i = before + 2; i < after; ++i )
        CHECK( std::abs( progressAt( s, i ) - 1.0 ) < 1e-9 );
    // Repeated terminal updates through the authority must not re-notify.
    sicnu::TaskCenter::instance().markTaskCompleted( taskId );
    QCoreApplication::processEvents();
    CHECK( s.progressNotifications.size() == after );
}
