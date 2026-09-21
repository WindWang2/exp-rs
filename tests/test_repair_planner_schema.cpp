// tests/test_repair_planner_schema.cpp
//
// RS14-03 Scientific Repair Planner — Slice A: repair schema.
//
// Pins the ranking-independent, deterministic representation of repair plans:
// value objects, the versioned repair_plan/1.0 envelope, the closed
// vocabularies (risk classes mirror harness workflow_repair repair_risk::*
// byte-for-byte), SHA-256/16 fingerprinting over canonical content, and the
// fail-closed envelope reader.
//
// Planning-only invariant: nothing in this module executes a repair.

#include <json/json.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "repair_planner/repair_schema.h"
#include "repair_planner/repair_sha256.h"

#include <string>

using namespace sicnu::repair;

namespace {

Json::Value findingOf( const std::string &code )
{
  Json::Value f( Json::objectValue );
  f["code"] = code;
  f["severity"] = "error";
  return f;
}

RepairAction sampleAction()
{
  RepairAction a;
  a.id = "ra-grid-1";
  a.ruleId = "align_to_reference";
  a.kind = "capability_ref";
  a.operatorId = "rs:align";
  a.actionKey = "align_to_reference";
  a.params = Json::Value( Json::objectValue );
  a.riskClass = "shape_preserving";
  a.informationLoss = {};
  a.assumptions = { "reference grid is the authority" };
  a.cost.rank = 3;
  a.cost.costClass = "medium";
  a.cost.notes = "ordering device only";
  a.risk.riskClass = "shape_preserving";
  a.risk.severity = "low";
  a.risk.irreversible = false;
  a.factsSufficient = true;
  a.sourceFinding = findingOf( "GRID_MISMATCH" );
  return a;
}

} // namespace

