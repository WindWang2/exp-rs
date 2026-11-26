#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <catch2/catch_test_macros.hpp>

#include "agent/runtime/pi_conversation_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <memory>

using sicnu::agent::runtime::AgentConversationRuntime;
using sicnu::agent::runtime::ConversationConfig;
using sicnu::agent::runtime::PiConversationRuntime;

namespace
{
void application()
{
  if ( QCoreApplication::instance() ) return;
  static int argc = 1;
  static char name[] = "test_pi_conversation_runtime";
  static char *argv[] = { name, nullptr };
  new QCoreApplication( argc, argv );
}

bool until( const std::function<bool()> &predicate, int timeout = 130000 )
{
  QElapsedTimer timer;
  timer.start();
  while ( !predicate() && timer.elapsed() < timeout )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }
  return predicate();
}

QJsonObject tool( const QString &name = QStringLiteral( "masked_ndvi_tool" ),
                  const QString &canonical = QStringLiteral( "python:run_script" ) )
{
  const QJsonObject parameters{
    { QStringLiteral( "type" ), QStringLiteral( "object" ) },
    { QStringLiteral( "properties" ), QJsonObject{
      { QStringLiteral( "threshold" ), QJsonObject{ { QStringLiteral( "type" ), QStringLiteral( "number" ) } } } } }
  };
  return { { QStringLiteral( "name" ), name }, { QStringLiteral( "canonicalId" ), canonical },
    { QStringLiteral( "description" ), QStringLiteral( "Local adapter fixture only" ) },
    { QStringLiteral( "parameters" ), parameters } };
}

ConversationConfig config()
{
  ConversationConfig value;
  value.sessionId = QStringLiteral( "adapter-fixture-session" );
  value.profile.baseUrl = QStringLiteral( "https://model.invalid/v1" );
  value.profile.modelName = QStringLiteral( "adapter-fixture" );
  value.profile.apiKey = QStringLiteral( "adapter-fixture-key" );
  value.systemPrompt = QStringLiteral( "Use only the explicitly supplied Qt host tools." );
  value.toolRevision = QStringLiteral( "catalog-native-safe-v1" );
  value.tools = { tool() };
  return value;
}

QByteArray sse( const QJsonObject &delta, const QJsonValue &finish = QJsonValue::Null )
{
  const QJsonObject event{ { QStringLiteral( "id" ), QStringLiteral( "adapter-sse" ) },
    { QStringLiteral( "object" ), QStringLiteral( "chat.completion.chunk" ) }, { QStringLiteral( "model" ), QStringLiteral( "adapter-fixture" ) },
    { QStringLiteral( "choices" ), QJsonArray{ QJsonObject{ { QStringLiteral( "index" ), 0 }, { QStringLiteral( "delta" ), delta },
        { QStringLiteral( "finish_reason" ), finish } } } } };
  return QByteArray( "data: " ) + QJsonDocument( event ).toJson( QJsonDocument::Compact ) + "\n\n";
}

/// HTTP fixture lives in this Qt event loop. It never contacts another host.
class LoopModel
{
  public:
    QTcpServer server;
    QList<QJsonObject> requests;
    bool invalidRequest = false;
    bool authenticated = true;

    LoopModel()
    {
      REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
      QObject::connect( &server, &QTcpServer::newConnection, &server, [this] {
        while ( server.hasPendingConnections() )
        {
          QTcpSocket *socket = server.nextPendingConnection();
          struct Request { QByteArray bytes; bool complete = false; };
          const auto request = std::make_shared<Request>();
          QObject::connect( socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater );
          QObject::connect( socket, &QTcpSocket::readyRead, socket, [this, socket, request] {
            if ( request->complete ) { socket->readAll(); return; }
            request->bytes += socket->readAll();
            if ( request->bytes.size() > 1024 * 1024 ) { invalidRequest = true; socket->abort(); return; }
            const qsizetype split = request->bytes.indexOf( "\r\n\r\n" );
            if ( split < 0 ) return;
            const QByteArray headers = request->bytes.left( split ).toLower();
            qint64 length = -1;
            for ( const QByteArray &line : headers.split( '\n' ) )
              if ( line.startsWith( "content-length:" ) ) length = line.mid( 15 ).trimmed().toLongLong();
            if ( length < 0 || length > 1024 * 1024 ) { invalidRequest = true; socket->abort(); return; }
            if ( request->bytes.size() - split - 4 < length ) return;
            request->complete = true;
            authenticated &= headers.contains( "authorization: bearer adapter-fixture-key" );
            const auto document = QJsonDocument::fromJson( request->bytes.mid( split + 4, length ) );
            if ( !document.isObject() ) { invalidRequest = true; socket->abort(); return; }
            requests.append( document.object() );
            respond( socket, requests.size() );
          } );
        }
      } );
    }

