// tests/test_llm_streaming_client.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adversarial_corpus.h"
#include "agent/llm_config_manager.h"
#include "agent/llm_streaming_client.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>

#include "agent/runtime/native_conversation_runtime.h"
#include <QElapsedTimer>
#include <QPointer>
#include <functional>
#include <memory>

using namespace sicnu::agent;
using namespace sicnu::processing;

static void ensureQtApp()
{
  if ( !QCoreApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_llm_streaming_client";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
  }
}

TEST_CASE( "LlmConfigManager manages profiles and QSettings persistence", "[agent][config]" )
{
  ensureQtApp();
  auto presets = LlmConfigManager::presetProfiles();
  REQUIRE_FALSE( presets.isEmpty() );
  REQUIRE( presets.first().id == "deepseek" );

  LlmProviderProfile custom;
  custom.id = QStringLiteral( "test_custom" );
  custom.name = QStringLiteral( "Test Custom Model" );
  custom.baseUrl = QStringLiteral( "http://localhost:8000/v1" );
  custom.apiKey = QStringLiteral( "sk-test12345" );
  custom.modelName = QStringLiteral( "custom-rs-model" );

  LlmConfigManager::setActiveProfile( custom );

  auto active = LlmConfigManager::activeProfile();
  REQUIRE( active.id == "test_custom" );
  REQUIRE( active.name == "Test Custom Model" );
  REQUIRE( active.apiKey == "sk-test12345" );
}

TEST_CASE( "LlmConfigManager emits activeProfileChanged and profilesChanged signals", "[agent][config]" )
{
  ensureQtApp();

  bool profileSignalFired = false;
  bool profilesSignalFired = false;

  QObject::connect( &LlmConfigManager::instance(), &LlmConfigManager::activeProfileChanged, [&]( const LlmProviderProfile &p ) {
    profileSignalFired = true;
    REQUIRE( p.id == "test_signal_profile" );
  } );

  QObject::connect( &LlmConfigManager::instance(), &LlmConfigManager::profilesChanged, [&]() {
    profilesSignalFired = true;
  } );

  LlmProviderProfile testProfile;
  testProfile.id = QStringLiteral( "test_signal_profile" );
  testProfile.name = QStringLiteral( "Test Signal Model" );
  testProfile.baseUrl = QStringLiteral( "http://localhost:9000/v1" );

  LlmConfigManager::setActiveProfile( testProfile );

  REQUIRE( profileSignalFired );
  REQUIRE( profilesSignalFired );
}

TEST_CASE( "LlmStreamingClient parses SSE stream lines and emits signals", "[agent][client]" )
{
  ensureQtApp();
  AtomicAlgorithmRegistry::instance().reset();
  LlmStreamingClient client;

  QString reasoningCaptured;
  QString contentCaptured;
  QJsonObject toolCallCaptured;
  bool finishedCalled = false;

  QObject::connect( &client, &LlmStreamingClient::reasoningTokenReceived, [&]( const QString &text ) {
    reasoningCaptured += text;
  } );

  QObject::connect( &client, &LlmStreamingClient::contentTokenReceived, [&]( const QString &text ) {
    contentCaptured += text;
  } );

  QObject::connect( &client, &LlmStreamingClient::toolCallParsed, [&]( const QJsonObject &toolCall ) {
    toolCallCaptured = toolCall;
  } );

  QObject::connect( &client, &LlmStreamingClient::finished, [&]() {
    finishedCalled = true;
  } );

  // 1. Simulate DeepSeek-R1 reasoning content stream
  client.parseSseLine( QStringLiteral( "data: {\"choices\": [{\"delta\": {\"reasoning_content\": \"Thinking about NDVI...\"}}]}" ) );
  REQUIRE( reasoningCaptured == "Thinking about NDVI..." );

  // 2. Simulate regular content stream
  client.parseSseLine( QStringLiteral( "data: {\"choices\": [{\"delta\": {\"content\": \"Calculated NDVI map successfully.\"}}]}" ) );
  REQUIRE( contentCaptured == "Calculated NDVI map successfully." );

  // 3. Simulate Tool Call stream
  client.parseSseLine( QStringLiteral( "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"id\": \"call_999\", \"function\": {\"name\": \"rs_spectral_index\", \"arguments\": \"{\\\"index\\\":\\\"NDVI\\\"}\"}}]}}]}" ) );

  // 4. Simulate [DONE]
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

  REQUIRE( finishedCalled );
  REQUIRE( toolCallCaptured[QStringLiteral( "id" )].toString() == "call_999" );
  REQUIRE( toolCallCaptured[QStringLiteral( "function" )].toObject()[QStringLiteral( "name" )].toString() == "rs_spectral_index" );
}

TEST_CASE( "LlmStreamingClient::buildChatRequest normalizes the endpoint URL", "[agent][client]" )
{
  ensureQtApp();

  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://localhost:8000/v1" );

  // Bare base URL gains the /chat/completions suffix.
  ChatRequestPayload payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.url().toString() == QStringLiteral( "http://localhost:8000/v1/chat/completions" ) );

  // Trailing slash must not produce a double slash.
  profile.baseUrl = QStringLiteral( "http://localhost:8000/v1/" );
  payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.url().toString() == QStringLiteral( "http://localhost:8000/v1/chat/completions" ) );

  // Already-complete endpoint is left untouched.
  profile.baseUrl = QStringLiteral( "http://localhost:8000/v1/chat/completions" );
  payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.url().toString() == QStringLiteral( "http://localhost:8000/v1/chat/completions" ) );
}

TEST_CASE( "LlmStreamingClient::buildChatRequest sets content type and Bearer auth header", "[agent][client]" )
{
  ensureQtApp();

  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://localhost:8000/v1" );
  profile.apiKey = QStringLiteral( "sk-test12345" );

  ChatRequestPayload payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.header( QNetworkRequest::ContentTypeHeader ).toString() == QStringLiteral( "application/json" ) );
  REQUIRE( payload.request.rawHeader( "Authorization" ) == QByteArray( "Bearer sk-test12345" ) );

  // Empty apiKey: no Authorization header at all.
  profile.apiKey.clear();
  payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.rawHeader( "Authorization" ).isEmpty() );
}

TEST_CASE( "LlmStreamingClient::buildChatRequest assembles the wire body", "[agent][client]" )
{
  ensureQtApp();

  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://localhost:8000/v1" );
  profile.modelName = QStringLiteral( "deepseek-chat" );
  profile.temperature = 0.4;
  profile.stream = false; // ignored: the transport is SSE-only (ADR 0049)

  QJsonObject userMsg;
  userMsg[QStringLiteral( "role" )] = QStringLiteral( "user" );
  userMsg[QStringLiteral( "content" )] = QStringLiteral( "ping" );
  QJsonArray messages;
  messages.append( userMsg );

  // No tools supplied: the key stays off the wire entirely.
  ChatRequestPayload payload = LlmStreamingClient::buildChatRequest( profile, messages );
  const QJsonObject body = QJsonDocument::fromJson( payload.body ).object();
  REQUIRE( body[QStringLiteral( "model" )].toString() == QStringLiteral( "deepseek-chat" ) );
  REQUIRE( body[QStringLiteral( "messages" )].toArray() == messages );
  REQUIRE( body[QStringLiteral( "temperature" )].toDouble() == 0.4 );
  REQUIRE( body[QStringLiteral( "stream" )].toBool() == true );
  REQUIRE_FALSE( body.contains( QStringLiteral( "tools" ) ) );

  // Supplied tools go on the wire verbatim.
  QJsonObject funcObj;
  funcObj[QStringLiteral( "name" )] = QStringLiteral( "rs_spectral_index" );
  QJsonObject toolObj;
  toolObj[QStringLiteral( "type" )] = QStringLiteral( "function" );
  toolObj[QStringLiteral( "function" )] = funcObj;
  QJsonArray tools;
  tools.append( toolObj );

  payload = LlmStreamingClient::buildChatRequest( profile, messages, tools );
  const QJsonObject bodyWithTools = QJsonDocument::fromJson( payload.body ).object();
  REQUIRE( bodyWithTools[QStringLiteral( "tools" )].toArray() == tools );
}

