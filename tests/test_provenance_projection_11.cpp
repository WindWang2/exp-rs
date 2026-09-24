// tests/test_provenance_projection_11.cpp
//
// Compiler & Grounding 11.0: provenance projection + prepared decisions +
// explain. Oracle discipline: determinism is checked by byte-identity of
// repeated calls; the atomic sidecar contract by failure injection against a
// read-only directory; the science discipline by refusals NEVER becoming
// auto-applicable.

#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/json.h>

#include <string>
#include <vector>

#include "agent/harness/capability_catalog.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/capability_relations.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/provenance_projection.h"
#include "agent/harness/workflow_analysis.h"
#include "agent/harness/workflow_explain.h"
#include "agent/harness/workflow_repair.h"
#include "agent/spatial_tools/spatial_tool.h"

using namespace sicnu::agent::harness;
using namespace sicnu::agent::harness::projection;
using namespace sicnu::agent::spatial_tools;

namespace
{
Json::Value parse( const std::string &text )
{
  Json::Value out;
  Json::Reader reader;
  REQUIRE( reader.parse( text, out ) );
  return out;
}

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

/// NDVI over one optical slot with observed reflectance facts.
WorkflowIr healthyIr()
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndvi", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" },
        "inputs": [ { "input": "primary" } ],
        "outputs": [ { "name": "output",
                       "artifact": { "kind": "raster", "numeric_domain": "index" } } ],
        "semantic_output": "植被指数图" }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" ), ir, error ) );
  return ir;
}

IrAnalysisInput factsForPrimary()
{
  IrAnalysisInput input;
  input.inputFacts[ "primary" ] = parse( R"({
    "kind": "dataset_understanding",
    "source_kind": "raster",
    "crs": { "authid": "EPSG:32650" },
    "size": [ 100, 100 ],
    "pixel_size": [ 10, 10 ],
    "band_roles": [ "blue", "green", "red", "nir" ],
    "band_count": 4,
    "radiometric_state": "surface_reflectance"
  })" );
  return input;
}

/// An IR with a real CRS conflict so the repair pass has something to fix.
WorkflowIr crsConflictIr()
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "change",
    "inputs": [ { "name": "t1", "ref": "asset-1" }, { "name": "t2", "ref": "asset-2" } ],
    "nodes": [
      { "id": "diff", "operator": "rs:change_difference",
        "inputs": [ { "input": "t1", "as": "after" }, { "input": "t2", "as": "before" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" ), ir, error ) );
  return ir;
}

IrAnalysisInput conflictFacts()
{
  IrAnalysisInput input;
  input.inputFacts[ "t1" ] = parse( R"({
    "kind": "dataset_understanding", "source_kind": "raster",
    "crs": { "authid": "EPSG:32650" }, "crs_authid": "EPSG:32650",
    "size": [ 100, 100 ], "pixel_size": [ 10, 10 ],
    "band_roles": [ "nir", "red" ], "band_count": 2,
    "radiometric_state": "surface_reflectance"
  })" );
  input.inputFacts[ "t2" ] = parse( R"({
    "kind": "dataset_understanding", "source_kind": "raster",
    "crs": { "authid": "EPSG:32647" }, "crs_authid": "EPSG:32647",
    "size": [ 100, 100 ], "pixel_size": [ 10, 10 ],
    "band_roles": [ "nir", "red" ], "band_count": 2,
    "radiometric_state": "surface_reflectance"
  })" );
  return input;
}
} // namespace

// ---------------------------------------------------------------------------
// Projection determinism + shape.
// ---------------------------------------------------------------------------

TEST_CASE( "projection: same compile inputs produce byte-identical blocks", "[projection11]" )
{
  loadHarnessKnowledge();
  WorkflowIr ir = healthyIr();
  IrAnalysis analysis = analyzeWorkflowIr( ir, factsForPrimary() );
  IrRepairOutcome repair = planRepairs( ir, analysis, factsForPrimary() );

  const Json::Value first = compilerProjection( ir, analysis, repair.repairs, repair.refusals );
  WorkflowIr ir2 = healthyIr();
  IrAnalysis analysis2 = analyzeWorkflowIr( ir2, factsForPrimary() );
  IrRepairOutcome repair2 = planRepairs( ir2, analysis2, factsForPrimary() );
  const Json::Value second =
    compilerProjection( ir2, analysis2, repair2.repairs, repair2.refusals );

  REQUIRE( first == second );
  CHECK( projectionDigest( first ) == projectionDigest( second ) );
  CHECK( first["schema_version"].asString() == std::string( "1.0" ) );
  CHECK( first["analysis_verdict"].asString() == "ok" );
  CHECK( first["input_facts"]["primary"]["fact_status"]["radiometric_state"].asString() ==
         "observed" );
}