    QString baseUrl() const { return QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( server.serverPort() ); }

  private:
    void respond( QTcpSocket *socket, qsizetype index )
    {
      socket->write( "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n" );
      socket->write( sse( { { QStringLiteral( "role" ), QStringLiteral( "assistant" ) }, { QStringLiteral( "content" ), QString() } } ) );
      if ( index == 1 || index == 4 )
      {
        const auto tools = requests.last().value( QStringLiteral( "tools" ) ).toArray();
        if ( tools.isEmpty() ) { invalidRequest = true; socket->abort(); return; }
        const QString name = tools.first().toObject()
          .value( QStringLiteral( "function" ) ).toObject().value( QStringLiteral( "name" ) ).toString();
        socket->write( sse( { { QStringLiteral( "reasoning_content" ), QStringLiteral( "Fixture reasoning." ) } } ) );
        const QJsonObject call{ { QStringLiteral( "index" ), 0 }, { QStringLiteral( "id" ), QStringLiteral( "adapter-call-%1" ).arg( index ) },
          { QStringLiteral( "type" ), QStringLiteral( "function" ) }, { QStringLiteral( "function" ), QJsonObject{
            { QStringLiteral( "name" ), name }, { QStringLiteral( "arguments" ), QStringLiteral( "{\"threshold\":0.2}" ) } } } };
        socket->write( sse( { { QStringLiteral( "tool_calls" ), QJsonArray{ call } } } ) );
        socket->write( sse( {}, QStringLiteral( "tool_calls" ) ) );
      }
      else if ( index == 2 )
      {
        socket->write( sse( { { QStringLiteral( "content" ), QStringLiteral( "Committed fixture asset delivered." ) } } ) );
        socket->write( sse( {}, QStringLiteral( "stop" ) ) );
      }
      else if ( index == 3 )
      {
        socket->write( sse( { { QStringLiteral( "content" ), QStringLiteral( "Streaming fixture awaiting cancellation." ) } } ) );
        return; // Deliberately keep the model stream open until Qt cancels.
      }
      else invalidRequest = true;
      socket->write( "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":30,\"completion_tokens\":20,\"total_tokens\":50}}\n\ndata: [DONE]\n\n" );
      socket->disconnectFromHost();
    }
};
} // namespace

TEST_CASE( "Pi conversation rejects ambiguous or unsafe host tool snapshots", "[agent][pi][conversation]" )
{
  application();
  QTemporaryDir prefix;
  REQUIRE( prefix.isValid() );
  auto value = config();
  SECTION( "Duplicate provider name has distinct canonical identities" )
  { value.tools = { tool( QStringLiteral( "same_name" ), QStringLiteral( "python:first" ) ), tool( QStringLiteral( "same_name" ), QStringLiteral( "python:second" ) ) }; }
  SECTION( "Duplicate canonical identity has distinct provider names" )
  { value.tools = { tool( QStringLiteral( "first" ) ), tool( QStringLiteral( "second" ) ) }; }
  SECTION( "Unsafe name is refused rather than independently sanitized" )
  { value.tools = { tool( QStringLiteral( "python:run_script" ) ) }; }
  SECTION( "Names longer than the provider limit are refused" )
  { value.tools = { tool( QString( 65, QLatin1Char( 'x' ) ) ) }; }
  SECTION( "Default upstream tool names remain unavailable" )
  { value.tools = { tool( QStringLiteral( "bash" ) ) }; }
  PiConversationRuntime runtime( prefix.path() );
  QString refusal, error;
  int terminal = 0;
  QObject::connect( &runtime, &AgentConversationRuntime::statusChanged, [&]( const QString &, const QString &code ) { refusal = code; } );
  QObject::connect( &runtime, &AgentConversationRuntime::settled, [&]( const QString &run, bool aborted, const QString &message ) {
    CHECK( run.isEmpty() ); CHECK( aborted ); ++terminal; error = message;
  } );
  runtime.startSession( value );
  CHECK( terminal == 1 );
  CHECK( refusal == QStringLiteral( "invalid_tool_catalog" ) );
  CHECK_FALSE( error.contains( value.profile.apiKey ) );
  CHECK_FALSE( runtime.isReady() );
  CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "run" ), QStringLiteral( "No startup on an invalid catalog" ) ) );
}

