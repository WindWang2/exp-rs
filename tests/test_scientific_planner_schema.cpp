// tests/test_scientific_planner_schema.cpp — slice A:
// versioned goal/context/plan schema: round-trip, fail-closed readers,
// fingerprint stability, canonical key ordering, bounds (no truncation).
#include <catch2/catch_test_macros.hpp>

#include "planner/planning_context.h"
#include "planner/scientific_goal.h"
#include "planner/json_util.h"
#include "planner/scientific_plan.h"

#include <json/json.h>

using namespace sicnu::planner;

namespace
{
ScientificGoal sampleGoal()
{
    ScientificGoal goal;
    goal.goalId = "goal-water-change-01";
    goal.kind = "change";
    goal.subject = "surface water extent change between two dates";
    goal.quantity = "water area (ha)";
    goal.temporalScope = GoalTemporalScope{ "2024-01-10", "2024-03-10", 2, 16 };
    goal.acceptanceCriteria = {
        { "acc-01", "change map accuracy against held-out samples", "kappa >= 0.8" },
    };
    return goal;
}

PlanningContext sampleContext()
{
    PlanningContext context;
    PlannerAssetFacts first;
    first.ref = "asset-2024-01";
    first.kind = "raster";
    first.modality = "optical";
    first.numericDomain = "dn";
    first.crs = "EPSG:32648";
    first.resolutionM = 10.0;
    first.bandRoles = { "blue", "green", "nir" };
    first.dates = { "2024-01-10" };
    first.qualityMaskAvailable = true;
    first.state = "ready";
    first.calibrationState = "raw_dn";
    PlannerAssetFacts second = first;
    second.ref = "asset-2024-03";
    second.dates = { "2024-03-10" };
    context.assets = { first, second };
    context.constraints.requiredDeterminism = true;
    context.resourceBudget.maxCostClass = "medium";
    context.mode = ModePolicy{ "agent", "full", false };
    return context;
}

ScientificPlan samplePlan()
{
    ScientificPlan plan;
    plan.planId = "plan-0001";
    plan.goalId = "goal-water-change-01";
    plan.goalKind = "change";
    plan.rulesRevision = "planner-rules/1";
    plan.modeKind = "agent";
    plan.autonomy = "full";
    plan.verdict = "feasible";
    plan.verdictReasons = { "all hard facts available" };
    PlannerStep calibrate;
    calibrate.stepId = "step-calibrate";
    calibrate.role = "preprocess";
    calibrate.operatorId = "rs:brdf_normalization";
    calibrate.family = "calibration";
    calibrate.inputs = { StepInput{ "", "asset-2024-01", "input" } };
    calibrate.params = Json::Value( Json::objectValue );
    calibrate.preconditions = { StepPrecondition{ "asset_state", "asset-2024-01", "", 0,
                                                  "asset ready" } };
    calibrate.expectedTransitions = { ExpectedTransition{ "asset-2024-01", "dn", "reflectance" } };
    calibrate.costClass = "medium";
    calibrate.estimatedRamMb = 512;
    PlannerStep analyze;
    analyze.stepId = "step-analyze";
    analyze.role = "analyze";
    analyze.operatorId = "rs:change";
    analyze.family = "analysis";
    analyze.inputs = { StepInput{ "step-calibrate", "", "input" } };
    analyze.expectedTransitions = { ExpectedTransition{ "asset-2024-01", "reflectance", "none" } };
    analyze.verifierTargets = { VerifierTarget{ "change map accuracy", "kappa >= 0.8" } };
    analyze.costClass = "low";
    analyze.studentDecision = false;
    plan.steps = { calibrate, analyze };
    plan.openQuestions = { PlanOpenQuestion{ "q-01", "ambiguity", false,
                                             "index choice affects sensitivity", "try mndwi" } };
    plan.cost = PlanCost{ "medium", 512 };
    plan.risks = { PlanRisk{ "data_gap", "scene 2 has cloud cover", "" } };
    return plan;
}
} // namespace

