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
#include "agent/harness/capability_catalog.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/capability_relations.h"
#include "agent/harness/provenance_projection.h"

using namespace sicnu::agent::harness;

namespace
{
/// Repo convention (test_capability_drift.cpp): point the knowledge layers at
/// the source tree explicitly — the default search paths do not resolve from
/// the test working directory.
void loadHarnessKnowledge()
{
  const std::string source = CMAKE_SOURCE_DIR;
  CapabilityKnowledge::instance().setDirectory( source + "/data/agent/capabilities" );
  CapabilityKnowledge::instance().reload();
  CapabilityCatalog::instance().setDirectory(
    source + "/data/processing/algorithm_meta/capability" );
  CapabilityCatalog::instance().reload();
  CapabilityRelations::instance().setFilePath(
    source + "/data/processing/algorithm_meta/capability/capability_relations.json" );
  CapabilityRelations::instance().reload();
}
}

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
  loadHarnessKnowledge();
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

TEST_CASE( "A plan mutated after its compile supersedes the carried compiler block",
           "[workflow_planner][provenance][r5]" )
{
  // #1359: compilePlanToWorkflowJson copied plan.raw["compiler"] verbatim, so
  // a repair (or any post-compile mutation) re-compiled with the PRE-repair
  // provenance presented as this document's compile event. The block is now
  // bound to the plan fingerprint it was produced from; on mismatch it moves
  // to the compiler_superseded chain instead of claiming currency.
  loadHarnessKnowledge();
  HarnessError error;
  const CompiledWorkflow compiled = compileWorkflow( requestFor( ndviIr() ), error );
  REQUIRE( compiled.workflowJson.size() > 0 );
  REQUIRE( compiled.plan.raw.isMember( "compiler" ) );
  REQUIRE( compiled.plan.raw["compiler"].isMember( "plan_fingerprint" ) );

  // Unmutated content: the block is still bound to this fingerprint — the
  // verbatim round-trip (and its byte-identical engine JSON) is unchanged.
  {
    AgentPlan reparsed;
    REQUIRE( readAgentPlan( agentPlanToDocument( compiled.plan ), reparsed, error ) );
    Json::Value engineJson;
    REQUIRE( Json::Reader().parse( compilePlanToWorkflowJson( reparsed, error ),
                                   engineJson ) );
    CHECK( engineJson["metadata"]["compiler"] == compiled.plan.raw["compiler"] );
    CHECK_FALSE( engineJson["metadata"].isMember( "compiler_superseded" ) );
  }

  // Mutate the science content behind the block (what repair_plan does when
  // it renames steps or rewires outputs): the stale block may not ride as
  // current provenance.
  Json::Value doc = agentPlanToDocument( compiled.plan );
  doc["steps"][0]["params"]["index"] = "NDWI";
  AgentPlan mutated;
  REQUIRE( readAgentPlan( doc, mutated, error ) );
  Json::Value mutatedEngine;
  REQUIRE( Json::Reader().parse( compilePlanToWorkflowJson( mutated, error ),
                                 mutatedEngine ) );
  CHECK_FALSE( mutatedEngine["metadata"].isMember( "compiler" ) );
  CHECK( mutatedEngine["metadata"]["compiler_superseded"]
         == compiled.plan.raw["compiler"] );
  // The engine JSON still names the ACTUAL content it carries.
  CHECK( mutatedEngine["metadata"]["plan_fingerprint"].asString() ==
         planFingerprint( mutated ) );
  CHECK( mutatedEngine["metadata"]["plan_fingerprint"].asString() !=
         compiled.plan.raw["compiler"]["plan_fingerprint"].asString() );
}