TEST_CASE( "LlmStreamingClient drops truncated tool calls (#701)", "[agent][client]" )
{
  ensureQtApp();

  QJsonObject toolCallCaptured;
  bool toolCallEmitted = false;
  LlmStreamingClient client;
  QObject::connect( &client, &LlmStreamingClient::toolCallParsed,
                    [&]( const QJsonObject &toolCall ) {
                      toolCallEmitted = true;
                      toolCallCaptured = toolCall;
                    } );

  // Stream cut mid-tool-call: the name arrived, the arguments JSON did not.
  client.parseSseLine( QStringLiteral(
      "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"id\": \"call_1\", "
      "\"function\": {\"name\": \"rs_ndvi\", \"arguments\": \"{\\\"input\\\":\\\"scene.t\"}}]}}]}" ) );
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

  // The stream closed normally but the tool call is truncated: emitting it
  // would hand the executor a garbage arguments blob as if it were valid.
  REQUIRE_FALSE( toolCallEmitted );

  // A call with NO arguments is legitimate and must still be emitted: the
  // arguments-only stream cut is what must be dropped, not argument-less calls.
  LlmStreamingClient argumentLessClient;
  bool argumentLessEmitted = false;
  QObject::connect( &argumentLessClient, &LlmStreamingClient::toolCallParsed,
                    [&]( const QJsonObject & ) { argumentLessEmitted = true; } );
  argumentLessClient.parseSseLine( QStringLiteral(
      "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"id\": \"call_2\", "
      "\"function\": {\"name\": \"data_list_layers\"}}]}}]}" ) );
  argumentLessClient.parseSseLine( QStringLiteral( "data: [DONE]" ) );
  REQUIRE( argumentLessEmitted );
}

TEST_CASE( "LlmStreamingClient::buildChatRequest honours SICNU_LLM_TRANSFER_TIMEOUT_MS (#701)",
           "[agent][client]" )
{
  ensureQtApp();

  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://localhost:8000/v1" );

  qputenv( "SICNU_LLM_TRANSFER_TIMEOUT_MS", QByteArrayLiteral( "30000" ) );
  ChatRequestPayload payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.transferTimeout() == 30000 );

  // 0 disables the transfer timeout entirely (QNetworkRequest semantics) —
  // the escape hatch for long-silent reasoning streams.
  qputenv( "SICNU_LLM_TRANSFER_TIMEOUT_MS", QByteArrayLiteral( "0" ) );
  payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.transferTimeout() == 0 );

  // Unset: the historical default applies.
  qunsetenv( "SICNU_LLM_TRANSFER_TIMEOUT_MS" );
  payload = LlmStreamingClient::buildChatRequest( profile, QJsonArray() );
  REQUIRE( payload.request.transferTimeout() == 120000 );
}

// ---------------------------------------------------------------------------
// Review P1-7: API keys move from plaintext QSettings to the secure store,
// with one-shot migration of legacy plaintext values.
// ---------------------------------------------------------------------------
namespace
{
class FakeSecretStore final : public LlmSecretStore
{
  public:
    QMap<QString, QString> keys;
    bool usable = true;
    int writes = 0;
    QString backendName() const override { return QStringLiteral( "fake-keychain" ); }
    bool readKeys( QMap<QString, QString> *out, QString *error ) override
    {
      if ( !usable )
      {
        *error = QStringLiteral( "backend offline" );
        return false;
      }
      *out = keys;
      return true;
    }
    bool writeKeys( const QMap<QString, QString> &in, QString *error ) override
    {
      if ( !usable )
      {
        *error = QStringLiteral( "backend offline" );
        return false;
      }
      keys = in;
      ++writes;
      return true;
    }
};

/// Points QSettings() at a throw-away organization so the test never
/// touches (or depends on) the developer's real settings.
struct IsolatedSettings
{
    QString savedOrg = QCoreApplication::organizationName();
    QString savedApp = QCoreApplication::applicationName();
    IsolatedSettings()
    {
      QCoreApplication::setOrganizationName(
        QStringLiteral( "exp-rs-test-keychain-%1" ).arg( QCoreApplication::applicationPid() ) );
      QCoreApplication::setApplicationName( QStringLiteral( "llm-keys" ) );
      QSettings().clear();
    }
    ~IsolatedSettings()
    {
      QSettings settings;
      settings.clear();
      settings.sync();
      QFile::remove( settings.fileName() );
      QCoreApplication::setOrganizationName( savedOrg );
      QCoreApplication::setApplicationName( savedApp );
    }
};

void seedLegacyPlaintextProfile( const QString &id, const QString &apiKey )
{
  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_AgentProfiles" ) );
  settings.beginWriteArray( QStringLiteral( "profiles" ), 1 );
  settings.setArrayIndex( 0 );
  settings.setValue( QStringLiteral( "id" ), id );
  settings.setValue( QStringLiteral( "name" ), QStringLiteral( "Legacy" ) );
  settings.setValue( QStringLiteral( "baseUrl" ), QStringLiteral( "https://api.example.invalid/v1" ) );
  settings.setValue( QStringLiteral( "apiKey" ), apiKey );
  settings.setValue( QStringLiteral( "modelName" ), QStringLiteral( "m" ) );
  settings.endArray();
  settings.endGroup();
  settings.sync();
}

bool settingsHoldPlaintextKey()
{
  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_AgentProfiles" ) );
  const int n = settings.beginReadArray( QStringLiteral( "profiles" ) );
  bool found = false;
  for ( int i = 0; i < n; ++i )
  {
    settings.setArrayIndex( i );
    found = found || settings.contains( QStringLiteral( "apiKey" ) );
  }
  settings.endArray();
  settings.endGroup();
  return found;
}
} // namespace

TEST_CASE( "LlmConfigManager migrates plaintext API keys into the secure store", "[agent][config][security]" )
{
  ensureQtApp();
  const IsolatedSettings isolated;
  seedLegacyPlaintextProfile( QStringLiteral( "legacy" ), QStringLiteral( "sk-legacy-plaintext" ) );
  REQUIRE( settingsHoldPlaintextKey() );

  auto store = std::make_shared<FakeSecretStore>();
  LlmConfigManager manager;
  manager.setSecretStore( store );

  const auto profiles = manager.getProfiles();
  REQUIRE( profiles.size() == 1 );
  CHECK( profiles.first().apiKey == QStringLiteral( "sk-legacy-plaintext" ) );
  CHECK( store->keys.value( QStringLiteral( "legacy" ) ) == QStringLiteral( "sk-legacy-plaintext" ) );
  CHECK( manager.apiKeyStorage() == QStringLiteral( "fake-keychain" ) );
  // The plaintext copy is gone after migration.
  CHECK_FALSE( settingsHoldPlaintextKey() );

  SECTION( "saving keeps keys out of QSettings" )
  {
    auto edited = profiles;
    edited.first().apiKey = QStringLiteral( "sk-rotated" );
    manager.updateProfiles( edited );
    CHECK( store->keys.value( QStringLiteral( "legacy" ) ) == QStringLiteral( "sk-rotated" ) );
    CHECK_FALSE( settingsHoldPlaintextKey() );

    // A fresh manager reads the key back from the store only.
    LlmConfigManager reloaded;
    reloaded.setSecretStore( store );
    REQUIRE( reloaded.getProfiles().size() == 1 );
    CHECK( reloaded.getProfiles().first().apiKey == QStringLiteral( "sk-rotated" ) );
  }
}

