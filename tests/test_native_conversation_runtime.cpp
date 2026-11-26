#include <QJsonArray>
#include <QJsonObject>
#include <catch2/catch_test_macros.hpp>

#include "agent/runtime/native_conversation_runtime.h"
#include "runtime/observability/trace.h"
#include "support/qsettings_redirect.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>

using sicnu::agent::runtime::ConversationConfig;
using sicnu::agent::runtime::NativeConversationRuntime;
namespace trace = sicnu::runtime::observability::trace;

namespace
{
exprs_test::QSettingsUserRootRedirect settingsRedirect;

struct NativeUsageTraceScope
{
    std::shared_ptr<trace::ITraceSink> previous = trace::Trace::sink();
    std::shared_ptr<trace::RingTraceSink> sink = std::make_shared<trace::RingTraceSink>( 32 );
    explicit NativeUsageTraceScope( bool enabled = true ) { trace::Trace::install( enabled ? sink : nullptr ); }
    ~NativeUsageTraceScope() { trace::Trace::install( previous ); }
};

void application()
{
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  static int argc = 1;
  static char name[] = "test_native_conversation_runtime";
  static char *argv[] = { name, nullptr };
  if ( !QCoreApplication::instance() ) new QCoreApplication( argc, argv );
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "native-conversation-runtime" ) );
  static const bool configured = [] {
    sicnu::agent::LlmProviderProfile profile;
    profile.id = QStringLiteral( "native-fixture" );
    profile.baseUrl = QStringLiteral( "http://127.0.0.1:9/v1" );
    profile.modelName = QStringLiteral( "fixture" );
    sicnu::agent::LlmConfigManager::setActiveProfile( profile );
    return true;
  }();
  Q_UNUSED( configured )
}

bool until( const std::function<bool()> &predicate, int timeoutMs = 5000 )
{
  QElapsedTimer timer;
  timer.start();
  while ( !predicate() && timer.elapsed() < timeoutMs )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    QThread::msleep( 1 );
  }
  return predicate();
}

QByteArray response( const QJsonObject &delta, const QString &finish = QStringLiteral( "stop" ) )
{
  return "data: " + QJsonDocument( QJsonObject{ { QStringLiteral( "choices" ), QJsonArray{ QJsonObject{
    { QStringLiteral( "delta" ), delta }, { QStringLiteral( "finish_reason" ), finish } } } } } )
    .toJson( QJsonDocument::Compact ) + "\n\ndata: [DONE]\n\n";
}

QJsonObject tool( int index, const QString &id, const QString &name = QStringLiteral( "explicit_alias" ),
                  const QString &arguments = QStringLiteral( "{}" ) )
{
  return { { QStringLiteral( "index" ), index }, { QStringLiteral( "id" ), id },
           { QStringLiteral( "function" ), QJsonObject{ { QStringLiteral( "name" ), name },
                                                        { QStringLiteral( "arguments" ), arguments } } } };
}

// Real loopback HTTP/SSE: tests use the same public runtime interface as Dock.
// No settings manager, user's provider profile or credential file is accessed.
class ModelServer : public QTcpServer
{
  public:
    QList<QJsonObject> requests;
    std::function<QByteArray( int )> answer;
    bool httpError = false;

    ModelServer()
    {
      connect( this, &QTcpServer::newConnection, this, [this] {
        while ( hasPendingConnections() )
        {
          QTcpSocket *socket = nextPendingConnection();
          auto buffer = std::make_shared<QByteArray>();
          auto handled = std::make_shared<bool>( false );
          connect( socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
            *buffer += socket->readAll();
            if ( *handled ) return;
            const qsizetype headerEnd = buffer->indexOf( "\r\n\r\n" );
            if ( headerEnd < 0 ) return;
            qint64 length = -1;
            for ( const QByteArray &line : buffer->left( headerEnd ).split( '\n' ) )
              if ( line.toLower().startsWith( "content-length:" ) ) length = line.mid( 15 ).trimmed().toLongLong();
            if ( length < 0 || buffer->size() < headerEnd + 4 + length ) return;
            *handled = true;
            requests.append( QJsonDocument::fromJson( buffer->mid( headerEnd + 4, length ) ).object() );
            const QByteArray body = answer( static_cast<int>( requests.size() ) );
            socket->write( ( httpError ? QByteArray( "HTTP/1.1 400 Bad Request\r\n" ) : QByteArray( "HTTP/1.1 200 OK\r\n" ) )
                           + "Content-Type: text/event-stream\r\nContent-Length: " + QByteArray::number( body.size() )
                           + "\r\nConnection: close\r\n\r\n" + body );
            socket->disconnectFromHost();
          } );
        }
      } );
    }

    ConversationConfig config() const
    {
      ConversationConfig config;
      config.sessionId = QStringLiteral( "local-session" );
      config.systemPrompt = QStringLiteral( "Local deterministic fixture" );
      config.toolRevision = QStringLiteral( "fixture-revision" );
      config.profile.baseUrl = QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( serverPort() );
      config.profile.modelName = QStringLiteral( "fixture" );
      config.tools = { QJsonObject{ { QStringLiteral( "name" ), QStringLiteral( "explicit_alias" ) },
                                   { QStringLiteral( "canonicalId" ), QStringLiteral( "fixture:tool_with_underscore" ) },
                                   { QStringLiteral( "description" ), QStringLiteral( "Echo" ) },
                                   { QStringLiteral( "parameters" ), QJsonObject{ { QStringLiteral( "type" ), QStringLiteral( "object" ) } } } } };
      return config;
    }
};

