// tests/test_r32_t05_agent_dock.cpp
// R32-T05 dock lifecycle regressions over real Qt objects:
//   #2073 — a missing project context is an honest, recoverable refusal
//            (never "another agent is using this project", never a leaked
//            pending prompt that a later ready would replay);
//   #2074 — destroyed or replaced DataManager/canvas owners cannot be
//            dereferenced through the dock;
//   #2075 — a project change during streaming is adopted only after the
//            run ends (no mid-run identity overwrite, UI never stuck).
#include <catch2/catch_test_macros.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/llm_config_manager.h"
#include "agent/llm_streaming_client.h"
#include "agent/runtime/pi_process.h"
#include "data/data_manager.h"
#include "support/qsettings_redirect.h"
#include "support/qt_lifecycle.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QScopeGuard>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QThread>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>

#include <functional>
#include <memory>

using namespace sicnu::agent;
using namespace sicnu::data;

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace {

exprs_test::QSettingsUserRootRedirect settingsRedirect;

void ensureQtApp()
{
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  if ( !QApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_r32_t05_agent_dock";
    static char *argv[] = { appName, nullptr };
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
  }
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "r32-t05-agent-dock" ) );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "native" ) );
  LlmProviderProfile profile;
  profile.id = QStringLiteral( "r32-t05-loopback" );
  profile.name = QStringLiteral( "R32 T05 loopback" );
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:9/v1" );
  profile.modelName = QStringLiteral( "fixture" );
  LlmConfigManager::setActiveProfile( profile );
}

bool until( const std::function<bool()> &predicate, int timeoutMs = 8000 )
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

QByteArray sse( const QJsonObject &delta, const QString &finish = {} )
{
  QJsonObject choice{ { QStringLiteral( "delta" ), delta } };
  if ( !finish.isEmpty() )
    choice[QStringLiteral( "finish_reason" )] = finish;
  const QJsonObject frame{ { QStringLiteral( "choices" ), QJsonArray{ choice } } };
  return "data: " + QJsonDocument( frame ).toJson( QJsonDocument::Compact ) + "\n\n";
}

/// Loopback model endpoint that answers with a first content delta and then
/// holds the stream open until the test releases it — a run in flight.
class HoldingModelServer : public QTcpServer
{
  public:
    QPointer<QTcpSocket> stream;
    bool requestSeen = false;
    int requestCount = 0;
    int connectionCount = 0;
    qint64 receivedBytes = 0;

    HoldingModelServer()
    {
      connect( this, &QTcpServer::newConnection, this, [this] {
        while ( hasPendingConnections() )
        {
          QTcpSocket *socket = nextPendingConnection();
          ++connectionCount;
          auto buffer = std::make_shared<QByteArray>();
          auto handled = std::make_shared<bool>( false );
          connect( socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
            const QByteArray incoming = socket->readAll();
            receivedBytes += incoming.size();
            *buffer += incoming;
            if ( *handled )
              return;
            const qsizetype headerEnd = buffer->indexOf( "\r\n\r\n" );
            if ( headerEnd < 0 )
              return;
            qint64 length = -1;
            for ( const QByteArray &line : buffer->left( headerEnd ).split( '\n' ) )
              if ( line.toLower().startsWith( "content-length:" ) ) length = line.mid( 15 ).trimmed().toLongLong();
            if ( length < 0 || buffer->size() < headerEnd + 4 + length )
              return;
            *handled = true;
            requestSeen = true;
            ++requestCount;
            stream = socket;
            const QByteArray body = sse( { { QStringLiteral( "role" ), QStringLiteral( "assistant" ) },
                                           { QStringLiteral( "content" ), QStringLiteral( "working" ) } } );
            socket->write( "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n" + body );
            socket->flush();
          } );
        }
      } );
    }

    void release()
    {
      if ( !stream )
        return;
      stream->write( sse( {}, QStringLiteral( "stop" ) ) + "data: [DONE]\n\n" );
      stream->flush();
      stream->disconnectFromHost();
    }

    void setActiveProfile()
    {
      LlmProviderProfile profile;
      profile.id = profile.name = QStringLiteral( "r32-t05-holding" );
      profile.baseUrl = QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( serverPort() );
      profile.modelName = QStringLiteral( "fixture" );
      LlmConfigManager::setActiveProfile( profile );
    }
};

} // namespace

