#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/llm_streaming_client.h"
#include "app/application_language.h"
#include "support/application_translations.h"
#include "data/data_manager.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "support/qsettings_redirect.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>
#include <QScopeGuard>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <optional>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <qgsproject.h>

using namespace sicnu::agent;
using namespace sicnu::data;
using namespace sicnu::processing;

namespace {
exprs_test::QSettingsUserRootRedirect settingsRedirect;

void ensureQtApp()
{
  // Disable the OS credential store before constructing the product dock.
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  if ( !QApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_python_agent_gui_delivery";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "python-agent-gui-delivery" ) );
  QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), QStringLiteral( "native" ) );
}

} // namespace

TEST_CASE( "Approved Python GUI calls deliver the raster, lineage and terminal task row",
           "[agent][ui][python][real]" )
{
  ensureQtApp();
  const QByteArray environmentRoot = qgetenv( "SICNU_TEST_PYTHON_ENV_ROOT" );
  if ( environmentRoot.isEmpty() )
    SKIP( "Requires an explicitly prepared SICNU_TEST_PYTHON_ENV_ROOT; no implicit package installation." );
  const bool chinese = GENERATE( false, true );
  const bool piBackend = GENERATE( false, true );
  CAPTURE( chinese, piBackend );
  const QString piPrefix = qEnvironmentVariable( "SICNU_TEST_PI_PRODUCT_PREFIX" );
  if ( piBackend && piPrefix.isEmpty() )
  {
    if ( qEnvironmentVariableIntValue( "SICNU_TEST_REQUIRE_PI" ) == 1 )
      FAIL( "The required Pi GUI proof has no explicit installed product prefix." );
    SKIP( "Pi GUI integration requires an explicit installed Pi prefix." );
  }
  QSettings().setValue( QStringLiteral( "agent/conversationBackend" ), piBackend ? QStringLiteral( "pi" ) : QStringLiteral( "native" ) );
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  const QString previousProjectFile = QgsProject::instance()->fileName();
  const auto restoreProjectFile = qScopeGuard( [&] { QgsProject::instance()->setFileName( previousProjectFile ); } );
  REQUIRE( QgsProject::instance()->write( workspace.filePath( QStringLiteral( "ndvi-conversation.qgs" ) ) ) );
  const std::map<QByteArray, QByteArray> settings = {
    { "SICNU_PYTHON_ENV_ROOT", environmentRoot },
    { "SICNU_PYTHON_EXECUTION", "trusted_local" },
    { "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() },
    { "SICNU_CHECKPOINT_DIR", workspace.filePath( "checkpoints" ).toUtf8() },
    { "SICNU_WORKER_EXECUTION", "off" },
    { "SICNU_LANG", "" } };
  std::map<QByteArray, std::optional<QByteArray>> previous;
  for ( const auto &[name, value] : settings )
  {
    previous[name] = qEnvironmentVariableIsSet( name.constData() )
                       ? std::optional<QByteArray>( qgetenv( name.constData() ) ) : std::nullopt;
    qputenv( name.constData(), value );
  }
  const auto restoreEnvironment = qScopeGuard( [&] {
    for ( const auto &[name, value] : previous )
      value ? qputenv( name.constData(), *value ) : qunsetenv( name.constData() );
  } );

  QSettings uiSettings;
  const QString languageKey = QStringLiteral( "preferences/uiLanguage" );
  const bool hadLanguagePreference = uiSettings.contains( languageKey );
  const QVariant previousLanguage = uiSettings.value( languageKey );
  exprs_test::removeApplicationTranslators();
  uiSettings.setValue( languageKey, chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) );
  // Declare this before every data object and widget so translators remain
  // installed through their destruction, including on a failed assertion.
  const auto restoreTranslations = qScopeGuard( [&] {
    exprs_test::removeApplicationTranslators();
    if ( hadLanguagePreference )
      uiSettings.setValue( languageKey, previousLanguage );
    else
      uiSettings.remove( languageKey );
  } );
  const QString appliedLanguage = SicnuUi::installUiTranslations(
    *qApp, { QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) } );
  REQUIRE( appliedLanguage == ( chinese ? QStringLiteral( "zh_CN" ) : QStringLiteral( "en" ) ) );
  if ( chinese )
  {
    REQUIRE( qApp->findChild<QTranslator *>( QStringLiteral( "sicnuAppTranslator" ) ) != nullptr );
    REQUIRE( qApp->findChild<QTranslator *>( QStringLiteral( "sicnuQtTranslator" ) ) != nullptr );
  }

  AtomicAlgorithmRegistry::instance().initialize();
  auto probe = sicnu::operators::RSOperatorRegistry::instance().create( "python:environment" );
  REQUIRE( probe != nullptr );
  Json::Value probeParams( Json::objectValue );
  probeParams["action"] = "probe";
  sicnu::operators::RSOperatorContext context;
  const auto environment = probe->run( probeParams, context );
  INFO( environment.toStyledString() );
  REQUIRE( environment["ready"].asBool() );

  DataManager data;
  auto &center = sicnu::TaskCenter::instance();
  center.setCatalog( &data );
  const auto resetCatalog = qScopeGuard( [&] { center.setCatalog( nullptr ); } );
  ensureGdalInit();
  auto *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  const std::array<double, 6> grid = { 300000, 10, 0, 4000000, 0, -10 };
  const auto close = []( GDALDataset *dataset ) { GDALClose( dataset ); };
  const auto writeInput = [&]( const QString &path, std::array<float, 12> pixels ) {
    std::unique_ptr<GDALDataset, decltype( close )> raster(
      driver->Create( path.toUtf8().constData(), 4, 3, 1, GDT_Float32, nullptr ), close );
    REQUIRE( raster != nullptr );
    auto transform = grid;
    REQUIRE( raster->SetGeoTransform( transform.data() ) == CE_None );
    OGRSpatialReference crs;
    REQUIRE( crs.importFromEPSG( 32648 ) == OGRERR_NONE );
    char *wkt = nullptr;
    REQUIRE( crs.exportToWkt( &wkt ) == OGRERR_NONE );
    const auto projected = raster->SetProjection( wkt );
    CPLFree( wkt );
    REQUIRE( projected == CE_None );
    REQUIRE( raster->GetRasterBand( 1 )->SetNoDataValue( -9999 ) == CE_None );
    REQUIRE( raster->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 4, 3, pixels.data(),
                                                4, 3, GDT_Float32, 0, 0 ) == CE_None );
  };
  const QString before = workspace.filePath( "before.tif" );
  const QString after = workspace.filePath( "after.tif" );
  const QString output = workspace.filePath( "change_mask.tif" );
  writeInput( before, { .1f, .2f, -9999.f, .4f, .5f, .6f, .7f, .8f, .9f, .1f, -9999.f, .2f } );
  writeInput( after, { .4f, .25f, .8f, .8f, .55f, .9f, .72f, .5f, .85f, -9999.f, .9f, .21f } );
  QList<AssetId> inputAssets;
  for ( const auto &path : { before, after } )
  {
    RegisterRequest request;
    request.source.providerKey = QStringLiteral( "gdal" );
    request.source.canonicalSource = path;
    const auto registered = data.registerSource( request );
    REQUIRE_FALSE( registered.assetId.isNull() );
    inputAssets.append( registered.assetId );
  }

  Json::Value params( Json::objectValue );
  params["profile_id"] = "geospatial-v1";
  params["inputs"].append( before.toStdString() );
  params["inputs"].append( after.toStdString() );
  params["output"] = output.toStdString();
  params["script"] = R"PY(import numpy as np
