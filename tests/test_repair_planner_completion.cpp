// tests/test_repair_planner_completion.cpp
//
// RS14-03 Scientific Repair Planner — completion slices over the Slice A
// schema (repair_plan/1.0):
//
//   B. requirement synthesis — closed finding-code -> requirement mapping;
//      unknown codes become typed `unsupported` requirements, malformed
//      input becomes typed invalid_input.
//   C. capability provider — a leaf-side provider interface; candidates are
//      driven by capability-knowledge-shaped documents (never linked to the
//      harness). Entries without id/cost become typed refusals.
//   D. auto policy — auto_executable only when the action key is on the
//      preparation whitelist AND the risk class is shape_preserving AND the
//      facts suffice AND autonomy allows it AND the role is not a student.
//      Science-changing candidates always need confirmation.
//   E. planner — deterministic plan/result/fragment assembly with counted
//      budget truncation, typed no-safe-repair and unknown-capability paths.
//   F. views — the teaching view recursively withholds executable values
//      (params/operator_id/action_key); the agent view marks planning_only.
//   G. planning state — tamper-evident, idempotent, bounded, reloadable.
//
// Planning-only invariant: nothing here executes a repair.

#include <json/json.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "repair_planner/repair_schema.h"
#include "repair_planner/repair_requirement.h"
#include "repair_planner/repair_provider.h"
#include "repair_planner/repair_policy.h"
#include "repair_planner/repair_planner.h"
#include "repair_planner/repair_view.h"
#include "repair_planner/repair_state.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::repair;

namespace {

Json::Value findingOf( const std::string &code, const std::string &severity = "error",
                       const std::string &subject = "primary" )
{
  Json::Value f( Json::objectValue );
  f["code"] = code;
  f["severity"] = severity;
  f["subject"] = subject;
  return f;
}

/// A provider whose family table is spelled out in the test (the "fake").
class FakeProvider : public RepairCapabilityProvider
{
public:
  void add( const std::string &kind, const Json::Value &entry )
  {
    families_[kind].append( entry );
  }
  std::vector<Json::Value>
  capabilitiesForRequirement( const std::string &kind ) const override
  {
    const auto it = families_.find( kind );
    if ( it == families_.end() )
      return {};
    return std::vector<Json::Value>( it->second.begin(), it->second.end() );
  }
  bool knowsRequirementKind( const std::string &kind ) const override
  {
    return families_.count( kind ) > 0;
  }

private:
  std::map<std::string, Json::Value> families_;
};

Json::Value gridCapability( const std::string &id, const std::string &costClass = "medium" )
{
  Json::Value e( Json::objectValue );
  e["id"] = id;
  e["family"] = "preprocess";
  Json::Value res( Json::objectValue );
  res["cost_class"] = costClass;
  e["resource"] = res;
  return e;
}

bool containsKeyRecursive( const Json::Value &node, const std::string &key )
{
  if ( node.isObject() )
  {
    if ( node.isMember( key ) )
      return true;
    for ( const auto &name : node.getMemberNames() )
      if ( containsKeyRecursive( node[name], key ) )
        return true;
  }
  else if ( node.isArray() )
  {
    for ( const Json::Value &entry : node )
      if ( containsKeyRecursive( entry, key ) )
        return true;
  }
  return false;
}

/// Loads the real capability-knowledge documents shipped in the repository.
std::vector<Json::Value> loadRealCapabilityEntries( const std::string &sourceDir )
{
  const std::vector<std::string> files = { "preprocess.json", "geometric.json",
                                           "temporal.json",  "inference.json",
                                           "classify.json",  "sar.json" };
  std::vector<Json::Value> entries;
  for ( const std::string &file : files )
  {
    std::ifstream in( sourceDir + "/data/agent/capabilities/" + file );
    REQUIRE( in.is_open() );
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    REQUIRE( Json::parseFromStream( builder, in, &doc, &errors ) );
    REQUIRE( doc.isArray() );
    for ( const Json::Value &entry : doc )
      entries.push_back( entry );
  }
  return entries;
}

} // namespace

// ---------------------------------------------------------------------------
// B. Requirement synthesis
// ---------------------------------------------------------------------------

