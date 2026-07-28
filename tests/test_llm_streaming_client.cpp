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