TEST_CASE( "LlmConfigManager prefers stored keys over stale plaintext", "[agent][config][security]" )
{
  ensureQtApp();
  const IsolatedSettings isolated;
  seedLegacyPlaintextProfile( QStringLiteral( "p" ), QStringLiteral( "sk-stale" ) );
  auto store = std::make_shared<FakeSecretStore>();
  store->keys.insert( QStringLiteral( "p" ), QStringLiteral( "sk-current" ) );

  LlmConfigManager manager;
  manager.setSecretStore( store );
  REQUIRE( manager.getProfiles().size() == 1 );
  CHECK( manager.getProfiles().first().apiKey == QStringLiteral( "sk-current" ) );
  CHECK_FALSE( settingsHoldPlaintextKey() );
}

TEST_CASE( "LlmConfigManager falls back to QSettings when the secure store is unavailable", "[agent][config][security]" )
{
  ensureQtApp();
  const IsolatedSettings isolated;
  seedLegacyPlaintextProfile( QStringLiteral( "p" ), QStringLiteral( "sk-fallback" ) );
  auto store = std::make_shared<FakeSecretStore>();
  store->usable = false;

  LlmConfigManager manager;
  manager.setSecretStore( store );
  REQUIRE( manager.getProfiles().size() == 1 );
  CHECK( manager.getProfiles().first().apiKey == QStringLiteral( "sk-fallback" ) );
  CHECK( manager.apiKeyStorage() == QStringLiteral( "settings" ) );
  // Nothing was lost: the plaintext copy stays until a store can take it.
  CHECK( settingsHoldPlaintextKey() );

  auto edited = manager.getProfiles();
  edited.first().apiKey = QStringLiteral( "sk-new" );
  manager.updateProfiles( edited );
  CHECK( settingsHoldPlaintextKey() );
  CHECK( store->writes == 0 );
}

// — Track 8 R4 WP-A: the SSE lane of the LLM failure matrix ————————
// The client is the ONE production seam that consumes raw model output.
// #701 made it refuse truncated/unparseable tool calls, but the refusal was
// a qWarning only: the session and the journal cannot account for a drop
// they cannot observe. The R4 contract: every refusal emits the typed
// `malformedToolCall` signal ({reason, ...}) — observability, not silence.

namespace {

/// Records the malformed-tool-call refusals of one client.
struct MalformedRecorder
{
    QList<QJsonObject> refusals;
    LlmStreamingClient &client;

    explicit MalformedRecorder( LlmStreamingClient &target ) : client( target )
    {
        QObject::connect( &client, &LlmStreamingClient::malformedToolCall,
                          [ this ]( const QJsonObject &detail ) { refusals.append( detail ); } );
    }

    bool hasReason( const QString &reason ) const
    {
        for ( const QJsonObject &detail : refusals )
            if ( detail[QStringLiteral( "reason" )].toString() == reason )
                return true;
        return false;
    }
};

} // namespace

TEST_CASE( "LlmStreamingClient reports a length-truncated tool call as "
           "malformed, observably", "[agent][client][r4]" )
{
    ensureQtApp();
    LlmStreamingClient client;
    bool toolCallEmitted = false;
    QObject::connect( &client, &LlmStreamingClient::toolCallParsed,
                      [&]( const QJsonObject & ) { toolCallEmitted = true; } );
    MalformedRecorder malformed( client );

    // The name arrives, then the stream hits the token limit before ANY
    // argument fragment: a fabricated zero-argument call would execute the
    // tool with invented defaults.
    client.parseSseLine( QStringLiteral(
        "data: {\"choices\": [{\"finish_reason\": \"length\"}]}" ) );
    client.parseSseLine( QStringLiteral(
        "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"id\": \"call_9\", "
        "\"function\": {\"name\": \"rs_change_difference\"}}]}}]}" ) );
    client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

    CHECK_FALSE( toolCallEmitted );
    REQUIRE( malformed.hasReason( QStringLiteral( "truncated_arguments" ) ) );
    // The refusal carries what the session needs to re-prompt honestly.
    bool sawDetail = false;
    for ( const QJsonObject &detail : malformed.refusals )
    {
        if ( detail[QStringLiteral( "reason" )].toString() != QStringLiteral( "truncated_arguments" ) )
            continue;
        sawDetail = true;
        CHECK( detail[QStringLiteral( "name" )].toString() == QStringLiteral( "rs_change_difference" ) );
        CHECK( detail[QStringLiteral( "finish_reason" )].toString() == QStringLiteral( "length" ) );
    }
    CHECK( sawDetail );
}

TEST_CASE( "LlmStreamingClient reports unparseable streamed arguments as "
           "malformed with the byte size", "[agent][client][r4]" )
{
    ensureQtApp();
    LlmStreamingClient client;
    bool toolCallEmitted = false;
    QObject::connect( &client, &LlmStreamingClient::toolCallParsed,
                      [&]( const QJsonObject & ) { toolCallEmitted = true; } );
    MalformedRecorder malformed( client );

    // "Partial JSON": the stream ends cleanly but the accumulated arguments
    // are half of a document — the model rambled past its own syntax.
    client.parseSseLine( QStringLiteral(
        "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"id\": \"c1\", "
        "\"function\": {\"name\": \"harness_plan\", \"arguments\": \"{\\\"plan\\\": {\\\"steps\\\": [\"}}]}}]}" ) );
    client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

    CHECK_FALSE( toolCallEmitted );
    CHECK( malformed.hasReason( QStringLiteral( "unparseable_arguments" ) ) );
    for ( const QJsonObject &detail : malformed.refusals )
    {
        if ( detail[QStringLiteral( "reason" )].toString() != QStringLiteral( "unparseable_arguments" ) )
            continue;
        CHECK( detail[QStringLiteral( "size" )].toInt() > 0 );
    }
}

TEST_CASE( "LlmStreamingClient reports a nameless tool call as malformed "
           "instead of skipping it silently", "[agent][client][r4]" )
{
    ensureQtApp();
    LlmStreamingClient client;
    bool toolCallEmitted = false;
    QObject::connect( &client, &LlmStreamingClient::toolCallParsed,
                      [&]( const QJsonObject & ) { toolCallEmitted = true; } );
    MalformedRecorder malformed( client );

    // An id arrived, the function name never did. Before R4 this row simply
    // vanished; the provider contract (every tool call has a name) makes it
    // a malformed call.
    client.parseSseLine( QStringLiteral(
        "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"id\": \"call_ghost\"}]}}]}" ) );
    client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

    CHECK_FALSE( toolCallEmitted );
    CHECK( malformed.hasReason( QStringLiteral( "missing_name" ) ) );
}