TEST_CASE( "finding codes map onto the closed requirement vocabulary", "[repair][reqs]" )
{
  CHECK( requirementKindForFindingCode( "CRS_MISMATCH" ) == "crs_align" );
  CHECK( requirementKindForFindingCode( "GRID_MISMATCH" ) == "grid_align" );
  CHECK( requirementKindForFindingCode( "INVALID_RADIOMETRY" ) == "radiometric_state" );
  CHECK( requirementKindForFindingCode( "CALIBRATION_MISMATCH" ) == "calibration_domain" );
  CHECK( requirementKindForFindingCode( "POLARIZATION_MISMATCH" ) == "polarization_select" );
  CHECK( requirementKindForFindingCode( "MODALITY_MISMATCH" ) == "modality_check" );
  CHECK( requirementKindForFindingCode( "BAND_ROLE_UNRESOLVED" ) == "band_role" );
  CHECK( requirementKindForFindingCode( "WAVELENGTH_INCOMPATIBLE" ) == "band_role" );
  CHECK( requirementKindForFindingCode( "TIME_ORDER_INVALID" ) == "temporal_align" );
  CHECK( requirementKindForFindingCode( "TEMPORAL_MISALIGNMENT" ) == "temporal_align" );
  CHECK( requirementKindForFindingCode( "ACQUISITION_DATES_MISSING" ) == "temporal_align" );
  CHECK( requirementKindForFindingCode( "DATES_NOT_ASCENDING" ) == "temporal_align" );
  CHECK( requirementKindForFindingCode( "TEMPORAL_CALENDAR_CONFLICT" ) == "temporal_align" );
  CHECK( requirementKindForFindingCode( "MODEL_INCOMPATIBLE" ) == "model_contract" );
  CHECK( requirementKindForFindingCode( "MODEL_NOT_READY" ) == "model_contract" );
  CHECK( requirementKindForFindingCode( "TRAINING_INVALID" ) == "training_data" );
  CHECK( requirementKindForFindingCode( "DATASET_NOT_FOUND" ) == "dataset_substitution" );
  CHECK( requirementKindForFindingCode( "CATEGORICAL_MISMATCH" ) == "categorical_check" );

  // The mapping is closed: anything outside the table is NOT a requirement.
  CHECK( requirementKindForFindingCode( "TOTALLY_UNKNOWN" ).empty() );
  CHECK( requirementKindForFindingCode( "" ).empty() );
}

TEST_CASE( "unknown finding codes become typed unsupported requirements, never drops",
           "[repair][reqs]" )
{
  std::vector<Json::Value> findings = { findingOf( "SPF_RADIOMETRIC_STATE_MISMATCH" ),
                                        findingOf( "CRS_MISMATCH" ) };
  std::vector<RepairRequirement> reqs;
  RepairError error;
  REQUIRE( synthesizeRequirements( findings, reqs, error ) );
  REQUIRE( reqs.size() == 2 );

  // Unknown code survives as a typed unsupported requirement (fail-closed,
  // audible — the caller sees what could not be planned, in stable order).
  const RepairRequirement *unsupported = nullptr;
  const RepairRequirement *crs = nullptr;
  for ( const RepairRequirement &r : reqs )
  {
    if ( r.kind == "crs_align" )
      crs = &r;
    if ( r.kind == requirement_kind::kUnsupported )
      unsupported = &r;
  }
  REQUIRE( crs != nullptr );
  REQUIRE( unsupported != nullptr );
  CHECK( unsupported->findingCode == "SPF_RADIOMETRIC_STATE_MISMATCH" );
  CHECK( !unsupported->unsupportedReason.empty() );
  CHECK( isKnownRequirementKind( requirement_kind::kUnsupported ) );
}

TEST_CASE( "malformed finding documents are typed invalid_input", "[repair][reqs]" )
{
  std::vector<RepairRequirement> reqs;
  RepairError error;

  std::vector<Json::Value> nonObject = { Json::Value( Json::arrayValue ) };
  CHECK( !synthesizeRequirements( nonObject, reqs, error ) );
  CHECK( error.code == "invalid_input" );

  std::vector<Json::Value> noCode = { findingOf( "x" ) };
  noCode[0].removeMember( "code" );
  CHECK( !synthesizeRequirements( noCode, reqs, error ) );
  CHECK( error.code == "invalid_input" );

  std::vector<Json::Value> badSeverity = { findingOf( "CRS_MISMATCH", "catastrophic" ) };
  CHECK( !synthesizeRequirements( badSeverity, reqs, error ) );
  CHECK( error.code == "invalid_input" );
}

TEST_CASE( "requirement order is deterministic: severity desc, code asc, subject asc",
           "[repair][reqs]" )
{
  std::vector<Json::Value> findings = {
    findingOf( "TIME_ORDER_INVALID", "warning", "b" ),
    findingOf( "CRS_MISMATCH", "error", "z" ),
    findingOf( "CRS_MISMATCH", "error", "a" ),
    findingOf( "MODEL_INCOMPATIBLE", "error", "a" ),
  };
  std::vector<RepairRequirement> reqs;
  RepairError error;
  REQUIRE( synthesizeRequirements( findings, reqs, error ) );
  REQUIRE( reqs.size() == 4 );
  CHECK( reqs[0].severity == "error" );
  CHECK( reqs[0].findingCode == "CRS_MISMATCH" );
  CHECK( reqs[0].subject == "a" );
  CHECK( reqs[1].findingCode == "CRS_MISMATCH" );
  CHECK( reqs[1].subject == "z" );
  CHECK( reqs[2].findingCode == "MODEL_INCOMPATIBLE" );
  CHECK( reqs[2].subject == "a" );
  CHECK( reqs[3].severity == "warning" );

  // Same inputs -> same requirement ids (positional, deterministic).
  std::vector<RepairRequirement> again;
  REQUIRE( synthesizeRequirements( findings, again, error ) );
  for ( std::size_t i = 0; i < reqs.size(); ++i )
    CHECK( reqs[i].requirementId == again[i].requirementId );
}

