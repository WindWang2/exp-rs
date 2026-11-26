// tests/test_r31_t02_core.cpp
// R31-T02 core integration & security verification tests on origin/master:
//   1. AS-015 endpoint security parity across LlmStreamingClient, PiConversationRuntime
//      and PiProcess (loopback http allowed, remote plaintext http & malformed URLs
//      refused fail-fast in C++ before receipt verification or child spawn).
//   2. Product AgentToolHost single-writer DataManager lease & Python pipeline rejection.
//   3. ConversationCheckpointStore checksum integrity, tamper refusal & interruption refs.
//   4. Managed Python negative refusal, ExternalProcess hardening & live Python GDAL/NumPy
//      deterministic 4x3 GeoTIFF NDVI pixel verification.

#include <QJsonValue>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "agent/llm_config_manager.h"
#include "agent/llm_streaming_client.h"
#include "agent/runtime/agent_tool_host.h"
#include "agent/runtime/conversation_checkpoint_store.h"
#include "agent/runtime/pi_conversation_runtime.h"
#include "agent/runtime/pi_process.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "data/data_manager.h"
#include "exprs/external_process.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "support/application_translations.h"
#include "support/qsettings_redirect.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <array>
#include <memory>
#include <vector>

using namespace sicnu::agent;
using namespace sicnu::agent::runtime;
using namespace sicnu::data;
using namespace sicnu::processing;

