// tests/test_harness_catalog.cpp
//
// Harness 4.0 Phases 1/2/14: canonical tool manifest + taxonomy. Pins the
// closed vocabularies, risk classification of every mutating namespace, the
// AgentMetadata enrichment path, and bounded manifest sizes.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include "agent/harness/harness_tools.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/tool_manifest.h"
#include "agent/harness/tool_taxonomy.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <string>

using namespace sicnu::agent::harness;
using namespace sicnu::agent::spatial_tools;

TEST_CASE( "Taxonomy is a closed, total classification", "[harness][taxonomy]" )
{
  CHECK( taxonomyForTool( "rs:ndvi" ).toString() == "processing.run" );
  CHECK( taxonomyForTool( "gdal:reproject" ).toString() == "processing.run" );
  CHECK( taxonomyForTool( "spatial:raster_inspect" ).toString() == "raster.inspect" );
  CHECK( taxonomyForTool( "data:describe_dataset" ).toString() == "data.inspect" );
  CHECK( taxonomyForTool( "workflow:preflight" ).toString() == "workflow.preflight" );
  CHECK( taxonomyForTool( "cartography:compose" ).toString() == "map.compose" );
  CHECK( taxonomyForTool( "layout:export" ).toString() == "map.render" );
  CHECK( taxonomyForTool( "result:inspect" ).toString() == "result.inspect" );
  CHECK( taxonomyForTool( "lineage:upstream" ).toString() == "provenance.inspect" );
  CHECK( taxonomyForTool( "model:some_model" ).toString() == "model.run" );
  // Total over the id space: unknown ids degrade to other.other, never throw.
  CHECK( taxonomyForTool( "totally:unknown" ).toString() == "other.other" );
  CHECK( isKnownTaxonomyDomain( "raster" ) );
  CHECK( !isKnownTaxonomyDomain( "warp" ) );
  CHECK( isKnownTaxonomyAction( "raster", "inspect" ) );
  CHECK( !isKnownTaxonomyAction( "raster", "teleport" ) );
}

TEST_CASE( "Risk classes cover every mutating namespace", "[harness][manifest]" )
{
  using namespace risk_classes;
  CHECK( riskClassForToolId( "spatial:raster_inspect" ) == kReadOnly );
  CHECK( riskClassForToolId( "data:list_layers" ) == kReadOnly );
  CHECK( riskClassForToolId( "rs:ndvi" ) == kCreatesArtifact );
  CHECK( riskClassForToolId( "gdal:clip" ) == kExternalProcess );
  CHECK( riskClassForToolId( "symbology:apply_graduated" ) == kModifiesProject );
  CHECK( riskClassForToolId( "temporal:remove_collection" ) == kDestructive );
  CHECK( riskClassForToolId( "layout:remove_item" ) == kDestructive );
  CHECK( riskClassForToolId( "workspace:undo" ) == kModifiesProject );
  CHECK( riskClassForToolId( "layout:export" ) == kCreatesArtifact );
  CHECK( riskClassForToolId( "view:set_extent" ) == kModifiesDisplay );
  // Unknown ids default to the safe answer.
  CHECK( riskClassForToolId( "mystery:tool" ) == kReadOnly );
  CHECK( isKnownRiskClass( kModifiesDisplay ) );
  CHECK( !isKnownRiskClass( "kind_of_dangerous" ) );
}

TEST_CASE( "Manifests declare risk, side effects, and expected artifacts", "[harness][manifest]" )
{
  const ToolManifest operatorManifest = manifestForToolId( "rs:ndvi" );
  CHECK( operatorManifest.riskClass == risk_classes::kCreatesArtifact );
  CHECK( operatorManifest.cancellable );
  CHECK( operatorManifest.producesProvenance );
  REQUIRE( operatorManifest.expectedArtifacts.size() == 1 );
  CHECK( operatorManifest.expectedArtifacts[0].persistence == "committed_asset" );
  REQUIRE( !operatorManifest.preconditions.empty() );

  const ToolManifest readOnly = manifestForToolId( "spatial:raster_inspect" );
  CHECK( readOnly.riskClass == risk_classes::kReadOnly );
  CHECK( !readOnly.sideEffects );
  CHECK( readOnly.idempotent );
  CHECK( readOnly.expectedArtifacts.empty() );
}

TEST_CASE( "Manifest JSON stays within the bounded-size contract", "[harness][manifest]" )
{
  const std::string bigId( "rs:some_extremely_long_operator_name_for_stress" );
  const Json::Value json = manifestForToolId( bigId ).toJson();
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  const std::string serialized = Json::writeString( builder, json );
  // Well under the 512 KiB tool-output cap and small enough for one catalog row.
  CHECK( serialized.size() < 2048 );
}

TEST_CASE( "Harness tools register and answer deterministically", "[harness][tools]" )
{
  registerHarnessTools();
  registerHarnessTools(); // idempotent

  auto manifestTool = SpatialToolRegistry::instance().find( "harness:tool_manifest" );
  REQUIRE( manifestTool.has_value() );

  SECTION( "single unknown tool id is a typed failure" )
  {
    Json::Value input;
    input["tool_id"] = "rs:does_not_exist";
    const SpatialToolResult result = ( *manifestTool )->execute( input );
    CHECK( !result.success );
    CHECK( result.errorCode == "TOOL_NOT_FOUND" );
  }

  SECTION( "error taxonomy tool lists the closed vocabulary" )
  {
    auto codesTool = SpatialToolRegistry::instance().find( "harness:error_codes" );
    REQUIRE( codesTool.has_value() );
    const SpatialToolResult result = ( *codesTool )->execute( Json::Value() );
    REQUIRE( result.success );
    const Json::Value &codes = result.output["codes"];
    CHECK( codes.isArray() );
    CHECK( codes.size() >= 22 );
    bool sawTransient = false;
    for ( const auto &code : codes )
    {
      REQUIRE( code.isMember( "code" ) );
      REQUIRE( code.isMember( "retry_class" ) );
      if ( std::string( code["code"].asString() ) == "TRANSIENT_FAILURE" )
        sawTransient = code["retry_class"].asString() == "transient";
    }
    CHECK( sawTransient );
    CHECK( result.output["risk_classes"].size() == 7 );
  }
}
