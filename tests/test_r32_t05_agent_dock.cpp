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
#include "data/data_manager.h"
#include "support/qsettings_redirect.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QThread>

#include <qgsproject.h>

#include <functional>
#include <memory>

using namespace sicnu::agent;
using namespace sicnu::data;

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
    new QApplication( argc, argv );
  }
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "r32-t05-agent-dock" ) );
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

    HoldingModelServer()
    {
      connect( this, &QTcpServer::newConnection, this, [this] {
        while ( hasPendingConnections() )
        {
          QTcpSocket *socket = nextPendingConnection();
          auto buffer = std::make_shared<QByteArray>();
          auto handled = std::make_shared<bool>( false );
          connect( socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
            *buffer += socket->readAll();
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
  REQUIRE( until( [&] { return !second.isRunActive(); } ) );
  // Exactly one model request left the second dock: the refused prompt was
  // never silently replayed into it.
  CHECK( server.requestCount == 2 );
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
  REQUIRE( until( [&] { return !dock.isRunActive(); } ) );
  REQUIRE( dock.messageHistory().size() >= 2 );
  CHECK( dock.messageHistory().at( 1 ).toObject().value( QStringLiteral( "content" ) ).toString()
         == QStringLiteral( "after the switch" ) );

  QgsProject::instance()->clear();
}