namespace
{

exprs_test::QSettingsUserRootRedirect s_settingsRedirect;

void ensureQtApp()
{
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  if ( !QApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_r31_t02_core";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "r31-t02-core" ) );
}

struct ScopedEnv
{
  const char *key;
  bool hadPrev;
  QByteArray prev;

  ScopedEnv( const char *k, const QByteArray &val )
    : key( k ), hadPrev( qEnvironmentVariableIsSet( k ) ), prev( qgetenv( k ) )
  {
    qputenv( key, val );
  }
  explicit ScopedEnv( const char *k )
    : key( k ), hadPrev( qEnvironmentVariableIsSet( k ) ), prev( qgetenv( k ) )
  {
    qunsetenv( key );
  }
  ~ScopedEnv()
  {
    if ( hadPrev )
      qputenv( key, prev );
    else
      qunsetenv( key );
  }
};

QJsonObject fixtureTool( const QString &name = QStringLiteral( "python_run_script" ),
                         const QString &canonical = QStringLiteral( "python:run_script" ) )
{
  return {
    { QStringLiteral( "name" ), name },
    { QStringLiteral( "canonicalId" ), canonical },
    { QStringLiteral( "description" ), QStringLiteral( "AS-015 fixture tool" ) },
    { QStringLiteral( "parameters" ), QJsonObject{ { QStringLiteral( "type" ), QStringLiteral( "object" ) } } }
  };
}

ConversationConfig basePiConfig()
{
  ConversationConfig cfg;
  cfg.sessionId = QStringLiteral( "as015-session" );
  cfg.profile.id = QStringLiteral( "as015-profile" );
  cfg.profile.name = QStringLiteral( "AS-015 Fixture" );
  cfg.profile.modelName = QStringLiteral( "fixture-model" );
  cfg.profile.apiKey = QStringLiteral( "sk-secret-pi-as015-never-leak" );
  cfg.systemPrompt = QStringLiteral( "Use only explicit host tools." );
  cfg.toolRevision = QStringLiteral( "catalog-v1" );
  cfg.tools = { fixtureTool() };
  return cfg;
}

QByteArray sampleCheckpointPayload( const ConversationCheckpointBinding &binding )
{
  const QString ts = QStringLiteral( "2026-11-16T00:00:00.000Z" );
  const QJsonObject userEntry{
    { QStringLiteral( "type" ), QStringLiteral( "message" ) },
    { QStringLiteral( "id" ), QStringLiteral( "entry-u1" ) },
    { QStringLiteral( "parentId" ), QJsonValue::Null },
    { QStringLiteral( "timestamp" ), ts },
    { QStringLiteral( "message" ), QJsonObject{
      { QStringLiteral( "role" ), QStringLiteral( "user" ) },
      { QStringLiteral( "content" ), QStringLiteral( "Compute NDVI" ) },
      { QStringLiteral( "timestamp" ), 1 }
    } }
  };
  const QJsonObject assistantEntry{
    { QStringLiteral( "type" ), QStringLiteral( "message" ) },
    { QStringLiteral( "id" ), QStringLiteral( "entry-a1" ) },
    { QStringLiteral( "parentId" ), QStringLiteral( "entry-u1" ) },
    { QStringLiteral( "timestamp" ), ts },
    { QStringLiteral( "message" ), QJsonObject{
      { QStringLiteral( "role" ), QStringLiteral( "assistant" ) },
      { QStringLiteral( "content" ), QJsonArray{ QJsonObject{
        { QStringLiteral( "type" ), QStringLiteral( "text" ) },
        { QStringLiteral( "text" ), QStringLiteral( "NDVI delivered." ) }
      } } },
      { QStringLiteral( "api" ), QStringLiteral( "openai-completions" ) },
      { QStringLiteral( "provider" ), QStringLiteral( "exp-rs-product" ) },
      { QStringLiteral( "model" ), QStringLiteral( "fixture-model" ) },
      { QStringLiteral( "usage" ), QJsonObject{
        { QStringLiteral( "input" ), 10 },
        { QStringLiteral( "output" ), 5 },
        { QStringLiteral( "cacheRead" ), 0 },
        { QStringLiteral( "cacheWrite" ), 0 },
        { QStringLiteral( "totalTokens" ), 15 },
        { QStringLiteral( "cost" ), QJsonObject{
          { QStringLiteral( "input" ), 0 },
          { QStringLiteral( "output" ), 0 },
          { QStringLiteral( "cacheRead" ), 0 },
          { QStringLiteral( "cacheWrite" ), 0 },
          { QStringLiteral( "total" ), 0 }
        } }
      } },
      { QStringLiteral( "stopReason" ), QStringLiteral( "stop" ) },
      { QStringLiteral( "timestamp" ), 2 }
    } }
  };
  const QJsonObject doc{
    { QStringLiteral( "schema" ), QStringLiteral( "sicnu.pi-checkpoint/1" ) },
    { QStringLiteral( "piVersion" ), QStringLiteral( "1.5.2" ) },
    { QStringLiteral( "checkpointId" ), QStringLiteral( "ckpt-1" ) },
    { QStringLiteral( "completedRunId" ), QStringLiteral( "run-1" ) },
    { QStringLiteral( "projectId" ), binding.projectId },
    { QStringLiteral( "conversationId" ), binding.conversationId },
    { QStringLiteral( "sdkReceiptSha256" ), binding.sdkReceiptSha256 },
    { QStringLiteral( "toolRevision" ), binding.toolRevision },
    { QStringLiteral( "systemPromptSha256" ), binding.systemPromptSha256 },
    { QStringLiteral( "status" ), QStringLiteral( "completed" ) },
    { QStringLiteral( "header" ), QJsonObject{
      { QStringLiteral( "type" ), QStringLiteral( "session" ) },
      { QStringLiteral( "version" ), 3 },
      { QStringLiteral( "id" ), QStringLiteral( "sdk-session-1" ) },
      { QStringLiteral( "timestamp" ), ts },
      { QStringLiteral( "cwd" ), QStringLiteral( "/tmp/owned" ) }
    } },
    { QStringLiteral( "entries" ), QJsonArray{ userEntry, assistantEntry } },
    { QStringLiteral( "leafId" ), QStringLiteral( "entry-a1" ) }
  };
  return QJsonDocument( doc ).toJson( QJsonDocument::Compact );
}

struct GdalDatasetCloser
{
  void operator()( GDALDataset *ds ) const
  {
    if ( ds )
      GDALClose( static_cast<GDALDatasetH>( ds ) );
  }
};

using GdalDatasetPtr = std::unique_ptr<GDALDataset, GdalDatasetCloser>;

bool writeTwoBandRedNirRaster( const QString &path, int width, int height, float redVal, float nirVal )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GdalDatasetPtr ds( driver->Create( path.toUtf8().constData(), width, height, 2, GDT_Float32, nullptr ) );
  if ( !ds )
    return false;
  std::array<double, 6> gt = { 300000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
  ds->SetGeoTransform( gt.data() );
  OGRSpatialReference srs;
  srs.importFromEPSG( 32648 );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  ds->SetProjection( wkt );
  CPLFree( wkt );

  std::vector<float> red( static_cast<size_t>( width ) * height, redVal );
  std::vector<float> nir( static_cast<size_t>( width ) * height, nirVal );
  const CPLErr r1 = ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, width, height, red.data(), width, height, GDT_Float32, 0, 0 );
  const CPLErr r2 = ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, width, height, nir.data(), width, height, GDT_Float32, 0, 0 );
  return r1 == CE_None && r2 == CE_None;
}

} // namespace

