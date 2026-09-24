// tests/test_workflow_analysis_11.cpp
//
// Compiler & Grounding 11.0, analysis 2.0: temporal calendar, numeric-domain
// chain, band identity, and output identity checks — each with the pass /
// fail / skip discipline (UNKNOWN never fakes PASS), and the four new codes
// pinned in the closed taxonomy.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "agent/harness/capability_catalog.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/capability_relations.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/workflow_analysis.h"
#include "agent/harness/workflow_ir.h"

using namespace sicnu::agent::harness;

namespace
{
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

Json::Value parse( const std::string &text )
{
  Json::Value out;
  Json::Reader reader;
  REQUIRE( reader.parse( text, out ) );
  return out;
}

Json::Value opticalUnderstanding( const std::string &crs = "EPSG:32650" )
{
  Json::Value doc = parse( R"({
    "kind": "dataset_understanding",
    "source_kind": "raster",
    "crs": { "authid": ")" + crs + R"(" },
    "size": [ 100, 100 ],
    "pixel_size": [ 10, 10 ],
    "band_roles": [ "blue", "green", "red", "nir" ],
    "band_count": 4,
    "radiometric_state": "surface_reflectance",
    "sensor": "Sentinel-2",
    "modality": "optical"
  })" );
  doc["crs_authid"] = crs;
  return doc;
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

const Json::Value *findCheck( const IrAnalysis &analysis, const std::string &check )
{
  for ( const Json::Value &row : analysis.checks )
  {
    if ( row.isObject() && row["check"].asString() == check )
      return &row;
  }
  return nullptr;
}

/// A change-difference node over two slots — the canonical two-raster node.
Json::Value changeIr( Json::Value expectations = Json::Value( Json::objectValue ) )
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
  doc["expectations"] = expectations.isNull() ? Json::Value( Json::objectValue ) : expectations;
  return doc;
}

WorkflowIr readIr( const Json::Value &doc )
{
  WorkflowIr ir;
  HarnessError error;
  REQUIRE( readWorkflowIr( doc, ir, error ) );
  return ir;
}

IrAnalysis analyzeWith( Json::Value doc, Json::Value t1Facts, Json::Value t2Facts )
{
  WorkflowIr ir = readIr( doc );
  IrAnalysisInput input;
  if ( !t1Facts.isNull() )
    input.inputFacts[ "t1" ] = t1Facts;
  if ( !t2Facts.isNull() )
    input.inputFacts[ "t2" ] = t2Facts;
  return analyzeWorkflowIr( ir, input );
}
} // namespace

// ---------------------------------------------------------------------------
// temporal_calendar
// ---------------------------------------------------------------------------

TEST_CASE( "temporal calendar: no declared contract is an honest skip", "[analysis11][temporal]" )
{
  loadHarnessKnowledge();
  IrAnalysis analysis = analyzeWith( changeIr(), opticalUnderstanding(), opticalUnderstanding() );
  const Json::Value *check = findCheck( analysis, "temporal_calendar" );
  REQUIRE( check != nullptr );
  CHECK( (*check)["status"].asString() == "skip" );
  CHECK( findIssue( analysis, "TEMPORAL_CALENDAR_CONFLICT" ) == nullptr );
}

TEST_CASE( "temporal calendar: observed dates outside the declared range fail", "[analysis11][temporal]" )
{
  loadHarnessKnowledge();
  Json::Value expectations = parse( R"({
    "temporal": { "date_range": { "start": "2024-06-01", "end": "2024-09-01" } }
  })" );
  Json::Value early = opticalUnderstanding();
  early["acquisition_time"] = "2024-05-20";
  IrAnalysis analysis = analyzeWith( changeIr( expectations ), early, opticalUnderstanding() );
  const IrIssue *issue = findIssue( analysis, "TEMPORAL_CALENDAR_CONFLICT" );
  REQUIRE( issue != nullptr );
  CHECK( issue->severity == "error" );
  CHECK( issue->details["observed_first"].asString() == "2024-05-20" );

  Json::Value late = opticalUnderstanding();
  late["acquisition_time"] = "2024-10-01";
  IrAnalysis lateAnalysis = analyzeWith( changeIr( expectations ), opticalUnderstanding(), late );
  REQUIRE( findIssue( lateAnalysis, "TEMPORAL_CALENDAR_CONFLICT" ) != nullptr );
}

