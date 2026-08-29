// tests/test_agent_tool_catalog.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/tool_catalog/algorithm_tool_provider.h"
#include "agent/tool_catalog/interaction_tool_provider.h"
#include "agent/interaction_tool_registry.h"
#include "agent/tool_catalog/data_tool_provider.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include <atomic>
#include <thread>

using namespace sicnu::agent::tool_catalog;
using namespace sicnu::processing;

TEST_CASE( "AgentToolCatalog: Algorithm Discovery", "[agent][tool_catalog][algorithm]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  auto &catalog = AgentToolCatalog::instance();
  catalog.reset();

  SECTION( "Discovers processing algorithms from AtomicAlgorithmRegistry" )
  {
    const auto processingTools = catalog.listTools( ToolCategory::Processing );
    REQUIRE_FALSE( processingTools.empty() );

    bool foundSpectral = false;
    for ( const auto &tool : processingTools )
    {
      CHECK( tool.category == ToolCategory::Processing );
      if ( tool.name == "rs:spectral_index" )
      {
        foundSpectral = true;
        CHECK( tool.group == "spectral" );
        CHECK_FALSE( tool.displayName.empty() );
        CHECK_FALSE( tool.description.empty() );
        CHECK( tool.inputSchema.isObject() );
        CHECK( tool.inputSchema.isMember( "properties" ) );
        CHECK( tool.inputSchema["properties"].isMember( "input" ) );
        CHECK( tool.inputSchema["properties"].isMember( "index" ) );
      }
    }
    REQUIRE( foundSpectral );
  }

  SECTION( "Finds specific algorithm by exact name or normalized name" )
  {
    auto tool = catalog.findTool( "rs:spectral_index" );
    REQUIRE( tool.has_value() );
    CHECK( tool->name == "rs:spectral_index" );
    CHECK( tool->category == ToolCategory::Processing );

    // Normalized underscore query
    auto toolNorm = catalog.findTool( "rs_spectral_index" );
    REQUIRE( toolNorm.has_value() );
    CHECK( toolNorm->name == "rs:spectral_index" );
  }

  SECTION( "getSchema returns parameter schema for processing algorithm" )
  {
    Json::Value schema = catalog.getSchema( "rs:spectral_index" );
    REQUIRE( schema.isObject() );
    CHECK( schema["type"].asString() == "object" );
    CHECK( schema["properties"].isMember( "input" ) );
    CHECK( schema["properties"].isMember( "index" ) );
  }
}

TEST_CASE( "AgentToolCatalog: Interaction Discovery", "[agent][tool_catalog][interaction]" )
{
  auto &catalog = AgentToolCatalog::instance();
  catalog.reset();

  SECTION( "Discovers built-in canvas and interaction tools" )
  {
    const auto interactionTools = catalog.listTools( ToolCategory::Interaction );
    REQUIRE( interactionTools.size() >= 3 );

    bool foundDrawRoi = false;
    bool foundSetBand = false;
    bool foundSetStretch = false;

    for ( const auto &tool : interactionTools )
    {
      CHECK( tool.category == ToolCategory::Interaction );
      if ( tool.name == "canvas:draw_roi" )
      {
        foundDrawRoi = true;
        CHECK( tool.group == "canvas" );
        CHECK( tool.inputSchema.isObject() );
        CHECK( tool.inputSchema["properties"].isMember( "bbox" ) );
      }
      else if ( tool.name == "raster:set_band_composite" )
      {
        foundSetBand = true;
        CHECK( tool.group == "display" );
        CHECK( tool.inputSchema["properties"].isMember( "red_band" ) );
      }
      else if ( tool.name == "raster:set_stretch" )
      {
        foundSetStretch = true;
        CHECK( tool.group == "display" );
        CHECK( tool.inputSchema["properties"].isMember( "stretch_type" ) );
      }
    }

    REQUIRE( foundDrawRoi );
    REQUIRE( foundSetBand );
    REQUIRE( foundSetStretch );
  }

  SECTION( "Dynamic registration of interaction tools" )
  {
    auto interactionProv = std::dynamic_pointer_cast<InteractionToolProvider>(
      catalog.provider( "InteractionToolProvider" ) );
    REQUIRE( interactionProv != nullptr );

    AgentTool customTool;
    customTool.name = "canvas:test_highlight";
    customTool.displayName = "Highlight Feature";
    customTool.category = ToolCategory::Interaction;
    customTool.group = "canvas";
    customTool.description = "Highlight a feature on canvas";
    customTool.tags = { "canvas", "highlight" };

    interactionProv->registerTool( customTool );
    auto found = catalog.findTool( "canvas:test_highlight" );
    REQUIRE( found.has_value() );
    CHECK( found->displayName == "Highlight Feature" );

    interactionProv->unregisterTool( "canvas:test_highlight" );
    CHECK_FALSE( catalog.findTool( "canvas:test_highlight" ).has_value() );
  }
}