TEST_CASE( "R31-T02 core: AS-015 endpoint security parity across LlmStreamingClient, PiConversationRuntime and PiProcess",
           "[r31_t02][core][security][pi]" )
{
  ensureQtApp();
  exprs_test::removeApplicationTranslators();

  CHECK( LlmStreamingClient::isLoopbackHost( QStringLiteral( "localhost" ) ) );
  CHECK( LlmStreamingClient::isLoopbackHost( QStringLiteral( "LOCALHOST" ) ) );
  CHECK( LlmStreamingClient::isLoopbackHost( QStringLiteral( "127.0.0.1" ) ) );
  CHECK( LlmStreamingClient::isLoopbackHost( QStringLiteral( "127.9.8.7" ) ) );
  CHECK( LlmStreamingClient::isLoopbackHost( QStringLiteral( "::1" ) ) );
  CHECK( LlmStreamingClient::isLoopbackHost( QStringLiteral( "[::1]" ) ) );
  CHECK( LlmStreamingClient::isLoopbackHost( QStringLiteral( "::ffff:127.0.0.1" ) ) );
  CHECK_FALSE( LlmStreamingClient::isLoopbackHost( QStringLiteral( "api.example.invalid" ) ) );
  CHECK_FALSE( LlmStreamingClient::isLoopbackHost( QStringLiteral( "localhost.evil.invalid" ) ) );
  CHECK_FALSE( LlmStreamingClient::isLoopbackHost( QStringLiteral( "127.0.0.1.evil.invalid" ) ) );
  CHECK_FALSE( LlmStreamingClient::isLoopbackHost( QStringLiteral( "192.0.2.1" ) ) );
  CHECK_FALSE( LlmStreamingClient::isLoopbackHost( QStringLiteral( "2001:db8::1" ) ) );

  const QStringList blockedUrls{
    QStringLiteral( "http://api.example.invalid/v1" ),
    QStringLiteral( "HTTP://api.example.invalid/v1" ),
    QStringLiteral( "http://192.0.2.10:8080/v1" ),
    QStringLiteral( "http://[2001:db8::1]/v1" ),
    QStringLiteral( "http://localhost.evil.invalid/v1" ),
    QStringLiteral( "http://127.0.0.1.evil.invalid/v1" ),
    QStringLiteral( "https://user:pass@model.invalid/v1" ),
    QStringLiteral( "https://user@model.invalid/v1" ),
    QStringLiteral( "https://model.invalid/v1?token=leak" ),
    QStringLiteral( "https://model.invalid/v1#frag" ),
    QStringLiteral( "ftp://model.invalid/v1" ),
    QString()
  };

  QTemporaryDir prefixDir;
  REQUIRE( prefixDir.isValid() );

  for ( const QString &badUrl : blockedUrls )
  {
    INFO( badUrl.toStdString() );
    CHECK_FALSE( LlmStreamingClient::endpointSecurityRefusal( badUrl, true, true ).isEmpty() );

    PiConversationRuntime runtime( prefixDir.path() );
    QString statusCode;
    QString errorMsg;
    int settledCount = 0;
    QObject::connect( &runtime, &AgentConversationRuntime::statusChanged,
                      [&]( const QString &, const QString &code ) { statusCode = code; } );
    QObject::connect( &runtime, &AgentConversationRuntime::settled,
                      [&]( const QString &run, bool aborted, const QString &err ) {
                        CHECK( run.isEmpty() );
                        CHECK( aborted );
                        errorMsg = err;
                        ++settledCount;
                      } );

    ConversationConfig cfg = basePiConfig();
    cfg.profile.baseUrl = badUrl;
    runtime.startSession( cfg );

    CHECK( settledCount == 1 );
    CHECK( statusCode == QStringLiteral( "insecure_endpoint" ) );
    CHECK_FALSE( errorMsg.contains( cfg.profile.apiKey ) );
    CHECK_FALSE( runtime.isReady() );
    CHECK_FALSE( runtime.submitPrompt( QStringLiteral( "run-blocked" ), QStringLiteral( "Must not run" ) ) );

    PiProcess proc;
    QString failedCode;
    QString failedMsg;
    QObject::connect( &proc, &PiProcess::failed,
                      [&]( const QString &code, const QString &msg ) {
                        failedCode = code;
                        failedMsg = msg;
                      } );
    PiProcess::Config pCfg;
    pCfg.prefixPath = prefixDir.path();
    pCfg.sessionId = QStringLiteral( "proc-as015" );
    pCfg.generation = QStringLiteral( "gen-1" );
    pCfg.apiKey = QByteArray( "sk-proc-secret-never-leak" );
    pCfg.profile = {
      { QStringLiteral( "baseUrl" ), badUrl },
      { QStringLiteral( "model" ), QStringLiteral( "fixture" ) },
      { QStringLiteral( "contextWindow" ), 32768 },
      { QStringLiteral( "maxTokens" ), 4096 }
    };
    pCfg.systemPrompt = QStringLiteral( "AS-015 process check." );
    pCfg.tools = { fixtureTool() };
    proc.start( pCfg );

    CHECK( proc.state() == PiProcess::State::Closed );
    CHECK( failedCode == QStringLiteral( "insecure_endpoint" ) );
    CHECK_FALSE( failedMsg.contains( "sk-proc-secret-never-leak" ) );
  }

  // Native HTTP without an API key does not leak a credential, while Pi still refuses plaintext remote HTTP.
  CHECK( LlmStreamingClient::endpointSecurityRefusal( QStringLiteral( "http://api.example.invalid/v1" ), false, false ).isEmpty() );
  {
    PiConversationRuntime runtime( prefixDir.path() );
    QString statusCode;
    QObject::connect( &runtime, &AgentConversationRuntime::statusChanged,
                      [&]( const QString &, const QString &code ) { statusCode = code; } );
    ConversationConfig noKeyCfg = basePiConfig();
    noKeyCfg.profile.baseUrl = QStringLiteral( "http://api.example.invalid/v1" );
    noKeyCfg.profile.apiKey.clear();
    runtime.startSession( noKeyCfg );
    CHECK( statusCode == QStringLiteral( "insecure_endpoint" ) );

    PiProcess proc;
    QString failedCode;
    QObject::connect( &proc, &PiProcess::failed, [&]( const QString &c, const QString & ) { failedCode = c; } );
    PiProcess::Config pCfg;
    pCfg.prefixPath = prefixDir.path();
    pCfg.sessionId = QStringLiteral( "proc-nokey-http" );
    pCfg.generation = QStringLiteral( "gen-1" );
    pCfg.profile = {
      { QStringLiteral( "baseUrl" ), QStringLiteral( "http://api.example.invalid/v1" ) },
      { QStringLiteral( "model" ), QStringLiteral( "fixture" ) },
      { QStringLiteral( "contextWindow" ), 32768 },
      { QStringLiteral( "maxTokens" ), 4096 }
    };
    pCfg.systemPrompt = QStringLiteral( "AS-015 missing-key HTTP check." );
    pCfg.tools = { fixtureTool() };
    proc.start( pCfg );
    CHECK( failedCode == QStringLiteral( "insecure_endpoint" ) );
  }

  // Native LlmStreamingClient::buildChatRequest & sendChatCompletion refuse plaintext HTTP + API key before network I/O.
  for ( const QString &nativeBadUrl : { QStringLiteral( "http://api.example.invalid/v1" ),
                                        QStringLiteral( "HTTP://api.example.invalid/v1" ) } )
  {
    LlmProviderProfile nativeProfile;
    nativeProfile.baseUrl = nativeBadUrl;
    nativeProfile.modelName = QStringLiteral( "native-fixture" );
    nativeProfile.apiKey = QStringLiteral( "sk-native-secret-do-not-send" );

    const QJsonArray msgs{ QJsonObject{
      { QStringLiteral( "role" ), QStringLiteral( "user" ) },
      { QStringLiteral( "content" ), QStringLiteral( "ping" ) }
    } };
    const ChatRequestPayload reqPayload = LlmStreamingClient::buildChatRequest( nativeProfile, msgs );
    CHECK( reqPayload.refusalReason.contains( QStringLiteral( "plaintext http" ) ) );
    CHECK( reqPayload.request.rawHeader( "Authorization" ).isEmpty() );

    LlmStreamingClient client;
    client.setProfile( nativeProfile );
    QString emittedError;
    int finishedCount = 0;
    QObject::connect( &client, &LlmStreamingClient::errorOccurred,
                      [&]( const QString &err ) { emittedError = err; } );
    QObject::connect( &client, &LlmStreamingClient::finished, [&] { ++finishedCount; } );
    client.sendChatCompletion( msgs );
    CHECK( finishedCount == 1 );
    CHECK( emittedError.contains( QStringLiteral( "plaintext http" ) ) );
    CHECK_FALSE( emittedError.contains( nativeProfile.apiKey ) );
  }

  for ( const QString &allowedUrl : {
          QStringLiteral( "https://model.invalid/v1" ),
          QStringLiteral( "http://localhost:11434/v1" ),
          QStringLiteral( "http://LOCALHOST/v1" ),
          QStringLiteral( "HTTP://localhost:11434/v1" ),
          QStringLiteral( "http://127.0.0.1:9/v1" ),
          QStringLiteral( "http://127.9.8.7/v1" ),
          QStringLiteral( "http://[::1]:8080/v1" ),
          QStringLiteral( "http://[::ffff:127.0.0.1]:8080/v1" )
        } )
  {
    INFO( allowedUrl.toStdString() );
    CHECK( LlmStreamingClient::endpointSecurityRefusal( allowedUrl, true, true ).isEmpty() );

    // Absolute empty prefix advances past AS-015 synchronously and fails only asynchronously during verification.
    PiConversationRuntime runtime( prefixDir.path() );
    QStringList statuses;
    int settledCount = 0;
    QObject::connect( &runtime, &AgentConversationRuntime::statusChanged,
                      [&]( const QString &, const QString &code ) { statuses.append( code ); } );
    QObject::connect( &runtime, &AgentConversationRuntime::settled,
                      [&]( const QString &, bool, const QString & ) { ++settledCount; } );
    ConversationConfig cfg = basePiConfig();
    cfg.profile.baseUrl = allowedUrl;
    runtime.startSession( cfg );
    CHECK( statuses.isEmpty() );
    CHECK( settledCount == 0 );

    QElapsedTimer timer;
    timer.start();
    while ( settledCount == 0 && timer.elapsed() < 10000 )
    {
      QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    }
    CHECK( settledCount == 1 );
    CHECK_FALSE( statuses.contains( QStringLiteral( "insecure_endpoint" ) ) );
  }
}

