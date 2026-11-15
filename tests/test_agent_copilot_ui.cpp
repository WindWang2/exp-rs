// tests/test_agent_copilot_ui.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/interaction_tool_registry.h"
#include "agent/llm_settings_dialog.h"
#include "agent/llm_streaming_client.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/view_control_service.h"
#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/task_center.h"
#include "support/qsettings_redirect.h"

#include <QApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QCheckBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>

#include <chrono>
#include <thread>
#include <vector>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QVariant>
#include <qgsproject.h>
#include <atomic>
#include <functional>

using namespace sicnu::agent;
using namespace sicnu::data;
using namespace sicnu::processing;

namespace {

exprs_test::QSettingsUserRootRedirect settingsRedirect;

bool writeTinyRaster( const QString &path, int width, int height, int bands )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDataset *ds = driver->Create( path.toUtf8().constData(), width, height, bands, GDT_Float32, nullptr );
  if ( !ds )
    return false;

  std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
  ds->SetGeoTransform( gt.data() );
  OGRSpatialReference srs;
  srs.importFromEPSG( 32648 );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  ds->SetProjection( wkt );
  CPLFree( wkt );

  for ( int b = 1; b <= bands; ++b )
  {
    GDALRasterBand *band = ds->GetRasterBand( b );
    std::vector<float> data( width * height, static_cast<float>( b ) );
    band->RasterIO( GF_Write, 0, 0, width, height, data.data(), width, height, GDT_Float32, 0, 0 );
  }
  GDALClose( ( GDALDatasetH )ds );
  return true;
}

} // namespace

static void ensureQtApp()
{
  // The GUI fixture must never open the real user's model credential store.
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  static int argc = 1;
  static char appName[] = "test_agent_copilot_ui";
  static char *argv[] = { appName, nullptr };
  // Heap-allocated and intentionally leaked: static QApplication would
  // destruct after Catch2 and trigger teardown races with QGIS singletons.
  if ( !QApplication::instance() )
    new QApplication( argc, argv );
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "agent-copilot-ui" ) );
  QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "native" ) );
  static const bool configured = [] {
    LlmProviderProfile profile;
    profile.id = QStringLiteral( "ui-test-loopback" );
    profile.name = QStringLiteral( "UI test loopback" );
    profile.baseUrl = QStringLiteral( "http://127.0.0.1:9/v1" );
    profile.modelName = QStringLiteral( "fixture" );
    LlmConfigManager::setActiveProfile( profile );
    return true;
  }();
  Q_UNUSED( configured )
}

namespace {

// Minimal stub adapter for registry-based tool-call lifecycle tests.
class LifecycleStubAdapter : public AtomicAlgorithmAdapter
{
  public:
    enum class Mode { SleepThenComplete, WriteOutput };

    LifecycleStubAdapter( std::string id, Mode mode, int sleepMs = 0, QString outputPath = {} )
      : mId( std::move( id ) )
      , mMode( mode )
      , mSleepMs( sleepMs )
      , mOutputPath( std::move( outputPath ) ) {}

    std::string algorithmId() const override { return mId; }
    AlgorithmDescriptor descriptor() const override
    {
      AlgorithmDescriptor result;
      result.id = mId;
      result.displayName = mId;
      result.description = "Lifecycle fixture";
      return result;
    }

    Json::Value execute( const Json::Value &params, ProgressCallback,
                         std::function<bool()> isCancelled ) override
    {
      if ( mMode == Mode::SleepThenComplete )
      {
        // Respect cancellation so stop→cancel tests can observe terminal Canceled.
        const int sliceMs = 20;
        for ( int slept = 0; slept < mSleepMs; slept += sliceMs )
        {
          if ( isCancelled && isCancelled() )
            throw std::runtime_error( "Canceled" );
          std::this_thread::sleep_for( std::chrono::milliseconds( sliceMs ) );
        }
        Json::Value result( Json::objectValue );
        result["status"] = "ok";
        result["echo"] = params;
        return result;
      }
      else
      {
        Q_UNUSED( isCancelled )
        Json::Value result( Json::objectValue );
        result["status"] = "ok";
        result["output"] = mOutputPath.toStdString();
        return result;
      }
    }

  private:
    std::string mId;
    Mode mMode;
    int mSleepMs;
    QString mOutputPath;
};

class LifecycleRegistryGuard
{
  public:
    explicit LifecycleRegistryGuard( AtomicAlgorithmAdapterPtr fixture )
    {
      auto &registry = AtomicAlgorithmRegistry::instance();
      for ( const auto &descriptor : registry.listDescriptors() )
      {
        if ( auto adapter = registry.findAdapter( descriptor.id ) )
          m_previous.push_back( std::move( adapter ) );
        registry.unregisterAdapter( descriptor.id );
      }
      registry.registerAdapter( std::move( fixture ) );
      tool_catalog::AgentToolCatalog::instance().invalidateCache();
    }

    ~LifecycleRegistryGuard()
    {
      auto &registry = AtomicAlgorithmRegistry::instance();
      for ( const auto &descriptor : registry.listDescriptors() )
        registry.unregisterAdapter( descriptor.id );
      for ( const auto &adapter : m_previous ) registry.registerAdapter( adapter );
      tool_catalog::AgentToolCatalog::instance().invalidateCache();
    }

  private:
    std::vector<AtomicAlgorithmAdapterPtr> m_previous;
};

void wireRegistryFallback()
{
  sicnu::jobs::JobEngine::instance().setFallbackExecutor(
    []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
      const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
      if ( !adapter )
        throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
      ProgressCallback progressBridge;
      progressBridge = [&ctx]( int percent, const std::string &message ) {
        ctx.reportProgress( percent / 100.0, message );
      };
      return adapter->execute( req.params, progressBridge, [&ctx]() { return ctx.isCancelled(); } );
    } );
}