TEST_CASE( "AgentToolCatalog: Data Discovery", "[agent][tool_catalog][data]" )
{
  auto &catalog = AgentToolCatalog::instance();
  catalog.reset();

  const auto dataTools = catalog.listTools( ToolCategory::Data );
  REQUIRE( dataTools.size() >= 3 );

  bool foundListLayers = false;
  bool foundDescribe = false;
  bool foundLineage = false;

  for ( const auto &tool : dataTools )
  {
    CHECK( tool.category == ToolCategory::Data );
    if ( tool.name == "data:list_layers" ) foundListLayers = true;
    if ( tool.name == "data:describe_dataset" ) foundDescribe = true;
    if ( tool.name == "data:get_lineage" ) foundLineage = true;
  }

  REQUIRE( foundListLayers );
  REQUIRE( foundDescribe );
  REQUIRE( foundLineage );
}

TEST_CASE( "AgentToolCatalog: Search Capabilities", "[agent][tool_catalog][search]" )
{
  auto &catalog = AgentToolCatalog::instance();
  catalog.reset();

  SECTION( "Search 'show raster' returns raster:set_band_composite and raster:set_stretch" )
  {
    const auto results = catalog.searchTools( "show raster" );
    REQUIRE_FALSE( results.empty() );

    bool foundBandComposite = false;
    bool foundStretch = false;

    for ( const auto &tool : results )
    {
      if ( tool.name == "raster:set_band_composite" ) foundBandComposite = true;
      if ( tool.name == "raster:set_stretch" ) foundStretch = true;
    }

    REQUIRE( foundBandComposite );
    REQUIRE( foundStretch );
  }

  SECTION( "Search by structured query: group and tag" )
  {
    SearchQuery q1;
    q1.group = "spectral";
    const auto spectralTools = catalog.searchTools( q1 );
    REQUIRE_FALSE( spectralTools.empty() );
    // Group search is substring, case-insensitive (documented contract), so
    // "spectral" also matches related groups like "hyperspectral".
    for ( const auto &t : spectralTools )
    {
      CHECK( t.group.find( "spectral" ) != std::string::npos );
    }

    SearchQuery q2;
    q2.tag = "composite";
    const auto compositeTools = catalog.searchTools( q2 );
    REQUIRE_FALSE( compositeTools.empty() );
    bool found = false;
    for ( const auto &t : compositeTools )
    {
      if ( t.name == "raster:set_band_composite" ) found = true;
    }
    CHECK( found );
  }

  SECTION( "Search by category" )
  {
    SearchQuery q;
    q.category = ToolCategory::Interaction;
    const auto tools = catalog.searchTools( q );
    REQUIRE_FALSE( tools.empty() );
    for ( const auto &t : tools )
    {
      CHECK( t.category == ToolCategory::Interaction );
    }
  }

  SECTION( "Search by input/output types" )
  {
    SearchQuery q;
    q.inputType = "Raster";
    const auto rasterInputs = catalog.searchTools( q );
    REQUIRE_FALSE( rasterInputs.empty() );
  }
}

TEST_CASE( "AgentToolCatalog: Schema Merge and Export", "[agent][tool_catalog][export]" )
{
  auto &catalog = AgentToolCatalog::instance();
  catalog.reset();

  SECTION( "exportOpenAiToolDefinitions produces unified array with valid OpenAI schema" )
  {
    Json::Value openAiTools = catalog.exportOpenAiToolDefinitions();
    REQUIRE( openAiTools.isArray() );
    REQUIRE( openAiTools.size() >= 5 );

    bool foundSpectral = false;
    bool foundDrawRoi = false;
    bool foundListLayers = false;

    for ( const auto &item : openAiTools )
    {
      REQUIRE( item.isObject() );
      CHECK( item["type"].asString() == "function" );
      REQUIRE( item.isMember( "function" ) );
      const auto &fn = item["function"];
      REQUIRE( fn.isMember( "name" ) );
      REQUIRE( fn.isMember( "description" ) );
      REQUIRE( fn.isMember( "parameters" ) );
      CHECK( fn["parameters"]["type"].asString() == "object" );

      const std::string name = fn["name"].asString();
      if ( name == "rs_spectral_index" ) foundSpectral = true;
      if ( name == "canvas_draw_roi" ) foundDrawRoi = true;
      if ( name == "data_list_layers" ) foundListLayers = true;
    }

    REQUIRE( foundSpectral );
    REQUIRE( foundDrawRoi );
    REQUIRE( foundListLayers );
  }

  SECTION( "exportMcpTools produces { category, name, description, schema } format" )
  {
    Json::Value mcpTools = catalog.exportMcpTools();
    REQUIRE( mcpTools.isArray() );
    REQUIRE( mcpTools.size() >= 5 );

    for ( const auto &item : mcpTools )
    {
      REQUIRE( item.isObject() );
      CHECK( item.isMember( "category" ) );
      CHECK( item.isMember( "name" ) );
      CHECK( item.isMember( "description" ) );
      CHECK( item.isMember( "schema" ) );
      CHECK( item["schema"]["type"].asString() == "object" );
    }
  }

  SECTION( "exportSystemPromptCatalog produces valid Markdown" )
  {
    std::string md = catalog.exportSystemPromptCatalog();
    REQUIRE_FALSE( md.empty() );
    CHECK( md.find( "# AI Agent Unified Tool Catalog" ) != std::string::npos );
    CHECK( md.find( "## Processing Tools" ) != std::string::npos );
    CHECK( md.find( "## Interaction Tools" ) != std::string::npos );
    CHECK( md.find( "## Data Tools" ) != std::string::npos );
  }
}

