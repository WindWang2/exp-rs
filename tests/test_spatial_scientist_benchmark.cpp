// tests/test_spatial_scientist_benchmark.cpp
//
// Phase Q — Spatial Scientist Benchmark harness (111 structured tasks).
// Deterministic: graders verify tool selection, ranking, workflow preflight,
// map validity, repair convergence, and bounded context against the live
// registries — never against model behavior or heavy raster execution.
//
// Task dataset: data/benchmarks/spatial_scientist_tasks.json (path injected
// via the SICNU_BENCHMARK_TASKS_JSON compile definition, overridable with
// the SICNU_BENCHMARK_TASKS env var).
//
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>
#include <json/reader.h>

#include "agent/cartography/cartography_tools.h"
#include "agent/contracts/spatial_contracts.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/tool_catalog/agent_tool.h"
#include "agent/tool_catalog/agent_tool_catalog.h"

#include <qgsapplication.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsApplication::exitQgis();
  return result;
}

namespace {

using sicnu::agent::spatial_tools::SpatialToolRegistry;

Json::Value loadTasks()
{
  std::vector<std::string> candidates;
  if ( qEnvironmentVariableIsSet( "SICNU_BENCHMARK_TASKS" ) )
    candidates.push_back( qEnvironmentVariable( "SICNU_BENCHMARK_TASKS" ) );
#ifdef SICNU_BENCHMARK_TASKS_JSON
  candidates.push_back( SICNU_BENCHMARK_TASKS_JSON );
#endif
  for ( const auto &path : candidates )
  {
    std::ifstream stream( path );
    if ( !stream )
      continue;
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    if ( Json::parseFromStream( builder, stream, &doc, &errors ) )
      return doc;
  }
  return Json::Value();
}

bool isSpatialTool( const std::string &name )
{
  for ( const char *prefix : { "spatial:", "temporal:", "cartography:", "symbology:", "workflow:",
                               "workspace:", "layout:" } )
  {
    if ( name.rfind( prefix, 0 ) == 0 )
      return true;
  }
  return false;
}

bool toolHasSchemaKeys( const Json::Value &schema, const std::vector<std::string> &keys )
{
  if ( keys.empty() )
    return true;
  if ( !schema.isObject() || !schema.isMember( "properties" ) )
    return false;
  for ( const auto &key : keys )
    if ( !schema["properties"].isMember( key ) )
      return false;
  return true;
}

struct GraderResult
{
  bool ok = false;
  std::string reason;
};

GraderResult runTask( const Json::Value &task, Json::Value &evidence )
{
  const Json::Value &expect = task["expect"];
  const std::string kind = expect.get( "kind", "" ).asString();

  // --- tool registered + schema keys --------------------------------------
  if ( kind == "tool_registered" )
  {
    const std::string tool = expect["tool"].asString();
    std::vector<std::string> keys;
    for ( const auto &key : expect.get( "schema_keys", Json::Value( Json::arrayValue ) ) )
      keys.push_back( key.asString() );

    if ( isSpatialTool( tool ) )
    {
      const auto registered = SpatialToolRegistry::instance().find( tool );
      if ( !registered )
        return { false, "spatial tool not registered: " + tool };
      if ( !toolHasSchemaKeys( ( *registered )->inputSchema(), keys ) )
        return { false, "missing schema keys on " + tool };
      return { true, "" };
    }
    const auto catalogTool = sicnu::agent::tool_catalog::AgentToolCatalog::instance().findTool( tool );
    if ( !catalogTool )
      return { false, "catalog tool not found: " + tool };
    if ( !toolHasSchemaKeys( catalogTool->inputSchema, keys ) )
      return { false, "missing schema keys on " + tool };
    return { true, "" };
  }

  // --- capability ranking ---------------------------------------------------
  if ( kind == "capability_ranked" )
  {
    auto tool = SpatialToolRegistry::instance().find( "spatial:search_capabilities" );
    REQUIRE( tool.has_value() );
    Json::Value input( Json::objectValue );
    input["query"] = expect["query"].asString();
    input["limit"] = 15;
    const auto result = ( *tool )->execute( input );
    if ( !result.success )
      return { false, "search_capabilities failed: " + result.error };
    const Json::Value &candidates = result.output["candidates"];
    evidence["candidates"] = candidates;
    const std::string wanted = expect["rank_first_or_any"].asString();
    for ( const auto &candidate : candidates )
    {
      if ( candidate["candidate"].asString() == wanted )
        return { true, "" };
    }
    return { false, "expected capability not ranked in candidates: " + wanted };
  }

  // --- model selection --------------------------------------------------------
  if ( kind == "model_select" )
  {
    auto tool = SpatialToolRegistry::instance().find( "spatial:select_model" );
    REQUIRE( tool.has_value() );
    Json::Value input = task["payload"]["task_criteria"];
    const auto result = ( *tool )->execute( input );
    if ( !result.success )
      return { false, "select_model failed: " + result.error };
    evidence["candidate_count"] = result.output["total"];
    return { true, "" };
  }

  // --- workflow preflight -------------------------------------------------------
  if ( kind == "workflow_preflight" )
  {
    auto tool = SpatialToolRegistry::instance().find( "workflow:preflight" );
    REQUIRE( tool.has_value() );
    Json::Value input( Json::objectValue );
    input["workflow"] = task["payload"]["workflow"];
    const auto result = ( *tool )->execute( input );
    if ( !result.success )
      return { false, "workflow:preflight failed: " + result.error };
    const std::string verdict = result.output["verdict"].asString();
    evidence["verdict"] = verdict;
    bool verdictOk = false;
    for ( const auto &allowed : expect["allowed_verdicts"] )
      verdictOk = verdictOk || allowed.asString() == verdict;
    if ( !verdictOk )
      return { false, "unexpected verdict: " + verdict };
    for ( const auto &code : expect.get( "expected_codes", Json::Value( Json::arrayValue ) ) )
    {
      bool found = false;
      for ( const auto &issue : result.output["issues"] )
        found = found || issue["code"].asString() == code.asString();
      if ( !found )
        return { false, "missing expected issue code: " + code.asString() };
    }
    return { true, "" };
  }

  // --- map preflight --------------------------------------------------------------
  if ( kind == "map_preflight" )
  {
    auto tool = SpatialToolRegistry::instance().find( "cartography:preflight" );
    REQUIRE( tool.has_value() );
    Json::Value input( Json::objectValue );
    input["mapspec"] = task["payload"]["mapspec"];
    const auto result = ( *tool )->execute( input );
    if ( !result.success )
      return { false, "cartography:preflight failed: " + result.error };
    const bool passed = result.output["passed"].asBool();
    evidence["quality_score"] = result.output["quality_score"];
    if ( expect.isMember( "passed" ) && passed != expect["passed"].asBool() )
      return { false, "passed mismatch" };
    if ( expect.isMember( "min_score" ) && result.output["quality_score"].asInt() < expect["min_score"].asInt() )
      return { false, "quality score below threshold" };
    for ( const auto &code : expect.get( "expected_codes", Json::Value( Json::arrayValue ) ) )
    {
      bool found = false;
      for ( const auto &issue : result.output["issues"] )
        found = found || issue["code"].asString() == code.asString();
      if ( !found )
        return { false, "missing expected issue code: " + code.asString() };
    }
    return { true, "" };
  }

  // --- template instantiation ---------------------------------------------------------
  if ( kind == "template_instantiate" )
  {
    auto tool = SpatialToolRegistry::instance().find( "cartography:instantiate_template" );
    REQUIRE( tool.has_value() );
    Json::Value input( Json::objectValue );
    input["template"] = expect["template"].asString();
    input["params"] = task["payload"]["params"];
    const auto result = ( *tool )->execute( input );
    if ( !result.success )
      return { false, "instantiate failed: " + result.error };
    // Draft must pass structural validation (warnings allowed).
    auto preflight = SpatialToolRegistry::instance().find( "cartography:preflight" );
    REQUIRE( preflight.has_value() );
    Json::Value preflightInput( Json::objectValue );
    preflightInput["mapspec"] = result.output;
    const auto quality = ( *preflight )->execute( preflightInput );
    if ( !quality.success )
      return { false, "preflight failed on draft" };
    if ( !quality.output["passed"].asBool() )
      return { false, "template draft has blocking issues" };
    if ( quality.output["quality_score"].asInt() < 60 )
      return { false, "template draft quality below 60" };
    return { true, "" };
  }

  // --- repair convergence ---------------------------------------------------------------
  if ( kind == "map_repair" )
  {
    auto tool = SpatialToolRegistry::instance().find( "cartography:repair" );
    REQUIRE( tool.has_value() );
    Json::Value input( Json::objectValue );
    input["mapspec"] = task["payload"]["mapspec"];
    input["max_iterations"] = 4;
    const auto result = ( *tool )->execute( input );
    if ( !result.success )
      return { false, "cartography:repair failed: " + result.error };
    const int repairs = result.output["repairs_applied"].asInt();
    evidence["repairs_applied"] = repairs;
    const int minRepairs = expect.isMember( "min_repairs" ) ? expect["min_repairs"].asInt() : 1;
    if ( repairs < minRepairs )
      return { false, "expected at least " + std::to_string( minRepairs ) + " repairs, got " +
                        std::to_string( repairs ) };
    if ( !result.output["quality"]["passed"].asBool() )
      return { false, "repaired map still has blocking issues" };
    return { true, "" };
  }

  // --- bounded context -----------------------------------------------------------------------
  if ( kind == "bounded_search" )
  {
    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    const auto tools = catalog.searchTools( expect["query"].asString() );
    const Json::Value exported = catalog.exportMcpTools( tools );
    const size_t size = sicnu::agent::contracts::serializedSize( exported );
    evidence["bytes"] = static_cast<Json::UInt64>( size );
    const size_t cap = static_cast<size_t>( expect["max_bytes"].asLargestUInt() );
    if ( size > cap )
      return { false, "search response " + std::to_string( size ) + " bytes exceeds cap" };
    if ( tools.empty() )
      return { false, "expected non-empty search results" };
    return { true, "" };
  }

  return { false, "unknown grader kind: " + kind };
}

} // namespace