QJsonObject makeToolCallEnvelope( const QString &name, const QString &arguments = QStringLiteral( "{}" ) )
{
  QJsonObject envelope;
  envelope[QStringLiteral( "id" )] = QStringLiteral( "tc_1" );
  envelope[QStringLiteral( "type" )] = QStringLiteral( "function" );
  QJsonObject func;
  func[QStringLiteral( "name" )] = name;
  func[QStringLiteral( "arguments" )] = arguments;
  envelope[QStringLiteral( "function" )] = func;
  return envelope;
}


void approveOperation( AgentCopilotDockWidget &dock )
{
  QElapsedTimer deadline;
  deadline.start();
  QPushButton *button = nullptr;
  while ( !button && deadline.elapsed() < 5000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    button = dock.findChild<QPushButton *>( QStringLiteral( "agentApproveOperation" ) );
  }
  INFO( dock.runInspectorSummary().toStdString() );
  QStringList fixtureDetails;
  QJsonArray fixtureAdmission;
  if ( !button )
  {
    for ( const auto *label : dock.findChildren<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) ) )
      fixtureDetails.append( label->text() );
    if ( const auto *host = dock.findChild<runtime::AgentToolHost *>() )
    {
      const auto snapshot = host->catalogSnapshot();
      for ( const auto *card : dock.findChildren<QWidget *>( QStringLiteral( "toolCallCard" ) ) )
      {
        const QString id = card->property( "toolName" ).toString();
        bool exported = false;
        for ( const auto &entry : snapshot.tools )
          exported |= entry.toObject().value( QStringLiteral( "canonicalId" ) ).toString() == id;
        fixtureAdmission.append( QJsonObject{
          { QStringLiteral( "tool" ), id },
          { QStringLiteral( "registered" ), AtomicAlgorithmRegistry::instance().findAdapter( id.toStdString() ) != nullptr },
          { QStringLiteral( "exported" ), exported },
          { QStringLiteral( "catalogError" ), snapshot.error },
          { QStringLiteral( "catalogRevision" ), snapshot.revision } } );
      }
    }
  }
  INFO( "Fixture tool-card details: " << fixtureDetails.join( QLatin1Char( '\n' ) ).toStdString() );
  INFO( "Fixture admission: " << QJsonDocument( fixtureAdmission ).toJson( QJsonDocument::Compact ).toStdString() );
  REQUIRE( button != nullptr );
  REQUIRE( button->isEnabled() );
  button->click();
}

} // namespace

TEST_CASE( "LlmSettingsDialog initializes and manages provider profiles", "[agent][ui]" )
{
  ensureQtApp();

  LlmSettingsDialog dlg;
  REQUIRE( dlg.windowTitle().contains( QStringLiteral( "Copilot" ) ) );

  LlmProviderProfile profile = dlg.selectedProfile();
  REQUIRE_FALSE( profile.id.isEmpty() );
}

TEST_CASE( "AgentCopilotDockWidget instantiates and binds workspace context", "[agent][ui]" )
{
  ensureQtApp();

  DataManager dataMgr;

  AgentCopilotDockWidget dock;
  REQUIRE( dock.objectName() == "AgentCopilotDockWidget" );

  dock.setContext( &dataMgr, nullptr );

  // Test prompt submission triggers message card rendering
  dock.sendPrompt( QStringLiteral( "运行光谱指数测试" ) );
}

TEST_CASE( "Workspace context sharing is explicit and off by default", "[agent][ui][privacy]" )
{
  ensureQtApp();
  QSettings().setValue( QStringLiteral( "agent/includeWorkspaceContext" ), false );
  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );
  auto *toggle = dock.findChild<QCheckBox *>( QStringLiteral( "agentIncludeWorkspaceContext" ) );
  REQUIRE( toggle != nullptr );
  REQUIRE_FALSE( toggle->isChecked() );
  REQUIRE( dock.findChild<QLabel *>( QStringLiteral( "agentWelcomeLabel" ) ) != nullptr );

  dock.sendPrompt( QStringLiteral( "test without workspace context" ) );
  REQUIRE( !dock.messageHistory().isEmpty() );
  QString system = dock.messageHistory().first().toObject().value( QStringLiteral( "content" ) ).toString();
  CHECK( system.contains( QStringLiteral( "sharing is disabled" ) ) );
  CHECK_FALSE( system.contains( QStringLiteral( "Loaded Data Assets" ) ) );
  auto *userMessage = dock.findChild<QLabel *>( QStringLiteral( "agentUserMessage" ) );
  REQUIRE( userMessage != nullptr );
  CHECK( userMessage->textInteractionFlags() & Qt::TextSelectableByMouse );

  toggle->setChecked( true );
  QElapsedTimer firstRequest;
  firstRequest.start();
  while ( !dock.runInspectorSummary().contains( QStringLiteral( "stage=Failed" ) ) && firstRequest.elapsed() < 5000 )
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
  dock.sendPrompt( QStringLiteral( "test with workspace context" ) );
  system = dock.messageHistory().first().toObject().value( QStringLiteral( "content" ) ).toString();
  CHECK( system.contains( QStringLiteral( "[WORKSPACE CONTEXT]" ) ) );
  CHECK( system.contains( QStringLiteral( "Loaded Data Assets" ) ) );
  QSettings().setValue( QStringLiteral( "agent/includeWorkspaceContext" ), false );
}

TEST_CASE( "AgentCopilotDockWidget handles widget destruction during async plan execution", "[agent][ui]" )
{
  ensureQtApp();

  DataManager dataMgr;
  auto dock = std::make_unique<AgentCopilotDockWidget>();
  dock->setContext( &dataMgr, nullptr );

  // Destroying dock while async operations might be in flight must not crash
  dock.reset();
  REQUIRE( dock == nullptr );
}