TEST_CASE( "temporal calendar: dates inside the declared range pass", "[analysis11][temporal]" )
{
  loadHarnessKnowledge();
  Json::Value expectations = parse( R"({
    "temporal": { "date_range": { "start": "2024-06-01", "end": "2024-09-01" } }
  })" );
  Json::Value inRange = opticalUnderstanding();
  inRange["acquisition_time"] = "2024-07-10";
  IrAnalysis analysis = analyzeWith( changeIr( expectations ), inRange, inRange );
  CHECK( findIssue( analysis, "TEMPORAL_CALENDAR_CONFLICT" ) == nullptr );
  const Json::Value *check = findCheck( analysis, "temporal_calendar" );
  REQUIRE( check != nullptr );
  CHECK( (*check)["status"].asString() == "pass" );
}

TEST_CASE( "temporal calendar: require_regular fails an irregular folded series", "[analysis11][temporal]" )
{
  loadHarnessKnowledge();
  Json::Value expectations = parse( R"({ "temporal": { "require_regular": true } })" );
  Json::Value irregular = opticalUnderstanding();
  // Gaps 1d, 59d, 1d — hand-computed irregular.
  irregular["dates"] = parse( R"([ "2024-01-01", "2024-01-02", "2024-03-01", "2024-03-02" ])" );
  IrAnalysis analysis = analyzeWith( changeIr( expectations ), irregular, opticalUnderstanding() );
  const IrIssue *issue = findIssue( analysis, "TEMPORAL_CALENDAR_CONFLICT" );
  REQUIRE( issue != nullptr );
  CHECK( issue->details["regularity"].asString() == "irregular" );
}

TEST_CASE( "temporal calendar: declared cadence 16d matches a 16d series", "[analysis11][temporal]" )
{
  loadHarnessKnowledge();
  Json::Value expectations = parse( R"({ "temporal": { "cadence_days": 16 } })" );
  Json::Value regular = opticalUnderstanding();
  regular["dates"] =
    parse( R"([ "2024-01-01", "2024-01-17", "2024-02-02", "2024-02-18" ])" );
  IrAnalysis analysis = analyzeWith( changeIr( expectations ), regular, opticalUnderstanding() );
  CHECK( findIssue( analysis, "TEMPORAL_CALENDAR_CONFLICT" ) == nullptr );

  // A 5d series violates the declared 16d cadence.
  Json::Value dense = opticalUnderstanding();
  dense["dates"] = parse( R"([ "2024-01-01", "2024-01-06", "2024-01-11" ])" );
  IrAnalysis denseAnalysis =
    analyzeWith( changeIr( expectations ), dense, opticalUnderstanding() );
  REQUIRE( findIssue( denseAnalysis, "TEMPORAL_CALENDAR_CONFLICT" ) != nullptr );
}

TEST_CASE( "temporal calendar: declared contract with no observable dates is a skip, not a pass", "[analysis11][temporal]" )
{
  loadHarnessKnowledge();
  Json::Value expectations = parse( R"({
    "temporal": { "date_range": { "start": "2024-06-01", "end": "2024-09-01" } }
  })" );
  IrAnalysis analysis = analyzeWith( changeIr( expectations ), opticalUnderstanding(),
                                     opticalUnderstanding() );
  const Json::Value *check = findCheck( analysis, "temporal_calendar" );
  REQUIRE( check != nullptr );
  CHECK( (*check)["status"].asString() == "skip" );
  CHECK( findIssue( analysis, "TEMPORAL_CALENDAR_CONFLICT" ) == nullptr );
}

