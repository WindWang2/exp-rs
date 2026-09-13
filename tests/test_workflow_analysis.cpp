// tests/test_workflow_analysis.cpp
//
// Scientific Workflow Compiler 10.0 (ADR 0149): static analysis contract —
// typed issues over typed facts, fact-status severity discipline,
// deterministic order, closed new error codes.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <map>
#include <string>

#include "agent/harness/harness_error.h"
#include "agent/harness/workflow_analysis.h"
#include "agent/harness/capability_catalog.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/capability_relations.h"

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

/// An understanding document for a 4-band S2-like optical scene.
Json::Value opticalUnderstanding( const std::string &crs = "EPSG:32650",
                                  int width = 100, int height = 100,
                                  const std::string &pixelSize = "10" )
{
  Json::Value doc = parse( R"({
    "kind": "dataset_understanding",
    "source_kind": "raster",
    "crs": { "authid": ")" + crs + R"(" },
    "size": [ )" + std::to_string( width ) + ", " + std::to_string( height ) + R"( ],
    "pixel_size": [ )" + pixelSize + ", " + pixelSize + R"( ],
    "band_roles": [ "blue", "green", "red", "nir" ],
    "band_count": 4,
    "radiometric_state": "surface_reflectance",
    "sensor": "Sentinel-2",
    "modality": "optical"
  })" );
  doc["crs_authid"] = crs;
  return doc;
}

IrAnalysisInput factsFor( const std::string &slot, const Json::Value &doc )
{
  IrAnalysisInput input;
  input.inputFacts[ slot ] = doc;
  return input;
}

const IrIssue *findIssue( const IrAnalysis &analysis, const std::string &code,
                          const std::string &node = "" )
{
  for ( const IrIssue &issue : analysis.issues )
  {
    if ( issue.code == code && ( node.empty() || issue.node == node ) )
      return &issue;
  }
  return nullptr;
}

/// NDVI over one optical input — the canonical healthy plan.
Json::Value healthyNdviIr()
{
  return parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndvi", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" },
        "inputs": [ { "input": "primary" } ],
        "outputs": [ { "name": "output",
                       "artifact": { "kind": "raster", "numeric_domain": "index" } } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
}
} // namespace

TEST_CASE( "New compiler error codes joined the closed taxonomy", "[workflow_analysis][errors]" )
{
  loadHarnessKnowledge();
  CHECK( isKnownErrorCode( "WAVELENGTH_INCOMPATIBLE" ) );
  CHECK( isKnownErrorCode( "TEMPORAL_MISALIGNMENT" ) );
  CHECK( isKnownErrorCode( "CATEGORICAL_MISMATCH" ) );
  CHECK( isKnownErrorCode( "RESOURCE_OVER_BUDGET" ) );
  CHECK( isKnownErrorCode( "OUTPUT_PATH_COLLISION" ) );
  CHECK( isKnownErrorCode( "NONDETERMINISTIC_CHAIN" ) );
  CHECK( isKnownErrorCode( "FACT_CONFLICT" ) );
  // They map into category/retry classes like every other code.
  CHECK( errorCategoryForCode( "RESOURCE_OVER_BUDGET" ) == "validation" );
  CHECK( std::string( retryClassToString( retryClassForCode( "RESOURCE_OVER_BUDGET" ) ) ) ==
         "none" );
}

TEST_CASE( "A healthy NDVI plan analyzes clean against observed optical facts",
           "[workflow_analysis]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( healthyNdviIr(), ir, error ) );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, factsFor( "primary", opticalUnderstanding() ) );
  CHECK( analysis.verdict == "ok" );
  CHECK( analysis.errors().empty() );
  REQUIRE( !analysis.irFingerprint.empty() );
  // The facts the verdict rests on are echoed for auditability.
  CHECK( analysis.facts["primary"]["radiometric_state"].asString() == "surface_reflectance" );
  CHECK( analysis.factStatus["primary"]["radiometric_state"].asString() == "observed" );
}