TEST_CASE( "projection: repairs and refusals ride the block verbatim", "[projection11]" )
{
  loadHarnessKnowledge();
  WorkflowIr ir = crsConflictIr();
  IrAnalysis analysis = analyzeWorkflowIr( ir, conflictFacts() );
  REQUIRE( analysis.blocked() );
  IrCompileFixResult fixed = analyzeRepairAnalyze( ir, conflictFacts() );

  const Json::Value block =
    compilerProjection( fixed.ir, fixed.analysis, fixed.repairs, fixed.refusals );
  REQUIRE( block.isMember( "repairs" ) );
  REQUIRE( block["repairs"].isArray() );
  CHECK( block["repairs"][0]["rule_id"].asString() == "reproject_to_reference" );
  CHECK( block["repairs"][0]["params"]["targetCrs"].asString() == "EPSG:32650" );
  CHECK( block["digest"].isNull() ); // digest rides only attached documents
}

TEST_CASE( "projection: attach is additive over engine metadata keys", "[projection11]" )
{
  Json::Value workflow = parse( R"({
    "id": "agent_plan", "title": "t", "workspaceKind": "agent",
    "steps": [],
    "metadata": { "plan_fingerprint": "abc123", "cleanup": "keep_all" }
  })" );
  WorkflowIr ir = healthyIr();
  IrAnalysis analysis = analyzeWorkflowIr( ir, factsForPrimary() );
  IrRepairOutcome repair = planRepairs( ir, analysis, factsForPrimary() );
  const Json::Value block = compilerProjection( ir, analysis, repair.repairs, repair.refusals );

  const Json::Value attached = attachToWorkflowJson( workflow, block );
  CHECK( attached["metadata"]["plan_fingerprint"].asString() == "abc123" );
  CHECK( attached["metadata"]["cleanup"].asString() == "keep_all" );
  CHECK( attached["metadata"]["compiler"]["digest"].asString() == projectionDigest( block ) );
  CHECK_FALSE( attached["metadata"].isMember( "compiler_superseded" ) );

  // Attaching a DIFFERENT projection preserves the old block (bounded, one
  // level) — a re-compile is traceable, not destructive.
  Json::Value changed = block;
  changed["intent"] = "different";
  const Json::Value attached2 = attachToWorkflowJson( attached, changed );
  CHECK( attached2["metadata"]["compiler"]["digest"].asString() ==
         projectionDigest( changed ) );
  CHECK( attached2["metadata"]["compiler_superseded"]["digest"].asString() ==
         projectionDigest( block ) );
}

TEST_CASE( "sidecar: atomic write lands, read-only home fails honestly", "[projection11]" )
{
  WorkflowIr ir = healthyIr();
  IrAnalysis analysis = analyzeWorkflowIr( ir, factsForPrimary() );
  IrRepairOutcome repair = planRepairs( ir, analysis, factsForPrimary() );
  const Json::Value block = compilerProjection( ir, analysis, repair.repairs, repair.refusals );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string outputPath = dir.filePath( "out.tif" ).toStdString();
  const SidecarResult written = writeCompileSidecar( outputPath, block );
  REQUIRE( written.written );
  REQUIRE( written.error.empty() );
  QFileInfo info( QString::fromStdString( written.path ) );
  REQUIRE( info.exists() );

  // The sidecar round-trips.
  QFile file( QString::fromStdString( written.path ) );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  Json::Value parsed;
  Json::Reader reader;
  REQUIRE( reader.parse( file.readAll().toStdString(), parsed ) );
  CHECK( parsed["schema_version"].asString() == std::string( "1.0" ) );
  file.close();

  // A missing parent directory fails the write with a typed error — and
  // leaves no half-written file behind (QSaveFile atomicity). Portable:
  // POSIX read-only dirs do not stop a same-user writer on Windows.
  const std::string roOutput = ( dir.filePath( "missing_dir" ) + "/out.tif" ).toStdString();
  const SidecarResult failed = writeCompileSidecar( roOutput, block );
  CHECK_FALSE( failed.written );
  CHECK_FALSE( failed.error.empty() );
  CHECK( QFileInfo( QString::fromStdString( roOutput + ".compile.json" ) ).exists() ==
         false );
}


// ---------------------------------------------------------------------------
// Planner E2E: the compile surface carries the projection end to end.
// ---------------------------------------------------------------------------