TEST_CASE( "sha256 matches public test vectors", "[repair][sliceA]" )
{
  CHECK( sha256Hex( "" ) ==
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
  CHECK( sha256Hex( "abc" ) ==
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
  CHECK( sha256Hex( "The quick brown fox jumps over the lazy dog" ) ==
         "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592" );
}

TEST_CASE( "repair plan envelope is versioned and carries a fingerprint id", "[repair][sliceA]" )
{
  RepairPlan plan;
  plan.intent = "ndvi";
  plan.subject = "asset-1";
  plan.status = "planned";
  plan.selected.push_back( sampleAction() );

  const std::string id = assignRepairPlanIdentity( plan );
  REQUIRE( plan.planId == id );
  // "srp-" prefix + SHA-256/16 lowercase hex (agent_plan fingerprint convention)
  REQUIRE( id.size() == 20 );
  CHECK( id.substr( 0, 4 ) == "srp-" );
  const std::string hex = id.substr( 4 );
  // lowercase hex, 16 chars = SHA-256/16 convention (agent_plan planFingerprint)
  CHECK( hex.find_first_not_of( "0123456789abcdef" ) == std::string::npos );

  const Json::Value doc = repairPlanToJson( plan );
  REQUIRE( doc.isObject() );
  CHECK( doc["kind"].asString() == "repair_plan" );
  CHECK( doc["schema_version"].asString() == "1.0" );
  CHECK( doc["plan_id"].asString() == id );
  CHECK( doc["intent"].asString() == "ndvi" );
  CHECK( doc["status"].asString() == "planned" );
  CHECK( doc["selected"].isArray() );
  CHECK( doc["selected"].size() == 1 );
}

TEST_CASE( "fingerprint is content addressing: science changes move it, identity does not", "[repair][sliceA]" )
{
  RepairPlan a;
  a.intent = "ndvi";
  a.subject = "asset-1";
  a.status = "planned";
  a.selected.push_back( sampleAction() );

  RepairPlan b = a;
  b.subject = "asset-2"; // different science content

  RepairPlan c = a;
  c.planId = "srp-anything"; // identity only

  const std::string fa = repairPlanFingerprint( a );
  const std::string fb = repairPlanFingerprint( b );
  const std::string fc = repairPlanFingerprint( c );
  CHECK( fa != fb );
  CHECK( fa == fc );
}

TEST_CASE( "JSON representation is deterministic byte-for-byte", "[repair][sliceA]" )
{
  RepairPlan plan;
  plan.intent = "classify";
  plan.subject = "asset-9";
  plan.status = "planned";
  plan.selected.push_back( sampleAction() );
  assignRepairPlanIdentity( plan );

  const std::string first = jsonToString( repairPlanToJson( plan ) );
  const std::string second = jsonToString( repairPlanToJson( plan ) );
  CHECK( first == second );
  CHECK( !first.empty() );
}

TEST_CASE( "action JSON round-trip preserves the candidate contract", "[repair][sliceA]" )
{
  RepairAction a = sampleAction();
  a.informationLoss = { "resample changes effective resolution" };
  a.assumptions = { "reference grid is the authority", "nearest neighbour preserves values" };
  a.refusalCause = "";
  const Json::Value doc = repairActionToJson( a );

  CHECK( doc["id"].asString() == "ra-grid-1" );
  CHECK( doc["rule_id"].asString() == "align_to_reference" );
  CHECK( doc["kind"].asString() == "capability_ref" );
  CHECK( doc["operator_id"].asString() == "rs:align" );
  CHECK( doc["risk_class"].asString() == "shape_preserving" );
  CHECK( doc["cost"]["rank"].asInt() == 3 );
  CHECK( doc["risk"]["severity"].asString() == "low" );
  CHECK( doc["information_loss"].size() == 1 );
  CHECK( doc["assumptions"].size() == 2 );
  CHECK( doc["source_finding"]["code"].asString() == "GRID_MISMATCH" );

  RepairAction back;
  RepairError error;
  REQUIRE( repairActionFromJson( doc, back, error ) );
  CHECK( back.id == a.id );
  CHECK( back.operatorId == a.operatorId );
  CHECK( back.cost.rank == a.cost.rank );
  CHECK( back.informationLoss == a.informationLoss );
  CHECK( back.assumptions == a.assumptions );
}

TEST_CASE( "closed vocabularies: risk classes mirror the compiler repair_risk wire strings", "[repair][sliceA]" )
{
  // Compatibility oracle (single-truth discipline): the three risk classes
  // must stay byte-identical to src/agent/harness/workflow_repair.h
  // repair_risk::* so downstream consumers read one vocabulary.
  CHECK( std::string( repair_risk::kShapePreserving ) == "shape_preserving" );
  CHECK( std::string( repair_risk::kRadiometric ) == "radiometric" );
  CHECK( std::string( repair_risk::kScienceChanging ) == "science_changing" );

  CHECK( isKnownRiskClass( "shape_preserving" ) );
  CHECK( isKnownRiskClass( "radiometric" ) );
  CHECK( isKnownRiskClass( "science_changing" ) );
  CHECK( !isKnownRiskClass( "magic" ) );

  CHECK( isKnownActionKind( "capability_ref" ) );
  CHECK( isKnownActionKind( "declarative_transform" ) );
  CHECK( isKnownActionKind( "decision" ) );
  CHECK( !isKnownActionKind( "execute" ) );

  CHECK( isKnownPlanStatus( "planned" ) );
  CHECK( isKnownPlanStatus( "no_safe_repair" ) );
  CHECK( isKnownPlanStatus( "invalid_input" ) );
  CHECK( !isKnownPlanStatus( "ok" ) );
}

TEST_CASE( "validation rejects structurally unsafe actions", "[repair][sliceA]" )
{
  RepairError error;

  RepairAction unnamed = sampleAction();
  unnamed.id = "";
  CHECK( !validateRepairAction( unnamed, error ) );
  CHECK( error.code == "invalid_action" );

  RepairAction kernel = sampleAction(); // capability_ref without operator id
  kernel.operatorId = "";
  CHECK( !validateRepairAction( kernel, error ) );
  CHECK( error.code == "invalid_action" );

  RepairAction wild = sampleAction();
  wild.riskClass = "magic";
  CHECK( !validateRepairAction( wild, error ) );

  RepairAction uncosted = sampleAction();
  uncosted.cost.rank = 42; // closed convention: 1..9 (0 only for refusals)
  CHECK( !validateRepairAction( uncosted, error ) );

  RepairAction refusal = sampleAction();
  refusal.refusalCause = "band_physically_absent";
  refusal.cost.rank = 0; // refusals may leave cost unset
  CHECK( validateRepairAction( refusal, error ) );

  RepairAction badRefusal = refusal;
  badRefusal.refusalCause = ""; // rank 0 without a refusal cause is unset noise
  CHECK( !validateRepairAction( badRefusal, error ) );
}

TEST_CASE( "envelope reader is fail-closed", "[repair][sliceA]" )
{
  RepairPlan plan;
  plan.intent = "ndvi";
  plan.subject = "asset-1";
  plan.status = "planned";
  plan.selected.push_back( sampleAction() );
  assignRepairPlanIdentity( plan );
  const Json::Value doc = repairPlanToJson( plan );

  RepairError error;
  RepairPlan parsed;
  REQUIRE( readRepairPlan( doc, parsed, error ) );
  CHECK( parsed.planId == plan.planId );
  CHECK( parsed.intent == "ndvi" );
  REQUIRE( parsed.selected.size() == 1 );
  CHECK( parsed.selected[0].operatorId == "rs:align" );

  RepairPlan sink;
  Json::Value wrongKind = doc;
  wrongKind["kind"] = "execution_plan";
  CHECK( !readRepairPlan( wrongKind, sink, error ) );
  CHECK( error.code == "invalid_document" );

  Json::Value wrongVersion = doc;
  wrongVersion["schema_version"] = "9.9";
  CHECK( !readRepairPlan( wrongVersion, sink, error ) );
  CHECK( error.code == "unsupported_version" );

  CHECK( !readRepairPlan( Json::Value( Json::arrayValue ), sink, error ) );
  CHECK( error.code == "invalid_document" );
}