// ---------------------------------------------------------------------------
// C. Capability provider
// ---------------------------------------------------------------------------

TEST_CASE( "fake provider serves candidates per requirement kind", "[repair][provider]" )
{
  FakeProvider provider;
  provider.add( "grid_align", gridCapability( "rs:resample", "light" ) );
  provider.add( "grid_align", gridCapability( "rs:align", "medium" ) );

  REQUIRE( provider.knowsRequirementKind( "grid_align" ) );
  auto caps = provider.capabilitiesForRequirement( "grid_align" );
  REQUIRE( caps.size() == 2 );
  CHECK( caps[0]["id"].asString() == "rs:resample" );
  CHECK( caps[1]["id"].asString() == "rs:align" );

  CHECK( !provider.knowsRequirementKind( "model_contract" ) );
  CHECK( provider.capabilitiesForRequirement( "model_contract" ).empty() );
}

TEST_CASE( "json provider rejects capability entries without an id (fail-closed)",
           "[repair][provider]" )
{
  std::map<std::string, Json::Value> families;
  Json::Value entries( Json::arrayValue );
  Json::Value noId( Json::objectValue );
  noId["family"] = "preprocess";
  entries.append( noId );
  families["grid_align"] = entries;

  RepairError error;
  JsonRepairCapabilityProvider provider;
  CHECK( !JsonRepairCapabilityProvider::build( families, provider, error ) );
  CHECK( error.code == "invalid_input" );
}

TEST_CASE( "real capability knowledge documents drive candidates end to end",
           "[repair][provider][integration]" )
{
  // Real-shaped: the actual data/agent/capabilities/*.json documents, routed
  // through the closed requirement-kind -> capability-family table.
  const std::string sourceDir = std::string( CMAKE_SOURCE_DIR );
  auto entries = loadRealCapabilityEntries( sourceDir );
  REQUIRE( entries.size() > 20 );

  RepairError error;
  JsonRepairCapabilityProvider provider;
  REQUIRE( JsonRepairCapabilityProvider::buildFromCapabilityEntries( entries, provider, error ) );

  // grid: preprocess family ships rs:resample / rs:align.
  auto grid = provider.capabilitiesForRequirement( "grid_align" );
  REQUIRE( !grid.empty() );
  std::set<std::string> gridIds;
  for ( const Json::Value &e : grid )
    gridIds.insert( e["id"].asString() );
  CHECK( gridIds.count( "rs:resample" ) == 1 );
  CHECK( gridIds.count( "rs:align" ) == 1 );

  // quality mask: rs:qa_mask / rs:apply_mask are real operators.
  auto mask = provider.capabilitiesForRequirement( "quality_mask" );
  std::set<std::string> maskIds;
  for ( const Json::Value &e : mask )
    maskIds.insert( e["id"].asString() );
  CHECK( maskIds.count( "rs:qa_mask" ) == 1 );

  // temporal + model families serve their kinds.
  CHECK( !provider.capabilitiesForRequirement( "temporal_align" ).empty() );
  CHECK( !provider.capabilitiesForRequirement( "model_contract" ).empty() );

  // CRS shares the grid families; radiometry comes from preprocess.
  CHECK( !provider.capabilitiesForRequirement( "crs_align" ).empty() );
  CHECK( !provider.capabilitiesForRequirement( "radiometric_state" ).empty() );

  // Family-default documents ("family:*") are not operators and must not be
  // offered as executable candidates.
  for ( const char *kind : { "grid_align", "crs_align", "radiometric_state",
                                   "quality_mask", "temporal_align", "model_contract" } )
  {
    for ( const Json::Value &e : provider.capabilitiesForRequirement( kind ) )
      CHECK( e["id"].asString().rfind( "family:", 0 ) != 0 );
  }
}

// ---------------------------------------------------------------------------
// D. Auto policy
// ---------------------------------------------------------------------------