TEST_CASE( "A legacy unbound compiler block keeps the verbatim carry",
           "[workflow_planner][provenance][r5]" )
{
  // Backward-compat clause of the #1359 fix: wire documents produced before
  // the plan_fingerprint binding existed carry a digest-stamped block with
  // no binding. They must keep riding as current metadata.compiler — the
  // read gate already verified their integrity, and silently demoting every
  // legacy document's provenance would corrupt history wholesale.
  loadHarnessKnowledge();
  HarnessError error;

  Json::Value block( Json::objectValue );
  block["schema_version"] = "1.0";
  block["ir_fingerprint"] = "0123456789abcdef";
  block["ir_id"] = "ir-legacy";
  block["digest"] = projection::projectionDigest( block );

  Json::Value doc = parse( R"({
    "kind": "execution_plan", "schema_version": "2.0",
    "plan_id": "plan-legacy-block", "intent": "ndvi",
    "steps": [ { "id": "s1", "operator_id": "rs:ndvi",
                 "params": { "red": "/x.tif", "nir": "/x.tif" } } ]
  })" );
  doc["compiler"] = block;
  AgentPlan plan;
  REQUIRE( readAgentPlan( doc, plan, error ) );
  Json::Value engineJson;
  REQUIRE( Json::Reader().parse( compilePlanToWorkflowJson( plan, error ), engineJson ) );
  CHECK( engineJson["metadata"]["compiler"] == block );
  CHECK_FALSE( engineJson["metadata"].isMember( "compiler_superseded" ) );
}

TEST_CASE( "readAgentPlan rejects a torn compiler provenance block",
           "[workflow_planner][provenance][r5]" )
{
  // #1359 (record level): the raw block rode verbatim into persisted
  // documents and run context with no shape or integrity gate. A block whose
  // digest no longer matches its content is corruption, not provenance.
  loadHarnessKnowledge();
  HarnessError error;

  Json::Value block( Json::objectValue );
  block["schema_version"] = "1.0";
  block["ir_fingerprint"] = "0123456789abcdef";
  block["ir_id"] = "ir-1";
  Json::Value bound = block;
  bound["plan_fingerprint"] = "abcdef0123456789";
  bound["digest"] = projection::projectionDigest( bound );

  // A well-formed, digest-consistent block passes the read gate.
  Json::Value goodDoc = parse( R"({
    "kind": "execution_plan", "schema_version": "2.0",
    "plan_id": "plan-gate-1", "intent": "ndvi",
    "steps": [ { "id": "s1", "operator_id": "rs:ndvi",
                 "params": { "red": "/x.tif", "nir": "/x.tif" } } ]
  })" );
  goodDoc["compiler"] = bound;
  AgentPlan plan;
  REQUIRE( readAgentPlan( goodDoc, plan, error ) );

  // A non-object block is refused outright.
  Json::Value tornDoc = goodDoc;
  tornDoc["compiler"] = "compiler";
  CHECK( !readAgentPlan( tornDoc, plan, error ) );
  CHECK( error.code == "INVALID_PLAN" );

  // An edited block: digest no longer matches the carried content.
  Json::Value editedDoc = goodDoc;
  editedDoc["compiler"]["ir_fingerprint"] = "ffffffffffffffff";
  CHECK( !readAgentPlan( editedDoc, plan, error ) );

  // A truncated block missing its digest stamp is refused the same way.
  Json::Value unstampedDoc = goodDoc;
  unstampedDoc["compiler"].removeMember( "digest" );
  CHECK( !readAgentPlan( unstampedDoc, plan, error ) );

  // The companion lowering provenance block is shape-gated too: it is bound
  // into run context verbatim via the plan binding.
  Json::Value tornIrDoc = goodDoc;
  tornIrDoc["workflow_ir"] = "workflow_ir";
  CHECK( !readAgentPlan( tornIrDoc, plan, error ) );
}