TEST_CASE( "AgentToolCatalog: Duplicate Name Detection", "[agent][tool_catalog][duplicate]" )
{
  auto &catalog = AgentToolCatalog::instance();
  catalog.reset();

  SECTION( "No duplicates when the InteractionToolRegistry is populated (#641)" )
  {
    // The real app populates InteractionToolRegistry (which unconditionally
    // registers data:*) BEFORE the catalog singleton is first constructed.
    // Reproduce that order here: with a populated registry the derivation
    // used to re-adopt the data:* tools DataToolProvider already owns and
    // listTools() emitted each one twice.
    auto &registry = sicnu::agent::InteractionToolRegistry::instance();
    registry.reset();
    registry.registerDataTools( nullptr );

    catalog.reset();

    CHECK_FALSE( catalog.hasDuplicates() );
    CHECK( catalog.findDuplicateNames().empty() );

    std::size_t listLayersCount = 0;
    for ( const auto &tool : catalog.listTools() )
    {
      if ( tool.name == "data:list_layers" )
        ++listLayersCount;
    }
    CHECK( listLayersCount == 1 );

    registry.reset();
  }

  SECTION( "No duplicates in default catalog" )
  {
    CHECK_FALSE( catalog.hasDuplicates() );
    CHECK( catalog.findDuplicateNames().empty() );
  }

  SECTION( "Detects duplicate tool names when colliding tool is registered" )
  {
    AgentTool dupTool;
    dupTool.name = "rs:spectral_index"; // Same name as builtin RS algorithm
    dupTool.displayName = "Duplicate Spectral Index";
    dupTool.category = ToolCategory::Custom;
    dupTool.description = "Duplicate test";

    catalog.registerCustomTool( dupTool );

    CHECK( catalog.hasDuplicates() );
    const auto dupNames = catalog.findDuplicateNames();
    REQUIRE( dupNames.size() == 1 );
    CHECK( dupNames.front() == "rs:spectral_index" );

    catalog.unregisterCustomTool( "rs:spectral_index" );
    CHECK_FALSE( catalog.hasDuplicates() );
  }
}

TEST_CASE( "AgentToolCatalog concurrent tool registration invalidates export caches", "[agent][tool_catalog]" )
{
  auto &catalog = AgentToolCatalog::instance();
  catalog.reset();

  std::atomic<bool> running{ true };
  std::atomic<int> exportCount{ 0 };

  std::thread reader( [&]() {
    while ( running.load() )
    {
      const Json::Value defs = catalog.exportOpenAiToolDefinitions();
      REQUIRE( defs.isArray() );
      exportCount++;
      std::this_thread::yield();
    }
  } );

  for ( int i = 0; i < 50; ++i )
  {
    AgentTool tool;
    tool.name = "custom:test_tool_" + std::to_string( i );
    tool.displayName = "Test Tool";
    tool.category = ToolCategory::Custom;
    tool.description = "Test Description";
    catalog.registerCustomTool( tool );

    const Json::Value defs = catalog.exportOpenAiToolDefinitions();
    bool found = false;
    for ( const auto &item : defs )
    {
      if ( item.isObject() && item["function"]["name"].asString() == "custom_test_tool_" + std::to_string( i ) )
      {
        found = true;
        break;
      }
    }
    CHECK( found );

    catalog.unregisterCustomTool( tool.name );
  }

  running.store( false );
  reader.join();

  CHECK( exportCount.load() > 0 );
}