TEST_CASE( "compile_workflow engine JSON carries metadata.compiler end to end", "[projection11][e2e]" )
{
  loadHarnessKnowledge();
  SpatialToolRegistry::instance().registerBuiltinTools();
  SpatialToolPtr tool = SpatialToolRegistry::instance().find( "harness:compile_workflow" )
                          .value_or( nullptr );
  REQUIRE( tool != nullptr );

  Json::Value input = parse( R"({
    "ir": {
      "kind": "workflow_ir", "schema_version": "1.0",
      "intent": "change",
      "inputs": [ { "name": "t1", "ref": "asset-1" }, { "name": "t2", "ref": "asset-2" } ],
      "nodes": [
        { "id": "diff", "operator": "rs:change_difference",
          "inputs": [ { "input": "t1", "as": "after" },
                      { "input": "t2", "as": "before" } ] }
      ],
      "expectations": { "output_dir": "/tmp/out" }
    },
    "input_facts": {
      "t1": {
        "kind": "dataset_understanding", "source_kind": "raster",
        "crs": { "authid": "EPSG:32650" }, "crs_authid": "EPSG:32650",
        "size": [ 100, 100 ], "pixel_size": [ 10, 10 ],
        "band_roles": [ "nir", "red" ], "band_count": 2,
        "radiometric_state": "surface_reflectance"
      },
      "t2": {
        "kind": "dataset_understanding", "source_kind": "raster",
        "crs": { "authid": "EPSG:32647" }, "crs_authid": "EPSG:32647",
        "size": [ 100, 100 ], "pixel_size": [ 10, 10 ],
        "band_roles": [ "nir", "red" ], "band_count": 2,
        "radiometric_state": "surface_reflectance"
      }
    }
  })" );
  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  REQUIRE( result.output.isMember( "workflow_json" ) );

  Json::Value workflowDoc;
  Json::Reader reader;
  REQUIRE( reader.parse( result.output["workflow_json"].asString(), workflowDoc ) );
  CHECK( workflowDoc["metadata"]["compiler"]["schema_version"].asString() == std::string( "1.0" ) );
  const std::string digest = workflowDoc["metadata"]["compiler"]["digest"].asString();
  CHECK( digest.size() == 16 );
  CHECK_FALSE( workflowDoc["metadata"].isMember( "compiler_superseded" ) );

  // Engine-owned metadata keys survive the attach.
  CHECK( workflowDoc["metadata"].isMember( "plan_fingerprint" ) );
  CHECK( workflowDoc.isMember( "steps" ) );
}

// ---------------------------------------------------------------------------
// Prepared decisions.
// ---------------------------------------------------------------------------

TEST_CASE( "prepared decisions: deterministic order, refusals never auto-apply", "[repair11]" )
{
  loadHarnessKnowledge();
  WorkflowIr ir = crsConflictIr();
  IrAnalysis analysis = analyzeWorkflowIr( ir, conflictFacts() );
  IrRepairOutcome outcome = planRepairs( ir, analysis, conflictFacts() );

  const IrRepairPlan plan = planPreparedDecisions( outcome );
  REQUIRE_FALSE( plan.decisions.empty() );

  // Deterministic order: risk class, then cost, then evidence.
  for ( size_t i = 1; i < plan.decisions.size(); ++i )
  {
    const auto risk = []( const std::string &r ) {
      if ( r == repair_risk::kShapePreserving )
        return 0;
      if ( r == repair_risk::kRadiometric )
        return 1;
      return 2;
    };
    INFO( "decision " << i );
    CHECK( risk( plan.decisions[i - 1].riskClass ) <= risk( plan.decisions[i].riskClass ) );
    if ( risk( plan.decisions[i - 1].riskClass ) == risk( plan.decisions[i].riskClass ) )
      CHECK( plan.decisions[i - 1].costRank <= plan.decisions[i].costRank );
  }

  // The applied CRS repair is auto-applicable with the exact wiring.
  bool sawReproject = false;
  for ( const IrPreparedDecision &decision : plan.decisions )
  {
    if ( decision.ruleId == "reproject_to_reference" )
    {
      sawReproject = true;
      CHECK( decision.autoApplicable );
      CHECK( decision.operatorId == "io:reproject" );
      CHECK( decision.params["targetCrs"].asString() == "EPSG:32650" );
      CHECK( decision.evidenceRank >= 2 ); // target_crs + slot
      CHECK_FALSE( decision.whyZh.empty() );
    }
    else if ( decision.riskClass != repair_risk::kShapePreserving )
    {
      // Science-changing / radiometric: NEVER auto-applicable.
      CHECK_FALSE( decision.autoApplicable );
    }
  }
  CHECK( sawReproject );

  // Same inputs -> same plan bytes.
  WorkflowIr ir2 = crsConflictIr();
  IrAnalysis analysis2 = analyzeWorkflowIr( ir2, conflictFacts() );
  IrRepairOutcome outcome2 = planRepairs( ir2, analysis2, conflictFacts() );
  CHECK( plan.toJson() == planPreparedDecisions( outcome2 ).toJson() );
}