import rasterio
assert isinstance(inputs, tuple) and all(isinstance(path, str) for path in inputs)
with rasterio.open(inputs[0]) as a, rasterio.open(inputs[1]) as b:
    before, after = a.read(1), b.read(1)
    valid = np.isfinite(before) & np.isfinite(after) & (before != a.nodata) & (after != b.nodata)
    mask = np.full(before.shape, 255, dtype=np.uint8)
    mask[valid] = (np.abs(after[valid] - before[valid]) > .2).astype(np.uint8)
    profile = a.profile.copy()
profile.update(dtype='uint8', nodata=255)
with rasterio.open(output, 'w', **profile) as target:
    target.write(mask, 1)
result.update(valid_pixels=int(valid.sum()), changed_pixels=int((mask == 1).sum()))
)PY";
  Json::StreamWriterBuilder writer;
  const QString callId = QStringLiteral( "python-gui-delivery" );
  const QJsonObject envelope{
    { QStringLiteral( "id" ), callId },
    { QStringLiteral( "type" ), QStringLiteral( "function" ) },
    { QStringLiteral( "function" ), QJsonObject{
      { QStringLiteral( "name" ), QStringLiteral( "python_run_script" ) },
      { QStringLiteral( "arguments" ), QString::fromStdString( Json::writeString( writer, params ) ) } } } };

  const QString checkpointDirectory = workspace.filePath( QStringLiteral( "pi-conversations" ) );
  AgentCopilotDockWidget dock( nullptr, center, piPrefix, checkpointDirectory );
  dock.setContext( &data, nullptr );
  const char *translationContext = dock.metaObject()->className();
  const QString completedLabel = QCoreApplication::translate( translationContext, "Completed" );
  const QString succeededLabel = QCoreApplication::translate( translationContext, "Succeeded" );
  if ( chinese )
  {
    CHECK( completedLabel != QStringLiteral( "Completed" ) );
    CHECK( succeededLabel != QStringLiteral( "Succeeded" ) );
  }
  else
  {
    CHECK( completedLabel == QStringLiteral( "Completed" ) );
    CHECK( succeededLabel == QStringLiteral( "Succeeded" ) );
  }
  // A local model fixture requests the real registered Python tool. Approval,
  // execution and transactional publication use the current GUI authorities.
  QTcpServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost ) );
  bool finalAnswerReceived = false;
  int followUpRequests = 0;
  QList<QJsonObject> modelRequests;
  QObject::connect( &server, &QTcpServer::newConnection, &server, [&] {
    while ( auto *socket = server.nextPendingConnection() )
    {
      QObject::connect( socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater );
      auto received = std::make_shared<QByteArray>();
      QObject::connect( socket, &QTcpSocket::readyRead, &server, [&, socket, received] {
        received->append( socket->readAll() );
        const auto headerEnd = received->indexOf( "\r\n\r\n" );
        if ( headerEnd < 0 || socket->property( "answered" ).toBool() )
          return;
        int contentLength = 0;
        for ( const auto &line : received->left( headerEnd ).split( '\n' ) )
          if ( line.toLower().startsWith( "content-length:" ) )
            contentLength = line.mid( 15 ).trimmed().toInt();
        if ( received->size() < headerEnd + 4 + contentLength )
          return;
        socket->setProperty( "answered", true );
        modelRequests.append( QJsonDocument::fromJson( received->mid( headerEnd + 4, contentLength ) ).object() );
        ++followUpRequests;
        QJsonObject delta;
        QString finish;
        if ( followUpRequests == 1 )
        {
          QJsonObject call = envelope;
          call[QStringLiteral( "index" )] = 0;
          delta[QStringLiteral( "tool_calls" )] = QJsonArray{ call };
          finish = QStringLiteral( "tool_calls" );
        }
        else
        {
          delta[QStringLiteral( "content" )] = QStringLiteral( "NDVI result delivered" );
          finish = QStringLiteral( "stop" );
        }
        const QByteArray body = "data: " + QJsonDocument( QJsonObject{
          { QStringLiteral( "choices" ), QJsonArray{ QJsonObject{
            { QStringLiteral( "delta" ), delta }, { QStringLiteral( "finish_reason" ), finish } } } } } )
          .toJson( QJsonDocument::Compact ) + "\n\ndata: [DONE]\n\n";
        socket->write( "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: "
                       + QByteArray::number( body.size() ) + "\r\nConnection: close\r\n\r\n" + body );
        socket->disconnectFromHost();
      } );
    }
  } );
  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( server.serverPort() );
  profile.modelName = QStringLiteral( "local-final-prose" );
  profile.apiKey = QStringLiteral( "public-loopback-fixture-key" );
  profile.id = QStringLiteral( "local-python-gui-model" );
  profile.name = QStringLiteral( "Local Python GUI model" );
  LlmConfigManager::setActiveProfile( profile );

  long taskId = -1;
  int pythonTaskAdds = 0;
  bool hostCompletedSignal = false;
  QObject observer;
  QObject::connect( &center, &sicnu::TaskCenter::taskAdded, &observer, [&]( const sicnu::AlgorithmTaskInfo &info ) {
    if ( info.algorithmId == QStringLiteral( "python:run_script" ) )
    {
      taskId = info.taskId;
      ++pythonTaskAdds;
    }
  } );
  QObject::connect( &center, &sicnu::TaskCenter::taskUpdated, &observer, [&]( const sicnu::AlgorithmTaskInfo &info ) {
    if ( info.taskId == taskId && info.status == sicnu::TaskStatus::Completed )
      hostCompletedSignal = true;
  } );
  const auto cancelPending = qScopeGuard( [&] {
    dock.stopForProjectChange( 5000 );
    if ( taskId >= 0 && !sicnu::isTerminalStatus( center.getTaskInfo( taskId ).status ) )
    {
      center.cancelTask( taskId );
      center.waitForTask( taskId, std::chrono::seconds( 5 ) );
    }
  } );
  const auto cardJson = [&] {
    const auto *details = dock.findChild<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) );
    return details ? details->text().toStdString() : std::string( "No tool call card details" );
  };
  dock.sendPrompt( QStringLiteral( "Compute the NDVI change mask with Python" ) );
  // The user card also proves admission when a backend settles synchronously
  // before the test can observe isRunActive(). Do not exit before admission.
  bool runStarted = dock.isRunActive()
                 || dock.findChild<QLabel *>( QStringLiteral( "agentUserMessage" ) ) != nullptr;
  QElapsedTimer approvalDeadline;
  approvalDeadline.start();
  while ( !dock.findChild<QWidget *>( QStringLiteral( "planApprovalCard" ) ) && approvalDeadline.elapsed() < 1030000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    runStarted |= dock.isRunActive();
    if ( runStarted && !dock.isRunActive() )
      break;
    QThread::msleep( 10 );
  }
  auto *approval = dock.findChild<QWidget *>( QStringLiteral( "planApprovalCard" ) );
  INFO( dock.runInspectorSummary().toStdString() );
  INFO( cardJson() );
  REQUIRE( approval != nullptr );
  QPushButton *runButton = nullptr;
  for ( auto *button : approval->findChildren<QPushButton *>() )
    if ( button->property( "primary" ).toBool() )
      runButton = button;
  REQUIRE( runButton != nullptr );
  CHECK( taskId == -1 );
  CHECK_FALSE( QFileInfo::exists( output ) );
  runButton->click();

  QElapsedTimer deadline;
  deadline.start();
  while ( ( !finalAnswerReceived || dock.isRunActive() ) && deadline.elapsed() < 120000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    for ( auto *label : dock.findChildren<QLabel *>() )
      finalAnswerReceived |= label->text().contains( QStringLiteral( "NDVI result delivered" ) );
    if ( taskId >= 0 && center.getTaskInfo( taskId ).status == sicnu::TaskStatus::Failed )
      break;
    QThread::msleep( 10 );
  }
  INFO( dock.runInspectorSummary().toStdString() );
  INFO( cardJson() );
  REQUIRE( taskId >= 0 );
  const auto task = center.getTaskInfo( taskId );
  INFO( task.errorMessage.toStdString() );
  REQUIRE( hostCompletedSignal );
  REQUIRE( task.status == sicnu::TaskStatus::Completed );
  CHECK( task.resultPayload["statistics"]["valid_pixels"].asInt() == 9 );
  CHECK( task.resultPayload["statistics"]["changed_pixels"].asInt() == 4 );
  CHECK( followUpRequests == 2 );
  CHECK( finalAnswerReceived );
  CHECK_FALSE( dock.isRunActive() );
  CHECK( dock.runInspectorSummary().contains( QStringLiteral( "stage=%1" ).arg( completedLabel ) ) );
  CHECK( runButton->text() == completedLabel );
  auto *title = dock.findChild<QLabel *>( QStringLiteral( "ToolCallCardTitle" ) );
  REQUIRE( title != nullptr );
  CHECK( title->text().contains( succeededLabel ) );

  REQUIRE( QFileInfo::exists( output ) );
  const auto asset = data.findByPath( output );
  REQUIRE( asset.has_value() );
  REQUIRE( data.assets().size() == 3 );
  auto *details = dock.findChild<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) );
  REQUIRE( details != nullptr );
  CHECK( details->text().contains( output ) );
  CHECK( details->text().contains( asset->id().toString() ) );
  const auto provenance = data.provenance( asset->id() );
  REQUIRE( provenance.has_value() );
  REQUIRE( provenance->inputs.size() == 2 );
  for ( const auto &input : inputAssets )
    CHECK( std::any_of( provenance->inputs.cbegin(), provenance->inputs.cend(),
                       [&]( const DerivationInput &edge ) { return edge.assetId == input; } ) );
  CHECK_FALSE( provenance->parameters.contains( QStringLiteral( "script" ) ) );
  const auto execution = provenance->parameters.value( QStringLiteral( "python_execution" ) ).toObject();
  CHECK( execution.value( QStringLiteral( "profile_id" ) ).toString() == QStringLiteral( "geospatial-v1" ) );
  CHECK( execution.value( QStringLiteral( "python_version" ) ).toString().startsWith( QStringLiteral( "3.12." ) ) );
  CHECK( execution.value( QStringLiteral( "script_sha256" ) ).toString().size() == 64 );
  CHECK( execution.value( QStringLiteral( "environment_sha256" ) ).toString().size() == 64 );
  CHECK( QDir( workspace.path() ).entryList( { ".exprs-python-*" }, QDir::Dirs | QDir::Hidden ).isEmpty() );
  std::unique_ptr<GDALDataset, decltype( close )> raster(
    static_cast<GDALDataset *>( GDALOpen( output.toUtf8().constData(), GA_ReadOnly ) ), close );
  REQUIRE( raster != nullptr );
  REQUIRE( raster->GetRasterXSize() == 4 );
  REQUIRE( raster->GetRasterYSize() == 3 );
  CHECK( raster->GetRasterBand( 1 )->GetNoDataValue() == 255 );
  CHECK( raster->GetRasterBand( 1 )->GetRasterDataType() == GDT_Byte );
  std::array<double, 6> actualGrid;
  REQUIRE( raster->GetGeoTransform( actualGrid.data() ) == CE_None );
  CHECK( actualGrid == grid );
  OGRSpatialReference actualCrs( raster->GetProjectionRef() ), expectedCrs;
  REQUIRE( expectedCrs.importFromEPSG( 32648 ) == OGRERR_NONE );
  CHECK( actualCrs.IsSame( &expectedCrs ) );
  std::array<unsigned char, 12> actual;
  REQUIRE( raster->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, 4, 3, actual.data(),
                                              4, 3, GDT_Byte, 0, 0 ) == CE_None );
  const std::array<unsigned char, 12> expected = { 1, 0, 255, 1, 0, 1, 0, 1, 0, 255, 255, 0 };
  CHECK( actual == expected );

  // A new Chinese dock restores the completed English Pi conversation from
  // the same saved project. Initialization must not contact the model or
  // replay the native write. Only a new explicit prompt may continue it.
  if ( piBackend && !chinese )
  {
    REQUIRE( pythonTaskAdds == 1 );
    const int requestsBeforeRestore = followUpRequests;
    const auto taskCountBeforeRestore = center.allTasks().size();
    exprs_test::removeApplicationTranslators();
    uiSettings.setValue( languageKey, QStringLiteral( "zh_CN" ) );
    REQUIRE( SicnuUi::installUiTranslations( *qApp, { QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) } ) == QStringLiteral( "zh_CN" ) );
    AgentCopilotDockWidget restored( nullptr, center, piPrefix, checkpointDirectory );
    restored.setContext( &data, nullptr );
    auto *restoreButton = restored.findChild<QPushButton *>( QStringLiteral( "agentRestoreConversation" ) );
    REQUIRE( restoreButton != nullptr );
    CHECK( restoreButton->text() != QStringLiteral( "Restore conversation" ) );
    restoreButton->click();
    QElapsedTimer restoreDeadline;
    restoreDeadline.start();
    while ( restored.isRunActive() && restoreDeadline.elapsed() < 1030000 )
    {
      QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
      QThread::msleep( 10 );
    }
    INFO( restored.runInspectorSummary().toStdString() );
    REQUIRE_FALSE( restored.isRunActive() );
    CHECK( followUpRequests == requestsBeforeRestore );
    CHECK( pythonTaskAdds == 1 );
    CHECK( center.allTasks().size() == taskCountBeforeRestore );
    CHECK( data.assets().size() == 3 );
    bool restoredUserText = false;
    for ( auto *label : restored.findChildren<QLabel *>( QStringLiteral( "agentUserMessage" ) ) )
      restoredUserText |= label->text().contains( QStringLiteral( "Compute the NDVI change mask with Python" ) );
    REQUIRE( restoredUserText );
    restored.sendPrompt( QStringLiteral( "Summarize the earlier result. Do not run any tools." ) );
    QElapsedTimer resumedDeadline;
    resumedDeadline.start();
    while ( restored.isRunActive() && resumedDeadline.elapsed() < 120000 )
    {
      QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
      QThread::msleep( 10 );
    }
    INFO( restored.runInspectorSummary().toStdString() );
    REQUIRE_FALSE( restored.isRunActive() );
    REQUIRE( followUpRequests == requestsBeforeRestore + 1 );
    CHECK( pythonTaskAdds == 1 );
    CHECK( center.allTasks().size() == taskCountBeforeRestore );
    CHECK( data.assets().size() == 3 );
    REQUIRE( modelRequests.size() == followUpRequests );
    const QByteArray resumedMessages = QJsonDocument( modelRequests.last().value( QStringLiteral( "messages" ) ).toArray() ).toJson( QJsonDocument::Compact );
    CHECK( resumedMessages.contains( asset->id().toString().toUtf8() ) );
    CHECK( resumedMessages.contains( "Summarize the earlier result" ) );
    CHECK( resumedMessages.contains( "zh_CN" ) );
  }
}