// ---------------------------------------------------------------------------
// numeric_domain_chain
// ---------------------------------------------------------------------------

TEST_CASE( "numeric domain chain: DN beside surface reflectance is an error", "[analysis11][domain]" )
{
  loadHarnessKnowledge();
  Json::Value dn = opticalUnderstanding();
  dn["radiometric_state"] = "dn";
  IrAnalysis analysis = analyzeWith( changeIr(), dn, opticalUnderstanding() );
  const IrIssue *issue = findIssue( analysis, "NUMERIC_DOMAIN_CHAIN", "diff" );
  REQUIRE( issue != nullptr );
  CHECK( issue->severity == "error" );
  CHECK( issue->details["domain_a"].asString() == "dn" );
  CHECK( issue->details["domain_b"].asString() == "surface_reflectance" );
}

TEST_CASE( "numeric domain chain: dB beside linear power is an error", "[analysis11][domain]" )
{
  loadHarnessKnowledge();
  Json::Value db = opticalUnderstanding();
  db["radiometric_state"] = "db";
  Json::Value power = opticalUnderstanding();
  power["radiometric_state"] = "linear_power";
  IrAnalysis analysis = analyzeWith( changeIr(), db, power );
  REQUIRE( findIssue( analysis, "NUMERIC_DOMAIN_CHAIN", "diff" ) != nullptr );
}

TEST_CASE( "numeric domain chain: TOA beside surface reflectance is a warning", "[analysis11][domain]" )
{
  loadHarnessKnowledge();
  Json::Value toa = opticalUnderstanding();
  toa["radiometric_state"] = "toa";
  IrAnalysis analysis = analyzeWith( changeIr(), toa, opticalUnderstanding() );
  const IrIssue *warn = findIssue( analysis, "NUMERIC_DOMAIN_CHAIN", "diff" );
  REQUIRE( warn != nullptr );
  CHECK( warn->severity == "warning" );
}

TEST_CASE( "numeric domain chain: two reflectance inputs are compatible", "[analysis11][domain]" )
{
  loadHarnessKnowledge();
  IrAnalysis analysis = analyzeWith( changeIr(), opticalUnderstanding(), opticalUnderstanding() );
  CHECK( findIssue( analysis, "NUMERIC_DOMAIN_CHAIN" ) == nullptr );
  const Json::Value *check = findCheck( analysis, "numeric_domain_chain" );
  REQUIRE( check != nullptr );
  CHECK( (*check)["status"].asString() == "skip" ); // no DECIDABLE conflict pair
}

// ---------------------------------------------------------------------------
// band_identity
// ---------------------------------------------------------------------------