QList<QJsonObject> messagesWithRole( const QJsonObject &request, const QString &role )
{
  QList<QJsonObject> messages;
  for ( const auto &value : request.value( QStringLiteral( "messages" ) ).toArray() )
    if ( value.toObject().value( QStringLiteral( "role" ) ).toString() == role ) messages.append( value.toObject() );
  return messages;
}
}

TEST_CASE( "Native runtime waits for every tool result and preserves each reasoning batch on the wire",
           "[agent][runtime][native][wire]" )
{
  application();
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int number ) {
    if ( number > 2 ) return response( { { QStringLiteral( "content" ), QStringLiteral( "Verified final answer" ) } } );
    QJsonArray calls{ tool( 0, QStringLiteral( "call-%1-a" ).arg( number ), QStringLiteral( "explicit_alias" ),
                           QStringLiteral( "{\"note\":\"NDVI\\n\\\"quoted\\\"\",\"values\":[0,1,255]}" ) ) };
    if ( number == 1 ) calls.append( tool( 1, QStringLiteral( "call-1-b" ) ) );
    return response( { { QStringLiteral( "content" ), QStringLiteral( "content-%1" ).arg( number ) },
                       { QStringLiteral( "reasoning_content" ), QStringLiteral( "reasoning-%1" ).arg( number ) },
                       { QStringLiteral( "tool_calls" ), calls } }, QStringLiteral( "tool_calls" ) );
  };
  NativeConversationRuntime runtime;
  int requests = 0, settled = 0, started = 0;
  QString error;
  QList<QString> canonicalNames;
  QObject::connect( &runtime, &NativeConversationRuntime::responseStarted, [&]( const QString & ) { ++started; } );
  QObject::connect( &runtime, &NativeConversationRuntime::settled, [&]( const QString &, bool aborted, const QString &reason ) {
    ++settled; CHECK_FALSE( aborted ); error = reason;
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                    [&]( const QString &run, const QJsonObject &call, const QString &revision ) {
    ++requests;
    canonicalNames.append( call.value( QStringLiteral( "function" ) ).toObject().value( QStringLiteral( "name" ) ).toString() );
    CHECK( revision == QStringLiteral( "fixture-revision" ) );
    if ( call.value( QStringLiteral( "id" ) ).toString() != QStringLiteral( "call-1-b" ) )
    {
      const qsizetype priorRequests = server.requests.size();
      CHECK( runtime.completeTool( run, call.value( QStringLiteral( "id" ) ).toString(),
                                   { { QStringLiteral( "status" ), QStringLiteral( "success" ) },
                                     { QStringLiteral( "assetId" ), QStringLiteral( "committed-asset" ) } } ) );
      // Modal approval/interaction code may pump a nested event loop. A
      // queued continuation must still wait until this entire batch exits.
      QEventLoop nested;
      QTimer::singleShot( 20, &nested, &QEventLoop::quit );
      nested.exec();
      CHECK( server.requests.size() == priorRequests );
    }
  } );
  int ready = 0;
  QObject::connect( &runtime, &NativeConversationRuntime::ready, [&] { ++ready; } );
  runtime.startSession( server.config() );
  REQUIRE( ready == 1 );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "run" ), QStringLiteral( "Complete both rounds" ) ) );
  REQUIRE( until( [&] { return requests == 2 || settled; } ) );
  CHECK( settled == 0 );
  CHECK( server.requests.size() == 1 );
  CHECK_FALSE( runtime.completeTool( QStringLiteral( "old-run" ), QStringLiteral( "call-1-b" ), {} ) );
  CHECK_FALSE( runtime.completeTool( QStringLiteral( "run" ), QStringLiteral( "foreign-call" ), {} ) );
  REQUIRE( runtime.completeTool( QStringLiteral( "run" ), QStringLiteral( "call-1-b" ),
                                { { QStringLiteral( "status" ), QStringLiteral( "success" ) } } ) );
  CHECK_FALSE( runtime.completeTool( QStringLiteral( "run" ), QStringLiteral( "call-1-b" ), {} ) );
  // Completing a synchronous tool must not re-enter the transport on this stack.
  CHECK( server.requests.size() == 1 );
  REQUIRE( until( [&] { return settled != 0; } ) );
  CHECK( error.isEmpty() );
  CHECK( requests == 3 );
  CHECK( started == 3 );
  REQUIRE( server.requests.size() == 3 );
  for ( const QString &name : canonicalNames ) CHECK( name == QStringLiteral( "fixture:tool_with_underscore" ) );
  const auto firstBatch = messagesWithRole( server.requests.at( 1 ), QStringLiteral( "assistant" ) );
  REQUIRE( firstBatch.size() == 1 );
  CHECK( firstBatch.first().value( QStringLiteral( "content" ) ).toString() == QStringLiteral( "content-1" ) );
  CHECK( firstBatch.first().value( QStringLiteral( "reasoning_content" ) ).toString() == QStringLiteral( "reasoning-1" ) );
  REQUIRE( firstBatch.first().value( QStringLiteral( "tool_calls" ) ).toArray().size() == 2 );
  const auto lastBatches = messagesWithRole( server.requests.at( 2 ), QStringLiteral( "assistant" ) );
  REQUIRE( lastBatches.size() == 2 );
  CHECK( lastBatches.at( 1 ).value( QStringLiteral( "reasoning_content" ) ).toString() == QStringLiteral( "reasoning-2" ) );
  CHECK( lastBatches.at( 1 ).value( QStringLiteral( "content" ) ).toString() == QStringLiteral( "content-2" ) );
  const auto results = messagesWithRole( server.requests.at( 1 ), QStringLiteral( "tool" ) );
  REQUIRE( results.size() == 2 );
  CHECK( results.at( 0 ).value( QStringLiteral( "tool_call_id" ) ).toString() == QStringLiteral( "call-1-a" ) );
  CHECK( results.at( 1 ).value( QStringLiteral( "tool_call_id" ) ).toString() == QStringLiteral( "call-1-b" ) );
  for ( const auto &batch : lastBatches )
    for ( const auto &call : batch.value( QStringLiteral( "tool_calls" ) ).toArray() )
    {
      const auto function = call.toObject().value( QStringLiteral( "function" ) ).toObject();
      CHECK( function.value( QStringLiteral( "name" ) ).toString() == QStringLiteral( "explicit_alias" ) );
      REQUIRE( function.value( QStringLiteral( "arguments" ) ).isString() );
      const auto arguments = QJsonDocument::fromJson( function.value( QStringLiteral( "arguments" ) ).toString().toUtf8() ).object();
      if ( call.toObject().value( QStringLiteral( "id" ) ).toString().endsWith( QStringLiteral( "-a" ) ) )
        CHECK( arguments.value( QStringLiteral( "note" ) ).toString() == QStringLiteral( "NDVI\n\"quoted\"" ) );
    }
}