TEST_CASE( "auto policy requires whitelist AND shape_preserving AND facts AND autonomy",
           "[repair][policy]" )
{
  RepairPolicyContext ctx;
  ctx.role = "researcher";
  ctx.allowAutonomousExec = true;

  // A whitelisted, shape-preserving, facts-sufficient candidate -> auto.
  RepairAction auto_ok;
  auto_ok.id = "ra-1";
  auto_ok.ruleId = "align_to_reference";
  auto_ok.kind = action_kind::kCapabilityRef;
  auto_ok.operatorId = "rs:align";
  auto_ok.actionKey = "align_to_reference";
  auto_ok.riskClass = repair_risk::kShapePreserving;
  auto_ok.risk.riskClass = auto_ok.riskClass;
  auto_ok.risk.severity = "low";
  auto_ok.cost.rank = 3;
  auto_ok.factsSufficient = true;
  CHECK( evaluateRepairPolicy( auto_ok, ctx ).decision == policy_decision::kAutoExecutable );

  // Mutation oracle: drop ANY single conjunct and the decision must flip.
  RepairAction notWhitelisted = auto_ok;
  notWhitelisted.actionKey = "temporal_regularize";
  CHECK( evaluateRepairPolicy( notWhitelisted, ctx ).decision ==
         policy_decision::kNeedsConfirmation );
  CHECK( evaluateRepairPolicy( notWhitelisted, ctx ).reasonCode ==
         "action_key_not_preparation" );

  RepairAction scienceChanged = auto_ok;
  scienceChanged.riskClass = repair_risk::kScienceChanging;
  scienceChanged.risk.riskClass = scienceChanged.riskClass;
  CHECK( evaluateRepairPolicy( scienceChanged, ctx ).decision ==
         policy_decision::kNeedsConfirmation );

  RepairAction insufficient = auto_ok;
  insufficient.factsSufficient = false;
  CHECK( evaluateRepairPolicy( insufficient, ctx ).decision ==
         policy_decision::kNeedsConfirmation );
  CHECK( evaluateRepairPolicy( insufficient, ctx ).reasonCode == "facts_insufficient" );

  RepairPolicyContext noAutonomy = ctx;
  noAutonomy.allowAutonomousExec = false;
  CHECK( evaluateRepairPolicy( auto_ok, noAutonomy ).decision ==
         policy_decision::kNeedsConfirmation );
  CHECK( evaluateRepairPolicy( auto_ok, noAutonomy ).reasonCode == "autonomy_not_allowed" );

  RepairPolicyContext student = ctx;
  student.role = "";
  CHECK( evaluateRepairPolicy( auto_ok, student ).decision ==
         policy_decision::kTeachingOnly );
}

TEST_CASE( "science-changing candidates never become auto-executable, even when approved",
           "[repair][policy]" )
{
  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = true;
  ctx.scienceChangeApproved = true;

  RepairAction mask;
  mask.id = "ra-2";
  mask.kind = action_kind::kCapabilityRef;
  mask.operatorId = "rs:qa_mask";
  mask.actionKey = "apply_quality_mask";
  mask.riskClass = repair_risk::kScienceChanging;
  mask.risk.riskClass = mask.riskClass;
  mask.risk.severity = "medium";
  mask.cost.rank = 5;
  mask.factsSufficient = true;

  const RepairPolicyDecision decision = evaluateRepairPolicy( mask, ctx );
  CHECK( decision.decision == policy_decision::kNeedsConfirmation );
  CHECK( decision.reasonCode == "risk_class_not_shape_preserving" );

  // The approval is recorded, visible for audit — but changes nothing.
  RepairAction radiometric = mask;
  radiometric.riskClass = repair_risk::kRadiometric;
  radiometric.risk.riskClass = radiometric.riskClass;
  CHECK( evaluateRepairPolicy( radiometric, ctx ).decision ==
         policy_decision::kNeedsConfirmation );
}

TEST_CASE( "the preparation whitelist is exactly the harness kPreparationActions table",
           "[repair][policy]" )
{
  CHECK( isPreparationActionKey( "reproject_to_reference" ) );
  CHECK( isPreparationActionKey( "align_to_reference" ) );
  CHECK( isPreparationActionKey( "normalize_radiometry" ) );
  CHECK( isPreparationActionKey( "calibrate_consistently" ) );
  CHECK( !isPreparationActionKey( "apply_quality_mask" ) );
  CHECK( !isPreparationActionKey( "temporal_regularize" ) );
  CHECK( !isPreparationActionKey( "" ) );
}

// ---------------------------------------------------------------------------
// E. Planner — deterministic plan/result/fragment
// ---------------------------------------------------------------------------

namespace {

/// Two real findings: a grid blocker (whitelisted repair) and a radiometric
/// blocker (never auto). Deterministic inputs for the planner sections.
std::vector<Json::Value> standardFindings()
{
  Json::Value grid = findingOf( "GRID_MISMATCH" );
  grid["evidence"]["a_width"] = 512;
  Json::Value rad = findingOf( "INVALID_RADIOMETRY" );
  rad["evidence"]["states"] = "dn vs surface_reflectance";
  return { grid, rad };
}

FakeProvider standardProvider()
{
  FakeProvider p;
  p.add( "grid_align", gridCapability( "rs:align", "medium" ) );
  p.add( "grid_align", gridCapability( "rs:resample", "light" ) );
  p.add( "radiometric_state", gridCapability( "rs:radiometric_calibration", "heavy" ) );
  p.add( "radiometric_state", gridCapability( "rs:qa_mask", "medium" ) );
  return p;
}

} // namespace

