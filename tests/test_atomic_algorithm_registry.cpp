// tests/test_atomic_algorithm_registry.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_registry.h"
#include "operators/rs/rs_spectral_index_operator.h"

using namespace sicnu::processing;
using namespace sicnu::operators;
using namespace sicnu::operators::rs;

TEST_CASE( "AtomicAlgorithmRegistry singleton registers, looks up, and resets adapters", "[processing][registry]" )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();

  REQUIRE( registry.adapterCount() > 0 );

  // Check built-in operator presence
  auto adapter = registry.findAdapter( "rs:spectral_index" );
  REQUIRE( adapter != nullptr );
  REQUIRE( adapter->algorithmId() == "rs:spectral_index" );

  AlgorithmDescriptor desc = adapter->descriptor();
  REQUIRE( desc.id == "rs:spectral_index" );

  // Test custom adapter registration
  auto customOp = std::make_unique<RsSpectralIndexOperator>();
  auto customAdapter = std::make_shared<RsOperatorAdapter>( std::move( customOp ) );

  registry.registerAdapter( customAdapter );
  REQUIRE( registry.findAdapter( "rs:spectral_index" ) != nullptr );

  // Test unregistering
  bool erased = registry.unregisterAdapter( "rs:spectral_index" );
  REQUIRE( erased == true );
  REQUIRE( registry.findAdapter( "rs:spectral_index" ) == nullptr );

  // Test listDescriptors
  auto descriptors = registry.listDescriptors();
  REQUIRE( !descriptors.empty() );

  // Reset restores builtins
  registry.reset();
  REQUIRE( registry.findAdapter( "rs:spectral_index" ) != nullptr );
}

TEST_CASE( "AgentToolCallExporter exports OpenAI Tool Call JSON and Markdown Prompt Catalog", "[processing][exporter]" )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();

  Json::Value toolsJson = registry.exportOpenAiToolDefinitions();
  REQUIRE( toolsJson.isArray() );
  REQUIRE( toolsJson.size() > 0 );

  bool foundSpectralIndex = false;
  for ( const auto &item : toolsJson )
  {
    REQUIRE( item.isObject() );
    REQUIRE( item["type"].asString() == "function" );
    REQUIRE( item["function"].isObject() );

    std::string funcName = item["function"]["name"].asString();
    if ( funcName == "rs_spectral_index" )
    {
      foundSpectralIndex = true;
      REQUIRE( item["function"]["parameters"]["type"].asString() == "object" );
      REQUIRE( item["function"]["parameters"]["properties"].isMember( "input" ) );
    }
  }
  REQUIRE( foundSpectralIndex == true );

  std::string markdownCatalog = registry.exportSystemPromptCatalog();
  REQUIRE( !markdownCatalog.empty() );
  REQUIRE( markdownCatalog.find( "# AI Agent Remote Sensing Tool Catalog" ) != std::string::npos );
  REQUIRE( markdownCatalog.find( "`rs:spectral_index`" ) != std::string::npos );
}