TEST_CASE( "Native cancellation settles explicitly and a late tool result cannot replay into a new run",
           "[agent][runtime][native][cancel]" )
{
  application();
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int number ) {
    if ( number == 1 ) return response( { { QStringLiteral( "tool_calls" ), QJsonArray{ tool( 0, QStringLiteral( "owned-call" ) ) } } }, QStringLiteral( "tool_calls" ) );
    return response( { { QStringLiteral( "content" ), QStringLiteral( "New run answer" ) } } );
  };
  NativeConversationRuntime runtime;
  int calls = 0, cancellations = 0;
  QList<bool> terminals;
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                    [&]( const QString &, const QJsonObject &, const QString & ) { ++calls; } );
  QObject::connect( &runtime, &NativeConversationRuntime::cancelOwnedToolsRequested,
                    [&]( const QString &run ) { CHECK( run == QStringLiteral( "old" ) ); ++cancellations; } );
  QObject::connect( &runtime, &NativeConversationRuntime::settled,
                    [&]( const QString &, bool aborted, const QString & ) { terminals.append( aborted ); } );
  runtime.startSession( server.config() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "old" ), QStringLiteral( "Run old tool" ) ) );
  REQUIRE( until( [&] { return calls == 1; } ) );
  CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "overlap" ), QStringLiteral( "No overlap" ) ) );
  runtime.cancelRun();
  REQUIRE( terminals.size() == 1 );
  CHECK( terminals.first() );
  CHECK( cancellations == 1 );
  runtime.cancelRun();
  CHECK( cancellations == 1 );
  CHECK_FALSE( runtime.completeTool( QStringLiteral( "old" ), QStringLiteral( "owned-call" ), {} ) );
  CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "old" ), QStringLiteral( "Do not reuse a run identity" ) ) );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "new" ), QStringLiteral( "New instruction" ) ) );
  CHECK_FALSE( runtime.completeTool( QStringLiteral( "old" ), QStringLiteral( "owned-call" ), {} ) );
  REQUIRE( until( [&] { return terminals.size() == 2; } ) );
  CHECK_FALSE( terminals.last() );
  REQUIRE( server.requests.size() == 2 );
  CHECK( messagesWithRole( server.requests.last(), QStringLiteral( "tool" ) ).isEmpty() );
  const auto users = messagesWithRole( server.requests.last(), QStringLiteral( "user" ) );
  REQUIRE( users.size() == 1 );
  CHECK( users.first().value( QStringLiteral( "content" ) ).toString() == QStringLiteral( "New instruction" ) );
}