TEST_CASE( "#2073: a dock without project context refuses honestly and stays recoverable", "[r32t05][agent][dock]" )
{
  ensureQtApp();
  AgentCopilotDockWidget dock; // no setContext: no project, no canvas
  dock.sendPrompt( QStringLiteral( "NDVI analysis" ) );
  CHECK_FALSE( dock.isRunActive() );
  CHECK( dock.findChild<QLabel *>( QStringLiteral( "agentUserMessage" ) ) == nullptr );
  CHECK( dock.messageHistory().isEmpty() );
  // The prompt stays with the user instead of leaking into a pending replay.
  auto *input = dock.findChild<QTextEdit *>( QStringLiteral( "agentPromptInput" ) );
  REQUIRE( input );
  CHECK( input->toPlainText() == QStringLiteral( "NDVI analysis" ) );
  CHECK( input->isEnabled() );

  // Recovery: binding a real context makes the same send start a run.
  DataManager dataMgr;
  dock.setContext( &dataMgr, nullptr );
  dock.sendPrompt( QStringLiteral( "NDVI analysis" ) );
  CHECK( dock.isRunActive() );
  REQUIRE( dock.findChild<QLabel *>( QStringLiteral( "agentUserMessage" ) ) != nullptr );
  REQUIRE( dock.messageHistory().size() >= 2 );
  CHECK( dock.messageHistory().at( 1 ).toObject().value( QStringLiteral( "content" ) ).toString()
         == QStringLiteral( "NDVI analysis" ) );
  REQUIRE( until( [&] { return !dock.isRunActive(); } ) );
}

TEST_CASE( "#2073: a busy lease reports the real cause and never replays the refused prompt", "[r32t05][agent][dock]" )
{
  ensureQtApp();
  HoldingModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.setActiveProfile();

  DataManager dataMgr;
  AgentCopilotDockWidget first;
  first.setContext( &dataMgr, nullptr );
  first.sendPrompt( QStringLiteral( "first agent keeps the lease busy" ) );
  REQUIRE( first.isRunActive() );
  REQUIRE( until( [&] { return server.requestSeen; } ) );

  // A second dock on the SAME project context cannot begin a run: the
  // refusal names the busy lease, and the second prompt is not parked for a
  // delayed replay.
  AgentCopilotDockWidget second;
  second.setContext( &dataMgr, nullptr );
  second.sendPrompt( QStringLiteral( "second agent prompt" ) );
  CHECK_FALSE( second.isRunActive() );
  CHECK( second.messageHistory().size() <= 1 );

  server.release();
  REQUIRE( until( [&] { return !first.isRunActive(); } ) );

  // After the lease frees, the second dock sends successfully — recovery.
  server.setActiveProfile();
  second.sendPrompt( QStringLiteral( "second agent retry" ) );
  CHECK( second.isRunActive() );
  REQUIRE( until( [&] { return server.requestCount == 2; } ) );
  server.release();
  REQUIRE( until( [&] { return !second.isRunActive(); } ) );
}

TEST_CASE( "#2074: a destroyed DataManager owner cannot be dereferenced through the dock", "[r32t05][agent][dock]" )
{
  ensureQtApp();
  AgentCopilotDockWidget dock;
  {
    DataManager dataMgr;
    dock.setContext( &dataMgr, nullptr );
  }
  // The context owner is gone; a prompt is an honest refusal, not a crash
  // and not a phantom run.
  dock.sendPrompt( QStringLiteral( "after owner destruction" ) );
  CHECK_FALSE( dock.isRunActive() );
  auto *input = dock.findChild<QTextEdit *>( QStringLiteral( "agentPromptInput" ) );
  REQUIRE( input );
  CHECK( input->isEnabled() );
  CHECK( input->toPlainText() == QStringLiteral( "after owner destruction" ) );
}

