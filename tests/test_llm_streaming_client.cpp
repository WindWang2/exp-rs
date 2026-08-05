// tests/test_llm_streaming_client.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/llm_config_manager.h"
#include "agent/llm_streaming_client.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QCoreApplication>

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