TEST_CASE( "Native session rejects colliding wire names and canonical aliases before any request",
           "[agent][runtime][native][catalog]" )
{
  application();
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int ) { return response( {} ); };
  for ( bool collideWire : { false, true } )
  {
    NativeConversationRuntime runtime;
    auto config = server.config();
    auto duplicate = config.tools.first().toObject();
    duplicate[collideWire ? QStringLiteral( "canonicalId" ) : QStringLiteral( "name" )] = QStringLiteral( "another" );
    config.tools.append( duplicate );
    int ready = 0, errors = 0;
    QObject::connect( &runtime, &NativeConversationRuntime::ready, [&] { ++ready; } );
    QObject::connect( &runtime, &NativeConversationRuntime::settled,
                      [&]( const QString &, bool, const QString &error ) { if ( !error.isEmpty() ) ++errors; } );
    runtime.startSession( config );
    CHECK_FALSE( runtime.isReady() );
    CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "run" ), QStringLiteral( "Must not send" ) ) );
    CHECK( ready == 0 );
    CHECK( errors == 1 );
  }
  CHECK( server.requests.isEmpty() );
}

TEST_CASE( "Unexported native tools are visibly refused rather than inferred from their spelling",
           "[agent][runtime][native][refusal]" )
{
  application();
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int number ) {
    if ( number > 1 ) return response( { { QStringLiteral( "content" ), QStringLiteral( "Refusal understood" ) } } );
    return response( { { QStringLiteral( "tool_calls" ), QJsonArray{
      tool( 0, QStringLiteral( "unknown" ), QStringLiteral( "fixture_tool_with_underscore" ) ) } } }, QStringLiteral( "tool_calls" ) );
  };
  NativeConversationRuntime runtime;
  int refused = 0, dispatched = 0, terminals = 0;
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRefused,
                    [&]( const QString &, const QJsonObject &detail ) {
    ++refused; CHECK( detail.value( QStringLiteral( "reason" ) ).toString() == QStringLiteral( "unknown_tool" ) );
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                    [&]( const QString &, const QJsonObject &, const QString & ) { ++dispatched; } );
  QObject::connect( &runtime, &NativeConversationRuntime::settled,
                    [&]( const QString &, bool, const QString & ) { ++terminals; } );
  runtime.startSession( server.config() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "run" ), QStringLiteral( "Unknown call" ) ) );
  REQUIRE( until( [&] { return terminals != 0; } ) );
  CHECK( refused == 1 );
  CHECK( dispatched == 0 );
  REQUIRE( server.requests.size() == 2 );
  const auto results = messagesWithRole( server.requests.last(), QStringLiteral( "tool" ) );
  REQUIRE( results.size() == 1 );
  const auto result = QJsonDocument::fromJson( results.first().value( QStringLiteral( "content" ) ).toString().toUtf8() ).object();
  CHECK( result.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "error" ) );
  CHECK( result.value( QStringLiteral( "errorCode" ) ).toString() == QStringLiteral( "unknown_tool" ) );
}