TEST_CASE( "readAgentPlan refuses a non-numeric class domain declaration",
           "[workflow_planner][verification][r6]" )
{
  // agent-harness R4 item 15, declaration-side closure: verification probes
  // only numeric class-domain members, so a declaration carrying a string or
  // object member can never verify — refuse it at the read seam instead of
  // letting it fail opaquely at run time.
  loadHarnessKnowledge();
  HarnessError error;

  const char *base = R"({
    "kind": "execution_plan", "schema_version": "2.0",
    "plan_id": "plan-class-domain-1", "intent": "classify",
    "steps": [ { "id": "s1", "operator_id": "rs:classify",
                 "params": { "model": "/m.json", "input": "/x.tif" } } ],
    "verification": { "expectations": { "class_values": [1, 2] } }
  })";

  Json::Value goodDoc = parse( base );
  AgentPlan plan;
  REQUIRE( readAgentPlan( goodDoc, plan, error ) );

  Json::Value stringMember = parse( base );
  stringMember["verification"]["expectations"]["class_values"][1] = "urban";
  CHECK( !readAgentPlan( stringMember, plan, error ) );
  CHECK( error.code == "INVALID_PLAN" );

  Json::Value objectMember = parse( base );
  objectMember["verification"]["expectations"]["class_values"][0] = Json::Value( Json::objectValue );
  CHECK( !readAgentPlan( objectMember, plan, error ) );

  Json::Value notArray = parse( base );
  notArray["verification"]["expectations"]["class_values"] = "1,2,3";
  CHECK( !readAgentPlan( notArray, plan, error ) );

  Json::Value notObject = parse( base );
  notObject["verification"]["expectations"] = "expectations";
  CHECK( !readAgentPlan( notObject, plan, error ) );
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
        "params": { "index": "NDVI", "input": "/data/scenes/august.tif" },
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
  // The converted IR compiles through the full pipeline. Plans carry no
  // output_dir, so the compile request supplies one for output derivation.
  Json::Value irDoc = workflowIrToJson( ir );
  irDoc["expectations"] = parse( R"({ "output_dir": "/tmp/compile-out" })" );
  const CompiledWorkflow compiled = compileWorkflow( requestFor( irDoc ), error );
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

// ---------------------------------------------------------------------------
// R3 track 14 (planner live capability / WorkflowIR lowering / handoff):
// the ScientificPlan decision layer enters THIS chain through
// projectPlanToIr — the harness reader and compiler stay the only lowering
// authority. The Qt-free half of these oracles (bundle→plan→IR document,
// preflight, repair, proposals) lives in test_planner_handoff_e2e.cpp.
// ---------------------------------------------------------------------------

#include "contracts/scientific_contract.h"
#include "planner/planner_core.h"
#include "planner/plan_ir_projection.h"
#include "planner/planning_context.h"
#include "planner/scientific_goal.h"
#include "planner/scientific_plan.h"
#include "planner/provider_interfaces.h"

namespace planner_handoff
{

using namespace sicnu::planner;

/// Map-backed provider over contract-aligned facts for the operators the
/// scenario plans over (the planner re-checks them against the registry).
class MapProvider : public CapabilityProvider
{
public:
  void add( PlannerCapability fact ) { byFamily_[fact.family].push_back( fact ); }

  std::vector<PlannerCapability> capabilitiesForFamily(
    const std::string &family ) const override
  {
    auto it = byFamily_.find( family );
    return it == byFamily_.end() ? std::vector<PlannerCapability>{} : it->second;
  }

private:
  std::map<std::string, std::vector<PlannerCapability>> byFamily_;
};

/// A lawful NDVI measurement plan over one ready reflectance scene.
sicnu::planner::PlanningResult ndviPlan()
{
  MapProvider provider;
  auto fact = []( const char *id, const char *family, const char *cost ) {
    PlannerCapability fact;
    fact.operatorId = id;
    fact.family = family;
    fact.costClass = cost;
    const auto *contract = sicnu::contracts::findScientificContract( id );
    fact.inputDomain = contract ? contract->inputDomain : std::string();
    fact.outputDomain = contract ? contract->outputDomain : std::string();
    fact.deterministic = true;
    return fact;
  };
  provider.add( fact( "rs:landsat_import", "data_import", "low" ) );
  provider.add( fact( "rs:ndvi", "analysis", "low" ) );

  ScientificGoal goal;
  goal.goalId = "goal-handoff-ndvi";
  goal.kind = "measurement";
  goal.subject = "handoff ndvi";

  PlanningContext context;
  PlannerAssetFacts asset;
  asset.ref = "scene-a";
  asset.kind = "raster";
  asset.modality = "optical";
  asset.numericDomain = "reflectance";
  asset.crs = "EPSG:32650";
  asset.resolutionM = 10.0;
  asset.state = "ready";
  context.assets.push_back( asset );

  PlannerProviders providers;
  providers.capability = &provider;
  return planScientificWork( goal, context, providers );
}

CompileWorkflowRequest requestForPlanIr( const Json::Value &irDoc )
{
  CompileWorkflowRequest request;
  request.irDoc = irDoc;
  Json::Value understanding = opticalUnderstanding();
  understanding["entity"]["asset_entity_id"] = "scene-a";
  request.inputFacts["in-scene-a"] = understanding;
  return request;
}

} // namespace planner_handoff