TEST_CASE( "Unknown operators, missing params and unwired ports are typed errors",
           "[workflow_analysis]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "nodes": [
      { "id": "ghost", "operator": "rs:does_not_exist" },
      { "id": "no_params", "operator": "rs:spectral_index", "inputs": [] }
    ]
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, IrAnalysisInput{} );
  const IrIssue *unknown = findIssue( analysis, "INVALID_PLAN", "ghost" );
  REQUIRE( unknown );
  CHECK( unknown->details["operator"].asString() == "rs:does_not_exist" );
  // rs:spectral_index needs an index param (capability io contract).
  CHECK( findIssue( analysis, "INVALID_PARAMETER", "no_params" ) != nullptr );
  CHECK( analysis.verdict != "ok" );
}

TEST_CASE( "CRS and grid conflicts are repairable errors on shared-grid consumers",
           "[workflow_analysis]" )
{
  // change_difference demands one shared grid (ADR 0098).
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "change",
    "inputs": [ { "name": "t1", "ref": "asset-1" }, { "name": "t2", "ref": "asset-2" } ],
    "nodes": [
      { "id": "diff", "operator": "rs:change_difference",
        "inputs": [ { "input": "t1", "as": "after" }, { "input": "t2", "as": "before" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  input.inputFacts["t1"] = opticalUnderstanding( "EPSG:32650", 100, 100, "10" );
  input.inputFacts["t2"] = opticalUnderstanding( "EPSG:32650", 200, 200, "20" );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *grid = findIssue( analysis, "GRID_MISMATCH", "diff" );
  REQUIRE( grid );
  CHECK( grid->severity == "error" );
  CHECK( grid->repairable );
  CHECK( analysis.verdict == "fixable" );
}

TEST_CASE( "CRS mismatch between inputs is a typed repairable error", "[workflow_analysis]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "change",
    "inputs": [ { "name": "t1", "ref": "asset-1" }, { "name": "t2", "ref": "asset-2" } ],
    "nodes": [
      { "id": "diff", "operator": "rs:change_difference",
        "inputs": [ { "input": "t1", "as": "after" }, { "input": "t2", "as": "before" } ] }
    ]
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  input.inputFacts["t1"] = opticalUnderstanding( "EPSG:32650" );
  input.inputFacts["t2"] = opticalUnderstanding( "EPSG:4326" );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *crs = findIssue( analysis, "CRS_MISMATCH", "diff" );
  REQUIRE( crs );
  CHECK( crs->repairable );
  CHECK( crs->details["crs_a"].asString() == "EPSG:32650" );
  CHECK( crs->details["crs_b"].asString() == "EPSG:4326" );
}

TEST_CASE( "DN into a reflectance kernel is a warn-class radiometry finding (contract)",
           "[workflow_analysis]" )
{
  // The spectral_index family grades DN as warn (degraded), not invalid —
  // the analysis must mirror the contract, not invent an error.
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( healthyNdviIr(), ir, error ) );
  Json::Value dnScene = opticalUnderstanding();
  dnScene["radiometric_state"] = "dn";
  dnScene["numeric_domain"] = "dn";
  IrAnalysisInput input;
  input.inputFacts["primary"] = dnScene;
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *radiometry = findIssue( analysis, "INVALID_RADIOMETRY", "ndvi" );
  REQUIRE( radiometry );
  CHECK( radiometry->severity == "warning" );
  CHECK( radiometry->details["numeric_domain"].asString() == "dn" );
  // Warnings never block; the repair layer turns this into a decision.
  CHECK( analysis.verdict == "ok" );
}

TEST_CASE( "Warn-class radiometry degrades to a warning, unknown facts skip the check",
           "[workflow_analysis]" )
{
  // The spectral_index family warns on DN but accepts TOA.
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
  Json::Value toaScene = opticalUnderstanding();
  toaScene["radiometric_state"] = "toa";
  input.inputFacts["primary"] = toaScene;
  const IrAnalysis toa = analyzeWorkflowIr( ir, input );
  CHECK( toa.errors().empty() ); // TOA is acceptable

  Json::Value noFacts = opticalUnderstanding();
  noFacts.removeMember( "radiometric_state" );
  input.inputFacts["primary"] = noFacts;
  const IrAnalysis silent = analyzeWorkflowIr( ir, input );
  CHECK( silent.verdict == "ok" ); // unknown facts never fake failures
}

TEST_CASE( "Missing band roles are non-repairable errors; wavelength conflicts are typed",
           "[workflow_analysis]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( healthyNdviIr(), ir, error ) );
  IrAnalysisInput input;
  Json::Value rgbScene = opticalUnderstanding();
  rgbScene["band_roles"] = Json::Value( Json::arrayValue );
  rgbScene["band_roles"].append( "blue" );
  rgbScene["band_roles"].append( "green" );
  rgbScene["band_roles"].append( "red" );
  rgbScene["band_count"] = 3;
  input.inputFacts["primary"] = rgbScene;
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *roles = findIssue( analysis, "BAND_ROLE_UNRESOLVED", "ndvi" );
  REQUIRE( roles );
  CHECK( !roles->repairable ); // the compiler cannot synthesize a NIR band
  CHECK( roles->details["role"].asString() == "nir" );
  CHECK( analysis.verdict == "blocked" );

  // A NIR band whose declared wavelength sits outside the physical window.
  Json::Value badWavelength = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndvi", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" }, "inputs": [ { "input": "primary" } ] }
    ]
  })" );
  WorkflowIr ir2;
  REQUIRE( readWorkflowIr( badWavelength, ir2, error ) );
  IrAnalysisInput input2;
  Json::Value scene = opticalUnderstanding();
  scene["band_roles"] = Json::Value( Json::arrayValue );
  scene["band_roles"].append( "nir" );
  scene["wavelengths_nm"] = Json::Value( Json::arrayValue );
  Json::Value wavelength( Json::objectValue );
  wavelength["band"] = 1;
  wavelength["center"] = 440.0; // blue-range value on a NIR-declared band
  wavelength["min"] = 430.0;
  wavelength["max"] = 450.0;
  scene["wavelengths_nm"].append( wavelength );
  input2.inputFacts["primary"] = scene;
  const IrAnalysis wavelengthAnalysis = analyzeWorkflowIr( ir2, input2 );
  CHECK( findIssue( wavelengthAnalysis, "WAVELENGTH_INCOMPATIBLE", "ndvi" ) != nullptr );
}