TEST_CASE( "planner produces a deterministic plan/result pair from findings",
           "[repair][planner]" )
{
  FakeProvider provider = standardProvider();
  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = true;

  RepairPlannerOptions options;
  options.intent = "optical_change";

  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( standardFindings(), provider, ctx, options, out, error ) );

  // Deterministic identity: same inputs -> byte-identical plan, same id.
  RepairPlannerOutcome again;
  REQUIRE( planRepairsForFindings( standardFindings(), provider, ctx, options, again, error ) );
  const std::string planA = jsonToString( repairPlanToJson( out.plan ) );
  const std::string planB = jsonToString( repairPlanToJson( again.plan ) );
  CHECK( planA == planB );
  CHECK( out.plan.planId == again.plan.planId );
  CHECK( out.plan.planId.rfind( "srp-", 0 ) == 0 );

  // Requirements carried in requirement order with provenance.
  REQUIRE( out.plan.requirements.isArray() );
  CHECK( out.plan.requirements.size() == 2 );
  CHECK( out.plan.provenance["planner"].asString() == kPlannerId );
  CHECK( out.plan.provenance["findings_digest"].asString().size() == 16 );

  // Selected: one candidate per requirement, cheapest-first.
  REQUIRE( out.plan.selected.size() == 2 );
  CHECK( out.plan.selected[0].operatorId == "rs:resample" ); // light < medium
  CHECK( out.plan.selected[0].riskClass == repair_risk::kShapePreserving );
  CHECK( out.plan.selected[0].kind == action_kind::kCapabilityRef );
  CHECK( !out.plan.selected[0].beforeState.isNull() );
  CHECK( !out.plan.selected[0].afterState.isNull() );
  CHECK( !out.plan.selected[0].assumptions.empty() );
  CHECK( out.plan.selected[0].cost.rank >= 1 );
  CHECK( out.plan.selected[0].cost.rank <= 9 );
  CHECK( out.plan.selected[1].operatorId == "rs:qa_mask" ); // medium < heavy

  // The grid candidate is whitelisted+shape-preserving+autonomy-allowed ->
  // the policy block records it as auto_executable; the radiometric one is
  // never auto.
  CHECK( out.plan.policy["per_candidate"].isArray() );
  CHECK( out.plan.policy["per_candidate"].size() == 2 );
  CHECK( out.plan.policy["per_candidate"][0]["decision"].asString() ==
         policy_decision::kAutoExecutable );
  CHECK( out.plan.policy["per_candidate"][1]["decision"].asString() ==
         policy_decision::kNeedsConfirmation );

  // Alternatives keep the runner-ups with their full contracts.
  REQUIRE( out.plan.alternatives.isArray() );
  CHECK( out.plan.alternatives.size() == 2 );

  // Result document: deterministic, linked to the plan.
  const std::string resA = jsonToString( out.result );
  const std::string resB = jsonToString( again.result );
  CHECK( resA == resB );
  CHECK( out.result["kind"].asString() == "repair_result" );
  CHECK( out.result["schema_version"].asString() == "1.0" );
  CHECK( out.result["plan_id"].asString() == out.plan.planId );
  CHECK( out.result["status"].asString() == plan_status::kPlanned );
  CHECK( out.result["planning_only"].asBool() == true );
}

TEST_CASE( "planner orders candidates deterministically by cost then operator id",
           "[repair][planner]" )
{
  FakeProvider provider;
  provider.add( "grid_align", gridCapability( "rs:zeta", "medium" ) );
  provider.add( "grid_align", gridCapability( "rs:alpha", "medium" ) );
  provider.add( "grid_align", gridCapability( "rs:mike", "light" ) );

  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = false; // decisions still produced; nothing auto

  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( { findingOf( "GRID_MISMATCH" ) }, provider, ctx,
                                    RepairPlannerOptions{}, out, error ) );
  REQUIRE( out.plan.selected.size() == 1 );
  CHECK( out.plan.selected[0].operatorId == "rs:mike" ); // light first
  REQUIRE( out.plan.alternatives.size() == 2 );
  CHECK( out.plan.alternatives[0]["operator_id"].asString() == "rs:alpha" );
  CHECK( out.plan.alternatives[1]["operator_id"].asString() == "rs:zeta" );
}

TEST_CASE( "capability entries without a usable id or cost become typed refusals",
           "[repair][planner]" )
{
  FakeProvider provider;
  Json::Value noCost = gridCapability( "rs:nocost", "" );
  noCost["resource"].removeMember( "cost_class" );
  provider.add( "grid_align", noCost );
  provider.add( "grid_align", gridCapability( "rs:ok", "light" ) );

  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = true;

  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( { findingOf( "GRID_MISMATCH" ) }, provider, ctx,
                                    RepairPlannerOptions{}, out, error ) );

  // The cost-less entry is refused, auditable, and NOT silently dropped.
  bool foundRefusal = false;
  for ( const Json::Value &alt : out.plan.alternatives )
  {
    if ( alt["operator_id"].asString() == "rs:nocost" )
    {
      foundRefusal = true;
      CHECK( alt["refusal_cause"].asString() == "cost_unknown" );
      CHECK( alt["cost"]["rank"].asInt() == 0 );
      CHECK( alt["facts_sufficient"].asBool() == false );
    }
  }
  CHECK( foundRefusal );
  CHECK( out.plan.selected[0].operatorId == "rs:ok" );
}

