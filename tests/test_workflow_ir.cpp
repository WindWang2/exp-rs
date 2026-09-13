// tests/test_workflow_ir.cpp
//
// Scientific Workflow Compiler 10.0 (ADR 0149): WorkflowIR core contract —
// fail-closed reader, closed fact vocabulary, bounds, deterministic
// normalize/fingerprint, merge provenance, structure validation.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <string>

#include "agent/harness/workflow_ir.h"

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

Json::Value minimalIr()
{
  return parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "goal": "NDVI over the August scene",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndvi_1", "operator": "rs:spectral_index",
        "params": { "index": "NDVI", "output": "/tmp/out.tif" },
        "inputs": [ { "input": "primary" } ],
        "outputs": [ { "name": "output",
                       "artifact": { "kind": "raster", "numeric_domain": "index" } } ],
        "verification": "raster",
        "semantic_output": "植被指数图" }
    ],
    "outputs": [ { "name": "ndvi", "node": "ndvi_1", "kind": "raster" } ]
  })" );
}
} // namespace

TEST_CASE( "WorkflowIR reader accepts a valid document and fills defaults", "[workflow_ir]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( minimalIr(), ir, error ) );
  CHECK( ir.irId.rfind( "wir-", 0 ) == 0 );
  CHECK( ir.intent == "ndvi" );
  REQUIRE( ir.nodes.size() == 1 );
  CHECK( ir.nodes[0].operatorId == "rs:spectral_index" );
  CHECK( ir.nodes[0].inputs[0].input == "primary" );
  CHECK( ir.nodes[0].outputs[0].artifact["numeric_domain"].asString() == "index" );
  CHECK( ir.nodes[0].source == "agent" );
  REQUIRE( ir.inputs.size() == 1 );
  CHECK( ir.inputs[0].reference == "asset-3" );
}

TEST_CASE( "WorkflowIR reader is fail-closed on envelope, vocabulary and bounds", "[workflow_ir]" )
{
  WorkflowIr ir;
  HarnessError error;

  Json::Value wrongKind = minimalIr();
  wrongKind["kind"] = "execution_plan";
  REQUIRE( !readWorkflowIr( wrongKind, ir, error ) );
  CHECK( error.code == "INVALID_PLAN" );

  Json::Value wrongVersion = minimalIr();
  wrongVersion["schema_version"] = "9.9";
  REQUIRE( !readWorkflowIr( wrongVersion, ir, error ) );

  Json::Value unknownIntent = minimalIr();
  unknownIntent["intent"] = "teleportation";
  REQUIRE( !readWorkflowIr( unknownIntent, ir, error ) );

  Json::Value badDomain = minimalIr();
  badDomain["nodes"][0]["outputs"][0]["artifact"]["numeric_domain"] = "decibels";
  REQUIRE( !readWorkflowIr( badDomain, ir, error ) );

  Json::Value unknownKey = minimalIr();
  unknownKey["nodes"][0]["outputs"][0]["artifact"]["color"] = "green";
  REQUIRE( !readWorkflowIr( unknownKey, ir, error ) );

  Json::Value emptyNodes = minimalIr();
  emptyNodes["nodes"] = Json::Value( Json::arrayValue );
  REQUIRE( !readWorkflowIr( emptyNodes, ir, error ) );

  Json::Value badId = minimalIr();
  badId["nodes"][0]["id"] = "bad id!";
  REQUIRE( !readWorkflowIr( badId, ir, error ) );

  Json::Value bothEdgeForms = minimalIr();
  bothEdgeForms["nodes"][0]["inputs"][0]["node"] = "self";
  REQUIRE( !readWorkflowIr( bothEdgeForms, ir, error ) );

  // Bounds: nodes over the limit are rejected, not clipped.
  Json::Value many = minimalIr();
  many["nodes"] = Json::Value( Json::arrayValue );
  for ( int i = 0; i <= IrLimits::kMaxNodes; ++i )
  {
    Json::Value node( Json::objectValue );
    node["id"] = "n" + std::to_string( i );
    node["operator"] = "rs:band_math";
    many["nodes"].append( node );
  }
  REQUIRE( !readWorkflowIr( many, ir, error ) );
}

TEST_CASE( "WorkflowIR normalize is deterministic, topological and idempotent", "[workflow_ir]" )
{
  // Reverse-ordered document: consumer listed before its producer.
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "inputs": [ { "name": "primary", "ref": "asset-1" } ],
    "nodes": [
      { "id": "consumer", "operator": "rs:change_detection",
        "inputs": [ { "node": "producer_b" }, { "node": "producer_a", "as": "secondary" } ] },
      { "id": "producer_b", "operator": "rs:extract_bands",
        "inputs": [ { "input": "primary" } ] },
      { "id": "producer_a", "operator": "rs:extract_bands",
        "inputs": [ { "input": "primary" } ] }
    ]
  })" );
  WorkflowIr first;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, first, error ) );
  REQUIRE( first.nodes.size() == 3 );
  // The two producers have no order constraint between them; document order
  // keeps producer_b before producer_a among independents. Consumers come last.
  CHECK( first.nodes[2].id == "consumer" );

  WorkflowIr second = first;
  normalizeWorkflowIr( second );
  REQUIRE( second.nodes.size() == first.nodes.size() );
  for ( size_t i = 0; i < second.nodes.size(); ++i )
    CHECK( second.nodes[i].id == first.nodes[i].id );
  CHECK( workflowIrFingerprint( first ) == workflowIrFingerprint( second ) );
}