TEST_CASE( "Native malformed calls and provider errors settle safely without replay",
           "[agent][runtime][native][failure]" )
{
  application();
  const QString secret = QStringLiteral( "fixture-secret-never-display" );
  for ( bool providerError : { false, true } )
  {
    ModelServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
    server.httpError = providerError;
    server.answer = [&]( int ) {
      if ( providerError ) return QByteArray( "{\"error\":\"fixture-secret-never-display\"}" );
      return response( { { QStringLiteral( "tool_calls" ), QJsonArray{
        tool( 0, QStringLiteral( "truncated" ), QStringLiteral( "explicit_alias" ), QStringLiteral( "{" ) ) } } }, QStringLiteral( "length" ) );
    };
    NativeConversationRuntime runtime;
    auto config = server.config();
    config.profile.apiKey = secret; // Deliberately synthetic; sent only to loopback.
    int terminals = 0, calls = 0, refusals = 0;
    QString error;
    QObject::connect( &runtime, &NativeConversationRuntime::settled,
                      [&]( const QString &, bool, const QString &reason ) { ++terminals; error = reason; } );
    QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                      [&]( const QString &, const QJsonObject &, const QString & ) { ++calls; } );
    QObject::connect( &runtime, &NativeConversationRuntime::toolCallRefused,
                      [&]( const QString &, const QJsonObject & ) { ++refusals; } );
    runtime.startSession( config );
    REQUIRE( runtime.submitPrompt( QStringLiteral( "run" ), QStringLiteral( "Must fail truthfully" ) ) );
    REQUIRE( until( [&] { return terminals != 0; } ) );
    CHECK( terminals == 1 );
    CHECK( calls == 0 );
    CHECK( refusals == ( providerError ? 0 : 1 ) );
    CHECK_FALSE( error.isEmpty() );
    CHECK_FALSE( error.contains( secret ) );
    CHECK_FALSE( error.contains( config.profile.baseUrl ) );
    CHECK( server.requests.size() == 1 );
  }
}

TEST_CASE( "Native model and tool attempt budgets stop runaway loops without another request",
           "[agent][runtime][native][budget]" )
{
  application();
  for ( bool toolBudget : { false, true } )
  {
    ModelServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
    server.answer = [toolBudget]( int number ) {
      QJsonArray calls;
      const int count = toolBudget && number <= 2 ? 16 : 1;
      for ( int i = 0; i < count; ++i ) calls.append( tool( i, QStringLiteral( "call-%1-%2" ).arg( number ).arg( i ) ) );
      return response( { { QStringLiteral( "tool_calls" ), calls } }, QStringLiteral( "tool_calls" ) );
    };
    NativeConversationRuntime runtime;
    int terminals = 0, executions = 0;
    QString error;
    QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                      [&]( const QString &run, const QJsonObject &call, const QString & ) {
      ++executions;
      CHECK( runtime.completeTool( run, call.value( QStringLiteral( "id" ) ).toString(),
                                   { { QStringLiteral( "status" ), QStringLiteral( "success" ) } } ) );
    } );
    QObject::connect( &runtime, &NativeConversationRuntime::settled,
                      [&]( const QString &, bool, const QString &reason ) { ++terminals; error = reason; } );
    runtime.startSession( server.config() );
    REQUIRE( runtime.submitPrompt( QStringLiteral( "run" ), QStringLiteral( "Bounded loop" ) ) );
    REQUIRE( until( [&] { return terminals != 0; } ) );
    CHECK( terminals == 1 );
    CHECK_FALSE( error.isEmpty() );
    CHECK( server.requests.size() == ( toolBudget ? 3 : 16 ) );
    CHECK( executions == ( toolBudget ? 32 : 16 ) );
  }
}