TEST_CASE( "requirements the provider does not know surface typed no-candidate causes",
           "[repair][planner]" )
{
  FakeProvider provider; // serves nothing

  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = true;

  RepairPlannerOptions options;
  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( { findingOf( "GRID_MISMATCH" ) }, provider, ctx, options,
                                    out, error ) );
  CHECK( out.plan.status == plan_status::kNoSafeRepair );
  CHECK( out.plan.resolvesAllBlockers == false );
  REQUIRE( out.plan.noSafeRepair.isObject() );
  CHECK( out.plan.noSafeRepair["cause"].asString() == "no_candidate" );
  CHECK( out.plan.noSafeRepair["requirement_id"].asString() == out.plan.unresolved[0]["requirement_id"].asString() );
  REQUIRE( out.plan.unresolved.size() == 1 );
  CHECK( out.plan.selected.empty() );

  // Unknown (never-mapped) requirement kind -> its own typed cause.
  RepairPlannerOutcome out2;
  REQUIRE( planRepairsForFindings( { findingOf( "MYSTERY_CODE" ) }, provider, ctx, options,
                                    out2, error ) );
  REQUIRE( out2.plan.unresolved.size() == 1 );
  CHECK( out2.plan.unresolved[0]["cause"].asString() == "unsupported_finding" );
  CHECK( out2.plan.status == plan_status::kNoSafeRepair );
}

TEST_CASE( "planner budgets truncate deterministically and count the truncation",
           "[repair][planner]" )
{
  FakeProvider provider = standardProvider();

  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = false;

  RepairPlannerOptions options;
  options.maxRequirements = 1;      // second finding is cut, audibly
  options.maxCandidatesPerRequirement = 1;
  options.maxTotalCandidates = 1;

  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( standardFindings(), provider, ctx, options, out, error ) );

  CHECK( out.plan.requirements.size() == 1 ); // truncation applied
  CHECK( out.plan.bounds["requirements_truncated"].asBool() == true );
  CHECK( out.plan.bounds["max_requirements"].asInt() == 1 );
  CHECK( out.plan.bounds["dropped_requirements"].asInt() == 1 );
  CHECK( out.plan.bounds["truncated_candidates"].asInt() >= 1 );
  CHECK( out.plan.selected.size() == 1 );
  CHECK( out.plan.alternatives.empty() ); // candidates-per-requirement cap hit

  // Determinism survives truncation.
  RepairPlannerOutcome again;
  REQUIRE( planRepairsForFindings( standardFindings(), provider, ctx, options, again, error ) );
  CHECK( jsonToString( repairPlanToJson( out.plan ) ) ==
         jsonToString( repairPlanToJson( again.plan ) ) );
}

TEST_CASE( "planner refuses malformed input with typed errors", "[repair][planner]" )
{
  FakeProvider provider = standardProvider();
  RepairPolicyContext ctx;
  RepairPlannerOutcome out;
  RepairError error;

  std::vector<Json::Value> bad = { Json::Value( 42 ) };
  CHECK( !planRepairsForFindings( bad, provider, ctx, RepairPlannerOptions{}, out, error ) );
  CHECK( error.code == "invalid_input" );

  std::vector<Json::Value> empty;
  CHECK( !planRepairsForFindings( empty, provider, ctx, RepairPlannerOptions{}, out, error ) );
  CHECK( error.code == "invalid_input" );
}

TEST_CASE( "fragments isolate one requirement's slice, deterministically",
           "[repair][planner]" )
{
  FakeProvider provider = standardProvider();
  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = true;

  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( standardFindings(), provider, ctx, RepairPlannerOptions{},
                                   out, error ) );
  const std::string requirementId = out.plan.requirements[0]["requirement_id"].asString();

  Json::Value fragment;
  REQUIRE( repairPlanFragment( out.plan, requirementId, fragment, error ) );
  CHECK( fragment["kind"].asString() == "repair_fragment" );
  CHECK( fragment["schema_version"].asString() == "1.0" );
  CHECK( fragment["plan_id"].asString() == out.plan.planId );
  CHECK( fragment["plan_fingerprint"].asString() == repairPlanFingerprint( out.plan ) );
  CHECK( fragment["planning_only"].asBool() == true );
  CHECK( fragment["requirement"]["requirement_id"].asString() == requirementId );
  CHECK( fragment["candidates"].isArray() );
  CHECK( fragment["candidates"].size() >= 1 );

  Json::Value again;
  REQUIRE( repairPlanFragment( out.plan, requirementId, again, error ) );
  CHECK( jsonToString( fragment ) == jsonToString( again ) );

  // Unknown requirement id -> typed error, never an empty-success fragment.
  Json::Value bogus;
  CHECK( !repairPlanFragment( out.plan, "req-does-not-exist", bogus, error ) );
  CHECK( error.code == "invalid_input" );
}