TEST_CASE( "Pi conversation requires an absolute installation and cancels preparation", "[agent][pi][conversation]" )
{
  application();
  SECTION( "No ambient Node fallback" )
  {
    PiConversationRuntime runtime( QStringLiteral( "relative-prefix" ) );
    QString refusal;
    QObject::connect( &runtime, &AgentConversationRuntime::statusChanged, [&]( const QString &, const QString &code ) { refusal = code; } );
    runtime.startSession( config() );
    CHECK( refusal == QStringLiteral( "invalid_runtime_prefix" ) );
    CHECK_FALSE( runtime.isReady() );
  }
  SECTION( "Stop before verified startup prevents late readiness or a prompt" )
  {
    QTemporaryDir prefix;
    REQUIRE( prefix.isValid() );
    PiConversationRuntime runtime( prefix.path() );
    int terminal = 0, ready = 0;
    QObject::connect( &runtime, &AgentConversationRuntime::ready, [&] { ++ready; } );
    QObject::connect( &runtime, &AgentConversationRuntime::settled, [&]( const QString &run, bool aborted, const QString &error ) {
      CHECK( run.isEmpty() ); CHECK( aborted ); CHECK( error.isEmpty() ); ++terminal;
    } );
    runtime.startSession( config() );
    runtime.cancelRun();
    CHECK( terminal == 1 );
    CHECK( ready == 0 );
    CHECK_FALSE( runtime.isReady() );
    CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "late-run" ), QStringLiteral( "Must not replay" ) ) );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    CHECK( ready == 0 );
    CHECK( terminal == 1 );
  }
}