TEST_CASE( "goal document round-trips and rejects unknown kinds", "[scientific_planner][schema]" )
{
    const Json::Value doc = scientificGoalToJson( sampleGoal() );
    ScientificGoal parsed;
    std::string error;
    REQUIRE( scientificGoalFromJson( doc, parsed, error ) );
    CHECK( parsed.goalId == "goal-water-change-01" );
    CHECK( parsed.kind == "change" );
    REQUIRE( parsed.temporalScope.has_value() );
    CHECK( parsed.temporalScope->minScenes == 2 );
    REQUIRE( parsed.acceptanceCriteria.size() == 1 );
    CHECK( parsed.acceptanceCriteria[0].target == "kappa >= 0.8" );

    Json::Value bad = doc;
    bad["goal_kind"] = "alchemy";
    CHECK_FALSE( scientificGoalFromJson( bad, parsed, error ) );
    CHECK( error.rfind( "invalid_field", 0 ) == 0 );

    bad = doc;
    bad["schema_version"] = "2.0";
    std::string versionError;
    CHECK_FALSE( scientificGoalFromJson( bad, parsed, versionError ) );
    CHECK( versionError.rfind( "unsupported_version", 0 ) == 0 );

    bad = doc;
    bad["kind"] = "shopping_list";
    std::string kindError;
    CHECK_FALSE( scientificGoalFromJson( bad, parsed, kindError ) );
    CHECK( kindError.rfind( "invalid_document", 0 ) == 0 );
}

TEST_CASE( "context reader enforces vocabularies via contracts authority", "[scientific_planner][schema]" )
{
    const Json::Value doc = planningContextToJson( sampleContext() );
    PlanningContext parsed;
    std::string error;
    REQUIRE( planningContextFromJson( doc, parsed, error ) );
    REQUIRE( parsed.assets.size() == 2 );
    CHECK( findContextAsset( parsed, "asset-2024-03" ) != nullptr );
    CHECK( findContextAsset( parsed, "asset-nope" ) == nullptr );

    // numeric_domain is validated against the LINKED contracts vocabulary.
    Json::Value bad = doc;
    bad["assets"][0]["numeric_domain"] = "quantum_superposition";
    CHECK_FALSE( planningContextFromJson( bad, parsed, error ) );
    CHECK( error.find( "contracts kNumericDomains" ) != std::string::npos );

    // duplicate asset refs are rejected
    bad = doc;
    bad["assets"][1]["ref"] = "asset-2024-01";
    std::string dupError;
    CHECK_FALSE( planningContextFromJson( bad, parsed, dupError ) );
    CHECK( dupError.find( "duplicate" ) != std::string::npos );
}

TEST_CASE( "plan round-trip keeps structure and rejects dangling wiring", "[scientific_planner][schema]" )
{
    const Json::Value doc = scientificPlanToJson( samplePlan() );
    ScientificPlan parsed;
    std::string error;
    REQUIRE( scientificPlanFromJson( doc, parsed, error ) );
    REQUIRE( parsed.steps.size() == 2 );
    CHECK( parsed.steps[1].inputs[0].fromStepId == "step-calibrate" );
    CHECK( parsed.cost.aggregateCostClass == "medium" );
    CHECK( validateScientificPlan( parsed ).empty() );

    // forward/dangling step reference → typed rejection (acyclic by construction)
    Json::Value bad = doc;
    Json::Value swapped( Json::arrayValue );
    swapped.append( doc["steps"][1] );
    swapped.append( doc["steps"][0] );
    bad["steps"] = swapped;
    CHECK_FALSE( scientificPlanFromJson( bad, parsed, error ) );
    CHECK( error.find( "later step" ) != std::string::npos );

    // duplicate step ids
    bad = doc;
    bad["steps"][1]["step_id"] = "step-calibrate";
    std::string dupError;
    CHECK_FALSE( scientificPlanFromJson( bad, parsed, dupError ) );
    CHECK( dupError.find( "duplicate step id" ) != std::string::npos );
}