TEST_CASE( "Native rejects an oversized pending batch before executing any of its tools",
           "[agent][runtime][native][budget][batch]" )
{
  application();
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int ) {
    QJsonArray calls;
    for ( int i = 0; i < 17; ++i ) calls.append( tool( i, QStringLiteral( "batch-%1" ).arg( i ) ) );
    return response( { { QStringLiteral( "tool_calls" ), calls } }, QStringLiteral( "tool_calls" ) );
  };
  NativeConversationRuntime runtime;
  int calls = 0, refusals = 0, terminals = 0;
  QString error;
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                    [&]( const QString &, const QJsonObject &, const QString & ) { ++calls; } );
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRefused,
                    [&]( const QString &, const QJsonObject & ) { ++refusals; } );
  QObject::connect( &runtime, &NativeConversationRuntime::settled,
                    [&]( const QString &, bool, const QString &reason ) { ++terminals; error = reason; } );
  runtime.startSession( server.config() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "run" ), QStringLiteral( "No partial execution" ) ) );
  REQUIRE( until( [&] { return terminals != 0; } ) );
  CHECK( calls == 0 );
  CHECK( refusals == 1 );
  CHECK_FALSE( error.isEmpty() );
  CHECK( server.requests.size() == 1 );
}

TEST_CASE( "Native history retains the system prompt and never starts its tail with a tool reply",
           "[agent][runtime][native][history]" )
{
  QJsonArray history{ QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "system" ) } } };
  for ( int i = 0; i < 100; ++i )
  {
    history.append( QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "assistant" ) } } );
    history.append( QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "tool" ) } } );
    history.append( QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "user" ) } } );
  }
  const auto pruned = NativeConversationRuntime::pruneHistory( history );
  CHECK( pruned.size() == 61 );
  CHECK( pruned.first().toObject().value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "system" ) );
  CHECK( pruned.at( 1 ).toObject().value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "assistant" ) );
  CHECK( NativeConversationRuntime::pruneHistory( pruned ) == pruned );
  QJsonArray orphaned{ history.first(), QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "tool" ) } } };
  while ( orphaned.size() < 61 ) orphaned.append( QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "user" ) } } );
  CHECK( NativeConversationRuntime::pruneHistory( orphaned ).size() == 60 );
}