TEST_CASE( "WorkflowIR fingerprint changes with the science, not with bookkeeping", "[workflow_ir]" )
{
  WorkflowIr base;
  HarnessError error;
  REQUIRE( readWorkflowIr( minimalIr(), base, error ) );

  Json::Value differentParams = minimalIr();
  differentParams["nodes"][0]["params"]["index"] = "EVI";
  WorkflowIr changed;
  REQUIRE( readWorkflowIr( differentParams, changed, error ) );
  CHECK( workflowIrFingerprint( base ) != workflowIrFingerprint( changed ) );

  Json::Value renamedDoc = minimalIr();
  renamedDoc["ir_id"] = "wir-deadbeefdeadbeef";
  WorkflowIr renamed;
  REQUIRE( readWorkflowIr( renamedDoc, renamed, error ) );
  CHECK( workflowIrFingerprint( base ) == workflowIrFingerprint( renamed ) );
}

TEST_CASE( "WorkflowIR structure validation catches wiring, ports and cycles", "[workflow_ir]" )
{
  WorkflowIr ir;
  HarnessError error;

  Json::Value dangling = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "nodes": [
      { "id": "a", "operator": "rs:band_math",
        "inputs": [ { "node": "ghost" } ] }
    ]
  })" );
  REQUIRE( readWorkflowIr( dangling, ir, error ) );
  const auto issues = validateIrStructure( ir );
  REQUIRE( !issues.empty() );
  bool foundDangling = false;
  for ( const auto &issue : issues )
    if ( issue.error.details.isMember( "upstream" ) &&
         issue.error.details["upstream"].asString() == "ghost" )
      foundDangling = true;
  CHECK( foundDangling );

  Json::Value cyclic = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "nodes": [
      { "id": "a", "operator": "rs:band_math", "inputs": [ { "node": "b" } ] },
      { "id": "b", "operator": "rs:band_math", "inputs": [ { "node": "a" } ] }
    ]
  })" );
  REQUIRE( readWorkflowIr( cyclic, ir, error ) );
  bool foundCycle = false;
  for ( const auto &issue : validateIrStructure( ir ) )
    if ( issue.error.details.isMember( "cycle" ) )
      foundCycle = true;
  CHECK( foundCycle );

  Json::Value badPort = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "nodes": [
      { "id": "a", "operator": "rs:band_math",
        "outputs": [ { "name": "index_raster" } ] },
      { "id": "b", "operator": "rs:band_math", "inputs": [ { "node": "a", "output": "nope" } ] }
    ]
  })" );
  REQUIRE( readWorkflowIr( badPort, ir, error ) );
  bool foundPortIssue = false;
  for ( const auto &issue : validateIrStructure( ir ) )
    if ( issue.error.details.isMember( "port" ) && issue.error.details["port"].asString() == "nope" )
      foundPortIssue = true;
  CHECK( foundPortIssue );

  // A clean document produces no structural issues.
  WorkflowIr clean;
  REQUIRE( readWorkflowIr( minimalIr(), clean, error ) );
  CHECK( validateIrStructure( clean ).empty() );
}

TEST_CASE( "Artifact fact merge keeps observed over declared and records conflicts", "[workflow_ir]" )
{
  Json::Value declared = parse( R"({
    "kind": "raster", "numeric_domain": "dn", "sensor": "S2"
  })" );
  Json::Value observed = parse( R"({
    "kind": "raster", "numeric_domain": "surface_reflectance",
    "crs": "EPSG:32650", "envelope_note": "ignored"
  })" );
  Json::Value status;
  Json::Value conflicts;
  const Json::Value merged = mergeArtifactFacts( declared, observed, status, &conflicts );
  CHECK( merged["numeric_domain"].asString() == "surface_reflectance" );
  CHECK( merged["sensor"].asString() == "S2" );
  CHECK( merged["crs"].asString() == "EPSG:32650" );
  CHECK( status["numeric_domain"].asString() == "observed" );
  CHECK( status["sensor"].asString() == "declared" );
  CHECK( status["crs"].asString() == "observed" );
  REQUIRE( conflicts.size() == 1 );
  CHECK( conflicts[0]["key"].asString() == "numeric_domain" );
  // Non-fact keys from the understanding document never leak into the merge.
  CHECK( !merged.isMember( "envelope_note" ) );
}

TEST_CASE( "Closed vocabularies and derived ids are total and stable", "[workflow_ir]" )
{
  CHECK( isKnownNumericDomain( "db" ) );
  CHECK( isKnownNumericDomain( "linear_power" ) );
  CHECK( !isKnownNumericDomain( "dB" ) ); // wire strings are lowercase, closed
  CHECK( isKnownArtifactKind( "raster" ) );
  CHECK( !isKnownArtifactKind( "pointcloud" ) );
  CHECK( isLinearReflectiveDomain( "surface_reflectance" ) );
  CHECK( !isLinearReflectiveDomain( "db" ) );

  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( minimalIr(), ir, error ) );
  CHECK( deriveIrId( ir ) == ir.irId ); // generated id is content-addressed

  const Json::Value limits = irLimits();
  CHECK( limits["max_nodes"].asInt() == IrLimits::kMaxNodes );
}

TEST_CASE( "Canonical JSON round-trips through the reader", "[workflow_ir]" )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( minimalIr(), ir, error ) );
  const Json::Value canonical = workflowIrToJson( ir );
  WorkflowIr reparsed;
  REQUIRE( readWorkflowIr( canonical, reparsed, error ) );
  CHECK( reparsed.irId == ir.irId );
  CHECK( workflowIrFingerprint( reparsed ) == workflowIrFingerprint( ir ) );
  REQUIRE( reparsed.nodes.size() == 1 );
  CHECK( reparsed.nodes[0].semanticOutput == "植被指数图" );
}