TEST_CASE( "LlmStreamingClient refuses an oversized argument accumulation "
           "before parsing it", "[agent][client][r4][bounded]" )
{
    ensureQtApp();
    LlmStreamingClient client;
    bool toolCallEmitted = false;
    QObject::connect( &client, &LlmStreamingClient::toolCallParsed,
                      [&]( const QJsonObject & ) { toolCallEmitted = true; } );
    MalformedRecorder malformed( client );

    // Stream syntactically VALID fragments past the accumulation bound.
    // The refusal is about SIZE, not syntax: a hostile (or runaway) stream
    // must not make the client buffer without limit, and the refusal must
    // say so. Each fragment is a valid JSON string escape (\" for the
    // inner quotes) so the data: line itself parses — only the accumulated
    // arguments exceed the bound.
    const QString pad = QString( 4096, QLatin1Char( 'p' ) );
    client.parseSseLine( QStringLiteral(
        "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"id\": \"c2\", "
        "\"function\": {\"name\": \"bulk\", \"arguments\": \"{\"}}}]}]}" ) );
    for ( int i = 0; i < 600; ++i ) // 600 x ~4.1 KiB > 1 MiB bound
    {
        client.parseSseLine( QStringLiteral(
            "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"index\": 0, "
            "\"function\": {\"arguments\": \"\\\"pad\\\":\\\"%1\\\"\"}}]}}]}" )
                                 .arg( pad ) );
    }
    client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

    CHECK_FALSE( toolCallEmitted );
    CHECK( malformed.hasReason( QStringLiteral( "oversized_arguments" ) ) );
}

TEST_CASE( "LlmStreamingClient refuses a stream that mints unlimited "
           "tool-call indices", "[agent][client][r4][bounded]" )
{
  ensureQtApp();
  LlmStreamingClient client;
  QList<QJsonObject> refusals;
  QObject::connect( &client, &LlmStreamingClient::malformedToolCall,
                    [&]( const QJsonObject &detail ) { refusals.append( detail ); } );

  // A hostile stream sending {"index":N} for N = 0..999: each distinct
  // index would otherwise allocate an accumulator entry forever.
  for ( int i = 0; i < 1000; ++i )
  {
    client.parseSseLine( QStringLiteral(
        "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"index\": %1, "
        "\"function\": {\"name\": \"tool_%1\", \"arguments\": \"{}\"}}]}}]}" ).arg( i ) );
  }
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

  bool sawBound = false;
  for ( const QJsonObject &detail : refusals )
    if ( detail[QStringLiteral( "reason" )].toString() ==
         QStringLiteral( "too_many_tool_calls" ) )
      sawBound = true;
  CHECK( sawBound );
}

TEST_CASE( "LlmStreamingClient refuses an oversized data line as malformed",
           "[agent][client][r4][bounded]" )
{
    ensureQtApp();
    LlmStreamingClient client;
    MalformedRecorder malformed( client );

    // A single SSE line beyond the line bound: dropped with a typed refusal
    // (the transport cannot buffer a hostile stream without limit).
    const QString hugePayload = QStringLiteral( "x" ).repeated( 9 * 1024 * 1024 );
    client.parseSseLine( QStringLiteral( "data: " ) + hugePayload );

    CHECK( malformed.refusals.size() >= 1 );
    CHECK( malformed.hasReason( QStringLiteral( "oversized_line" ) ) );
}

// — WP-G: corpus-driven SSE cases (same single sample source as the
// plan-layer matrix; load schema-validates the corpus). ————————————————

TEST_CASE( "corpus SSE samples behave exactly as their recorded contract "
           "says", "[agent][client][r4][corpus]" )
{
  ensureQtApp();
  sicnu::testing::AdversarialCorpus corpus;
  std::string error;
  REQUIRE( sicnu::testing::AdversarialCorpus::load(
    sicnu::testing::adversarialCorpusPath(), corpus, &error ) );

  const auto samples = corpus.forSeam( "llm_streaming.parse" );
  REQUIRE_FALSE( samples.empty() );
  for ( const sicnu::testing::AdversarialSample &sample : samples )
  {
    INFO( "corpus sample: " << sample.id );
    LlmStreamingClient client;
    bool toolCallEmitted = false;
    QObject::connect( &client, &LlmStreamingClient::toolCallParsed,
                      [&]( const QJsonObject & ) { toolCallEmitted = true; } );
    QList<QJsonObject> refusals;
    QObject::connect( &client, &LlmStreamingClient::malformedToolCall,
                      [&]( const QJsonObject &detail ) { refusals.append( detail ); } );

    const Json::Value &lines = sample.payload["lines"];
    REQUIRE( lines.isArray() );
    for ( const Json::Value &line : lines )
      client.parseSseLine( QString::fromStdString( line.asString() ) );

    const bool expectedEmitted = sample.expected["tool_call_emitted"].asBool();
    CHECK( toolCallEmitted == expectedEmitted );
    if ( sample.expected.isMember( "reason" ) )
    {
      const QString reason = QString::fromStdString( sample.expected["reason"].asString() );
      bool found = false;
      for ( const QJsonObject &detail : refusals )
        if ( detail[QStringLiteral( "reason" )].toString() == reason )
          found = true;
      CHECK( found );
    }
  }
}

// — Robustness review #1452: a direct-connected consumer re-entering
// sendChatCompletion from toolCallParsed used to clear() m_toolCalls while
// emitParsedToolCallOnce was still iterating it (undefined behavior), and the
// [DONE] handler then saw the re-entrant flag reset and emitted a stray
// finished() for the superseded stream.
TEST_CASE( "a consumer re-entering sendChatCompletion from the first "
           "toolCallParsed still receives every parallel tool call (#1452)",
           "[agent][client][lifecycle]" )
{
  ensureQtApp();

  LlmProviderProfile profile;
  // Unreachable by design: the re-entrant request only has to disturb client
  // state synchronously, never to deliver anything (no event loop runs here).
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:1/v1" );
  profile.modelName = QStringLiteral( "stub" );

  LlmStreamingClient client;
  client.setProfile( profile );

  QList<QString> receivedIds;
  int finishedCount = 0;
  bool reEntered = false;
  QObject::connect( &client, &LlmStreamingClient::toolCallParsed,
                    [&]( const QJsonObject &toolCall )
                    {
                      receivedIds.append( toolCall[QStringLiteral( "id" )].toString() );
                      // The dock pattern that triggered the bug: the FIRST
                      // tool call synchronously starts the next turn.
                      if ( !reEntered )
                      {
                        reEntered = true;
                        client.sendChatCompletion( QJsonArray() );
                      }
                    } );
  QObject::connect( &client, &LlmStreamingClient::finished, [&]() { ++finishedCount; } );

  // Two PARALLEL tool calls (distinct streamed indices) finalized by [DONE].
  client.parseSseLine( QStringLiteral(
      "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"index\": 0, \"id\": \"call_a0\", "
      "\"function\": {\"name\": \"rs_ndvi\", \"arguments\": \"{\\\"x\\\":1}\"}}]}}]}" ) );
  client.parseSseLine( QStringLiteral(
      "data: {\"choices\": [{\"delta\": {\"tool_calls\": [{\"index\": 1, \"id\": \"call_a1\", "
      "\"function\": {\"name\": \"rs_evi\", \"arguments\": \"{\\\"x\\\":2}\"}}]}}]}" ) );
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );

  // The accumulation was detached before the first emit: the second call
  // survives the re-entrant clear() and is still delivered.
  REQUIRE( receivedIds.size() == 2 );
  CHECK( receivedIds.contains( QStringLiteral( "call_a0" ) ) );
  CHECK( receivedIds.contains( QStringLiteral( "call_a1" ) ) );
  // The superseded stream must not report finished() while the re-entrant
  // request is in flight: the new stream owns the terminal signal.
  CHECK( finishedCount == 0 );
}