TEST_CASE( "Native model attempts and standard SSE usage are observed across a tool round trip",
           "[agent][runtime][native][usage][wire]" )
{
  application();
  const NativeUsageTraceScope tracing;
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int number ) {
    const QJsonObject delta = number == 1
      ? QJsonObject{ { QStringLiteral( "tool_calls" ), QJsonArray{ tool( 0, QStringLiteral( "observed-tool" ) ) } } }
      : QJsonObject{ { QStringLiteral( "content" ), QStringLiteral( "Complete after tool" ) } };
    QByteArray stream = response( delta, number == 1 ? QStringLiteral( "tool_calls" ) : QStringLiteral( "stop" ) );
    // The standard final usage frame has no choices. Count 30 prompt tokens,
    // including 10 cached tokens, and 20 completion tokens per completion.
    const QByteArray usage = "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":30,\"completion_tokens\":20,\"total_tokens\":50,"
                             "\"prompt_tokens_details\":{\"cached_tokens\":10}}}\n\n";
    stream.replace( QByteArray( "data: [DONE]\n\n" ), usage + "data: [DONE]\n\n" );
    return stream;
  };
  NativeConversationRuntime runtime;
  int runs = 0, attempts = 0, terminals = 0, tools = 0;
  QList<QJsonObject> usages;
  QObject::connect( &runtime, &NativeConversationRuntime::runStarted, [&]( const QString &run ) {
    CHECK( run == QStringLiteral( "usage-run" ) ); ++runs;
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::modelRequestStarted, [&]( const QString &run ) {
    CHECK( run == QStringLiteral( "usage-run" ) ); ++attempts;
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::usageUpdated,
                    [&]( const QString &run, const QJsonObject &envelope ) {
    CHECK( run == QStringLiteral( "usage-run" ) );
    usages.append( envelope.value( QStringLiteral( "usage" ) ).toObject() );
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                    [&]( const QString &run, const QJsonObject &call, const QString & ) {
    ++tools;
    CHECK( runtime.completeTool( run, call.value( QStringLiteral( "id" ) ).toString(),
      { { QStringLiteral( "status" ), QStringLiteral( "success" ) } } ) );
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::settled,
                    [&]( const QString &, bool aborted, const QString &error ) {
    ++terminals; CHECK_FALSE( aborted ); CHECK( error.isEmpty() );
  } );
  runtime.startSession( server.config() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "usage-run" ), QStringLiteral( "Synthetic tool and usage round trip" ) ) );
  REQUIRE( until( [&] { return terminals != 0; } ) );
  CHECK( runs == 1 );
  CHECK( attempts == 2 );
  CHECK( tools == 1 );
  CHECK( terminals == 1 );
  REQUIRE( usages.size() == 2 );
  const QJsonObject expectedUsage{ { QStringLiteral( "input" ), 20 }, { QStringLiteral( "output" ), 20 },
    { QStringLiteral( "cacheRead" ), 10 }, { QStringLiteral( "cacheWrite" ), 0 }, { QStringLiteral( "totalTokens" ), 50 } };
  for ( const QJsonObject &usage : usages ) CHECK( usage == expectedUsage );
  REQUIRE( server.requests.size() == 2 );
  const QJsonObject expectedOptions{ { QStringLiteral( "include_usage" ), true } };
  for ( const QJsonObject &request : server.requests )
    CHECK( request.value( QStringLiteral( "stream_options" ) ).toObject() == expectedOptions );
}

TEST_CASE( "Missing usage, HTTP failure and cancellation never fabricate Native token usage",
           "[agent][runtime][native][usage][terminal]" )
{
  application();
  for ( const QString &mode : { QStringLiteral( "missing" ), QStringLiteral( "http-error" ), QStringLiteral( "cancel" ) } )
  {
    INFO( mode.toStdString() );
    ModelServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
    server.httpError = mode == QStringLiteral( "http-error" );
    server.answer = [mode]( int ) {
      if ( mode == QStringLiteral( "http-error" ) ) return QByteArray( "{\"error\":\"synthetic-provider-error\"}" );
      if ( mode == QStringLiteral( "cancel" ) )
        return response( { { QStringLiteral( "tool_calls" ), QJsonArray{ tool( 0, QStringLiteral( "pending-cancel" ) ) } } }, QStringLiteral( "tool_calls" ) );
      return response( { { QStringLiteral( "content" ), QStringLiteral( "Provider omitted usage" ) } } );
    };
    NativeConversationRuntime runtime;
    int runs = 0, attempts = 0, usages = 0, tools = 0, terminals = 0;
    bool wasAborted = false;
    QString terminalError;
    QObject::connect( &runtime, &NativeConversationRuntime::runStarted, [&]( const QString & ) { ++runs; } );
    QObject::connect( &runtime, &NativeConversationRuntime::modelRequestStarted, [&]( const QString & ) { ++attempts; } );
    QObject::connect( &runtime, &NativeConversationRuntime::usageUpdated,
                      [&]( const QString &, const QJsonObject & ) { ++usages; } );
    QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                      [&]( const QString &, const QJsonObject &, const QString & ) { ++tools; } );
    QObject::connect( &runtime, &NativeConversationRuntime::settled,
                      [&]( const QString &, bool aborted, const QString &error ) {
      ++terminals; wasAborted = aborted; terminalError = error;
    } );
    runtime.startSession( server.config() );
    REQUIRE( runtime.submitPrompt( mode, QStringLiteral( "Synthetic unknown usage" ) ) );
    if ( mode == QStringLiteral( "cancel" ) )
    {
      REQUIRE( until( [&] { return tools == 1; } ) );
      runtime.cancelRun();
    }
    REQUIRE( until( [&] { return terminals != 0; } ) );
    CHECK( runs == 1 );
    CHECK( attempts == 1 );
    CHECK( usages == 0 );
    CHECK( terminals == 1 );
    CHECK( wasAborted == ( mode == QStringLiteral( "cancel" ) ) );
    CHECK( terminalError.isEmpty() == ( mode != QStringLiteral( "http-error" ) ) );
    CHECK( server.requests.size() == 1 );
  }
}