TEST_CASE( "Modality mismatch is an error on observed facts", "[workflow_analysis]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( healthyNdviIr(), ir, error ) );
  IrAnalysisInput input;
  Json::Value sarScene = opticalUnderstanding();
  sarScene["modality"] = "sar";
  sarScene["band_roles"] = Json::Value( Json::arrayValue );
  sarScene["band_roles"].append( "vv" );
  input.inputFacts["primary"] = sarScene;
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *modality = findIssue( analysis, "MODALITY_MISMATCH", "ndvi" );
  REQUIRE( modality );
  CHECK( modality->details["modality"].asString() == "sar" );
}

TEST_CASE( "Categorical inputs are refused by continuous kernels", "[workflow_analysis]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( healthyNdviIr(), ir, error ) );
  IrAnalysisInput input;
  Json::Value classes = opticalUnderstanding();
  classes["numeric_domain"] = "categorical";
  classes["class_count"] = 8;
  input.inputFacts["primary"] = classes;
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  CHECK( findIssue( analysis, "CATEGORICAL_MISMATCH", "ndvi" ) != nullptr );
}

TEST_CASE( "Resource over-budget and output path collisions are document-level errors",
           "[workflow_analysis]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "a", "operator": "rs:band_math",
        "params": { "output": "/tmp/same.tif" },
        "inputs": [ { "input": "primary" } ],
        "resource_estimate_mb": 4000 },
      { "id": "b", "operator": "rs:band_math",
        "params": { "output": "/tmp/same.tif" },
        "inputs": [ { "input": "primary" } ],
        "resource_estimate_mb": 2000 }
    ],
    "expectations": { "max_ram_mb": 4096, "output_dir": "/tmp/out" }
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, IrAnalysisInput{} );
  CHECK( findIssue( analysis, "RESOURCE_OVER_BUDGET" ) != nullptr );
  CHECK( findIssue( analysis, "OUTPUT_PATH_COLLISION" ) != nullptr );
  CHECK( findIssue( analysis, "OUTPUT_PATH_COLLISION" )->details["first_node"].asString() == "a" );
}

TEST_CASE( "Stochastic operators under deterministic expectations warn, not fail",
           "[workflow_analysis]" )
{
  // rs:spectral_index is not stochastic; use the declared-determinism route.
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "a", "operator": "rs:band_math",
        "params": { "expression": "B1" },
        "inputs": [ { "input": "primary" } ], "determinism": "stochastic" }
    ],
    "expectations": { "deterministic": true, "output_dir": "/tmp/out" }
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, IrAnalysisInput{} );
  const IrIssue *nondet = findIssue( analysis, "NONDETERMINISTIC_CHAIN" );
  REQUIRE( nondet );
  CHECK( nondet->severity == "warning" );
  CHECK( analysis.verdict == "ok" ); // warnings never block
}