TEST_CASE( "An accepted ScientificPlan enters the compiler chain through projectPlanToIr",
           "[workflow_planner][planner_handoff]" )
{
  loadHarnessKnowledge();
  using namespace planner_handoff;

  const sicnu::planner::PlanningResult planned = ndviPlan();
  REQUIRE( planned.primary() != nullptr );
  const sicnu::planner::ScientificPlan &primary = *planned.primary();
  REQUIRE( primary.verdict != "infeasible" );

  std::vector<std::string> warnings;
  std::string projectionError;
  const Json::Value irDoc =
    sicnu::planner::projectPlanToIr( primary, &warnings, &projectionError );
  REQUIRE( projectionError.empty() );

  // The harness reader accepts the projected document (data-level
  // conformance against THE authority, not a fixture).
  WorkflowIr ir;
  HarnessError readError;
  INFO( "read error: " << readError.code << " " << readError.summary );
  REQUIRE( readWorkflowIr( irDoc, ir, readError ) );

  // The whole chain lowers the plan into an executable workflow. The
  // analysis may hand repairable findings to the repair stage (the designed
  // "fixable" path); what it must NEVER do is refuse the planner's document
  // at the reader or lower without a workflow.
  HarnessError error;
  const CompiledWorkflow compiled = compileWorkflow( requestForPlanIr( irDoc ), error );
  INFO( "verdict: " << compiled.verdict() );
  CHECK( ( compiled.verdict() == "ok" || compiled.verdict() == "fixable" ) );
  CHECK( compiled.workflowJson.size() > 0 );
  CHECK( compiled.planError.code.empty() );

  // Plan provenance survives: the ir id rides the lowered plan document. A
  // "fixable" analysis rewrites the IR before lowering and RE-DERIVES the
  // identity from the repaired content, so the provenance carries the
  // post-repair id (self-consistent with compiled.ir); plan identity itself
  // travels on every node source below.
  CHECK( compiled.plan.raw["workflow_ir"]["ir_id"].asString() == compiled.ir.irId );
  bool sawPlannerSource = false;
  for ( const auto &node : compiled.ir.nodes )
    sawPlannerSource =
      sawPlannerSource || node.source.rfind( "planner:" + primary.planId, 0 ) == 0;
  CHECK( sawPlannerSource );
}

TEST_CASE( "A hostile IR cannot borrow the lowering chain past the authority gates",
           "[workflow_planner][planner_handoff]" )
{
  loadHarnessKnowledge();
  using namespace planner_handoff;

  const sicnu::planner::PlanningResult planned = ndviPlan();
  REQUIRE( planned.primary() != nullptr );
  std::string projectionError;
  Json::Value irDoc =
    sicnu::planner::projectPlanToIr( *planned.primary(), nullptr, &projectionError );
  REQUIRE( projectionError.empty() );

  SECTION( "an operator no authority declares is refused (never lowered)" )
  {
    REQUIRE( irDoc["nodes"].isArray() );
    // The analysis node (import stays lawful): the analysis stage's
    // known_operator gate is the authority boundary.
    irDoc["nodes"][1]["operator"] = "rs:total_wipe";
    HarnessError error;
    const CompiledWorkflow compiled = compileWorkflow( requestForPlanIr( irDoc ), error );
    CHECK( compiled.verdict() == "blocked" );
    CHECK( compiled.executionBlocked );
    CHECK( compiled.workflowJson.empty() );
    const Json::Value analysisJson = compiled.analysis.toJson();
    bool sawUnknownOperator = false;
    for ( const auto &check : analysisJson["checks"] )
      sawUnknownOperator = sawUnknownOperator
                           || check["check"].asString() == "known_operator";
    CHECK( sawUnknownOperator );
  }

  SECTION( "an ungrounded hostile slot refuses instead of guessing a path" )
  {
    irDoc["inputs"][0]["name"] = "hostile-slot";
    HarnessError error;
    CompileWorkflowRequest request = requestForPlanIr( irDoc );
    request.inputFacts.clear();
    const CompiledWorkflow compiled = compileWorkflow( request, error );
    CHECK( compiled.workflowJson.empty() );
  }
}