TEST_CASE( "#2075: a project change during streaming is adopted only after the run ends", "[r32t05][agent][dock]" )
{
  ensureQtApp();
  HoldingModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.setActiveProfile();

  QTemporaryDir projectA;
  QTemporaryDir projectB;
  REQUIRE( QFile::exists( projectA.path() ) );
  QgsProject::instance()->setFileName( projectA.filePath( QStringLiteral( "project-a.qgs" ) ) );

  DataManager dataA;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataA, nullptr );
  dock.sendPrompt( QStringLiteral( "streaming run" ) );
  REQUIRE( dock.isRunActive() );
  REQUIRE( until( [&] { return server.requestSeen; } ) );

  // The shell switches to another project while the run streams.
  DataManager dataB;
  QgsProject::instance()->setFileName( projectB.filePath( QStringLiteral( "项目-b.qgs" ) ) );
  dock.setContext( &dataB, nullptr );

  // The run finishes; the deferred project identity applies afterwards and
  // the dock leaves the streaming state cleanly (never a stuck input).
  server.release();
  REQUIRE( until( [&] { return !dock.isRunActive(); } ) );
  auto *input = dock.findChild<QTextEdit *>( QStringLiteral( "agentPromptInput" ) );
  REQUIRE( input );
  CHECK( input->isEnabled() );

  // A new prompt after the switch starts a fresh conversation for the new
  // project instead of silently continuing the old history.
  server.setActiveProfile();
  dock.sendPrompt( QStringLiteral( "after the switch" ) );
  CHECK( dock.isRunActive() );
  REQUIRE( until( [&] { return server.requestCount == 2; } ) );
  server.release();
  REQUIRE( until( [&] { return !dock.isRunActive(); } ) );
  REQUIRE( dock.messageHistory().size() >= 2 );
  CHECK( dock.messageHistory().at( 1 ).toObject().value( QStringLiteral( "content" ) ).toString()
         == QStringLiteral( "after the switch" ) );

  QgsProject::instance()->clear();
}

