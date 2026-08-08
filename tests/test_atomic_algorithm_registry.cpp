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

// ---------------------------------------------------------------------------
// ADR 0062 convergence invariant: every algorithm must be reachable through the
// AtomicAlgorithmRegistry with a round-trippable descriptor and a valid input
// schema. This is the executable form of the "全量算子注册/描述/反射调用成功率
// 100%" verification standard. It runs over whatever providers are populated
// (RS operators always; gdal/otb/qgis/python when their environment is present)
// rather than asserting a fixed set, so it stays deterministic and green in any
// build configuration while still catching any adapter that bypasses the
// registry or returns a malformed descriptor.
// ---------------------------------------------------------------------------
TEST_CASE( "AtomicAlgorithmRegistry convergence: every descriptor resolves, round-trips, and exports a valid schema",
           "[processing][registry][convergence]" )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();

  const auto descriptors = registry.listDescriptors();
  REQUIRE( !descriptors.empty() );

  for ( const auto &desc : descriptors )
  {
    INFO( "algorithm id: " << desc.id );

    // 1. Reflection: the id advertised by listDescriptors() must resolve.
    const auto adapter = registry.findAdapter( desc.id );
    REQUIRE( adapter != nullptr );

    // 2. Round-trip: findAdapter() must return an adapter whose descriptor
    //    matches the one we iterated (no stale/mismatched entries).
    const auto reflected = adapter->descriptor();
    REQUIRE( reflected.id == desc.id );
    REQUIRE( reflected.id == adapter->algorithmId() );

    // 3. Contract: displayName is non-empty (LLM catalog + UI requirement).
    REQUIRE( !desc.displayName.empty() );

    // 4. Contract: input schema must be a well-formed JSON Schema object so
    //    the descriptor is usable by ToolCallDispatcher / the agent catalog
    //    without per-adapter glue.
    const Json::Value inputSchema = desc.toInputSchema();
    REQUIRE( inputSchema.isObject() );
    REQUIRE( inputSchema.isMember( "type" ) );
    REQUIRE( inputSchema["type"].asString() == "object" );
    REQUIRE( inputSchema.isMember( "properties" ) );
    REQUIRE( inputSchema["properties"].isObject() );
  }
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



TEST_CASE( "Operators expose execution estimates through agent metadata", "[processing][registry][large-raster]" )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();
  const auto descriptors = registry.listDescriptors();
  REQUIRE_FALSE( descriptors.empty() );

  bool foundRx = false;
  bool foundApplyMask = false;
  for ( const auto &d : descriptors )
  {
    if ( d.id == "rs:rx_anomaly" )
    {
      foundRx = true;
      REQUIRE( d.agentMetadata.execution.isObject() );
      // FullRaster operators declare a peak-RAM estimate and no tile.
      CHECK( d.agentMetadata.execution["estimatedRamBytes"].asInt64() > 0 );
      CHECK( d.agentMetadata.execution["tileWidth"].asInt64() == 0 );
      CHECK( d.agentMetadata.memoryPolicy == "full_raster" );
    }
    if ( d.id == "rs:apply_mask" )
    {
      foundApplyMask = true;
      REQUIRE( d.agentMetadata.execution.isObject() );
      // Streaming operators declare a tile and a modest RAM estimate.
      CHECK( d.agentMetadata.execution["tileHeight"].asInt64() > 0 );
      CHECK( d.agentMetadata.execution["estimatedRamBytes"].asInt64() > 0 );
      CHECK( d.agentMetadata.memoryPolicy == "streaming" );
    }
  }
  REQUIRE( foundRx );
  REQUIRE( foundApplyMask );

  // Every registered rs: operator must declare a large-raster memory policy
  // (the execution estimate itself may legitimately stay unknown).
  for ( const auto &d : descriptors )
  {
    if ( d.id.rfind( "rs:", 0 ) == 0 )
      CHECK_FALSE( d.agentMetadata.memoryPolicy.empty() );
  }
}
