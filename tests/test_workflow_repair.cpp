// tests/test_workflow_repair.cpp
//
// Scientific Workflow Compiler 10.0 (ADR 0149): deterministic repair
// insertion — closed rule table, risk classes, evidence records, refusals,
// shape-preserving auto-insertion, idempotence.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <map>
#include <string>

#include "agent/harness/workflow_repair.h"

#include <vector>

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

Json::Value understanding( const std::string &crs, int width, int height,
                           const std::string &pixel,
                           const std::string &state = "surface_reflectance" )
{
  const std::string text = R"({
    "kind": "dataset_understanding",
    "source_kind": "raster",
    "crs": { "authid": ")" + crs + R"(" },
    "size": [ )" + std::to_string( width ) + ", " + std::to_string( height ) + R"( ],
    "pixel_size": [ )" + pixel + ", " + pixel + R"( ],
    "band_roles": [ "blue", "green", "red", "nir" ],
    "radiometric_state": ")" + state + R"(",
    "modality": "optical"
  })";
  return parse( text );
}

/// Two-scene change plan: the canonical repair scenario.
Json::Value changeIr()
{
  return parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "change",
    "inputs": [ { "name": "t1", "ref": "asset-1" }, { "name": "t2", "ref": "asset-2" } ],
    "nodes": [
      { "id": "diff", "operator": "rs:change_difference",
        "inputs": [ { "input": "t1", "as": "after" }, { "input": "t2", "as": "before" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
}

IrAnalysisInput factsForChange( const std::string &crs1, int w1, const std::string &px1,
                                const std::string &crs2, int w2, const std::string &px2 )
{
  IrAnalysisInput input;
  input.inputFacts["t1"] = understanding( crs1, w1, w1, px1.c_str() );
  input.inputFacts["t2"] = understanding( crs2, w2, w2, px2.c_str() );
  return input;
}

const IrRefusal *findRefusal( const IrRepairOutcome &outcome, const std::string &ruleId )
{
  for ( const IrRefusal &refusal : outcome.refusals )
    if ( refusal.ruleId == ruleId )
      return &refusal;
  return nullptr;
}
} // namespace

TEST_CASE( "The repair rule table is closed, documented and risk-classed",
           "[workflow_repair]" )
{
  const auto &table = repairRuleTable();
  CHECK( table.size() >= 7 );
  bool sawShape = false;
  bool sawRadiometric = false;
  bool sawScience = false;
  for ( const IrRepairRuleSpec &spec : table )
  {
    CHECK( repairRuleKnown( spec.ruleId ) );
    CHECK( !spec.description.empty() );
    if ( spec.riskClass == repair_risk::kShapePreserving )
    {
      sawShape = true;
      // Shape-preserving rules always name a concrete operator.
      CHECK( !spec.operatorId.empty() );
    }
    if ( spec.riskClass == repair_risk::kRadiometric )
      sawRadiometric = true;
    if ( spec.riskClass == repair_risk::kScienceChanging )
    {
      sawScience = true;
      // Science-changing rules never auto-insert.
    }
  }
  CHECK( sawShape );
  CHECK( sawRadiometric );
  CHECK( sawScience );
  CHECK( !repairRuleKnown( "vibes_based_repair" ) );
}

TEST_CASE( "Grid mismatch auto-inserts rs:align onto the reference grid with evidence",
           "[workflow_repair]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( changeIr(), ir, error ) );
  const IrAnalysisInput input = factsForChange( "EPSG:32650", 100, "10", "EPSG:32650", 200, "20" );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  REQUIRE( analysis.verdict == "fixable" );

  const IrRepairOutcome outcome = planRepairs( ir, analysis, input );
  REQUIRE( outcome.changed );
  REQUIRE( outcome.repairs.size() == 1 );
  CHECK( outcome.repairs[0].ruleId == "align_to_reference" );
  CHECK( outcome.repairs[0].risk == repair_risk::kShapePreserving );
  CHECK( outcome.repairs[0].issueCode == "GRID_MISMATCH" );
  CHECK( outcome.repairs[0].factsUsed["aligned_slot"].asString() == "t1" );
  CHECK( outcome.repairs[0].factsUsed["reference_slot"].asString() == "t2" );

  // The inserted node exists exactly once, is an rs:align wired to both
  // slots, and the consumer now reads from it.
  const IrNode *align = nullptr;
  int alignCount = 0;
  for ( const IrNode &node : outcome.ir.nodes )
  {
    if ( node.operatorId == "rs:align" )
    {
      ++alignCount;
      align = &node;
    }
  }
  REQUIRE( alignCount == 1 );
  REQUIRE( align );
  CHECK( align->source == "repair:align_to_reference" );
  REQUIRE( align->inputs.size() == 2 );
  CHECK( align->inputs[0].input == "t1" );
  CHECK( align->inputs[0].as == "input" );
  CHECK( align->inputs[1].input == "t2" );
  CHECK( align->inputs[1].as == "reference" );
  CHECK( align->params["output"].asString() == "/tmp/out/" + outcome.ir.irId + "_" + align->id + ".tif" );
  CHECK( align->outputs[0].artifact["kind"].asString() == "raster" );

  bool rewired = false;
  for ( const IrNode &node : outcome.ir.nodes )
  {
    if ( node.id != "diff" )
      continue;
    for ( const IrNodeInput &edge : node.inputs )
      if ( edge.as == "input" && edge.node == align->id && edge.input.empty() )
        rewired = true;
  }
  CHECK( rewired );

  // The re-analysis of the repaired IR is clean.
  WorkflowIr repairedCopy = outcome.ir;
  const IrAnalysis after = analyzeWorkflowIr( repairedCopy, input );
  CHECK( after.verdict == "ok" );
}

TEST_CASE( "CRS mismatch auto-inserts io:reproject with the reference CRS",
           "[workflow_repair]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( changeIr(), ir, error ) );
  const IrAnalysisInput input = factsForChange( "EPSG:32650", 100, "10", "EPSG:4326", 100, "10" );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  CHECK( analysis.verdict == "fixable" );

  WorkflowIr mutableIr = ir;
  const IrRepairOutcome outcome = planRepairs( mutableIr, analysis, input );
  REQUIRE( outcome.changed );
  CHECK( outcome.repairs[0].ruleId == "reproject_to_reference" );
  const IrNode *reprojected = nullptr;
  for ( const IrNode &node : outcome.ir.nodes )
    if ( node.operatorId == "io:reproject" )
      reprojected = &node;
  REQUIRE( reprojected );
  CHECK( reprojected->params["targetCrs"].asString() == "EPSG:4326" );
  CHECK( reprojected->outputs[0].artifact["crs"].asString() == "EPSG:4326" );
}

TEST_CASE( "Repairs are deterministic: same input, byte-identical repaired IR",
           "[workflow_repair]" )
{
  Json::Value doc = changeIr();
  WorkflowIr a;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, a, error ) );
  WorkflowIr b;
  REQUIRE( readWorkflowIr( doc, b, error ) );
  const IrAnalysisInput input = factsForChange( "EPSG:32650", 100, "10", "EPSG:32650", 200, "20" );
  const IrAnalysis analysisA = analyzeWorkflowIr( a, input );
  const IrAnalysis analysisB = analyzeWorkflowIr( b, input );
  const IrRepairOutcome outcomeA = planRepairs( a, analysisA, input );
  const IrRepairOutcome outcomeB = planRepairs( b, analysisB, input );
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  CHECK( Json::writeString( builder, workflowIrToJson( outcomeA.ir ) ) ==
         Json::writeString( builder, workflowIrToJson( outcomeB.ir ) ) );
  // The fingerprint moved (science changed by insertion) and is stable.
  CHECK( workflowIrFingerprint( outcomeA.ir ) == workflowIrFingerprint( outcomeB.ir ) );
  CHECK( workflowIrFingerprint( outcomeA.ir ) != workflowIrFingerprint( a ) );
}

TEST_CASE( "Radiometric repairs are prepared decisions, never silent insertions",
           "[workflow_repair]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndvi", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" }, "inputs": [ { "input": "primary" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  Json::Value dnScene = understanding( "EPSG:32650", 100, 100, "10", "dn" );
  dnScene["numeric_domain"] = "dn";
  input.inputFacts["primary"] = dnScene;
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  REQUIRE( analysis.verdict == "ok" ); // warn-class per contract

  const IrRepairOutcome outcome = planRepairs( ir, analysis, input );
  CHECK( !outcome.changed ); // nothing inserted
  const IrRefusal *decision = findRefusal( outcome, "calibrate_toa_reflectance" );
  REQUIRE( decision );
  CHECK( decision->decisionRequired );
  CHECK( decision->why.find( "rs:radiometric_calibration" ) != std::string::npos );
}

TEST_CASE( "SAR DN calibration without coefficients is refused with missing facts",
           "[workflow_repair]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "sar",
    "inputs": [ { "name": "primary", "ref": "asset-9" } ],
    "nodes": [
      { "id": "backscatter", "operator": "rs:sar_backscatter",
        "inputs": [ { "input": "primary" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  input.inputFacts["primary"] = parse( R"({
    "kind": "dataset_understanding",
    "source_kind": "raster",
    "modality": "sar",
    "numeric_domain": "dn",
    "polarizations": [ "vv" ],
    "size": [ 100, 100 ],
    "pixel_size": [ 10, 10 ]
  })" );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  // sar family expects sigma0/gamma0; DN input -> CALIBRATION_MISMATCH.
  bool sawCalibration = false;
  for ( const IrIssue &issue : analysis.issues )
    if ( issue.code == "CALIBRATION_MISMATCH" )
      sawCalibration = true;
  CHECK( sawCalibration );

  const IrRepairOutcome outcome = planRepairs( ir, analysis, input );
  CHECK( !outcome.changed );
  const IrRefusal *refusal = findRefusal( outcome, "sar_dn_calibration" );
  REQUIRE( refusal );
  CHECK( refusal->decisionRequired );
  bool mentionsCoefficients = false;
  for ( const Json::Value &fact : refusal->missingFacts )
    if ( fact.asString() == "calibration_coefficients" )
      mentionsCoefficients = true;
  CHECK( mentionsCoefficients );
}

TEST_CASE( "Science-changing opportunities and dataset choices stay decisions",
           "[workflow_repair]" )
{
  // Quality masks observed on an optical NDVI consumer: opportunity refusal.
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndvi", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" }, "inputs": [ { "input": "primary" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  Json::Value maskedScene = understanding( "EPSG:32650", 100, 100, "10" );
  Json::Value mask( Json::objectValue );
  mask["band"] = 5;
  mask["role"] = "scl";
  maskedScene["quality_masks"] = Json::Value( Json::arrayValue );
  maskedScene["quality_masks"].append( mask );
  input.inputFacts["primary"] = maskedScene;
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrRepairOutcome outcome = planRepairs( ir, analysis, input );
  CHECK( !outcome.changed );
  const IrRefusal *qa = findRefusal( outcome, "apply_qa_mask" );
  REQUIRE( qa );
  CHECK( qa->decisionRequired );

  // Missing bands: dataset choice refusal.
  Json::Value rgbOnly = understanding( "EPSG:32650", 100, 100, "10" );
  rgbOnly["band_roles"] = Json::Value( Json::arrayValue );
  for ( const char *role : { "blue", "green", "red" } )
    rgbOnly["band_roles"].append( role );
  input.inputFacts["primary"] = rgbOnly;
  const IrAnalysis blocked = analyzeWorkflowIr( ir, input );
  const IrRepairOutcome refused = planRepairs( ir, blocked, input );
  const IrRefusal *selection = findRefusal( refused, "select_other_dataset" );
  REQUIRE( selection );
  CHECK( selection->issueCode == "BAND_ROLE_UNRESOLVED" );
}

TEST_CASE( "Repair without expectations.output_dir refuses instead of guessing paths",
           "[workflow_repair]" )
{
  Json::Value doc = changeIr();
  doc.removeMember( "expectations" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  const IrAnalysisInput input = factsForChange( "EPSG:32650", 100, "10", "EPSG:32650", 200, "20" );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrRepairOutcome outcome = planRepairs( ir, analysis, input );
  CHECK( !outcome.changed );
  CHECK( findRefusal( outcome, "align_to_reference" ) != nullptr );
  for ( const IrRefusal &refusal : outcome.refusals )
    if ( refusal.ruleId == "align_to_reference" )
    {
      bool mentionsOutputDir = false;
      for ( const Json::Value &fact : refusal.missingFacts )
        if ( fact.asString() == "output_dir" )
          mentionsOutputDir = true;
      CHECK( mentionsOutputDir );
    }
}

TEST_CASE( "analyzeRepairAnalyze converges: repaired IR re-analyzes clean",
           "[workflow_repair]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( changeIr(), ir, error ) );
  const IrAnalysisInput input = factsForChange( "EPSG:32650", 100, "10", "EPSG:32650", 200, "20" );
  const IrCompileFixResult result = analyzeRepairAnalyze( ir, input );
  CHECK( result.analysis.verdict == "ok" );
  CHECK( result.repairs.size() == 1 );
  CHECK( result.ir.nodes.size() == 2 ); // diff + one align
}