TEST_CASE( "Pi conversation installed SDK routes a local model loop and cancels only its conversation", "[agent][pi][conversation][installed]" )
{
  application();
  // This is required proof, not an optional skip or a substituted global Node.
  const QString prefix = qEnvironmentVariable( "SICNU_TEST_PI_PRODUCT_PREFIX" );
  if ( prefix.isEmpty() )
  {
    if ( qEnvironmentVariableIntValue( "SICNU_TEST_REQUIRE_PI" ) == 1 )
      FAIL( "The required installed Pi proof has no explicit product prefix." );
    SKIP( "Installed Pi integration requires SICNU_TEST_PI_PRODUCT_PREFIX; no global runtime fallback." );
  }
  REQUIRE( QDir::isAbsolutePath( prefix ) );
  QFile receipt( QDir( prefix ).filePath( QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" ) ) );
  REQUIRE( receipt.open( QIODevice::ReadOnly ) );
  REQUIRE( QJsonDocument::fromJson( receipt.readAll() ).object().value( QStringLiteral( "files" ) ).toArray().size() > 10000 );
  LoopModel model;
  PiConversationRuntime runtime( prefix );
  auto value = config();
  value.profile.baseUrl = model.baseUrl();
  int ready = 0, calls = 0;
  QString content, reasoning, pendingCall;
  QStringList responseRuns, cancelledRuns, statuses, acceptedRuns, modelRequests;
  QList<QJsonObject> usage;
  QHash<QString, QPair<bool, QString>> terminal;
  bool nativeCompleted = false, heldNativeStillRunning = false;
  QObject::connect( &runtime, &AgentConversationRuntime::ready, [&] { ++ready; } );
  QObject::connect( &runtime, &AgentConversationRuntime::runStarted, [&]( const QString &run ) { acceptedRuns.append( run ); } );
  QObject::connect( &runtime, &AgentConversationRuntime::modelRequestStarted, [&]( const QString &run ) { modelRequests.append( run ); } );
  QObject::connect( &runtime, &AgentConversationRuntime::responseStarted, [&]( const QString &run ) { responseRuns.append( run ); } );
  QObject::connect( &runtime, &AgentConversationRuntime::contentDelta, [&]( const QString &, const QString &text ) { content += text; } );
  QObject::connect( &runtime, &AgentConversationRuntime::reasoningDelta, [&]( const QString &, const QString &text ) { reasoning += text; } );
  QObject::connect( &runtime, &AgentConversationRuntime::usageUpdated, [&]( const QString &, const QJsonObject &event ) { usage.append( event ); } );
  QObject::connect( &runtime, &AgentConversationRuntime::statusChanged, [&]( const QString &, const QString &code ) { statuses.append( code ); } );
  QObject::connect( &runtime, &AgentConversationRuntime::cancelOwnedToolsRequested, [&]( const QString &run ) { cancelledRuns.append( run ); } );
  QObject::connect( &runtime, &AgentConversationRuntime::settled, [&]( const QString &run, bool aborted, const QString &error ) {
    CHECK_FALSE( terminal.contains( run ) );
    CHECK_FALSE( error.contains( value.profile.apiKey ) );
    terminal.insert( run, { aborted, error } );
  } );
  QObject::connect( &runtime, &AgentConversationRuntime::toolCallRequested, [&]( const QString &run, const QJsonObject &call, const QString &revision ) {
    ++calls;
    CHECK( revision == value.toolRevision );
    CHECK( call.value( QStringLiteral( "type" ) ).toString() == QStringLiteral( "function" ) );
    const auto function = call.value( QStringLiteral( "function" ) ).toObject();
    CHECK( function.value( QStringLiteral( "name" ) ).toString() == QStringLiteral( "python:run_script" ) );
    CHECK( QJsonDocument::fromJson( function.value( QStringLiteral( "arguments" ) ).toString().toUtf8() ).object()
      .value( QStringLiteral( "threshold" ) ).toDouble() == 0.2 );
    pendingCall = call.value( QStringLiteral( "id" ) ).toString();
    CHECK_FALSE( runtime.completeTool( QStringLiteral( "foreign-run" ), pendingCall, {} ) );
    if ( run == QLatin1String( "committed-run" ) )
    {
      CHECK_FALSE( runtime.isReady() );
      nativeCompleted = true; // The external host, not Pi, decides this point.
      CHECK( runtime.completeTool( run, pendingCall, { { QStringLiteral( "status" ), QStringLiteral( "completed" ) },
        { QStringLiteral( "assetId" ), QStringLiteral( "fixture-committed-asset" ) }, { QStringLiteral( "execution_id" ), QStringLiteral( "task-42" ) } } ) );
      CHECK_FALSE( runtime.completeTool( run, pendingCall, {} ) );
    }
    else heldNativeStillRunning = true;
  } );
  CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "before-ready" ), QStringLiteral( "Do not submit before verification" ) ) );
  runtime.startSession( value );
  REQUIRE( until( [&] { return ready || !terminal.isEmpty(); }, 15 * 60 * 1000 + 130000 ) );
  REQUIRE( ready == 1 );
  REQUIRE( terminal.isEmpty() );
  REQUIRE( runtime.isReady() );
  runtime.startSession( value );
  CHECK( statuses.contains( QStringLiteral( "session_busy" ) ) );
  CHECK( runtime.isReady() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "committed-run" ), QStringLiteral( "Call the fixture host tool and report its committed result." ) ) );
  REQUIRE( until( [&] { return terminal.contains( QStringLiteral( "committed-run" ) ); } ) );
  CHECK_FALSE( terminal.value( QStringLiteral( "committed-run" ) ).first );
  CHECK( terminal.value( QStringLiteral( "committed-run" ) ).second.isEmpty() );
  CHECK( nativeCompleted );
  CHECK( content.contains( QStringLiteral( "Committed fixture asset delivered." ) ) );
  CHECK( reasoning.contains( QStringLiteral( "Fixture reasoning." ) ) );
  CHECK( calls == 1 );
  CHECK( responseRuns.count( QStringLiteral( "committed-run" ) ) == 2 );
  CHECK( acceptedRuns.count( QStringLiteral( "committed-run" ) ) == 1 );
  CHECK( modelRequests.count( QStringLiteral( "committed-run" ) ) == 2 );
  REQUIRE( usage.size() == 2 );
  CHECK( usage.last().value( QStringLiteral( "usage" ) ).toObject().value( QStringLiteral( "totalTokens" ) ).toInt() == 50 );
  CHECK( usage.last().value( QStringLiteral( "cumulativeUsage" ) ).toObject().value( QStringLiteral( "totalTokens" ) ).toInt() == 100 );
  REQUIRE( model.requests.size() == 2 );
  const auto advertisedTools = model.requests.first().value( QStringLiteral( "tools" ) ).toArray();
  REQUIRE( advertisedTools.size() == 1 );
  const auto firstFunction = advertisedTools.first().toObject().value( QStringLiteral( "function" ) ).toObject();
  CHECK( firstFunction.value( QStringLiteral( "name" ) ).toString() == QStringLiteral( "masked_ndvi_tool" ) );
  CHECK( model.requests.first().value( QStringLiteral( "max_tokens" ) ).toInt() == 4096 );
  bool delivered = false;
  for ( const auto &message : model.requests.last().value( QStringLiteral( "messages" ) ).toArray() )
    delivered |= message.toObject().value( QStringLiteral( "role" ) ).toString() == QLatin1String( "tool" )
      && QJsonDocument( message.toObject() ).toJson( QJsonDocument::Compact ).contains( "fixture-committed-asset" );
  CHECK( delivered );
  CHECK( runtime.isReady() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "cancel-run" ), QStringLiteral( "Stream until stopped." ) ) );
  REQUIRE( until( [&] { return content.contains( QStringLiteral( "awaiting cancellation" ) ); } ) );
  runtime.cancelRun();
  REQUIRE( until( [&] { return terminal.contains( QStringLiteral( "cancel-run" ) ); } ) );
  CHECK( terminal.value( QStringLiteral( "cancel-run" ) ).first );
  CHECK( cancelledRuns.count( QStringLiteral( "cancel-run" ) ) == 1 );
  REQUIRE( model.requests.size() == 3 );
  CHECK( acceptedRuns.count( QStringLiteral( "cancel-run" ) ) == 1 );
  CHECK( modelRequests.count( QStringLiteral( "cancel-run" ) ) == 1 );
  REQUIRE( runtime.isReady() );
  REQUIRE( runtime.submitPrompt( QStringLiteral( "close-run" ), QStringLiteral( "Request a host tool, then close the conversation." ) ) );
  REQUIRE( until( [&] { return heldNativeStillRunning; } ) );
  runtime.closeSession();
  REQUIRE( until( [&] { return terminal.contains( QStringLiteral( "close-run" ) ) && statuses.contains( QStringLiteral( "closed" ) ); }, 10000 ) );
  CHECK( terminal.value( QStringLiteral( "close-run" ) ).first );
  CHECK( cancelledRuns.count( QStringLiteral( "close-run" ) ) == 1 );
  CHECK( heldNativeStillRunning ); // A Pi terminal event cannot complete a native task.
  CHECK_FALSE( runtime.completeTool( QStringLiteral( "close-run" ), pendingCall, {} ) );
  CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "replay" ), QStringLiteral( "Never restart implicitly." ) ) );
  CHECK( model.requests.size() == 4 );
  CHECK( acceptedRuns.count( QStringLiteral( "close-run" ) ) == 1 );
  CHECK( modelRequests.count( QStringLiteral( "close-run" ) ) == 1 );
  CHECK_FALSE( acceptedRuns.contains( QStringLiteral( "before-ready" ) ) );
  CHECK_FALSE( acceptedRuns.contains( QStringLiteral( "replay" ) ) );
  CHECK( model.authenticated );
  CHECK_FALSE( model.invalidRequest );
}