TEST_CASE( "R31-T02 core: AgentToolHost single-writer DataManager lease and python:run_script workflow isolation",
           "[r31_t02][core][host]" )
{
  ensureQtApp();
  AtomicAlgorithmRegistry::instance().initialize();
  sicnu::agent::tool_catalog::AgentToolCatalog::instance().initializeDefaults();

  DataManager data;
  auto &taskCenter = sicnu::TaskCenter::instance();
  AgentToolHost hostA( taskCenter );
  AgentToolHost hostB( taskCenter );
  REQUIRE( hostA.setContext( &data ) );
  REQUIRE( hostB.setContext( &data ) );

  const auto catalog = hostA.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );

  REQUIRE( hostA.beginRun( QStringLiteral( "run-lease-a" ), catalog.revision ) );
  CHECK_FALSE( hostB.beginRun( QStringLiteral( "run-lease-b" ), catalog.revision ) );

  // Generic run_workflow plans cannot smuggle python:run_script around separate Python approval.
  QJsonObject completedResult;
  QObject::connect( &hostA, &AgentToolHost::toolCompleted,
                    [&]( const QString &, const QString &, const QJsonObject &res ) { completedResult = res; } );
  const QJsonObject smuggledPlan{
    { QStringLiteral( "id" ), QStringLiteral( "call-plan-python" ) },
    { QStringLiteral( "type" ), QStringLiteral( "function" ) },
    { QStringLiteral( "function" ), QJsonObject{
      { QStringLiteral( "name" ), QStringLiteral( "run_workflow" ) },
      { QStringLiteral( "arguments" ), QJsonObject{
        { QStringLiteral( "pipeline" ), QJsonObject{
          { QStringLiteral( "id" ), QStringLiteral( "p1" ) },
          { QStringLiteral( "steps" ), QJsonArray{ QJsonObject{
            { QStringLiteral( "id" ), QStringLiteral( "s1" ) },
            { QStringLiteral( "operatorId" ), QStringLiteral( "python:run_script" ) },
            { QStringLiteral( "params" ), QJsonObject{} }
          } } }
        } }
      } }
    } }
  };
  hostA.requestTool( QStringLiteral( "run-lease-a" ), smuggledPlan, catalog.revision );
  hostA.finishConversation( QStringLiteral( "run-lease-a" ) );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
  CHECK( completedResult.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "error" ) );
  CHECK( completedResult.value( QStringLiteral( "errorCode" ) ).toString() == QStringLiteral( "submit_python_separately" ) );
  CHECK( hostA.isRunIdle( QStringLiteral( "run-lease-a" ) ) );

  // Once hostA is idle, hostB can acquire the single-writer DataManager lease.
  REQUIRE( hostB.beginRun( QStringLiteral( "run-lease-b" ), catalog.revision ) );
  hostB.finishConversation( QStringLiteral( "run-lease-b" ) );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
  CHECK( hostB.isRunIdle( QStringLiteral( "run-lease-b" ) ) );
}