// — Robustness review 2026-10-02 (P1): the TRANSPORT lane of the line bound.
// The seam test above proves parseSseLine refuses an oversized line; this
// regression drives the real network path. The refusal used to cancel() the
// reply and nothing else: abort() surfaces as OperationCanceledError (which
// onReplyError deliberately skips) and the reply's finished() is dropped by
// onReplyFinished's null-reply guard — so no client-level terminal signal
// ever fired and the caller's turn hung forever. failStream() must emit
// errorOccurred + finished after the refusal.
TEST_CASE( "an oversized unterminated SSE line fails the whole turn on the "
           "transport path", "[agent][client][r4][bounded][transport]" )
{
  ensureQtApp();

  QTcpServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  const quint16 port = server.serverPort();

  QObject::connect( &server, &QTcpServer::newConnection, &server, [&]()
  {
    QTcpSocket *socket = server.nextPendingConnection();
    // Headers only: no Content-Length and no chunking, so the body length is
    // "read until close" — and we never close and never send a newline. The
    // 9 MiB write buffers inside QTcpSocket and flushes asynchronously; do
    // NOT block on it: the reader that drains the loopback lives in this
    // same thread's event loop.
    socket->write( "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/event-stream\r\n"
                   "Cache-Control: no-cache\r\n\r\n" );
    socket->write( QByteArray( "data: " ) + QByteArray( 9 * 1024 * 1024, 'x' ) );
  } );

  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( port );
  profile.modelName = QStringLiteral( "stub" );

  LlmStreamingClient client;
  client.setProfile( profile );

  bool oversizedRefusal = false;
  bool errorSeen = false;
  bool finishedSeen = false;
  QString errorText;
  QObject::connect( &client, &LlmStreamingClient::malformedToolCall,
                    [&]( const QJsonObject &detail )
                    {
                      oversizedRefusal = detail[QStringLiteral( "reason" )].toString() ==
                                         QStringLiteral( "oversized_line" );
                    } );
  QObject::connect( &client, &LlmStreamingClient::errorOccurred,
                    [&]( const QString &message ) { errorSeen = true; errorText = message; } );
  QObject::connect( &client, &LlmStreamingClient::finished,
                    [&]() { finishedSeen = true; } );

  client.sendChatCompletion( QJsonArray() );

  // Deadline-bounded pump: before the fix this loop ran the full deadline
  // with all three flags false — the turn never terminated on its own.
  const QDateTime deadline = QDateTime::currentDateTimeUtc().addSecs( 20 );
  while ( !finishedSeen && QDateTime::currentDateTimeUtc() < deadline )
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

  CHECK( oversizedRefusal );
  CHECK( errorSeen );
  CHECK( finishedSeen );
  CHECK( errorText.contains( QStringLiteral( "bound" ) ) );
}

namespace
{
// Isolate both QSettings and the singleton's secret store BEFORE constructing
// a client (its constructor loads an active profile). Only synthetic settings
// and an empty in-memory secret store are available to these telemetry tests.
struct UsageFixtureSettings
{
    IsolatedSettings isolated;
    UsageFixtureSettings() { LlmConfigManager::instance().setSecretStore( std::make_shared<FakeSecretStore>() ); }
    ~UsageFixtureSettings() { LlmConfigManager::instance().setSecretStore( nullptr ); }
};

QString usageLine( const QJsonValue &usage, const QString &content = {} )
{
  QJsonArray choices;
  if ( !content.isEmpty() )
    choices.append( QJsonObject{ { QStringLiteral( "delta" ), QJsonObject{ { QStringLiteral( "content" ), content } } } } );
  return QStringLiteral( "data: " ) + QString::fromUtf8( QJsonDocument( QJsonObject{
    { QStringLiteral( "choices" ), choices }, { QStringLiteral( "usage" ), usage } } ).toJson( QJsonDocument::Compact ) );
}

QJsonObject fixtureUsage()
{
  return { { QStringLiteral( "prompt_tokens" ), 30 }, { QStringLiteral( "completion_tokens" ), 20 },
           { QStringLiteral( "total_tokens" ), 50 } };
}
}

TEST_CASE( "OpenAI streaming usage is an explicit request option", "[agent][client][usage]" )
{
  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:8000/v1" );
  profile.modelName = QStringLiteral( "fixture" );
  const QJsonArray messages{ QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "user" ) },
                                         { QStringLiteral( "content" ), QStringLiteral( "synthetic prompt" ) } } };
  const QJsonArray tools{ QJsonObject{ { QStringLiteral( "type" ), QStringLiteral( "function" ) } } };
  const QJsonObject original = QJsonDocument::fromJson( LlmStreamingClient::buildChatRequest( profile, messages, tools ).body ).object();
  QJsonObject observed = QJsonDocument::fromJson( LlmStreamingClient::buildChatRequest( profile, messages, tools, true ).body ).object();
  CHECK_FALSE( original.contains( QStringLiteral( "stream_options" ) ) );
  const QJsonObject options{ { QStringLiteral( "include_usage" ), true } };
  CHECK( observed.take( QStringLiteral( "stream_options" ) ).toObject() == options );
  CHECK( observed == original );
}

TEST_CASE( "Empty-choice final SSE usage is normalized exactly once before finished", "[agent][client][usage]" )
{
  ensureQtApp();
  const UsageFixtureSettings settings;
  LlmStreamingClient client;
  QList<QJsonObject> received;
  QStringList order;
  int errors = 0;
  QObject::connect( &client, &LlmStreamingClient::usageReceived, [&]( const QJsonObject &usage ) {
    received.append( usage ); order.append( QStringLiteral( "usage" ) );
  } );
  QObject::connect( &client, &LlmStreamingClient::finished, [&] { order.append( QStringLiteral( "finished" ) ); } );
  QObject::connect( &client, &LlmStreamingClient::errorOccurred, [&]( const QString & ) { ++errors; } );
  QJsonObject usage = fixtureUsage();
  usage[QStringLiteral( "prompt_tokens_details" )] = QJsonObject{ { QStringLiteral( "cached_tokens" ), 10 } };
  usage[QStringLiteral( "completion_tokens_details" )] = QJsonObject{ { QStringLiteral( "reasoning_tokens" ), 7 } };
  usage[QStringLiteral( "cost" )] = 123; // Non-token metadata must not escape.
  usage[QStringLiteral( "provider_echo" )] = QStringLiteral( "synthetic-secret" );
  client.parseSseLine( usageLine( usage ) );
  client.parseSseLine( usageLine( usage ) ); // duplicate completion accounting
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );
  client.parseSseLine( usageLine( fixtureUsage() ) ); // late usage after terminal
  REQUIRE( received.size() == 1 );
  const QJsonObject expected{ { QStringLiteral( "input" ), 20 }, { QStringLiteral( "output" ), 20 },
    { QStringLiteral( "cacheRead" ), 10 }, { QStringLiteral( "cacheWrite" ), 0 }, { QStringLiteral( "totalTokens" ), 50 } };
  CHECK( received.first() == expected );
  const QStringList expectedOrder{ QStringLiteral( "usage" ), QStringLiteral( "finished" ) };
  CHECK( order == expectedOrder );
  CHECK( errors == 0 );
}