TEST_CASE( "AgentCopilotDockWidget completion callback is safe after destruction", "[agent][ui][lifecycle]" )
{
  ensureQtApp();
  wireRegistryFallback();
  LifecycleRegistryGuard registry(
    std::make_shared<LifecycleStubAdapter>( "stub:sleepy_lifecycle",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            600 ) );

  auto dataMgr = std::make_unique<DataManager>();
  auto dock = std::make_unique<AgentCopilotDockWidget>();
  dock->setContext( dataMgr.get(), nullptr );

  REQUIRE( QMetaObject::invokeMethod( dock.get(), "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:sleepy_lifecycle" ) ) ) ) );
  approveOperation( *dock );

  bool destroyed = false;
  QTimer::singleShot( 200, [&] {
    dock.reset();
    dataMgr.reset();
    destroyed = true;
  } );

  QEventLoop loop;
  QTimer::singleShot( 1200, &loop, &QEventLoop::quit );
  loop.exec();

  REQUIRE( destroyed );
}

TEST_CASE( "AgentCopilotDockWidget clear cancels stale completion callbacks", "[agent][ui][lifecycle]" )
{
  ensureQtApp();
  wireRegistryFallback();
  LifecycleRegistryGuard registry(
    std::make_shared<LifecycleStubAdapter>( "stub:sleepy_clear",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            600 ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:sleepy_clear" ) ) ) ) );
  approveOperation( dock );

  QTimer::singleShot( 100, &dock, [&] {
    REQUIRE( QMetaObject::invokeMethod( &dock, "onClearClicked" ) );
  } );

  QEventLoop loop;
  QTimer::singleShot( 1200, &loop, &QEventLoop::quit );
  loop.exec();
}

TEST_CASE( "Copilot tool follow-ups preserve the OpenAI wire contract across reasoning rounds",
           "[agent][ui][wire]" )
{
  ensureQtApp();
  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  int toolExecutions = 0;
  InteractionToolDefinition echo;
  echo.name = "view:get_state";
  echo.displayName = "Wire echo";
  echo.inputSchema["type"] = "object";
  echo.handler = [&toolExecutions]( const Json::Value &arguments ) {
    ++toolExecutions;
    Json::Value result( Json::objectValue );
    result["status"] = "success";
    result["received"] = arguments;
    return result;
  };
  InteractionToolRegistry::instance().registerTool( std::move( echo ) );

  QTcpServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  QList<QJsonObject> requests;
  QStringList violations;
  bool finalAnswerReceived = false;
  auto argumentsForRound = []( int round ) {
    return QJsonObject{ { QStringLiteral( "round" ), round },
                        { QStringLiteral( "note" ), QStringLiteral( "NDVI\n\"quoted\"" ) },
                        { QStringLiteral( "values" ), QJsonArray{ 0, 1, 255 } } };
  };
  QObject::connect( &server, &QTcpServer::newConnection, &server, [&]() {
    while ( server.hasPendingConnections() )
    {
      QTcpSocket *socket = server.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();
      auto handled = std::make_shared<bool>( false );
      QObject::connect( socket, &QTcpSocket::readyRead, &server, [&, socket, buffer, handled]() {
        *buffer += socket->readAll();
        if ( *handled )
          return;
        const qsizetype headerEnd = buffer->indexOf( "\r\n\r\n" );
        if ( headerEnd < 0 )
          return;
        qint64 contentLength = -1;
        for ( const QByteArray &line : buffer->left( headerEnd ).split( '\n' ) )
          if ( line.toLower().startsWith( "content-length:" ) )
            contentLength = line.mid( 15 ).trimmed().toLongLong();
        if ( contentLength < 0 || buffer->size() < headerEnd + 4 + contentLength )
          return;
        *handled = true;
        const QJsonObject request = QJsonDocument::fromJson(
          buffer->mid( headerEnd + 4, contentLength ) ).object();
        requests.append( request );
        const int requestNumber = static_cast<int>( requests.size() );
        int assistantRounds = 0;
        for ( const QJsonValue &messageValue : request.value( QStringLiteral( "messages" ) ).toArray() )
        {
          const QJsonObject message = messageValue.toObject();
          if ( message.value( QStringLiteral( "role" ) ) != QStringLiteral( "assistant" ) )
            continue;
          ++assistantRounds;
          if ( message.value( QStringLiteral( "reasoning_content" ) ).toString()
               != QStringLiteral( "reasoning-%1" ).arg( assistantRounds ) )
            violations.append( QStringLiteral( "Reasoning did not round-trip for round %1" ).arg( assistantRounds ) );
          if ( message.value( QStringLiteral( "content" ) ).toString()
               != QStringLiteral( "content-%1" ).arg( assistantRounds ) )
            violations.append( QStringLiteral( "Assistant content did not round-trip for round %1" ).arg( assistantRounds ) );
          const QJsonArray calls = message.value( QStringLiteral( "tool_calls" ) ).toArray();
          if ( calls.size() != 1 )
          {
            violations.append( QStringLiteral( "Expected one tool call per assistant round" ) );
            continue;
          }
          const QJsonObject call = calls.first().toObject();
          const QJsonValue arguments = call.value( QStringLiteral( "function" ) ).toObject()
                                         .value( QStringLiteral( "arguments" ) );
          if ( !arguments.isString() )
            violations.append( QStringLiteral( "function.arguments must be a JSON string" ) );
          else if ( QJsonDocument::fromJson( arguments.toString().toUtf8() ).object()
                    != argumentsForRound( assistantRounds ) )
            violations.append( QStringLiteral( "Tool argument values changed on the wire" ) );
          if ( call.value( QStringLiteral( "id" ) ).toString()
               != QStringLiteral( "wire-%1" ).arg( assistantRounds ) )
            violations.append( QStringLiteral( "Tool call identity changed" ) );
        }
        if ( assistantRounds != requestNumber - 1 )
          violations.append( QStringLiteral( "A prior tool exchange was lost" ) );
        if ( !violations.isEmpty() )
        {
          const QByteArray error = R"({"error":{"message":"Invalid assistant tool-call wire payload"}})";
          socket->write( "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
                         + QByteArray::number( error.size() ) + "\r\nConnection: close\r\n\r\n" + error );
          socket->disconnectFromHost();
          return;
        }

        QJsonObject delta;
        if ( requestNumber <= 2 )
        {
          delta[QStringLiteral( "reasoning_content" )] = QStringLiteral( "reasoning-%1" ).arg( requestNumber );
          delta[QStringLiteral( "content" )] = QStringLiteral( "content-%1" ).arg( requestNumber );
          QJsonObject function;
          function[QStringLiteral( "name" )] = QStringLiteral( "view_get_state" );
          function[QStringLiteral( "arguments" )] = QString::fromUtf8(
            QJsonDocument( argumentsForRound( requestNumber ) ).toJson( QJsonDocument::Compact ) );
          delta[QStringLiteral( "tool_calls" )] = QJsonArray{ QJsonObject{
            { QStringLiteral( "index" ), 0 },
            { QStringLiteral( "id" ), QStringLiteral( "wire-%1" ).arg( requestNumber ) },
            { QStringLiteral( "function" ), function } } };
        }
        else
          delta[QStringLiteral( "content" )] = QStringLiteral( "Verified final answer" );
        QJsonObject choice;
        choice[QStringLiteral( "delta" )] = delta;
        choice[QStringLiteral( "finish_reason" )] = requestNumber <= 2
          ? QStringLiteral( "tool_calls" ) : QStringLiteral( "stop" );
        const QByteArray body = "data: " + QJsonDocument( QJsonObject{
          { QStringLiteral( "choices" ), QJsonArray{ choice } } } ).toJson( QJsonDocument::Compact )
          + "\n\ndata: [DONE]\n\n";
        socket->write( "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: "
                       + QByteArray::number( body.size() ) + "\r\nConnection: close\r\n\r\n" + body );
        socket->disconnectFromHost();
      } );
    }
  } );

  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( server.serverPort() );
  profile.modelName = QStringLiteral( "wire-contract-fixture" );
  profile.id = QStringLiteral( "wire-contract-fixture" );
  profile.name = QStringLiteral( "Wire contract fixture" );
  LlmConfigManager::setActiveProfile( profile );
  dock.sendPrompt( QStringLiteral( "Run the two local echo checks" ) );
  QElapsedTimer deadline;
  deadline.start();
  // This checks wire semantics through the real metadata/host path, rather
  // than asserting a ten-second performance budget for three model rounds.
  while ( ( !finalAnswerReceived || dock.isRunActive() ) && violations.isEmpty() && deadline.elapsed() < 30000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    for ( auto *label : dock.findChildren<QLabel *>() )
      finalAnswerReceived |= label->text().contains( QStringLiteral( "Verified final answer" ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  INFO( dock.runInspectorSummary().toStdString() );
  REQUIRE( dock.stopForProjectChange( 5000 ) );
  InteractionToolRegistry::instance().unregisterTool( "view:get_state" );
  INFO( violations.join( QStringLiteral( "; " ) ).toStdString() );
  REQUIRE( requests.size() >= 2 );
  CHECK( violations.isEmpty() );
  CHECK( requests.size() == 3 );
  CHECK( toolExecutions == 2 );
  CHECK( finalAnswerReceived );
}

TEST_CASE( "AgentCopilotDockWidget bounds the LLM conversation history", "[agent][ui][history]" )
{
  QJsonObject systemMsg;
  systemMsg[QStringLiteral( "role" )] = QStringLiteral( "system" );
  systemMsg[QStringLiteral( "content" )] = QStringLiteral( "system prompt" );

  // Runaway tool loop: assistant(tool_calls) + tool + user per round.
  QJsonArray history;
  history.append( systemMsg );
  for ( int i = 0; i < 100; ++i )
  {
    QJsonObject assistantMsg;
    assistantMsg[QStringLiteral( "role" )] = QStringLiteral( "assistant" );
    history.append( assistantMsg );
    QJsonObject toolMsg;
    toolMsg[QStringLiteral( "role" )] = QStringLiteral( "tool" );
    history.append( toolMsg );
    QJsonObject userMsg;
    userMsg[QStringLiteral( "role" )] = QStringLiteral( "user" );
    history.append( userMsg );
  }
  REQUIRE( history.size() > AgentCopilotDockWidget::kMaxHistoryMessages );

  const QJsonArray pruned = AgentCopilotDockWidget::pruneHistory( history );
  CHECK( pruned.size() == 1 + AgentCopilotDockWidget::kMaxHistoryMessages );
  CHECK( pruned.first().toObject().value( QStringLiteral( "role" ) ).toString()
         == QStringLiteral( "system" ) );
  CHECK( pruned.at( 1 ).toObject().value( QStringLiteral( "role" ) ).toString()
         != QStringLiteral( "tool" ) );
  CHECK( AgentCopilotDockWidget::pruneHistory( pruned ) == pruned );

  // A tail starting on an orphaned tool reply drops it rather than sending an
  // invalid tool sequence to the endpoint.
  QJsonArray orphaned;
  orphaned.append( systemMsg );
  QJsonObject orphanTool;
  orphanTool[QStringLiteral( "role" )] = QStringLiteral( "tool" );
  orphaned.append( orphanTool );
  while ( orphaned.size() < AgentCopilotDockWidget::kMaxHistoryMessages + 1 )
  {
    QJsonObject userMsg;
    userMsg[QStringLiteral( "role" )] = QStringLiteral( "user" );
    orphaned.append( userMsg );
  }
  const QJsonArray orphanPruned = AgentCopilotDockWidget::pruneHistory( orphaned );
  CHECK( orphanPruned.size() == AgentCopilotDockWidget::kMaxHistoryMessages );
  CHECK( orphanPruned.at( 1 ).toObject().value( QStringLiteral( "role" ) ).toString()
         != QStringLiteral( "tool" ) );
}

TEST_CASE( "AgentCopilotDockWidget stop cancels submitted TaskCenter tasks", "[agent][ui][cancel]" )
{
  ensureQtApp();
  wireRegistryFallback();
  LifecycleRegistryGuard registry(
    std::make_shared<LifecycleStubAdapter>( "stub:sleepy_stop",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            5000 ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:sleepy_stop" ) ) ) ) );
  approveOperation( dock );

  // Pump until the task is registered in the run inspector.
  long taskId = -1;
  for ( int i = 0; i < 200 && taskId < 0; ++i )
  {
    QCoreApplication::processEvents();
    const QString summary = dock.runInspectorSummary();
    if ( summary.contains( QStringLiteral( "tasks=1" ) ) )
    {
      const auto tasks = sicnu::TaskCenter::instance().allTasks();
      for ( const auto &info : tasks )
      {
        if ( info.algorithmId == QStringLiteral( "stub:sleepy_stop" ) )
          taskId = std::max( taskId, info.taskId );
      }
    }
    if ( taskId < 0 )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  REQUIRE( taskId > 0 );

  // The private stop path is routed through onSendClicked in production; here
  // we exercise the cancel hook directly because driving the LLM streaming
  // state in a headless unit test is unstable.
  REQUIRE( QMetaObject::invokeMethod( &dock, "cancelCurrentRunTasks" ) );

  // The task may briefly be "Cancelling"; wait for the terminal transition.
  sicnu::AlgorithmTaskInfo finalInfo;
  for ( int i = 0; i < 200; ++i )
  {
    finalInfo = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( sicnu::isTerminalStatus( finalInfo.status ) )
      break;
    QCoreApplication::processEvents();
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  CHECK( finalInfo.status == sicnu::TaskStatus::Canceled );
  QElapsedTimer idleDeadline;
  idleDeadline.start();
  while ( dock.isRunActive() && idleDeadline.elapsed() < 5000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  REQUIRE_FALSE( dock.isRunActive() );
  auto *approve = dock.findChild<QPushButton *>( QStringLiteral( "agentApproveOperation" ) );
  REQUIRE( approve != nullptr );
  CHECK( approve->text() == QStringLiteral( "Canceled" ) );
  CHECK_FALSE( approve->isEnabled() );
  auto *title = dock.findChild<QLabel *>( QStringLiteral( "ToolCallCardTitle" ) );
  REQUIRE( title != nullptr );
  CHECK( title->text().contains( QStringLiteral( "Canceled" ) ) );
  CHECK_FALSE( title->text().contains( QStringLiteral( "Failed" ) ) );
  CHECK_FALSE( dock.runInspectorSummary().contains( QStringLiteral( "error=" ) ) );
  CHECK( dataMgr.assets().isEmpty() );
  CHECK( dock.findChild<QPushButton *>( QStringLiteral( "agentShowResult" ) ) == nullptr );
}

TEST_CASE( "Operator rejection remains visible without becoming a task or run failure", "[agent][ui][approval][reject]" )
{
  ensureQtApp();
  wireRegistryFallback();
  LifecycleRegistryGuard registry(
    std::make_shared<LifecycleStubAdapter>( "stub:reject_operation",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            50 ) );
  DataManager data;
  AgentCopilotDockWidget dock;
  dock.setContext( &data, nullptr );
  auto *host = dock.findChild<runtime::AgentToolHost *>();
  REQUIRE( host != nullptr );
  QStringList terminalStates;
  QJsonObject result;
  QObject::connect( host, &runtime::AgentToolHost::toolUpdated, &dock,
    [&]( const QString &, const QString &, const QString &state, const QString & ) {
      if ( state == QStringLiteral( "failed" ) || state == QStringLiteral( "rejected" ) )
        terminalStates.append( state );
    } );
  QObject::connect( host, &runtime::AgentToolHost::toolCompleted, &dock,
    [&]( const QString &, const QString &, const QJsonObject &value ) { result = value; } );
  const qsizetype taskCount = sicnu::TaskCenter::instance().allTasks().size();
  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed", Qt::QueuedConnection,
    Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:reject_operation" ) ) ) ) );
  QElapsedTimer deadline;
  deadline.start();
  QPushButton *reject = nullptr;
  while ( !reject && deadline.elapsed() < 5000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    reject = dock.findChild<QPushButton *>( QStringLiteral( "agentRejectOperation" ) );
    if ( !reject ) std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
  }
  REQUIRE( reject != nullptr );
  auto *approve = dock.findChild<QPushButton *>( QStringLiteral( "agentApproveOperation" ) );
  REQUIRE( approve != nullptr );
  reject->click();
  while ( dock.isRunActive() && deadline.elapsed() < 5000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
  }
  REQUIRE_FALSE( dock.isRunActive() );
  CHECK( approve->text() == QStringLiteral( "Rejected" ) );
  CHECK_FALSE( approve->isEnabled() );
  CHECK_FALSE( reject->isEnabled() );
  REQUIRE( terminalStates.size() == 1 );
  CHECK( terminalStates.first() == QStringLiteral( "rejected" ) );
  auto *title = dock.findChild<QLabel *>( QStringLiteral( "ToolCallCardTitle" ) );
  REQUIRE( title != nullptr );
  CHECK( title->text().contains( QStringLiteral( "Rejected" ) ) );
  CHECK_FALSE( title->text().contains( QStringLiteral( "Failed" ) ) );
  CHECK( result.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "error" ) );
  CHECK( result.value( QStringLiteral( "errorCode" ) ).toString() == QStringLiteral( "approval_rejected" ) );
  CHECK_FALSE( result.value( QStringLiteral( "retryable" ) ).toBool( true ) );
  CHECK_FALSE( dock.runInspectorSummary().contains( QStringLiteral( "error=" ) ) );
  CHECK( sicnu::TaskCenter::instance().allTasks().size() == taskCount );
  CHECK( data.assets().isEmpty() );
  CHECK( dock.findChild<QPushButton *>( QStringLiteral( "agentShowResult" ) ) == nullptr );
}

TEST_CASE( "AgentCopilotDockWidget tool-call card shows result summary", "[agent][ui][toolcard]" )
{
  ensureQtApp();
  wireRegistryFallback();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outputPath = tmp.path() + QStringLiteral( "/stub_output_agent.tif" );
  REQUIRE( writeTinyRaster( outputPath, 4, 4, 1 ) );

  LifecycleRegistryGuard registry(
    std::make_shared<LifecycleStubAdapter>( "stub:write_output",
                                            LifecycleStubAdapter::Mode::WriteOutput,
                                            0,
                                            outputPath ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:write_output" ) ) ) ) );
  approveOperation( dock );

  // Wait for the task to complete and the card to be updated.
  bool foundSummary = false;
  QString lastDetails;
  for ( int i = 0; i < 400 && !foundSummary; ++i )
  {
    QCoreApplication::processEvents();
    for ( QLabel *label : dock.findChildren<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) ) )
    {
      lastDetails = label->text();
      if ( lastDetails.contains( outputPath ) || lastDetails.contains( QStringLiteral( "verified" ) ) )
      {
        foundSummary = true;
        break;
      }
    }
    if ( !foundSummary )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  INFO( "card details: " << lastDetails.toStdString() );
  REQUIRE( foundSummary );
  CHECK( dock.runInspectorSummary().contains( QStringLiteral( "stage=Completed" ) ) );

  // A delivered task result must remain displayable even when automatic map
  // loading is unavailable. The human action goes to the shell's normal load
  // authority; showing a result must not submit another task or retain it.
  const QJsonObject delivered = QJsonDocument::fromJson( lastDetails.toUtf8() ).object();
  const QString committedOutput = delivered.value( QStringLiteral( "output" ) ).toString();
  REQUIRE( committedOutput != outputPath );
  REQUIRE( QFileInfo::exists( committedOutput ) );
  const auto asset = dataMgr.findByPath( committedOutput );
  REQUIRE( asset.has_value() );
  REQUIRE( asset->id().toString() == delivered.value( QStringLiteral( "assetId" ) ).toString() );
  REQUIRE( asset->persistence() == PersistencePolicy::TaskTemporary );
  auto *show = dock.findChild<QPushButton *>( QStringLiteral( "agentShowResult" ) );
  REQUIRE( show != nullptr );
  QStringList requestedAssets;
  QObject::connect( &dock, &AgentCopilotDockWidget::showResultOnMapRequested,
                    &dock, [&]( const QString &id ) { requestedAssets.append( id ); } );
  const qsizetype taskCount = sicnu::TaskCenter::instance().allTasks().size();
  const qsizetype assetCount = dataMgr.assets().size();
  show->click();
  show->click();
  REQUIRE( requestedAssets.size() == 2 );
  CHECK( requestedAssets.at( 0 ) == asset->id().toString() );
  CHECK( requestedAssets.at( 1 ) == asset->id().toString() );
  CHECK( sicnu::TaskCenter::instance().allTasks().size() == taskCount );
  CHECK( dataMgr.assets().size() == assetCount );
  REQUIRE( dataMgr.asset( asset->id() ).has_value() );
  CHECK( dataMgr.asset( asset->id() )->persistence() == PersistencePolicy::TaskTemporary );
}

TEST_CASE( "AgentCopilotDockWidget inspector state reflects run lifecycle", "[agent][ui][inspector]" )
{
  ensureQtApp();
  wireRegistryFallback();
  LifecycleRegistryGuard registry(
    std::make_shared<LifecycleStubAdapter>( "stub:inspector_lifecycle",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            50 ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  CHECK( dock.runInspectorSummary().contains( QStringLiteral( "stage=-" ) ) );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:inspector_lifecycle" ) ) ) ) );
  approveOperation( dock );

  // Eventually the inspector should report a terminal stage.
  bool terminal = false;
  for ( int i = 0; i < 400 && !terminal; ++i )
  {
    QCoreApplication::processEvents();
    const QString summary = dock.runInspectorSummary();
    terminal = summary.contains( QStringLiteral( "stage=Completed" ) ) || summary.contains( QStringLiteral( "stage=Failed" ) );
    if ( !terminal )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  CHECK( terminal );
}

// #1395 (agent-harness-r4 review #10): LlmStreamingClient reports every
// streamed tool call it refuses; the dock is the consumer. A refusal must be
// visible in the run inspector — the old silent drop left the user with a turn
// that produced no work and an inspector reporting zero errors.
TEST_CASE( "A transport-refused tool call is visible in the run inspector",
           "[agent][ui][refusal]" )
{
  ensureQtApp();
  wireRegistryFallback();

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  CHECK_FALSE( dock.runInspectorSummary().contains( QStringLiteral( "refused=" ) ) );

  QJsonObject detail;
  detail[QStringLiteral( "reason" )] = QStringLiteral( "truncated" );
  detail[QStringLiteral( "finish_reason" )] = QStringLiteral( "length" );
  detail[QStringLiteral( "name" )] = QStringLiteral( "rs:band_math" );
  REQUIRE( QMetaObject::invokeMethod( &dock, "onMalformedToolCall",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, detail ) ) );

  bool counted = false;
  for ( int i = 0; i < 400 && !counted; ++i )
  {
    QCoreApplication::processEvents();
    counted = dock.runInspectorSummary().contains( QStringLiteral( "refused=1" ) );
    if ( !counted )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  INFO( "inspector: " << dock.runInspectorSummary().toStdString() );
  CHECK( counted );

  // The refusal is a stream fact, not a run failure: the run stays open.
  CHECK_FALSE( dock.runInspectorSummary().contains( QStringLiteral( "stage=Failed" ) ) );
}

TEST_CASE( "InteractionToolRegistry handler returns error after service destruction", "[agent][interaction][lifecycle]" )
{
  ensureQtApp();
  InteractionToolRegistry::instance().reset();

  {
    ViewControlService service;
    service.setDataManager( nullptr );
    service.setMapCanvas( nullptr );
    InteractionToolRegistry::instance().registerBuiltinTools( &service, nullptr, nullptr );
  } // service destroyed

  const Json::Value result =
    InteractionToolRegistry::instance().execute( "view:get_state", Json::Value( Json::objectValue ) );

  REQUIRE( result.isObject() );
  REQUIRE( result["status"].asString() == "error" );
}



namespace
{
bool checkpointEventually( const std::function<bool()> &predicate )
{
  QElapsedTimer timer;
  timer.start();
  while ( !predicate() && timer.elapsed() < 15000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
  }
  return predicate();
}

QString checkpointLabels( const AgentCopilotDockWidget &dock )
{
  QString text;
  for ( auto *label : dock.findChildren<QLabel *>() ) text += label->text() + QLatin1Char( '\n' );
  return text;
}

struct CheckpointUiFixture
{
  QTemporaryDir directory;
  QString previousProject = QgsProject::instance()->fileName();
  QVariant previousBoot = QCoreApplication::instance()->property( "sicnuAgentCheckpointBootId" );
  QString prefix;
  QString storeDirectory;
  sicnu::agent::runtime::ConversationCheckpointBinding binding;

  CheckpointUiFixture()
  {
    REQUIRE( directory.isValid() );
    prefix = directory.path() + QStringLiteral( "/installation" );
    storeDirectory = directory.path() + QStringLiteral( "/owned-checkpoints" );
    const QString receiptDirectory = prefix + QStringLiteral( "/share/sicnu-geo-rs" );
    REQUIRE( QDir().mkpath( receiptDirectory ) );
    const QByteArray receiptBytes( "{\"fixture\":\"receipt-only-no-node\"}" );
    QFile receipt( receiptDirectory + QStringLiteral( "/pi-runtime.json" ) );
    REQUIRE( receipt.open( QIODevice::WriteOnly ) );
    REQUIRE( receipt.write( receiptBytes ) == receiptBytes.size() );
    receipt.close();
    const QString projectPath = directory.path() + QStringLiteral( "/saved-fixture.qgz" );
    QFile project( projectPath );
    REQUIRE( project.open( QIODevice::WriteOnly ) );
    REQUIRE( project.write( "fixture" ) == 7 );
    project.close();
    QgsProject::instance()->setFileName( projectPath );
    const QString projectId = QStringLiteral( "project-" ) + QString::fromLatin1( QCryptographicHash::hash(
      QFileInfo( projectPath ).canonicalFilePath().toUtf8(), QCryptographicHash::Sha256 ).toHex() );
    const QString conversationId = QStringLiteral( "11111111-1111-4111-8111-111111111111" );
    QSettings().setValue( QStringLiteral( "agent/piConversationIds/" ) + projectId, conversationId );
    QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "pi" ) );
    QCoreApplication::instance()->setProperty( "sicnuAgentCheckpointBootId", QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    // Marker-only recovery has no snapshot/config binding to restore. The SDK
    // receipt is present for the Dock's bounded hash read; no Node payload
    // exists, so any accidental runtime start would fail these no-process tests.
    binding = { projectId, conversationId, QString::fromLatin1( QCryptographicHash::hash( receiptBytes, QCryptographicHash::Sha256 ).toHex() ),
      QStringLiteral( "fixture-catalog" ), QString( 64, QLatin1Char( 'b' ) ) };
  }

  ~CheckpointUiFixture()
  {
    QCoreApplication::instance()->setProperty( "sicnuAgentCheckpointBootId", previousBoot );
    QgsProject::instance()->setFileName( previousProject );
    QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "native" ) );
  }
};

struct CheckpointNativeWorker
{
  sicnu::TaskCenter &center = sicnu::TaskCenter::instance();
  std::atomic_bool release = false;
  std::atomic_int executions = 0;
  long taskId = -1;

  CheckpointNativeWorker()
  {
    center.shutdownForTests();
    sicnu::jobs::JobRequest request;
    request.algorithmId = "fixture:checkpoint_native_query";
    request.params = Json::Value( Json::objectValue );
    taskId = center.submitJob( request, [this]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
      ++executions;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
      while ( !release && std::chrono::steady_clock::now() < deadline ) std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
      Json::Value result( Json::objectValue );
      result["status"] = "success";
      return result;
    }, {}, false );
    REQUIRE( taskId > 0 );
    REQUIRE( checkpointEventually( [this] {
      const auto info = center.getTaskInfo( taskId );
      return executions == 1 && !info.jobId.empty() && info.status == sicnu::TaskStatus::Running;
    } ) );
  }

  ~CheckpointNativeWorker() { release = true; center.shutdownForTests(); }
};
} // namespace

TEST_CASE( "Pi Restore queries native identity and quiescence before acknowledgement without replay", "[agent][ui][checkpoint]" )
{
  ensureQtApp();
  tool_catalog::AgentToolCatalog::instance().initializeDefaults();
  CheckpointUiFixture files;
  CheckpointNativeWorker worker;
  DataManager data;
  AgentCopilotDockWidget dock( nullptr, worker.center, files.prefix, files.storeDirectory );
  dock.setContext( &data, nullptr );
  sicnu::agent::runtime::ConversationCheckpointStore store( files.storeDirectory );
  REQUIRE( store.markRunStarted( files.binding, QStringLiteral( "interrupted-run" ) ).error.isEmpty() );
  const auto info = worker.center.getTaskInfo( worker.taskId );
  REQUIRE( store.recordOwnedExecutions( files.binding, QStringLiteral( "interrupted-run" ), {
    { sicnu::agent::runtime::ConversationExecutionReference::Kind::Task, worker.taskId, QString::fromStdString( info.jobId ),
      QCoreApplication::instance()->property( "sicnuAgentCheckpointBootId" ).toString() }
  } ).error.isEmpty() );
  auto *restore = dock.findChild<QPushButton *>( QStringLiteral( "agentRestoreConversation" ) );
  REQUIRE( restore );
  const qsizetype taskCount = worker.center.allTasks().size();
  restore->click();
  CHECK( restore->text() == QStringLiteral( "Restore conversation" ) );
  CHECK( checkpointLabels( dock ).contains( QStringLiteral( "still active or not yet quiescent" ) ) );
  CHECK( store.load( files.binding ).interruptedRunId == QStringLiteral( "interrupted-run" ) );
  CHECK( worker.executions == 1 );
  // Only native authority changes. The same durable marker still contains no
  // status/result facts; the changed UI outcome therefore proves a fresh query.
  worker.release = true;
  REQUIRE( checkpointEventually( [&] {
    const auto now = worker.center.getTaskInfo( worker.taskId );
    return sicnu::isTerminalStatus( now.status ) && worker.center.isExecutionQuiescent( now.jobId );
  } ) );
  restore->click();
  CHECK( restore->text() == QStringLiteral( "Acknowledge and restore" ) );
  CHECK( checkpointLabels( dock ).contains( QStringLiteral( "currently reports a terminal state" ) ) );
  CHECK_FALSE( store.load( files.binding ).interruptedRunId.isEmpty() );
  restore->click();
  CHECK( store.load( files.binding ).interruptedRunId.isEmpty() );
  CHECK( worker.executions == 1 );
  CHECK( worker.center.allTasks().size() == taskCount );
  CHECK_FALSE( dock.isRunActive() );
  for ( auto *process : dock.findChildren<QProcess *>() ) CHECK( process->state() == QProcess::NotRunning );
}

TEST_CASE( "Pi Restore reports foreign and missing references as unknown and never replays", "[agent][ui][checkpoint]" )
{
  ensureQtApp();
  tool_catalog::AgentToolCatalog::instance().initializeDefaults();
  CheckpointUiFixture files;
  auto &center = sicnu::TaskCenter::instance();
  DataManager data;
  AgentCopilotDockWidget dock( nullptr, center, files.prefix, files.storeDirectory );
  dock.setContext( &data, nullptr );
  sicnu::agent::runtime::ConversationCheckpointStore store( files.storeDirectory );
  REQUIRE( store.markRunStarted( files.binding, QStringLiteral( "foreign-run" ) ).error.isEmpty() );
  const QString foreignBoot = QStringLiteral( "33333333-3333-4333-8333-333333333333" );
  REQUIRE( store.recordOwnedExecutions( files.binding, QStringLiteral( "foreign-run" ), {
    { sicnu::agent::runtime::ConversationExecutionReference::Kind::Task, 9001, QStringLiteral( "job-9001" ), foreignBoot },
    { sicnu::agent::runtime::ConversationExecutionReference::Kind::Pipeline, 9002, {}, foreignBoot }
  } ).error.isEmpty() );
  auto *restore = dock.findChild<QPushButton *>( QStringLiteral( "agentRestoreConversation" ) );
  REQUIRE( restore );
  const qsizetype taskCount = center.allTasks().size();
  restore->click();
  const QString labels = checkpointLabels( dock );
  CHECK( labels.contains( QStringLiteral( "task-9001 / job-9001" ) ) );
  CHECK( labels.contains( QStringLiteral( "pipeline-9002" ) ) );
  CHECK( labels.contains( QStringLiteral( "state is unknown" ) ) );
  CHECK( restore->text() == QStringLiteral( "Acknowledge and restore" ) );
  CHECK_FALSE( store.load( files.binding ).interruptedRunId.isEmpty() );
  restore->click();
  CHECK( store.load( files.binding ).interruptedRunId.isEmpty() );
  CHECK( store.load( files.binding ).executionRefs.isEmpty() );
  CHECK( center.allTasks().size() == taskCount );
  CHECK_FALSE( dock.isRunActive() );
  for ( auto *process : dock.findChildren<QProcess *>() ) CHECK( process->state() == QProcess::NotRunning );
}

TEST_CASE( "Pi Restore never interprets an empty execution reference list as zero work", "[agent][ui][checkpoint]" )
{
  ensureQtApp();
  tool_catalog::AgentToolCatalog::instance().initializeDefaults();
  CheckpointUiFixture files;
  auto &center = sicnu::TaskCenter::instance();
  DataManager data;
  AgentCopilotDockWidget dock( nullptr, center, files.prefix, files.storeDirectory );
  dock.setContext( &data, nullptr );
  QPointer<sicnu::agent::runtime::AgentConversationRuntime> previousTransport =
    dock.findChild<sicnu::agent::runtime::AgentConversationRuntime *>();
  REQUIRE( previousTransport );
  sicnu::agent::runtime::ConversationCheckpointStore store( files.storeDirectory );
  REQUIRE( store.markRunStarted( files.binding, QStringLiteral( "unobserved-run" ) ).error.isEmpty() );
  auto *restore = dock.findChild<QPushButton *>( QStringLiteral( "agentRestoreConversation" ) );
  REQUIRE( restore );
  const qsizetype taskCount = center.allTasks().size();
  restore->click();
  CHECK( checkpointLabels( dock ).contains( QStringLiteral( "No durable execution reference" ) ) );
  CHECK( checkpointLabels( dock ).contains( QStringLiteral( "state is unknown" ) ) );
  CHECK_FALSE( store.load( files.binding ).interruptedRunId.isEmpty() );
  restore->click();
  CHECK( store.load( files.binding ).interruptedRunId.isEmpty() );
  // With no completed checkpoint, acknowledgement ends the previous transport;
  // an unsuccessful first round must not leak its SDK history into a new turn.
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  CHECK( previousTransport.isNull() );
  CHECK( center.allTasks().size() == taskCount );
  CHECK_FALSE( dock.isRunActive() );
}