TEST_CASE( "Analysis is deterministic: two runs, byte-identical documents",
           "[workflow_analysis]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "change",
    "inputs": [ { "name": "t1", "ref": "asset-1" }, { "name": "t2", "ref": "asset-2" } ],
    "nodes": [
      { "id": "diff", "operator": "rs:change_difference",
        "inputs": [ { "input": "t2", "as": "reference" }, { "input": "t1", "as": "input" } ] }
    ]
  })" );
  WorkflowIr a;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, a, error ) );
  WorkflowIr b = a;
  IrAnalysisInput input;
  input.inputFacts["t1"] = opticalUnderstanding( "EPSG:32650" );
  input.inputFacts["t2"] = opticalUnderstanding( "EPSG:4326", 50, 50, "20" );
  const Json::Value first = analyzeWorkflowIr( a, input ).toJson();
  const Json::Value second = analyzeWorkflowIr( b, input ).toJson();
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  CHECK( Json::writeString( builder, first ) == Json::writeString( builder, second ) );
}

TEST_CASE( "Model seam operators demand a model id and honor recorded contracts",
           "[workflow_analysis]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "infer", "operator": "rs:infer",
        "params": { "model": "landcover-v1" },
        "inputs": [ { "input": "primary" } ] }
    ]
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  input.inputFacts["primary"] = opticalUnderstanding();
  Json::Value contract = parse( R"({
    "model": "landcover-v1",
    "input_band_roles": { "nir": 1, "red": 1 }
  })" );
  input.modelContracts["landcover-v1"] = contract;
  const IrAnalysis ok = analyzeWorkflowIr( ir, input );
  CHECK( findIssue( ok, "MODEL_INCOMPATIBLE" ) == nullptr );

  // A model demanding NIR over an RGB-only scene is incompatible.
  Json::Value rgbScene = opticalUnderstanding();
  rgbScene["band_roles"] = Json::Value( Json::arrayValue );
  for ( const char *role : { "blue", "green", "red" } )
    rgbScene["band_roles"].append( role );
  input.inputFacts["primary"] = rgbScene;
  const IrAnalysis incompatible = analyzeWorkflowIr( ir, input );
  const IrIssue *model = findIssue( incompatible, "MODEL_INCOMPATIBLE", "infer" );
  REQUIRE( model );
  CHECK( model->details["role"].asString() == "nir" );

  // No model parameter at all: typed INVALID_PARAMETER.
  Json::Value noModel = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [ { "id": "infer", "operator": "rs:infer", "inputs": [ { "input": "primary" } ] } ]
  })" );
  WorkflowIr ir2;
  REQUIRE( readWorkflowIr( noModel, ir2, error ) );
  const IrAnalysis missing = analyzeWorkflowIr( ir2, IrAnalysisInput{} );
  const IrIssue *param = findIssue( missing, "INVALID_PARAMETER", "infer" );
  REQUIRE( param );
  CHECK( param->details["parameter"].asString() == "model" );
}