TEST_CASE( "Unknown or invalid SSE usage preserves content and terminal behavior", "[agent][client][usage]" )
{
  ensureQtApp();
  const UsageFixtureSettings settings;
  const QList<QJsonValue> invalid{
    QJsonValue( QJsonValue::Null ), QJsonObject{},
    QJsonObject{ { QStringLiteral( "prompt_tokens" ), -1 }, { QStringLiteral( "completion_tokens" ), 20 }, { QStringLiteral( "total_tokens" ), 19 } },
    QJsonObject{ { QStringLiteral( "prompt_tokens" ), 30.5 }, { QStringLiteral( "completion_tokens" ), 20 }, { QStringLiteral( "total_tokens" ), 50.5 } },
    QJsonObject{ { QStringLiteral( "prompt_tokens" ), QStringLiteral( "30" ) }, { QStringLiteral( "completion_tokens" ), 20 }, { QStringLiteral( "total_tokens" ), 50 } },
    QJsonObject{ { QStringLiteral( "prompt_tokens" ), 9007199254740992.0 }, { QStringLiteral( "completion_tokens" ), 0 }, { QStringLiteral( "total_tokens" ), 9007199254740992.0 } },
    QJsonObject{ { QStringLiteral( "prompt_tokens" ), 30 }, { QStringLiteral( "completion_tokens" ), 20 }, { QStringLiteral( "total_tokens" ), 51 } },
    QJsonObject{ { QStringLiteral( "prompt_tokens" ), 30 }, { QStringLiteral( "completion_tokens" ), 20 }, { QStringLiteral( "total_tokens" ), 50 },
                 { QStringLiteral( "prompt_tokens_details" ), QJsonObject{ { QStringLiteral( "cached_tokens" ), 31 } } } },
    QJsonObject{ { QStringLiteral( "prompt_tokens" ), 30 }, { QStringLiteral( "completion_tokens" ), 20 }, { QStringLiteral( "total_tokens" ), 50 },
                 { QStringLiteral( "prompt_tokens_details" ), QJsonObject{ { QStringLiteral( "cached_tokens" ), -1 } } } }
  };
  for ( const QJsonValue &usage : invalid )
  {
    LlmStreamingClient client;
    int observed = 0, errors = 0, finished = 0;
    QString text;
    QObject::connect( &client, &LlmStreamingClient::usageReceived, [&]( const QJsonObject & ) { ++observed; } );
    QObject::connect( &client, &LlmStreamingClient::contentTokenReceived, [&]( const QString &delta ) { text += delta; } );
    QObject::connect( &client, &LlmStreamingClient::errorOccurred, [&]( const QString & ) { ++errors; } );
    QObject::connect( &client, &LlmStreamingClient::finished, [&] { ++finished; } );
    client.parseSseLine( usageLine( usage, QStringLiteral( "retained" ) ) );
    client.parseSseLine( QStringLiteral( "data: [DONE]" ) );
    CHECK( observed == 0 );
    CHECK( text == QStringLiteral( "retained" ) );
    CHECK( errors == 0 );
    CHECK( finished == 1 );
  }
}

TEST_CASE( "SSE usage absence is unknown and valid usage may share a content chunk", "[agent][client][usage]" )
{
  ensureQtApp();
  const UsageFixtureSettings settings;
  LlmStreamingClient client;
  int observed = 0, errors = 0, finished = 0;
  QString text;
  QObject::connect( &client, &LlmStreamingClient::usageReceived, [&]( const QJsonObject & ) { ++observed; } );
  QObject::connect( &client, &LlmStreamingClient::contentTokenReceived, [&]( const QString &delta ) { text += delta; } );
  QObject::connect( &client, &LlmStreamingClient::errorOccurred, [&]( const QString & ) { ++errors; } );
  QObject::connect( &client, &LlmStreamingClient::finished, [&] { ++finished; } );
  client.parseSseLine( QStringLiteral( "data: {\"choices\":[{\"delta\":{\"content\":\"before\"}}]}" ) );
  CHECK( observed == 0 );
  client.parseSseLine( usageLine( QJsonValue( QJsonValue::Null ) ) );
  CHECK( observed == 0 );
  client.parseSseLine( usageLine( fixtureUsage(), QStringLiteral( "after" ) ) );
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );
  CHECK( observed == 1 );
  CHECK( text == QStringLiteral( "beforeafter" ) );
  CHECK( errors == 0 );
  CHECK( finished == 1 );
}

TEST_CASE( "Usage observer reentry cannot attribute old content to the new stream", "[agent][client][usage][lifecycle]" )
{
  ensureQtApp();
  const UsageFixtureSettings settings;
  LlmStreamingClient client;
  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:1/v1" );
  profile.modelName = QStringLiteral( "fixture" );
  client.setProfile( profile );
  int observed = 0, finished = 0;
  QString content;
  QObject::connect( &client, &LlmStreamingClient::usageReceived, [&]( const QJsonObject & ) {
    if ( ++observed == 1 ) client.sendChatCompletion( {} );
  } );
  QObject::connect( &client, &LlmStreamingClient::contentTokenReceived, [&]( const QString &text ) { content += text; } );
  QObject::connect( &client, &LlmStreamingClient::finished, [&] { ++finished; } );
  client.parseSseLine( usageLine( fixtureUsage(), QStringLiteral( "stale content" ) ) );
  CHECK( observed == 1 );
  CHECK( content.isEmpty() );
  CHECK( finished == 0 );
  client.parseSseLine( usageLine( fixtureUsage(), QStringLiteral( "new content" ) ) );
  client.parseSseLine( QStringLiteral( "data: [DONE]" ) );
  CHECK( observed == 2 ); // send reset this stream's once-only observation
  CHECK( content == QStringLiteral( "new content" ) );
  CHECK( finished == 1 );
  client.cancel();
}

TEST_CASE( "Opted-in streaming usage crosses the real loopback transport", "[agent][client][usage][wire]" )
{
  ensureQtApp();
  const UsageFixtureSettings settings;
  QTcpServer server;
  REQUIRE( server.listen( QHostAddress::LocalHost, 0 ) );
  QList<QJsonObject> requests;
  QObject::connect( &server, &QTcpServer::newConnection, &server, [&] {
    QTcpSocket *socket = server.nextPendingConnection();
    auto buffer = std::make_shared<QByteArray>();
    auto handled = std::make_shared<bool>( false );
    QObject::connect( socket, &QTcpSocket::readyRead, socket, [&, socket, buffer, handled] {
      *buffer += socket->readAll();
      if ( *handled ) return;
      const qsizetype end = buffer->indexOf( "\r\n\r\n" );
      if ( end < 0 ) return;
      qint64 length = -1;
      for ( const QByteArray &line : buffer->left( end ).split( '\n' ) )
        if ( line.toLower().startsWith( "content-length:" ) ) length = line.mid( 15 ).trimmed().toLongLong();
      if ( length < 0 || buffer->size() < end + 4 + length ) return;
      *handled = true;
      requests.append( QJsonDocument::fromJson( buffer->mid( end + 4, length ) ).object() );
      const QByteArray body = usageLine( fixtureUsage() ).toUtf8() + "\n\ndata: [DONE]\n\n";
      socket->write( "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: "
                     + QByteArray::number( body.size() ) + "\r\nConnection: close\r\n\r\n" + body );
      socket->disconnectFromHost();
    } );
  } );
  LlmProviderProfile profile;
  profile.baseUrl = QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( server.serverPort() );
  profile.modelName = QStringLiteral( "fixture" );
  LlmStreamingClient client;
  client.setProfile( profile );
  int observed = 0, finished = 0, errors = 0;
  QObject::connect( &client, &LlmStreamingClient::usageReceived, [&]( const QJsonObject & ) { ++observed; } );
  QObject::connect( &client, &LlmStreamingClient::finished, [&] { ++finished; } );
  QObject::connect( &client, &LlmStreamingClient::errorOccurred, [&]( const QString & ) { ++errors; } );
  for ( int run = 0; run < 2; ++run )
  {
    client.setIncludeUsage( run == 1 );
    client.sendChatCompletion( {} );
    const QDateTime deadline = QDateTime::currentDateTimeUtc().addSecs( 5 );
    while ( finished < run + 1 && QDateTime::currentDateTimeUtc() < deadline )
      QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    REQUIRE( finished == run + 1 );
    CHECK( observed == run + 1 );
  }
  REQUIRE( requests.size() == 2 );
  CHECK_FALSE( requests.first().contains( QStringLiteral( "stream_options" ) ) );
  const QJsonObject expected{ { QStringLiteral( "include_usage" ), true } };
  CHECK( requests.last().value( QStringLiteral( "stream_options" ) ).toObject() == expected );
  CHECK( errors == 0 );
}