// ---------------------------------------------------------------------------
// F. Views — teaching leakage
// ---------------------------------------------------------------------------

TEST_CASE( "teaching view withholds every executable value, recursively",
           "[repair][view]" )
{
  FakeProvider provider = standardProvider();
  RepairPolicyContext ctx;
  ctx.role = "student"; // teaching surface
  ctx.allowAutonomousExec = false;

  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( standardFindings(), provider, ctx, RepairPlannerOptions{},
                                   out, error ) );
  const Json::Value planDoc = repairPlanToJson( out.plan );

  Json::Value teaching;
  REQUIRE( teachingRepairView( planDoc, teaching, error ) );
  CHECK( teaching["view"].asString() == "teaching" );
  CHECK( teaching["planning_only"].asBool() == true );
  CHECK( teaching["plan_id"].asString() == out.plan.planId );
  // Why/what/risk stay visible for the student...
  CHECK( containsKeyRecursive( teaching, "risk_class" ) );
  CHECK( containsKeyRecursive( teaching, "information_loss" ) );
  CHECK( containsKeyRecursive( teaching, "assumptions" ) );
  // ...but nothing executable survives.
  CHECK( !containsKeyRecursive( teaching, "params" ) );
  CHECK( !containsKeyRecursive( teaching, "operator_id" ) );
  CHECK( !containsKeyRecursive( teaching, "action_key" ) );

  // Leakage oracle: a marker value that only lives in executable fields must
  // not appear anywhere in the serialized teaching view.
  RepairPlannerOutcome marked = out;
  for ( RepairAction &action : marked.plan.selected )
    action.params["secret_marker"] = "TOPSECRET-XYZZY";
  Json::Value markedDoc = repairPlanToJson( marked.plan );
  Json::Value markedTeaching;
  REQUIRE( teachingRepairView( markedDoc, markedTeaching, error ) );
  CHECK( jsonToString( markedTeaching ).find( "TOPSECRET-XYZZY" ) == std::string::npos );
}

TEST_CASE( "agent view keeps the full contract and marks planning-only",
           "[repair][view]" )
{
  RepairPlan plan;
  plan.intent = "optical_change";
  plan.subject = "asset-1";
  plan.status = plan_status::kPlanned;
  plan.selected.push_back( [] {
    RepairAction a = [] {
      RepairAction x;
      x.id = "ra-1";
      x.kind = action_kind::kCapabilityRef;
      x.operatorId = "rs:align";
      x.actionKey = "align_to_reference";
      x.riskClass = repair_risk::kShapePreserving;
      x.risk.riskClass = x.riskClass;
      x.risk.severity = "low";
      x.cost.rank = 3;
      return x;
    }();
    return a;
  }() );
  assignRepairPlanIdentity( plan );
  const Json::Value planDoc = repairPlanToJson( plan );

  RepairError error;
  Json::Value agent;
  REQUIRE( agentRepairView( planDoc, agent, error ) );
  CHECK( agent["planning_only"].asBool() == true );
  CHECK( agent["view"].asString() == "agent" );
  CHECK( containsKeyRecursive( agent, "operator_id" ) );
  CHECK( containsKeyRecursive( agent, "params" ) );

  // Views are fail-closed readers: garbage in, typed error out.
  Json::Value garbage;
  Json::Value sink;
  CHECK( !teachingRepairView( Json::Value( Json::arrayValue ), sink, error ) );
  CHECK( error.code == "invalid_document" );
  Json::Value wrongKind = planDoc;
  wrongKind["kind"] = "something_else";
  CHECK( !agentRepairView( wrongKind, sink, error ) );
  CHECK( error.code == "invalid_document" );
}

// ---------------------------------------------------------------------------
// G. Planning state — idempotency, tamper evidence, bounds
// ---------------------------------------------------------------------------