TEST_CASE( "band identity: same source wired to both ports of a role-distinct node warns", "[analysis11][identity]" )
{
  loadHarnessKnowledge();
  // rs:spectral_index demands distinct roles (red + nir); wiring the SAME
  // slot to both ports is the duplication this check exists for.
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" } ],
    "nodes": [
      { "id": "ndx", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" },
        "inputs": [ { "input": "primary", "as": "nir" }, { "input": "primary", "as": "red" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
  IrAnalysisInput input;
  input.inputFacts[ "primary" ] = opticalUnderstanding();
  WorkflowIr ir = readIr( doc );
  IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *issue = findIssue( analysis, "BAND_IDENTITY_MISMATCH", "ndx" );
  REQUIRE( issue != nullptr );
  CHECK( issue->severity == "warning" );
  CHECK( issue->details["wired_inputs"].asInt() == 2 );
}

TEST_CASE( "band identity: distinct sources pass, missing demand skips", "[analysis11][identity]" )
{
  loadHarnessKnowledge();
  Json::Value doc = parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "intent": "ndvi",
    "inputs": [ { "name": "primary", "ref": "asset-3" }, { "name": "secondary", "ref": "asset-4" } ],
    "nodes": [
      { "id": "ndx", "operator": "rs:spectral_index",
        "params": { "index": "NDVI" },
        "inputs": [ { "input": "primary", "as": "nir" }, { "input": "secondary", "as": "red" } ] }
    ],
    "expectations": { "output_dir": "/tmp/out" }
  })" );
  IrAnalysisInput input;
  input.inputFacts[ "primary" ] = opticalUnderstanding();
  input.inputFacts[ "secondary" ] = opticalUnderstanding();
  WorkflowIr ir = readIr( doc );
  IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  CHECK( findIssue( analysis, "BAND_IDENTITY_MISMATCH" ) == nullptr );
  const Json::Value *check = findCheck( analysis, "band_identity" );
  REQUIRE( check != nullptr );
  CHECK( (*check)["status"].asString() == "pass" );
}

// ---------------------------------------------------------------------------
// output_identity
// ---------------------------------------------------------------------------

TEST_CASE( "output identity: declared kind disagreeing with the artifact kind fails", "[analysis11][output]" )
{
  loadHarnessKnowledge();
  Json::Value doc = parse( R"({
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
    "outputs": [ { "name": "map", "node": "ndvi", "port": "output", "kind": "vector" } ]
  })" );
  IrAnalysisInput input;
  input.inputFacts[ "primary" ] = opticalUnderstanding();
  WorkflowIr ir = readIr( doc );
  IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  const IrIssue *issue = findIssue( analysis, "OUTPUT_IDENTITY_MISMATCH" );
  REQUIRE( issue != nullptr );
  CHECK( issue->severity == "error" );
  CHECK( issue->details["declared_kind"].asString() == "vector" );
  CHECK( issue->details["artifact_kind"].asString() == "raster" );
}

TEST_CASE( "output identity: agreeing kinds pass; dangling ports skip", "[analysis11][output]" )
{
  loadHarnessKnowledge();
  Json::Value doc = parse( R"({
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
    "outputs": [ { "name": "map", "node": "ndvi", "port": "output", "kind": "raster" } ]
  })" );
  IrAnalysisInput input;
  input.inputFacts[ "primary" ] = opticalUnderstanding();
  WorkflowIr ir = readIr( doc );
  IrAnalysis analysis = analyzeWorkflowIr( ir, input );
  CHECK( findIssue( analysis, "OUTPUT_IDENTITY_MISMATCH" ) == nullptr );
  const Json::Value *check = findCheck( analysis, "output_identity" );
  REQUIRE( check != nullptr );
  CHECK( (*check)["status"].asString() == "pass" );

  // A duplicate declaration of the same port warns even when kinds agree.
  doc["outputs"].append( parse( R"({ "name": "map2", "node": "ndvi", "port": "output",
                                 "kind": "raster" })" ) );
  WorkflowIr irDup = readIr( doc );
  IrAnalysis duplicated = analyzeWorkflowIr( irDup, input );
  const IrIssue *dup = findIssue( duplicated, "OUTPUT_IDENTITY_MISMATCH" );
  REQUIRE( dup != nullptr );
  CHECK( dup->severity == "warning" );
}

// ---------------------------------------------------------------------------
// New codes joined the closed taxonomy.
// ---------------------------------------------------------------------------

TEST_CASE( "analysis 2.0 codes joined the closed taxonomy", "[analysis11][errors]" )
{
  const std::vector<std::string> codes = allErrorCodes();
  auto contains = [&]( const std::string &code ) {
    return std::find( codes.begin(), codes.end(), code ) != codes.end();
  };
  CHECK( contains( "TEMPORAL_CALENDAR_CONFLICT" ) );
  CHECK( contains( "NUMERIC_DOMAIN_CHAIN" ) );
  CHECK( contains( "BAND_IDENTITY_MISMATCH" ) );
  CHECK( contains( "OUTPUT_IDENTITY_MISMATCH" ) );
  CHECK( errorCategoryForCode( "TEMPORAL_CALENDAR_CONFLICT" ) == "validation" );
  CHECK( retryClassForCode( "NUMERIC_DOMAIN_CHAIN" ) == RetryClass::None );
}