TEST_CASE( "decision cost table follows the closed convention", "[repair11]" )
{
  CHECK( decisionCostRank( "align_to_reference" ) == 1 );
  CHECK( decisionCostRank( "reproject_to_reference" ) == 2 );
  CHECK( decisionCostRank( "apply_qa_mask" ) == 3 );
  CHECK( decisionCostRank( "calibrate_toa_reflectance" ) == 4 );
  CHECK( decisionCostRank( "sar_dn_calibration" ) == 4 );
  CHECK( decisionCostRank( "temporal_gap_fill" ) == 6 );
  CHECK( decisionCostRank( "select_other_dataset" ) == 7 );
  CHECK( decisionCostRank( "no_such_rule" ) == 9 );
}

// ---------------------------------------------------------------------------
// Explain.
// ---------------------------------------------------------------------------

TEST_CASE( "explain: blocked compile traces back to the failed check in Chinese", "[explain11]" )
{
  loadHarnessKnowledge();
  WorkflowIr ir = crsConflictIr();
  IrAnalysis analysis = analyzeWorkflowIr( ir, conflictFacts() );
  REQUIRE( analysis.blocked() );

  explain::ExplainRequest request;
  request.ir = &ir;
  request.analysis = &analysis;
  const Json::Value doc = explain::explainDecisionChain( request );

  CHECK( doc["causes_total"].asInt() >= 1 );
  REQUIRE( doc["causes"].isArray() );
  CHECK( doc["causes"].size() >= 1 );
  CHECK( doc["causes"][0]["kind"].asString() == "check_failed" );
  CHECK( doc["causes"][0]["code"].asString() == "CRS_MISMATCH" );
  const std::string summary = doc["summary_zh"].asString();
  CHECK( summary.find( "编译被阻断" ) != std::string::npos );
  // The zh cause text is real Chinese (non-ASCII), bounded.
  const std::string msg = doc["causes"][0]["msg_zh"].asString();
  CHECK( msg.find( "坐标系" ) != std::string::npos );
  CHECK( static_cast<int>( msg.size() ) <= explain::ExplainLimits::kMaxTextChars );
  CHECK( doc["serialized_bytes"].asInt() <= explain::ExplainLimits::kMaxTextBytes );
}

TEST_CASE( "explain: run failure + refusal + repair all appear, ordered", "[explain11]" )
{
  loadHarnessKnowledge();
  WorkflowIr ir = crsConflictIr();
  IrAnalysis analysis = analyzeWorkflowIr( ir, conflictFacts() );
  IrRepairOutcome outcome = planRepairs( ir, analysis, conflictFacts() );

  explain::ExplainRequest request;
  request.analysis = &analysis;
  request.repairs = &outcome.repairs;
  request.refusals = &outcome.refusals;
  request.runErrorCode = error_codes::kInsufficientMemory;
  const Json::Value doc = explain::explainDecisionChain( request );

  REQUIRE( doc["causes"].size() >= 1 );
  CHECK( doc["causes"][0]["kind"].asString() == "run_failure" );
  CHECK( doc["causes"][0]["msg_zh"].asString().find( "内存不足" ) != std::string::npos );
  CHECK( doc["causes_total"].asInt() >= 2 );
  CHECK( doc["truncated"].asBool() == false );

  // Bounded: a pathological request cannot exceed the cause cap.
  std::vector<IrRepairRecord> many( 64 );
  for ( size_t i = 0; i < many.size(); ++i )
  {
    many[i].ruleId = "reproject_to_reference";
    many[i].risk = repair_risk::kShapePreserving;
  }
  explain::ExplainRequest flood;
  IrAnalysis okAnalysis; // verdict "" — no issues, but repairs present
  flood.analysis = &okAnalysis;
  flood.repairs = &many;
  const Json::Value bounded = explain::explainDecisionChain( flood );
  CHECK( bounded["causes"].size() == explain::ExplainLimits::kMaxCauses );
  CHECK( bounded["causes_total"].asInt() == 64 );
  CHECK( bounded["truncated"].asBool() );
}

TEST_CASE( "explain: zh templates cover the compiler taxonomy", "[explain11]" )
{
  CHECK_FALSE( explain::zhTemplateForCode( error_codes::kCrsMismatch ).empty() );
  CHECK_FALSE( explain::zhTemplateForCode( error_codes::kTemporalCalendarConflict ).empty() );
  CHECK_FALSE( explain::zhTemplateForCode( error_codes::kNumericDomainChain ).empty() );
  CHECK_FALSE( explain::zhTemplateForCode( error_codes::kBandIdentityMismatch ).empty() );
  CHECK_FALSE( explain::zhTemplateForCode( error_codes::kOutputIdentityMismatch ).empty() );
  CHECK( explain::zhTemplateForCode( "NOT_A_REAL_CODE" ).empty() );
}