TEST_CASE( "Spatial Scientist Benchmark suite", "[benchmark][spatial_scientist]" )
{
  auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
  catalog.initializeDefaults();
  SpatialToolRegistry::instance().registerBuiltinTools();

  const Json::Value doc = loadTasks();
  REQUIRE( doc.isObject() );
  REQUIRE( doc["tasks"].isArray() );
  const Json::Value &tasks = doc["tasks"];
  REQUIRE( tasks.size() >= 100 );

  std::map<std::string, std::pair<int, int>> perCategory; // category -> {passed, total}
  std::vector<std::string> failures;

  for ( const auto &task : tasks )
  {
    const std::string category = task["category"].asString();
    const std::string id = task["id"].asString();
    Json::Value evidence;
    const GraderResult result = runTask( task, evidence );
    auto &score = perCategory[category];
    score.second += 1;
    if ( result.ok )
    {
      score.first += 1;
    }
    else
    {
      failures.push_back( id + " [" + category + "]: " + result.reason );
      WARN( "benchmark failure " << id << " [" << category << "]: " << result.reason );
    }
  }

  // Every task must pass for the suite to be green; the per-category summary
  // keeps failures attributable.
  for ( const auto &[ category, score ] : perCategory )
  {
    INFO( "category " << category << ": " << score.first << "/" << score.second );
    CHECK( score.first == score.second );
  }
  INFO( "total tasks: " << tasks.size() << ", failures: " << failures.size() );
  CHECK( failures.empty() );
}