TEST_CASE( "planning state is idempotent and tamper-evident", "[repair][state]" )
{
  RepairPlanningState state;
  RepairError error;

  RepairPlanningRecord record;
  record.subject = "asset-1";
  record.findingsDigest = "0123456789abcdef";
  record.planId = "srp-aaaabbbbccccdddd";
  record.planFingerprint = "aaaabbbbccccdddd";
  record.status = plan_status::kPlanned;
  record.sequence = 1;
  REQUIRE( state.record( record, error ) );

  // Same (subject, digest) + same fingerprint -> idempotent no-op.
  CHECK( state.record( record, error ) );

  // Same key, DIFFERENT fingerprint -> typed conflict, state unchanged.
  RepairPlanningRecord conflicting = record;
  conflicting.planFingerprint = "eeeeffff00001111";
  conflicting.planId = "srp-eeeeffff00001111";
  CHECK( !state.record( conflicting, error ) );
  CHECK( error.code == "invalid_state" );
  CHECK( state.size() == 1 );

  // A stored plan document verifies against its fingerprint; a mutated one
  // does not (tamper evidence).
  RepairPlan plan;
  plan.intent = "optical_change";
  plan.subject = "asset-1";
  plan.status = plan_status::kPlanned;
  assignRepairPlanIdentity( plan );
  RepairPlanningRecord linked;
  linked.subject = plan.subject;
  linked.findingsDigest = "fedcba9876543210";
  linked.planId = plan.planId;
  linked.planFingerprint = repairPlanFingerprint( plan );
  linked.status = plan.status;
  linked.sequence = 2;
  REQUIRE( state.record( linked, error ) );

  const Json::Value doc = repairPlanToJson( plan );
  CHECK( state.verifyPlan( doc ) == RepairPlanningState::Verify::Match );
  Json::Value tampered = doc;
  tampered["intent"] = "silent_substitution";
  CHECK( state.verifyPlan( tampered ) == RepairPlanningState::Verify::Tampered );
  Json::Value unknownPlan = doc;
  unknownPlan["plan_id"] = "srp-0000000000000000";
  unknownPlan["subject"] = "other-asset";
  CHECK( state.verifyPlan( unknownPlan ) == RepairPlanningState::Verify::Unknown );
}

TEST_CASE( "planning state round-trips with a digest and rejects tampering",
           "[repair][state]" )
{
  RepairPlanningState state;
  RepairError error;
  RepairPlanningRecord record;
  record.subject = "asset-9";
  record.findingsDigest = "aaaaaaaaaaaaaaaa";
  record.planId = "srp-1111222233334444";
  record.planFingerprint = "1111222233334444";
  record.status = plan_status::kNoSafeRepair;
  record.sequence = 7;
  REQUIRE( state.record( record, error ) );

  const std::string bytes = jsonToString( state.toJson() );
  Json::Value doc;
  {
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream stream( bytes );
    REQUIRE( Json::parseFromStream( builder, stream, &doc, &errs ) );
  }
  RepairPlanningState reloaded;
  REQUIRE( RepairPlanningState::fromJson( doc, reloaded, error ) );
  CHECK( reloaded.size() == 1 );
  CHECK( jsonToString( reloaded.toJson() ) == bytes );

  // Tamper with one byte of the stored digest -> reload refuses.
  Json::Value evil = doc;
  evil["records"][0]["plan_fingerprint"] = "9999999999999999";
  RepairPlanningState refuse;
  CHECK( !RepairPlanningState::fromJson( evil, refuse, error ) );
  CHECK( error.code == "tampered_state" );
}

TEST_CASE( "planning state bounds its history deterministically", "[repair][state]" )
{
  RepairPlanningState state;
  RepairError error;
  const std::size_t cap = 8;
  for ( int i = 0; i < 20; ++i )
  {
    RepairPlanningRecord r;
    r.subject = "asset-" + std::to_string( i );
    r.findingsDigest = "dddddddddddddddd";
    const std::string digits = std::to_string( 100000 + i );
    r.planId = "srp-" + digits + "aaaaaaaaaa";
    r.planFingerprint = digits + "aaaaaaaaaa";
    r.status = plan_status::kPlanned;
    r.sequence = i;
    REQUIRE( state.record( r, error ) );
  }
  CHECK( state.size() == cap );

  // Deterministic eviction: exactly the lowest-sequence records are gone.
  Json::Value doc = state.toJson();
  CHECK( doc["evicted"].asInt() == 12 );
  std::set<int> sequences;
  for ( const Json::Value &r : doc["records"] )
    sequences.insert( r["sequence"].asInt() );
  CHECK( sequences.size() == cap );
  CHECK( *sequences.begin() == 12 );
  CHECK( *sequences.rbegin() == 19 );
}

TEST_CASE( "state accepts real plan/result pairs end to end (integration)",
           "[repair][state][integration]" )
{
  FakeProvider provider = standardProvider();
  RepairPolicyContext ctx;
  ctx.role = "agent";
  ctx.allowAutonomousExec = true;

  RepairPlannerOutcome out;
  RepairError error;
  REQUIRE( planRepairsForFindings( standardFindings(), provider, ctx, RepairPlannerOptions{},
                                   out, error ) );

  RepairPlanningState state;
  RepairPlanningRecord record;
  record.subject = out.plan.subject;
  record.findingsDigest = out.result["findings_digest"].asString();
  record.planId = out.plan.planId;
  record.planFingerprint = repairPlanFingerprint( out.plan );
  record.status = out.plan.status;
  record.sequence = 1;
  REQUIRE( state.record( record, error ) );
  CHECK( state.verifyPlan( repairPlanToJson( out.plan ) ) ==
         RepairPlanningState::Verify::Match );
  CHECK( state.resultDigestMatches( out.result ) );
}