TEST_CASE( "R31-T02 core: ConversationCheckpointStore tamper refusal and interrupted execution marker preservation",
           "[r31_t02][core][checkpoint]" )
{
  ensureQtApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString storeDir = dir.filePath( QStringLiteral( "ckpt-store" ) );
  ConversationCheckpointStore store( storeDir );
  const ConversationCheckpointBinding binding{
    QStringLiteral( "project-abc" ),
    QStringLiteral( "11111111-1111-4111-8111-111111111111" ),
    QString( 64, QLatin1Char( 'a' ) ),
    QStringLiteral( "rev-1" ),
    QString( 64, QLatin1Char( 'b' ) )
  };

  const QByteArray payload = sampleCheckpointPayload( binding );
  REQUIRE( static_cast<bool>( store.saveCompleted( binding, payload ) ) );

  auto loaded = store.load( binding );
  REQUIRE( static_cast<bool>( loaded ) );
  CHECK( loaded.found );
  CHECK( loaded.checkpointJson == payload );

  const auto files = QDir( storeDir ).entryList( { QStringLiteral( "*.conversation.json" ) }, QDir::Files );
  REQUIRE( files.size() == 1 );
  const QString storedPath = QDir( storeDir ).filePath( files.first() );
#ifdef Q_OS_UNIX
  const QFile::Permissions perms = QFileInfo( storedPath ).permissions();
  CHECK( ( perms & ( QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther ) ) == QFile::Permissions() );
#endif

  // Marking a new started run & recording execution refs preserves earlier completed checkpoint.
  const QString bootUuid = QStringLiteral( "22222222-2222-4222-8222-222222222222" );
  REQUIRE( static_cast<bool>( store.markRunStarted( binding, QStringLiteral( "run-interrupted" ) ) ) );
  REQUIRE( static_cast<bool>( store.recordOwnedExecutions(
    binding, QStringLiteral( "run-interrupted" ),
    { { ConversationExecutionReference::Kind::Task, 42, QStringLiteral( "job-42" ), bootUuid } } ) ) );
  loaded = store.load( binding );
  REQUIRE( static_cast<bool>( loaded ) );
  CHECK( loaded.interruptedRunId == QStringLiteral( "run-interrupted" ) );
  REQUIRE( loaded.executionRefs.size() == 1 );
  CHECK( loaded.executionRefs.first().jobId == QStringLiteral( "job-42" ) );
  CHECK( loaded.checkpointJson == payload );

  // Corrupting stored payload refuses load instead of returning tampered data.
  {
    QFile f( storedPath );
    REQUIRE( f.open( QIODevice::ReadWrite ) );
    QByteArray raw = f.readAll();
    const qsizetype idx = raw.indexOf( "NDVI delivered." );
    REQUIRE( idx > 0 );
    raw[idx] = 'X';
    f.seek( 0 );
    f.write( raw );
    f.resize( raw.size() );
  }
  const auto tampered = store.load( binding );
  CHECK_FALSE( tampered.error.isEmpty() );
  CHECK( tampered.checkpointJson.isEmpty() );
}

