// tests/test_workflow_planner.cpp
//
// Compiler 10.0: the staged planner — pure compileWorkflow over an
// agent-authored IR and a recipe, plan<->IR conversion, lowering with
// deterministic output paths, compiler provenance riding into the plan.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <map>
#include <string>

#include "agent/harness/workflow_planner.h"

using namespace sicnu::agent::harness;

namespace
{
Json::Value parse( const std::string &text )
{
  Json::Value out;
  Json::Reader reader;
  REQUIRE( reader.parse( text, out ) );
  return out;
}

Json::Value opticalUnderstanding()
{
  return parse( R"({
    "kind": "dataset_understanding",
    "source_kind": "raster",
    "path": "/data/scenes/august.tif",
    "crs": { "authid": "EPSG:32650" },
    "crs_authid": "EPSG:32650",
    "size": [ 512, 512 ],
    "pixel_size": [ 10, 10 ],
    "band_roles": [ "blue", "green", "red", "nir" ],
    "radiometric_state": "surface_reflectance",
    "modality": "optical",
    "entity": { "asset_entity_id": "asset-3", "revision": 2 }
  })" );
}

Json::Value ndviIr()
{
  return parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "goal": "NDVI over the August scene",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndvi", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" },
        "inputs": [ { "input": "primary" } ],
        "outputs": [ { "name": "output",
                       "artifact": { "kind": "raster", "numeric_domain": "index" } } ],
        "verification": "raster" }
    ],
    "outputs": [ { "name": "ndvi", "node": "ndvi", "kind": "raster" } ],
    "expectations": { "output_dir": "/tmp/compile-out" }
  })" );
}

CompileWorkflowRequest requestFor( const Json::Value &ir )
{
  CompileWorkflowRequest request;
  request.irDoc = ir;
  request.inputFacts["primary"] = opticalUnderstanding();
  return request;
}
} // namespace

TEST_CASE( "compileWorkflow runs every stage and lowers a clean IR to an executable plan",
           "[workflow_planner]" )
{
  HarnessError error;
  const CompiledWorkflow compiled = compileWorkflow( requestFor( ndviIr() ), error );
  REQUIRE( error.code.empty() );
  CHECK( compiled.verdict() == "ok" );

  // The closed stage list is fully reported.
  std::map<std::string, std::string> stages;
  for ( const PlannerStageReport &stage : compiled.stages )
    stages[ stage.stage ] = stage.status;
  CHECK( stages["parse"] == "ok" );
  CHECK( stages["ground"] == "ok" );
  CHECK( stages["candidates"] == "ok" );
  CHECK( stages["analysis"] == "ok" );
  CHECK( stages["repair"] == "ok" );
  CHECK( stages["lower"] == "ok" );

  // The lowered plan is executable: valid, wired, with derived output paths.
  REQUIRE( compiled.workflowJson.size() > 0 );
  CHECK( compiled.planError.code.empty() );
  CHECK( compiled.plan.steps[0]["params"]["output"].asString() ==
         "/tmp/compile-out/" + compiled.ir.irId + "_ndvi.tif" );
  CHECK( compiled.plan.inputs[0]["ref"].asString() == "asset-3" );

  // Compiler provenance rides in the raw document.
  const Json::Value provenance = compiled.plan.raw["workflow_ir"];
  REQUIRE( provenance.isObject() );
  CHECK( provenance["ir_id"].asString() == compiled.ir.irId );
  CHECK( provenance["ir_fingerprint"] == workflowIrFingerprint( compiled.ir ) );

  // Alternatives and limitations are honest outputs.
  CHECK( compiled.alternatives.isArray() );
}

TEST_CASE( "The lowered plan round-trips into a readable AgentPlan document",
           "[workflow_planner]" )
{
  HarnessError error;
  const CompiledWorkflow compiled = compileWorkflow( requestFor( ndviIr() ), error );
  REQUIRE( compiled.workflowJson.size() > 0 );

  const Json::Value doc = agentPlanToDocument( compiled.plan );
  AgentPlan reparsed;
  REQUIRE( readAgentPlan( doc, reparsed, error ) );
  CHECK( reparsed.planId == compiled.plan.planId );
  CHECK( reparsed.intent == "ndvi" );
  REQUIRE( reparsed.raw.isMember( "workflow_ir" ) );
  CHECK( reparsed.raw["workflow_ir"]["ir_id"].asString() == compiled.ir.irId );
  // The wire document compiles to the same engine JSON.
  const std::string json = compilePlanToWorkflowJson( reparsed, error );
  CHECK( json == compiled.workflowJson );
}

TEST_CASE( "Slot wiring lowers to grounded paths; ungrounded slots refuse honestly",
           "[workflow_planner]" )
{
  HarnessError error;
  const CompiledWorkflow compiled = compileWorkflow( requestFor( ndviIr() ), error );
  // The input edge from slot 'primary' must have become the grounded path.
  CHECK( compiled.plan.steps[0]["params"]["input"].asString() == "/data/scenes/august.tif" );

  // Without facts the lower stage fails typed, not silently.
  CompileWorkflowRequest ungrounded;
  ungrounded.irDoc = ndviIr();
  const CompiledWorkflow failed = compileWorkflow( ungrounded, error );
  CHECK( failed.workflowJson.empty() );
  CHECK( failed.planError.code == "INVALID_PLAN" );
  bool sawGroundSkip = false;
  for ( const PlannerStageReport &stage : failed.stages )
    if ( stage.stage == "ground" && stage.status == "skipped" )
      sawGroundSkip = true;
  CHECK( sawGroundSkip );
}

TEST_CASE( "planToWorkflowIr converts AgentPlan documents so recipes compile too",
           "[workflow_planner]" )
{
  const Json::Value planDoc = parse( R"({
    "kind": "execution_plan", "schema_version": "2.0",
    "plan_id": "plan-recipe-1",
    "goal": "NDVI",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "steps": [
      { "id": "step_1", "operator_id": "rs:spectral_index",
        "params": { "index": "NDVI" },
        "inputs": [],
        "verification": "raster" }
    ],
    "outputs": [ { "name": "ndvi", "from_step": "step_1", "port": "output", "kind": "raster" } ]
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( planToWorkflowIr( planDoc, ir, error ) );
  CHECK( ir.intent == "ndvi" );
  REQUIRE( ir.nodes.size() == 1 );
  CHECK( ir.nodes[0].operatorId == "rs:spectral_index" );
  CHECK( ir.outputs[0].node == "step_1" );
  // The converted IR compiles through the full pipeline.
  const CompiledWorkflow compiled = compileWorkflow( requestFor( workflowIrToJson( ir ) ), error );
  CHECK( compiled.verdict() == "ok" );
  CHECK( compiled.workflowJson.size() > 0 );
}

TEST_CASE( "compileWorkflow is deterministic: byte-identical documents",
           "[workflow_planner]" )
{
  const Json::Value ir = ndviIr();
  CompileWorkflowRequest a = requestFor( ir );
  CompileWorkflowRequest b = requestFor( ir );
  HarnessError error;
  const Json::Value first = compileWorkflow( a, error ).analysis.toJson();
  const Json::Value second = compileWorkflow( b, error ).analysis.toJson();
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  CHECK( Json::writeString( builder, first ) == Json::writeString( builder, second ) );
}

TEST_CASE( "Compile refuses requests without an IR or recipe (typed)", "[workflow_planner]" )
{
  HarnessError error;
  CompileWorkflowRequest empty;
  const CompiledWorkflow compiled = compileWorkflow( empty, error );
  CHECK( compiled.workflowJson.empty() );
  CHECK( error.code == "INVALID_PARAMETER" );
}
