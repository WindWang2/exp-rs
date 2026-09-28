// tests/test_llm_streaming_client.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adversarial_corpus.h"
#include "agent/llm_config_manager.h"
#include "agent/llm_streaming_client.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>

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