// Append after the usage fixture helpers/tests in tests/test_llm_streaming_client.cpp.
// Required extra includes: <QElapsedTimer>, <QPointer>, <functional>.
// These are integration tests: all parser entry points come from actual HTTP
// replies to sendChatCompletion(), never from direct parseSseLine() calls.

namespace
{
bool waitForUsageTransport( const std::function<bool()> &ready )
{
  QElapsedTimer timer;
  timer.start();
  while ( !ready() && timer.elapsed() < 5000 )
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
  return ready();
}

QByteArray usageContentFrame( const QString &content )
{
  return QByteArrayLiteral( "data: " ) + QJsonDocument( QJsonObject{
    { QStringLiteral( "choices" ), QJsonArray{ QJsonObject{
      { QStringLiteral( "delta" ), QJsonObject{ { QStringLiteral( "content" ), content } } }
    } } }
  } ).toJson( QJsonDocument::Compact );
}

QByteArray usageToolFrame( const QString &arguments, bool first )
{
  QJsonObject function{ { QStringLiteral( "arguments" ), arguments } };
  QJsonObject call{ { QStringLiteral( "index" ), 0 } };
  if ( first )
  {
    call[QStringLiteral( "id" )] = QStringLiteral( "nested_call" );
    function[QStringLiteral( "name" )] = QStringLiteral( "nested_tool" );
  }
  call[QStringLiteral( "function" )] = function;
  return QByteArrayLiteral( "data: " ) + QJsonDocument( QJsonObject{
    { QStringLiteral( "choices" ), QJsonArray{ QJsonObject{
      { QStringLiteral( "delta" ), QJsonObject{ { QStringLiteral( "tool_calls" ), QJsonArray{ call } } } }
    } } }
  } ).toJson( QJsonDocument::Compact );
}

// Read the complete synthetic POST before replying. The callback can leave a
// response open and later append bytes to the same socket, so EOF is explicit.
struct UsageReentryServer
{
    QTcpServer server;
    QList<QPointer<QTcpSocket>> peers;
    int requests = 0;
    std::function<void( int, QTcpSocket * )> respond;

    UsageReentryServer()
    {
      QObject::connect( &server, &QTcpServer::newConnection, &server, [this] {
        QTcpSocket *socket = server.nextPendingConnection();
        const int index = static_cast<int>( peers.size() );
        peers.append( socket );
        auto bytes = std::make_shared<QByteArray>();
        auto handled = std::make_shared<bool>( false );
        QObject::connect( socket, &QTcpSocket::readyRead, &server,
                          [this, socket, index, bytes, handled] {
          *bytes += socket->readAll();
          if ( *handled ) return;
          const qsizetype end = bytes->indexOf( "\r\n\r\n" );
          if ( end < 0 ) return;
          qint64 length = -1;
          for ( const QByteArray &line : bytes->left( end ).split( '\n' ) )
            if ( line.toLower().startsWith( "content-length:" ) )
              length = line.mid( 15 ).trimmed().toLongLong();
          if ( length < 0 || bytes->size() < end + 4 + length ) return;
          *handled = true;
          ++requests;
          respond( index, socket );
        } );
      } );
    }

    static void beginResponse( QTcpSocket *socket, const QByteArray &body, bool eof )
    {
      QByteArray headers = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n";
      if ( eof ) headers += "Content-Length: " + QByteArray::number( body.size() ) + "\r\n";
      socket->write( headers + "\r\n" + body );
      if ( eof ) socket->disconnectFromHost();
    }

    LlmProviderProfile profile() const
    {
      LlmProviderProfile profile;
      profile.baseUrl = QStringLiteral( "http://127.0.0.1:%1/v1" ).arg( server.serverPort() );
      profile.modelName = QStringLiteral( "synthetic-usage-reentry" );
      profile.apiKey = QStringLiteral( "sk-synthetic-usage-reentry" );
      return profile;
    }
};
} // namespace

TEST_CASE( "A direct usage observer can delete the client owner during a real HTTP callback",
           "[agent][client][usage][wire][lifecycle]" )
{
  bool tailAtEof = false;
  SECTION( "readyRead parses a newline while the HTTP response stays open" ) {}
  SECTION( "replyFinished parses an EOF tail without a newline" ) { tailAtEof = true; }

  ensureQtApp();
  const UsageFixtureSettings settings;
  UsageReentryServer fixture;
  REQUIRE( fixture.server.listen( QHostAddress::LocalHost, 0 ) );
  fixture.respond = [&]( int, QTcpSocket *socket ) {
    QByteArray body = usageLine( fixtureUsage(), QStringLiteral( "old-inline" ) ).toUtf8();
    if ( !tailAtEof ) body += "\n\n" + usageContentFrame( QStringLiteral( "old-buffered" ) ) + "\n\n";
    // The open response can only enter via readyRead. The newline-free closed
    // response can only emit usage from onReplyFinished's buffered-tail path.
    UsageReentryServer::beginResponse( socket, body, tailAtEof );
  };

  auto owner = std::make_unique<QObject>();
  QPointer<LlmStreamingClient> client = new LlmStreamingClient( owner.get() );
  client->setProfile( fixture.profile() );
  client->setIncludeUsage( true );
  int usage = 0, finished = 0, errors = 0;
  QString content;
  QObject::connect( client, &LlmStreamingClient::contentTokenReceived, &fixture.server,
                    [&]( const QString &text ) { content += text; }, Qt::DirectConnection );
  QObject::connect( client, &LlmStreamingClient::finished, &fixture.server,
                    [&] { ++finished; }, Qt::DirectConnection );
  QObject::connect( client, &LlmStreamingClient::errorOccurred, &fixture.server,
                    [&]( const QString & ) { ++errors; }, Qt::DirectConnection );
  QObject::connect( client, &LlmStreamingClient::usageReceived, &fixture.server,
                    [&]( const QJsonObject & ) { ++usage; owner.reset(); }, Qt::DirectConnection );

  client->sendChatCompletion( {} );
  REQUIRE( waitForUsageTransport( [&] { return client.isNull(); } ) );
  CHECK( fixture.requests == 1 );
  CHECK( owner == nullptr );
  CHECK( usage == 1 );
  CHECK( content.isEmpty() );
  CHECK( finished == 0 );
  CHECK( errors == 0 );
}