TEST_CASE( "Assumed-only numeric domains degrade SAR findings to warnings",
           "[workflow_analysis]" )
{
  // A producer inherits an assumed numeric_domain downstream; the consumer's
  // SAR calibration finding must be a WARNING, never an error grounded in a
  // heuristic (review A-5).
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "sar",
    "inputs": [ { "name": "primary", "ref": "asset-9",
                  "artifact": { "modality": "sar", "numeric_domain": "dn" } } ],
    "nodes": [
      { "id": "filter", "operator": "rs:sar_speckle",
        "inputs": [ { "input": "primary" } ] },
      { "id": "backscatter", "operator": "rs:sar_backscatter",
        "inputs": [ { "node": "filter" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, IrAnalysisInput{} );
  // The consumer inherits the domain as ASSUMED — its finding is a warning.
  const IrIssue *calibration = findIssue( analysis, "CALIBRATION_MISMATCH", "backscatter" );
  REQUIRE( calibration );
  CHECK( calibration->severity == "warning" );
  // The producer sees the slot's DECLARED domain — its finding is the
  // fact-backed (repairable) error; the verdict reflects that honestly.
  const IrIssue *producer = findIssue( analysis, "CALIBRATION_MISMATCH", "filter" );
  REQUIRE( producer );
  CHECK( producer->severity == "error" );
  CHECK( analysis.verdict == "fixable" );
}

TEST_CASE( "Duplicate input port bindings are rejected at read time",
           "[workflow_analysis][workflow_ir]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "inputs": [ { "name": "t1", "ref": "a" }, { "name": "t2", "ref": "b" } ],
    "nodes": [
      { "id": "diff", "operator": "rs:change_difference",
        "inputs": [ { "input": "t1", "as": "after" }, { "input": "t2", "as": "after" } ] }
    ]
  })" );
  WorkflowIr ir;
  HarnessError error;
  CHECK( !readWorkflowIr( doc, ir, error ) );
  CHECK( error.code == "INVALID_PLAN" );
}

TEST_CASE( "Object-shaped grid facts (the real inspect shapes) are compared",
           "[workflow_analysis]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "change",
    "inputs": [ { "name": "t1", "ref": "asset-1" }, { "name": "t2", "ref": "asset-2" } ],
    "nodes": [
      { "id": "diff", "operator": "rs:change_difference",
        "inputs": [ { "input": "t1", "as": "after" }, { "input": "t2", "as": "before" } ] }
    ]
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  // The shapes raster_inspect actually emits (objects, not arrays).
  Json::Value doc1 = parse( R"({
    "kind": "dataset_understanding", "source_kind": "raster",
    "crs": { "authid": "EPSG:32650" },
    "size": { "width": 100, "height": 100 },
    "pixel_size": { "x": 10, "y": 10 },
    "band_roles": [ "red", "nir" ]
  })" );
  Json::Value doc2 = parse( R"({
    "kind": "dataset_understanding", "source_kind": "raster",
    "crs": { "authid": "EPSG:32650" },
    "size": { "width": 200, "height": 200 },
    "pixel_size": { "x": 20, "y": 20 },
    "band_roles": [ "red", "nir" ]
  })" );
  input.inputFacts["t1"] = doc1;
  input.inputFacts["t2"] = doc2;
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  CHECK( findIssue( analysis, "GRID_MISMATCH", "diff" ) != nullptr );
  // Case-folded CRS: epsg:4326 vs EPSG:4326 is NOT a conflict (review A-7).
  Json::Value doc3 = parse( R"({
    "kind": "dataset_understanding", "source_kind": "raster",
    "crs": { "authid": "epsg:4326" },
    "size": { "width": 100, "height": 100 },
    "pixel_size": { "x": 10, "y": 10 },
    "band_roles": [ "red", "nir" ]
  })" );
  Json::Value doc4 = parse( R"({
    "kind": "dataset_understanding", "source_kind": "raster",
    "crs_authid": "EPSG:4326",
    "size": { "width": 100, "height": 100 },
    "pixel_size": { "x": 10, "y": 10 },
    "band_roles": [ "red", "nir" ]
  })" );
  input.inputFacts["t1"] = doc3;
  input.inputFacts["t2"] = doc4;
  const IrAnalysis noCrsNoise = analyzeWorkflowIr( ir, input );
  // Case-folded + crs_authid fallback: one CRS, no noise; identical grids.
  CHECK( findIssue( noCrsNoise, "CRS_MISMATCH" ) == nullptr );
  CHECK( findIssue( noCrsNoise, "GRID_MISMATCH" ) == nullptr );
}

TEST_CASE( "Declared facts conflicting with observations become FACT_CONFLICT warnings",
           "[workflow_analysis]" )
{
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3",
                  "artifact": { "crs": { "authid": "EPSG:4326" } } } ],
    "nodes": [
      { "id": "ndvi", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" }, "inputs": [ { "input": "primary" } ] }
    ]
  })" );
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  IrAnalysisInput input;
  input.inputFacts["primary"] = opticalUnderstanding( "EPSG:32650" );
  const IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *conflict = findIssue( analysis, "FACT_CONFLICT" );
  REQUIRE( conflict );
  CHECK( conflict->severity == "warning" );
  CHECK( conflict->details["key"].asString() == "crs" );
  // The observed value wins — the analysis facts echo shows it.
  CHECK( analysis.facts["primary"]["crs"]["authid"].asString() == "EPSG:32650" );
}