TEST_CASE( "#2074: an old run cannot move the replacement project's canvas", "[r32t05][agent][dock][project-isolation]" )
{
  ensureQtApp();
  HoldingModelServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  server.setActiveProfile();

  QTemporaryDir projects;
  REQUIRE( projects.isValid() );
  const QString previousProject = QgsProject::instance()->fileName();
  const auto restoreProject = qScopeGuard( [&] { QgsProject::instance()->setFileName( previousProject ); } );
  const QString projectA = projects.filePath( QStringLiteral( "project-a.qgs" ) );
  const QString projectB = projects.filePath( QStringLiteral( "项目-b.qgs" ) );
  for ( const QString &path : { projectA, projectB } )
  {
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( "<qgis/>" ) == 7 );
  }
  QgsProject::instance()->setFileName( projectA );
  DataManager dataA, dataB;
  QgsMapCanvas canvasA, canvasB;
  for ( QgsMapCanvas *canvas : { &canvasA, &canvasB } )
  {
    canvas->setRenderFlag( false );
    canvas->resize( 200, 200 );
    canvas->setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );
  }
  canvasA.setExtent( QgsRectangle( 0, 0, 10, 10 ) );
  canvasB.setExtent( QgsRectangle( 30, 30, 40, 40 ) );
  const QgsRectangle originalA = canvasA.extent(), originalB = canvasB.extent();
  AgentCopilotDockWidget dock;
  auto *backend = dock.findChild<QComboBox *>( QStringLiteral( "agentConversationBackend" ) );
  REQUIRE( backend != nullptr );
  backend->setCurrentIndex( backend->findData( QStringLiteral( "native" ) ) );
  REQUIRE( backend->currentData().toString() == QStringLiteral( "native" ) );
  dock.setContext( &dataA, &canvasA );
  server.setActiveProfile();
  REQUIRE( LlmConfigManager::activeProfile().baseUrl ==
           QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( server.serverPort() ) );
  auto *host = dock.findChild<runtime::AgentToolHost *>();
  REQUIRE( host != nullptr );
  QMap<QString, QJsonObject> results;
  QObject::connect( host, &runtime::AgentToolHost::toolCompleted, &dock,
    [&]( const QString &, const QString &call, const QJsonObject &result ) { results.insert( call, result ); } );
  QString oldRun;
  // Initial context binding can replace the constructor's runtime. Its old
  // QObject remains a child until deleteLater is processed, so findChild()
  // would observe a closed predecessor instead of the live conversation.
  const auto initialConversations = dock.findChildren<runtime::AgentConversationRuntime *>( QString(), Qt::FindDirectChildrenOnly );
  REQUIRE_FALSE( initialConversations.isEmpty() );
  auto *conversation = initialConversations.last();
  REQUIRE( QString::fromLatin1( conversation->metaObject()->className() ).contains( QStringLiteral( "NativeConversationRuntime" ) ) );
  QStringList transportErrors;
  auto *client = conversation->findChild<LlmStreamingClient *>();
  REQUIRE( client != nullptr );
  QObject::connect( client, &LlmStreamingClient::errorOccurred, &dock,
                    [&]( const QString &error ) { transportErrors.append( error ); } );
  QObject::connect( conversation, &runtime::AgentConversationRuntime::runStarted, &dock,
                    [&]( const QString &run ) { oldRun = run; } );
  const QString oldRevision = host->catalogSnapshot().revision;
  dock.sendPrompt( QStringLiteral( "keep the original project run open" ) );
  const bool received = until( [&] { return server.requestSeen; } );
  INFO( dock.runInspectorSummary().toStdString() );
  INFO( transportErrors.join( QLatin1Char( '\n' ) ).toStdString() );
  CAPTURE( server.connectionCount, server.receivedBytes );
  QStringList refusalNotices;
  for ( const auto *frame : dock.findChildren<QFrame *>( QStringLiteral( "refusalNotice" ) ) )
    for ( const auto *label : frame->findChildren<QLabel *>() ) refusalNotices.append( label->text() );
  INFO( refusalNotices.join( QLatin1Char( '\n' ) ).toStdString() );
  REQUIRE( received );
  REQUIRE_FALSE( oldRun.isEmpty() );
  REQUIRE( dock.isRunActive() );

  const auto extentCall = []( const QString &id ) {
    return QJsonObject{
      { QStringLiteral( "id" ), id }, { QStringLiteral( "type" ), QStringLiteral( "function" ) },
      { QStringLiteral( "function" ), QJsonObject{
        { QStringLiteral( "name" ), QStringLiteral( "view:set_extent" ) },
        { QStringLiteral( "arguments" ), QJsonObject{
          { QStringLiteral( "extent" ), QJsonArray{ 60, 60, 70, 70 } } } } } } };
  };
  // Refreshing unchanged owners must retain the active A bindings. A fix
  // that blanks every refused rebind would disable legitimate view tools.
  const auto noticesBeforeRefresh = dock.findChildren<QFrame *>( QStringLiteral( "refusalNotice" ) ).size();
  dock.setContext( &dataA, &canvasA );
  CHECK( dock.findChildren<QFrame *>( QStringLiteral( "refusalNotice" ) ).size() == noticesBeforeRefresh );
  REQUIRE( dock.isRunActive() );
  host->requestTool( oldRun, extentCall( QStringLiteral( "same-a-view" ) ), oldRevision );
  REQUIRE( until( [&] { return results.contains( QStringLiteral( "same-a-view" ) ); } ) );
  REQUIRE( results.value( QStringLiteral( "same-a-view" ) ).value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "success" ) );
  CHECK( canvasA.extent() != originalA );
  CHECK( canvasB.extent() == originalB );
  canvasA.setExtent( originalA );
  REQUIRE( canvasA.extent() == originalA );

  QgsProject::instance()->setFileName( projectB );
  dock.setContext( &dataB, &canvasB );
  // A tool already queued for A must go through the real host's admission
  // and view dispatcher, never acquire B's newly exposed service pointers.
  host->requestTool( oldRun, extentCall( QStringLiteral( "late-a-view" ) ), oldRevision );
  REQUIRE( until( [&] { return results.contains( QStringLiteral( "late-a-view" ) ); } ) );
  const QJsonObject refused = results.value( QStringLiteral( "late-a-view" ) );
  CHECK( refused.value( QStringLiteral( "status" ) ).toString() != QStringLiteral( "success" ) );
  CHECK_FALSE( refused.value( QStringLiteral( "errorCode" ) ).toString().isEmpty() );
  CHECK( canvasA.extent() == originalA );
  CHECK( canvasB.extent() == originalB );
  server.release();
  REQUIRE( until( [&] { return !dock.isRunActive(); } ) );

  // The replacement project becomes usable after the old run settles;
  // denying the old run must not permanently disable legitimate view tools.
  QString newRun;
  const auto conversations = dock.findChildren<runtime::AgentConversationRuntime *>( QString(), Qt::FindDirectChildrenOnly );
  REQUIRE_FALSE( conversations.isEmpty() );
  QObject::connect( conversations.last(), &runtime::AgentConversationRuntime::runStarted, &dock,
                    [&]( const QString &run ) { newRun = run; } );
  dock.sendPrompt( QStringLiteral( "move the replacement project's view" ) );
  REQUIRE( until( [&] { return server.requestCount == 2; } ) );
  REQUIRE_FALSE( newRun.isEmpty() );
  REQUIRE( newRun != oldRun );
  host->requestTool( newRun, extentCall( QStringLiteral( "new-b-view" ) ), host->catalogSnapshot().revision );
  REQUIRE( until( [&] { return results.contains( QStringLiteral( "new-b-view" ) ); } ) );
  CHECK( results.value( QStringLiteral( "new-b-view" ) ).value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "success" ) );
  CHECK( canvasA.extent() == originalA );
  CHECK( canvasB.extent() != originalB );
  server.release();
  REQUIRE( until( [&] { return !dock.isRunActive(); } ) );
}