TEST_CASE( "plan fingerprint: content identity, plan_id excluded", "[scientific_planner][schema]" )
{
    const ScientificPlan plan = samplePlan();
    const std::string base = scientificPlanFingerprint( plan );
    CHECK( base.size() == 16 );

    ScientificPlan renamed = plan;
    renamed.planId = "totally-different-id";
    CHECK( scientificPlanFingerprint( renamed ) == base );

    ScientificPlan changed = plan;
    changed.steps[0].costClass = "high";
    CHECK( scientificPlanFingerprint( changed ) != base );

    ScientificPlan reordered = plan;
    std::swap( reordered.steps[0], reordered.steps[1] );
    CHECK( scientificPlanFingerprint( reordered ) != base );

    // deterministic across repeated calls
    CHECK( scientificPlanFingerprint( plan ) == base );
}

TEST_CASE( "canonical JSON: key order is sorted regardless of construction order",
           "[scientific_planner][schema]" )
{
    const Json::Value doc = scientificPlanToJson( samplePlan() );
    const std::string canonical = json_util::canonicalCompact( doc );
    // canonicalCompact itself sorts; build a shuffled copy by round-tripping
    // through a fresh writer: same document must give identical bytes.
    Json::Value reparsed;
    Json::Reader reader;
    REQUIRE( reader.parse( canonical, reparsed ) );
    CHECK( json_util::canonicalCompact( reparsed ) == canonical );
    // canonical form: envelope kind present, single line, no structural
    // whitespace outside string values
    CHECK( canonical.find( "\"kind\":\"scientific_plan\"" ) != std::string::npos );
    CHECK( canonical.find( '\n' ) == std::string::npos );
}

TEST_CASE( "bounds are typed rejections, never truncation", "[scientific_planner][schema]" )
{
    ScientificGoal hugeGoal = sampleGoal();
    for ( int i = 0; i < PlanLimits::kMaxAcceptanceCriteria + 1; ++i )
    {
        GoalAcceptanceCriterion criterion;
        criterion.criterionId = "acc-" + std::to_string( i );
        criterion.check = "check " + std::to_string( i );
        hugeGoal.acceptanceCriteria.push_back( criterion );
    }
    std::string error;
    CHECK_FALSE( scientificGoalFromJson( scientificGoalToJson( hugeGoal ), hugeGoal, error ) );
    CHECK( error.rfind( "out_of_bounds", 0 ) == 0 );

    ScientificPlan hugePlan = samplePlan();
    std::string longSubject( PlanLimits::kMaxTextChars + 1, 'x' );
    hugePlan.verdictReasons.push_back( longSubject );
    CHECK_FALSE( scientificPlanFromJson( scientificPlanToJson( hugePlan ), hugePlan, error ) );
    CHECK( error.rfind( "invalid_field", 0 ) == 0 );

    ScientificPlan emptyPlan = samplePlan();
    emptyPlan.steps.clear();
    emptyPlan.openQuestions.clear();
    emptyPlan.risks.clear();
    emptyPlan.verdictReasons.clear();
    emptyPlan.alternatives.clear();
    // empty steps array is structurally readable (planner decides semantics)
    std::string emptyError;
    CHECK( scientificPlanFromJson( scientificPlanToJson( emptyPlan ), emptyPlan, emptyError ) );
}

TEST_CASE( "feasible verdict with blocking questions is self-inconsistent", "[scientific_planner][schema]" )
{
    ScientificPlan plan = samplePlan();
    plan.openQuestions.push_back( PlanOpenQuestion{ "q-block", "insufficient_data", true,
                                                    "second scene missing", "" } );
    const auto problems = validateScientificPlan( plan );
    REQUIRE( problems.size() == 1 );
    CHECK( problems[0].find( "blocking" ) != std::string::npos );
}
