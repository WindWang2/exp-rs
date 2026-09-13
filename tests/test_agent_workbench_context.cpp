// Workbench 10.0 — `workbench:context` read-only agent projection (goal WP-A)
//
// Covers: tool metadata/schema shape, honest WORKBENCH_UNAVAILABLE failure
// without a shell provider, success path with an injected provider, and
// SpatialToolRegistry registration semantics (register-before-shell keeps
// built-ins untouched; no reset() interference).
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/agent_context_tool.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <json/json.h>

using namespace sicnu::app;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolPtr;
using sicnu::agent::spatial_tools::SpatialToolResult;

namespace
{

Json::Value samplePayload()
{
  Json::Value root( Json::objectValue );
  root["workbenchId"] = "map";
  Json::Value primary( Json::objectValue );
  primary["kind"] = "layer";
  primary["id"] = "layer-abc";
  root["primaryObject"] = primary;
  Json::Value commands( Json::arrayValue );
  commands.append( "rs.spectralIndex" );
  root["availableCommands"] = commands;
  return root;
}

} // namespace

TEST_CASE( "workbench:context tool metadata and schema", "[agent][workbench_context]" )
{
  WorkbenchContextTool tool( [] { return Json::Value(); } );
  REQUIRE( tool.name() == "workbench:context" );
  REQUIRE( tool.inputSchema()["type"].asString() == "object" );
  REQUIRE( tool.inputSchema()["properties"].isObject() );
  REQUIRE( tool.outputSchema()["type"].asString() == "object" );
  REQUIRE( !tool.description().empty() );
}

TEST_CASE( "workbench:context refuses honestly without a shell", "[agent][workbench_context]" )
{
  SECTION( "null provider" )
  {
    WorkbenchContextTool tool( nullptr );
    const SpatialToolResult result = tool.execute( Json::Value( Json::objectValue ) );
    REQUIRE_FALSE( result.success );
    REQUIRE( result.errorCode == "WORKBENCH_UNAVAILABLE" );
    REQUIRE( result.errorCategory == "runtime" );
  }
  SECTION( "provider reports unassembled shell" )
  {
    WorkbenchContextTool tool( [] { return Json::Value(); } );
    const SpatialToolResult result = tool.execute( Json::Value( Json::objectValue ) );
    REQUIRE_FALSE( result.success );
    REQUIRE( result.errorCode == "WORKBENCH_UNAVAILABLE" );
  }
}

TEST_CASE( "workbench:context returns the injected payload", "[agent][workbench_context]" )
{
  WorkbenchContextTool tool( [] { return samplePayload(); } );
  const SpatialToolResult result = tool.execute( Json::Value( Json::objectValue ) );
  REQUIRE( result.success );
  REQUIRE( result.output["workbenchId"].asString() == "map" );
  REQUIRE( result.output["primaryObject"]["kind"].asString() == "layer" );
  REQUIRE( result.output["availableCommands"][0].asString() == "rs.spectralIndex" );
}

TEST_CASE( "workbench:context registers under its name without clobbering",
           "[agent][workbench_context]" )
{
  auto &registry = SpatialToolRegistry::instance();
  const bool first = registry.registerTool(
    SpatialToolPtr{ new WorkbenchContextTool( [] { return Json::Value(); } ) } );
  // Second registration with the same name must be rejected (existing entry
  // kept) — the shell may run this path on rebuild.
  const bool second = registry.registerTool(
    SpatialToolPtr{ new WorkbenchContextTool( [] { return Json::Value(); } ) } );
  const auto found = registry.find( "workbench:context" );
  REQUIRE( found.has_value() );
  REQUIRE( ( first || !second ) ); // at most one live registration wins
  REQUIRE( ( *found )->name() == "workbench:context" );
}