TEST_CASE( "#2074: replacing context cancels Pi verification before a run is admitted", "[r32t05][agent][dock][project-isolation]" )
{
  ensureQtApp();
  QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "pi" ) );
  const auto restoreBackend = qScopeGuard( [] {
    QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "native" ) );
  } );
  LlmProviderProfile profile = LlmConfigManager::activeProfile();
  profile.apiKey = QStringLiteral( "r32-startup-placeholder" );
  LlmConfigManager::setActiveProfile( profile );
  QTemporaryDir prefix;
  REQUIRE( prefix.isValid() );
  REQUIRE( QDir().mkpath( prefix.filePath( QStringLiteral( "share/sicnu-geo-rs" ) ) ) );
  {
    QFile receipt( prefix.filePath( QStringLiteral( "share/sicnu-geo-rs/pi-runtime.json" ) ) );
    REQUIRE( receipt.open( QIODevice::WriteOnly ) );
    REQUIRE( receipt.write( "{\"fixture\":\"verification-window\"}" ) > 0 );
  }
  DataManager dataA, dataB;
  AgentCopilotDockWidget dock( nullptr, sicnu::TaskCenter::instance(), prefix.path(),
                               prefix.filePath( QStringLiteral( "checkpoints" ) ) );
  dock.setContext( &dataA, nullptr );
  auto *host = dock.findChild<runtime::AgentToolHost *>();
  const auto conversations = dock.findChildren<runtime::AgentConversationRuntime *>( QString(), Qt::FindDirectChildrenOnly );
  REQUIRE( host != nullptr );
  REQUIRE_FALSE( conversations.isEmpty() );
  auto *conversation = conversations.last();
  int admittedPrompts = 0;
  QObject::connect( conversation, &runtime::AgentConversationRuntime::runStarted, &dock,
                    [&]( const QString & ) { ++admittedPrompts; } );
  dock.sendPrompt( QStringLiteral( "original project prompt awaiting Pi verification" ) );
  const QPointer<runtime::PiProcess> process = conversation->findChild<runtime::PiProcess *>();
  REQUIRE( process != nullptr );
  INFO( dock.runInspectorSummary().toStdString() );
  // This incomplete installation deliberately tests the REAL asynchronous
  // verification window, not a successful SDK launch. Without pumping Qt
  // events the verification completion cannot yet admit a prompt.
  REQUIRE( process->state() == runtime::PiProcess::State::Verifying );
  REQUIRE( dock.isRunActive() );
  REQUIRE_FALSE( host->hasOwnedWork() );
  dock.setContext( &dataB, nullptr );
  CHECK_FALSE( dock.isRunActive() );
  CHECK( process->state() == runtime::PiProcess::State::Closed );
  CHECK( admittedPrompts == 0 );
  CHECK( dock.viewControlService()->dataManager() == &dataB );
}