TEST_CASE( "A real nested stream keeps its partial SSE frame and tool arguments after usage reentry",
           "[agent][client][usage][wire][lifecycle]" )
{
  bool tailAtEof = false;
  SECTION( "readyRead resumes after the direct usage callback" ) {}
  SECTION( "replyFinished resumes after a newline-free usage tail" ) { tailAtEof = true; }

  ensureQtApp();
  const UsageFixtureSettings settings;
  UsageReentryServer fixture;
  REQUIRE( fixture.server.listen( QHostAddress::LocalHost, 0 ) );
  const QByteArray partial = usageContentFrame( QStringLiteral( "nested-tail" ) );
  const qsizetype split = partial.size() / 2;
  fixture.respond = [&]( int index, QTcpSocket *socket ) {
    if ( index == 0 )
    {
      QByteArray body = usageLine( fixtureUsage(), QStringLiteral( "old-inline" ) ).toUtf8();
      if ( !tailAtEof ) body += "\n\n" + usageContentFrame( QStringLiteral( "old-buffered" ) ) + "\n\n";
      UsageReentryServer::beginResponse( socket, body, tailAtEof );
    }
    else
    {
      // One small write delivers a complete tool fragment, a public progress
      // marker, and the beginning of another SSE line to the nested readyRead.
      const QByteArray body = usageToolFrame( QStringLiteral( "{\"count\":" ), true ) + "\n\n"
                            + usageContentFrame( QStringLiteral( "nested-marker" ) ) + "\n\n"
                            + partial.left( split );
      UsageReentryServer::beginResponse( socket, body, false );
    }
  };

  LlmStreamingClient client;
  client.setProfile( fixture.profile() );
  client.setIncludeUsage( true );
  int usage = 0, finished = 0, errors = 0, malformed = 0;
  bool nestedReady = false;
  QString content;
  QList<QJsonObject> calls;
  QObject::connect( &client, &LlmStreamingClient::contentTokenReceived, &fixture.server,
                    [&]( const QString &text ) { content += text; }, Qt::DirectConnection );
  QObject::connect( &client, &LlmStreamingClient::finished, &fixture.server,
                    [&] { ++finished; }, Qt::DirectConnection );
  QObject::connect( &client, &LlmStreamingClient::errorOccurred, &fixture.server,
                    [&]( const QString & ) { ++errors; }, Qt::DirectConnection );
  QObject::connect( &client, &LlmStreamingClient::malformedToolCall, &fixture.server,
                    [&]( const QJsonObject & ) { ++malformed; }, Qt::DirectConnection );
  QObject::connect( &client, &LlmStreamingClient::toolCallParsed, &fixture.server,
                    [&]( const QJsonObject &call ) { calls.append( call ); }, Qt::DirectConnection );
  QObject::connect( &client, &LlmStreamingClient::usageReceived, &fixture.server,
                    [&]( const QJsonObject & ) {
    ++usage;
    client.sendChatCompletion( {} );
    // Pump the second response before returning through the first response's
    // parseSseLine and outer transport handler. No assertion throws inside Qt.
    nestedReady = waitForUsageTransport( [&] {
      return fixture.requests == 2 && content == QStringLiteral( "nested-marker" );
    } );
  }, Qt::DirectConnection );

  client.sendChatCompletion( {} );
  REQUIRE( waitForUsageTransport( [&] { return nestedReady; } ) );
  REQUIRE( fixture.peers.size() == 2 );
  REQUIRE( fixture.peers.last() );
  CHECK( content == QStringLiteral( "nested-marker" ) );
  CHECK( calls.isEmpty() );
  CHECK( malformed == 0 );
  CHECK( finished == 0 );

  // Complete BOTH pending pieces only after the superseded handler returned.
  // An old EOF handler clearing m_buffer loses nested-tail; flushing the new
  // tool accumulator early causes a refusal and loses the eventual full call.
  fixture.peers.last()->write( partial.mid( split ) + "\n\n"
      + usageToolFrame( QStringLiteral( "1}" ), false ) + "\n\ndata: [DONE]\n\n" );
  fixture.peers.last()->disconnectFromHost();
  REQUIRE( waitForUsageTransport( [&] { return finished == 1; } ) );
  CHECK( content == QStringLiteral( "nested-markernested-tail" ) );
  REQUIRE( calls.size() == 1 );
  CHECK( calls.first().value( QStringLiteral( "id" ) ).toString() == QStringLiteral( "nested_call" ) );
  const QJsonObject function = calls.first().value( QStringLiteral( "function" ) ).toObject();
  CHECK( function.value( QStringLiteral( "name" ) ).toString() == QStringLiteral( "nested_tool" ) );
  const QJsonObject expectedArguments{ { QStringLiteral( "count" ), 1 } };
  CHECK( function.value( QStringLiteral( "arguments" ) ).toObject() == expectedArguments );
  CHECK( usage == 1 );
  CHECK( malformed == 0 );
  CHECK( errors == 0 );
  CHECK( finished == 1 );
}

TEST_CASE( "A direct Native usage observer can delete its runtime during a real HTTP callback",
           "[agent][client][usage][wire][lifecycle][native]" )
{
  bool tailAtEof = false;
  SECTION( "readyRead emits usageUpdated while the HTTP response remains open" ) {}
  SECTION( "replyFinished emits usageUpdated from a newline-free EOF tail" ) { tailAtEof = true; }

  ensureQtApp();
  const UsageFixtureSettings settings;
  UsageReentryServer fixture;
  REQUIRE( fixture.server.listen( QHostAddress::LocalHost, 0 ) );
  fixture.respond = [&]( int, QTcpSocket *socket ) {
    QByteArray body = usageLine( fixtureUsage(), QStringLiteral( "old-inline" ) ).toUtf8();
    if ( !tailAtEof ) body += "\n\n" + usageContentFrame( QStringLiteral( "old-buffered" ) ) + "\n\n";
    UsageReentryServer::beginResponse( socket, body, tailAtEof );
  };

  using sicnu::agent::runtime::NativeConversationRuntime;
  auto owner = std::make_unique<NativeConversationRuntime>();
  QPointer<NativeConversationRuntime> runtime = owner.get();
  sicnu::agent::runtime::ConversationConfig config;
  config.sessionId = QStringLiteral( "synthetic-usage-deletion" );
  config.profile = fixture.profile();
  config.systemPrompt = QStringLiteral( "Synthetic loopback lifecycle fixture" );
  int usage = 0, settled = 0;
  QString content, observedRun;
  QObject::connect( runtime, &NativeConversationRuntime::contentDelta, &fixture.server,
                    [&]( const QString &, const QString &text ) { content += text; }, Qt::DirectConnection );
  QObject::connect( runtime, &NativeConversationRuntime::settled, &fixture.server,
                    [&]( const QString &, bool, const QString & ) { ++settled; }, Qt::DirectConnection );
  QObject::connect( runtime, &NativeConversationRuntime::usageUpdated, &fixture.server,
                    [&]( const QString &run, const QJsonObject & ) {
    observedRun = run;
    ++usage;
    // This exercises Native's destructor/cancel path before QObject destroys
    // the owned client, while the client's real network parser is still active.
    owner.reset();
  }, Qt::DirectConnection );

  runtime->startSession( config );
  REQUIRE( runtime->submitPrompt( QStringLiteral( "delete-in-usage" ), QStringLiteral( "Synthetic request" ) ) );
  REQUIRE( waitForUsageTransport( [&] { return runtime.isNull(); } ) );
  CHECK( fixture.requests == 1 );
  CHECK( owner == nullptr );
  CHECK( usage == 1 );
  CHECK( observedRun == QStringLiteral( "delete-in-usage" ) );
  CHECK( content.isEmpty() );
  CHECK( settled == 0 );
}