TEST_CASE( "R31-T02 core: Managed Python refusal and ExternalProcess boundary hardening",
           "[r31_t02][core][python][process][security]" )
{
  ensureQtApp();
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  ScopedEnv envWs( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
  ScopedEnv envExec( "SICNU_PYTHON_EXECUTION" );
  ScopedEnv envRoot( "SICNU_PYTHON_ENV_ROOT" );

  // PythonScriptOperator refuses without explicit trusted_local opt-in.
  AtomicAlgorithmRegistry::instance().initialize();
  auto scriptOp = sicnu::operators::RSOperatorRegistry::instance().create( "python:run_script" );
  REQUIRE( scriptOp != nullptr );
  {
    Json::Value params( Json::objectValue );
    params["script"] = "print('blocked')";
    params["output"] = workspace.filePath( QStringLiteral( "blocked.tif" ) ).toStdString();
    sicnu::operators::RSOperatorContext ctx;
    CHECK_THROWS( scriptOp->run( params, ctx ) );
  }

  // ExternalProcess refuses LD_PRELOAD loader injection before spawn.
  {
    exprs::ExternalProcessRequest req;
    req.argv = { "/bin/echo", "hi" };
    req.workingDirectory = workspace.path().toStdString();
    req.environment["LD_PRELOAD"] = "/tmp/evil.so";
    const exprs::ExternalProcessResult res = exprs::ExternalProcess::run( req );
    CHECK_FALSE( res.started );
    CHECK( res.refusedByPolicy );
  }

  // ExternalProcess refuses paths escaping SICNU_MCP_WORKSPACE.
  {
    QTemporaryDir outside;
    REQUIRE( outside.isValid() );
    exprs::ExternalProcessRequest req;
    req.argv = { "/usr/bin/env", outside.filePath( QStringLiteral( "outside.txt" ) ).toStdString() };
    req.workingDirectory = workspace.path().toStdString();
    const exprs::ExternalProcessResult res = exprs::ExternalProcess::run( req );
    CHECK_FALSE( res.started );
    CHECK( res.refusedByPolicy );
  }
}

TEST_CASE( "R31-T02 core: ExternalProcess live Python GDAL/NumPy 4x3 GeoTIFF NDVI",
           "[r31_t02][core][python][gdal]" )
{
  ensureQtApp();
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  ScopedEnv envWs( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
  ScopedEnv envExec( "SICNU_PYTHON_EXECUTION" );
  ScopedEnv envRoot( "SICNU_PYTHON_ENV_ROOT" );

  const QString configuredPython = qEnvironmentVariable( "SICNU_PYTHON_EXECUTABLE" );
  QString pythonBin = configuredPython;
  if ( !configuredPython.isEmpty() )
  {
    REQUIRE( QDir::isAbsolutePath( configuredPython ) );
    REQUIRE( QFileInfo( configuredPython ).isFile() );
  }
#ifdef SICNU_TEST_PYTHON_EXECUTABLE
  const QString compiledPy = QStringLiteral( SICNU_TEST_PYTHON_EXECUTABLE );
  if ( pythonBin.isEmpty() && !compiledPy.isEmpty() && QFileInfo::exists( compiledPy ) )
    pythonBin = compiledPy;
#endif
  if ( pythonBin.isEmpty() )
    pythonBin = QStandardPaths::findExecutable( QStringLiteral( "python3" ) );
  if ( pythonBin.isEmpty() )
    SKIP( "No host python3 executable found for external GeoTIFF NDVI check." );

  {
    exprs::ExternalProcessRequest probeReq;
    probeReq.argv = { pythonBin.toStdString(), "-I", "-B", "-c", "from osgeo import gdal; import numpy; print('ok')" };
    probeReq.workingDirectory = workspace.path().toStdString();
    probeReq.timeoutSeconds = 20;
    const exprs::ExternalProcessResult probeRes = exprs::ExternalProcess::run( probeReq );
    INFO( probeRes.stdErr );
    INFO( probeRes.error );
    if ( !configuredPython.isEmpty() )
      REQUIRE( probeRes.exitedCleanly() );
    if ( !probeRes.exitedCleanly() )
      SKIP( "Host python3 lacks osgeo.gdal or numpy; skipping live GeoTIFF NDVI check." );
  }

  const QString inputTif = workspace.filePath( QStringLiteral( "red_nir_4x3.tif" ) );
  const QString outputTif = workspace.filePath( QStringLiteral( "ndvi_4x3.tif" ) );
  const QString scriptPath = workspace.filePath( QStringLiteral( "compute_ndvi.py" ) );
  REQUIRE( writeTwoBandRedNirRaster( inputTif, 4, 3, 0.2f, 0.6f ) );
  {
    QFile f( scriptPath );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( R"PY(
import sys
import numpy as np
from osgeo import gdal

in_path, out_path = sys.argv[1], sys.argv[2]
ds = gdal.Open(in_path, gdal.GA_ReadOnly)
red = ds.GetRasterBand(1).ReadAsArray().astype(np.float32)
nir = ds.GetRasterBand(2).ReadAsArray().astype(np.float32)
ndvi = (nir - red) / (nir + red)
drv = gdal.GetDriverByName("GTiff")
out_ds = drv.Create(out_path, ds.RasterXSize, ds.RasterYSize, 1, gdal.GDT_Float32)
out_ds.SetGeoTransform(ds.GetGeoTransform())
out_ds.SetProjection(ds.GetProjection())
out_ds.GetRasterBand(1).WriteArray(ndvi)
out_ds.FlushCache()
out_ds = None
ds = None
print("NDVI_OK")
)PY" );
  }

  exprs::ExternalProcessRequest pyReq;
  pyReq.argv = {
    pythonBin.toStdString(), "-I", "-B", "-u", "-X", "utf8",
    scriptPath.toStdString(), inputTif.toStdString(), outputTif.toStdString()
  };
  pyReq.workingDirectory = workspace.path().toStdString();
  pyReq.timeoutSeconds = 60;
  pyReq.memoryLimitBytes = 1024ULL * 1024ULL * 1024ULL;
  const exprs::ExternalProcessResult pyRes = exprs::ExternalProcess::run( pyReq );
  INFO( pyRes.stdErr );
  INFO( pyRes.error );
  REQUIRE( pyRes.exitedCleanly() );
  CHECK( pyRes.stdOut.find( "NDVI_OK" ) != std::string::npos );

  GDALAllRegister();
  GdalDatasetPtr outDs( static_cast<GDALDataset *>( GDALOpen( outputTif.toUtf8().constData(), GA_ReadOnly ) ) );
  REQUIRE( outDs != nullptr );
  CHECK( outDs->GetRasterXSize() == 4 );
  CHECK( outDs->GetRasterYSize() == 3 );
  std::vector<float> pixels( 12, 0.0f );
  REQUIRE( outDs->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, 4, 3, pixels.data(), 4, 3, GDT_Float32, 0, 0 ) == CE_None );
  for ( float p : pixels )
    CHECK( p == Catch::Approx( 0.5f ).epsilon( 1e-4 ) );
}