TEST_CASE( "Native usage observers retain the original run identity through cancellation and replacement",
           "[agent][runtime][native][usage][lifecycle]" )
{
  application();
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int number ) {
    QByteArray stream = response( { { QStringLiteral( "content" ), QStringLiteral( "Completion %1" ).arg( number ) } } );
    const QByteArray usage = "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":30,\"completion_tokens\":20,\"total_tokens\":50}}\n\n";
    stream.replace( QByteArray( "data: [DONE]\n\n" ), usage + "data: [DONE]\n\n" );
    return stream;
  };
  NativeConversationRuntime runtime;
  const QString oldRun = QStringLiteral( "observed-old" );
  const QString newRun = QStringLiteral( "observed-replacement" );
  QStringList firstObserverRuns, secondObserverRuns;
  QHash<QString, QPair<bool, QString>> terminals;
  bool cancelledFromUsage = false;
  bool replacementAccepted = false;
  QObject::connect( &runtime, &NativeConversationRuntime::settled,
                    [&]( const QString &run, bool aborted, const QString &error ) {
    terminals.insert( run, { aborted, error } );
    if ( run == oldRun )
      replacementAccepted = runtime.submitPrompt( newRun, QStringLiteral( "Synthetic replacement" ) );
  } );
  // This observer runs before the recorder below. Cancellation settles the
  // old run synchronously; the settled consumer accepts a replacement before
  // usageUpdated finishes dispatching to its remaining direct consumers.
  QObject::connect( &runtime, &NativeConversationRuntime::usageUpdated,
                    [&]( const QString &run, const QJsonObject & ) {
    firstObserverRuns.append( run );
    if ( run == oldRun && !cancelledFromUsage )
    {
      cancelledFromUsage = true;
      runtime.cancelRun();
    }
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::usageUpdated,
                    [&]( const QString &run, const QJsonObject & ) { secondObserverRuns.append( run ); } );
  runtime.startSession( server.config() );
  REQUIRE( runtime.submitPrompt( oldRun, QStringLiteral( "Synthetic old run" ) ) );
  REQUIRE( until( [&] { return terminals.contains( newRun ); } ) );
  CHECK( cancelledFromUsage );
  CHECK( replacementAccepted );
  REQUIRE( terminals.size() == 2 );
  CHECK( terminals.value( oldRun ).first );
  CHECK( terminals.value( oldRun ).second.isEmpty() );
  CHECK_FALSE( terminals.value( newRun ).first );
  CHECK( terminals.value( newRun ).second.isEmpty() );
  const QStringList expectedRuns{ oldRun, newRun };
  CHECK( firstObserverRuns == expectedRuns );
  CHECK( secondObserverRuns == expectedRuns );
  CHECK( server.requests.size() == 2 );
}

TEST_CASE( "Native streaming usage opt in is fixed at accepted run start",
           "[agent][runtime][native][usage][opt-in]" )
{
  application();
  const NativeUsageTraceScope tracing( false );
  ModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.answer = []( int number ) {
    if ( number == 2 )
      return response( { { QStringLiteral( "tool_calls" ), QJsonArray{ tool( 0, QStringLiteral( "toggle-mid-run" ) ) } } }, QStringLiteral( "tool_calls" ) );
    return response( { { QStringLiteral( "content" ), QStringLiteral( "Completed request %1" ).arg( number ) } } );
  };
  NativeConversationRuntime runtime;
  int terminals = 0, tools = 0;
  QObject::connect( &runtime, &NativeConversationRuntime::settled,
                    [&]( const QString &, bool aborted, const QString &error ) {
    ++terminals; CHECK_FALSE( aborted ); CHECK( error.isEmpty() );
  } );
  QObject::connect( &runtime, &NativeConversationRuntime::toolCallRequested,
                    [&]( const QString &run, const QJsonObject &call, const QString & ) {
    ++tools;
    CHECK( run == QStringLiteral( "trace-on" ) );
    CHECK( trace::Trace::enabled() );
    // Changing the diagnostic sink while awaiting a tool must not change the
    // request contract which this accepted run already selected.
    trace::Trace::install( nullptr );
    CHECK( runtime.completeTool( run, call.value( QStringLiteral( "id" ) ).toString(),
      { { QStringLiteral( "status" ), QStringLiteral( "success" ) } } ) );
  } );
  runtime.startSession( server.config() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "trace-off" ), QStringLiteral( "Synthetic tracing off" ) ) );
  REQUIRE( until( [&] { return terminals == 1; } ) );
  trace::Trace::install( tracing.sink );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "trace-on" ), QStringLiteral( "Synthetic tracing on" ) ) );
  REQUIRE( until( [&] { return terminals == 2; } ) );
  CHECK( tools == 1 );
  REQUIRE( server.requests.size() == 3 );
  CHECK_FALSE( server.requests.at( 0 ).contains( QStringLiteral( "stream_options" ) ) );
  const QJsonObject expectedOptions{ { QStringLiteral( "include_usage" ), true } };
  CHECK( server.requests.at( 1 ).value( QStringLiteral( "stream_options" ) ).toObject() == expectedOptions );
  CHECK( server.requests.at( 2 ).value( QStringLiteral( "stream_options" ) ).toObject() == expectedOptions );
  CHECK_FALSE( trace::Trace::enabled() );
}
