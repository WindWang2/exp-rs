// tests/test_scientific_planner_review.cpp — independent adversarial review
// regression pins + mutation oracles. Each MUTATION section describes a
// concrete wrong implementation; the test proves the suite kills it.
#include <catch2/catch_test_macros.hpp>

#include "planner/plan_ir_projection.h"
#include "planner/plan_teaching.h"
#include "planner/json_util.h"
#include "planner/planner_core.h"
#include "planner/planner_rules.h"
#include "planner/planner_proposal.h"

#include <json/json.h>

#include <map>

using namespace sicnu::planner;

namespace
{
class FakeProvider : public CapabilityProvider
{
public:
    void add( const std::string &family, PlannerCapability capability )
    {
        capability.family = family;
        byFamily_[family].push_back( std::move( capability ) );
    }
    std::vector<PlannerCapability> capabilitiesForFamily( const std::string &family ) const override
    {
        auto it = byFamily_.find( family );
        return it == byFamily_.end() ? std::vector<PlannerCapability>{} : it->second;
    }

private:
    std::map<std::string, std::vector<PlannerCapability>> byFamily_;
};

PlannerCapability capability( const std::string &operatorId, const std::string &costClass,
                              const std::string &inputDomain, const std::string &outputDomain,
                              bool deterministic = true )
{
    PlannerCapability capability;
    capability.operatorId = operatorId;
    capability.costClass = costClass;
    capability.inputDomain = inputDomain;
    capability.outputDomain = outputDomain;
    capability.deterministic = deterministic;
    capability.estimatedRamMb = 256;
    return capability;
}

ScientificGoal waterGoal()
{
    ScientificGoal goal;
    goal.goalId = "goal-water";
    goal.kind = "change";
    goal.subject = "water change";
    return goal;
}

PlanningContext twoDnAssets()
{
    PlanningContext context;
    for ( const char *ref : { "asset-a", "asset-b" } )
    {
        PlannerAssetFacts asset;
        asset.ref = ref;
        asset.kind = "raster";
        asset.numericDomain = "dn";
        asset.state = "ready";
        context.assets.push_back( asset );
    }
    context.mode = ModePolicy{ "agent", "full", false };
    return context;
}

Json::Value proposalDoc()
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = "scientific_plan";
    doc["schema_version"] = "1.0";
    doc["plan_id"] = "plan-x";
    doc["goal_id"] = "goal-water";
    doc["goal_kind"] = "change";
    doc["rules_revision"] = "planner-rules/1";
    Json::Value mode( Json::objectValue );
    mode["kind"] = "agent";
    mode["autonomy"] = "full";
    doc["mode"] = mode;
    doc["verdict"] = "feasible";
    Json::Value analyze( Json::objectValue );
    analyze["step_id"] = "step-analyze-01";
    analyze["role"] = "analyze";
    analyze["operator_id"] = "rs:mndwi";
    analyze["family"] = "analysis";
    analyze["cost_class"] = "low";
    Json::Value steps( Json::arrayValue );
    steps.append( analyze );
    doc["steps"] = steps;
    Json::Value cost( Json::objectValue );
    cost["aggregate_cost_class"] = "low";
    doc["cost"] = cost;
    return doc;
}
} // namespace

// ---------------------------------------------------------------------------
// MUTATION 1 — calibration gate faked as success:
//   wrong impl: when no bridge exists, "insert a calibration step anyway"
//   or silently downgrade the asset domain to make the plan look feasible.
//   Killed by: the gate test demands NO calibration step and a blocking
//   question when no verified bridge exists (test_scientific_planner_core).
// Here we additionally pin the ACCEPTED bridge transitions against contracts:
TEST_CASE( "mutation oracle: a fabricated bridge operator cannot produce a lawful plan",
           "[scientific_planner][review][mutation1]" )
{
    ScientificGoal goal = waterGoal();
    goal.kind = "measurement"; // probability analysis outputs are lawful here
    const PlanningContext context = twoDnAssets();
    FakeProvider provider;
    provider.add( "calibration", capability( "rs:hallucinated_bridge", "low", "dn", "reflectance" ) );
    provider.add( "analysis", capability( "rs:ace", "low", "reflectance", "probability" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    const ScientificPlan &plan = *result.primary();
    // the hallucinated bridge must NOT appear in any step
    for ( const auto &step : plan.steps )
        CHECK( step.operatorId != "rs:hallucinated_bridge" );
    // and the gap must be typed, not faked away
    bool unbridgeable = false;
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.blocking && question.kind == "insufficient_data"
             && question.detail.find( "dn→reflectance" ) != std::string::npos )
            unbridgeable = true;
    }
    CHECK( unbridgeable );
    CHECK( plan.verdict == "feasible_with_gaps" );
}

// ---------------------------------------------------------------------------
// MUTATION 2 — teaching masking leak:
//   wrong impl: hiddenAnswer view masks only the FIRST student-decision step,
//   or keeps one answer key, or reports masking_applied=true while leaking.
//   Killed by: multi-step student decisions + exact key-absence assertions.
TEST_CASE( "mutation oracle: every student-decision step is masked, none leak",
           "[scientific_planner][review][mutation2]" )
{
    ScientificPlan plan;
    plan.planId = "plan-leak";
    plan.goalId = "goal-leak";
    plan.goalKind = "change";
    plan.rulesRevision = kPlannerRulesRevision;
    plan.modeKind = "teaching";
    plan.autonomy = "guided";
    plan.verdict = "feasible";

    for ( const char *id : { "step-analyze-01", "step-analyze-02" } )
    {
        PlannerStep step;
        step.stepId = id;
        step.role = "analyze";
        step.operatorId = "rs:mndwi";
        step.family = "analysis";
        step.costClass = "low";
        step.studentDecision = true;
        step.params[std::string( id ) + "_threshold"] = "secret-0.42";
        plan.steps.push_back( step );
    }
    plan.cost = PlanCost{ "low", 512 };

    const TeachingViews views = teachingViews( plan, ModePolicy{ "teaching", "guided", false } );
    const std::string hidden = json_util::canonicalCompact( views.hiddenAnswer );
    CHECK( views.hiddenAnswer["masking_applied"].asBool() );
    CHECK( hidden.find( "_threshold" ) == std::string::npos );
    CHECK( hidden.find( "secret-0.42" ) == std::string::npos );
    REQUIRE( views.hiddenAnswer["steps"].size() == 2 );
    CHECK( views.hiddenAnswer["steps"][0]["params"]["masked"].asBool() );
    CHECK( views.hiddenAnswer["steps"][1]["params"]["masked"].asBool() );

    // minimal autonomy masks too; full autonomy does not
    CHECK( teachingViews( plan, ModePolicy{ "teaching", "minimal", false } )
               .hiddenAnswer["masking_applied"]
               .asBool() );
    CHECK( json_util::canonicalCompact(
               teachingViews( plan, ModePolicy{ "teaching", "full", false } ).hiddenAnswer )
               .find( "secret-0.42" )
           != std::string::npos );
}

// ---------------------------------------------------------------------------
// MUTATION 3 — proposal validator fails open:
//   wrong impl: accept proposals whose transition domains lie about contracts
//   (or accept with an empty rejection code set). Killed by: typed rejection
//   assertions below plus the vocabulary closure in the vocab test.
TEST_CASE( "mutation oracle: contract-lying transitions and verdict flips cannot pass",
           "[scientific_planner][review][mutation3]" )
{
    const ScientificGoal goal = waterGoal();
    PlanningContext context = twoDnAssets();
    context.mode = ModePolicy{ "agent", "full", false };
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );

    Json::Value doc = proposalDoc();
    Json::Value transition( Json::objectValue );
    transition["asset_ref"] = "asset-a";
    transition["from_domain"] = "dn";
    transition["to_domain"] = "temperature"; // contract says index
    Json::Value transitions( Json::arrayValue );
    transitions.append( transition );
    doc["steps"][0]["expected_transitions"] = transitions;
    const auto outcome = validateProposal( doc, goal, context, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_unverified_transition" );
    CHECK_FALSE( outcome.rejection.reasons.empty() );

    // self-inconsistent verdict cannot pass either
    Json::Value flip = proposalDoc();
    flip["verdict"] = "infeasible";
    const auto flipped = validateProposal( flip, goal, context, PlannerProviders{ &provider } );
    CHECK_FALSE( flipped.accepted );
    CHECK( flipped.rejection.code == "planner:proposal_inconsistent_verdict" );
}

// ---------------------------------------------------------------------------
// MUTATION 4 — IR projection fakes a verdict:
//   wrong impl: project radiance/temperature as "surface_reflectance" or
//   drop the warning. Killed by: unknown-token + warning assertions
//   (test_scientific_planner_ir). Here we pin the negative: no warning-free
//   projection of a degrading domain.
TEST_CASE( "mutation oracle: degrading domain projection always carries its warning",
           "[scientific_planner][review][mutation4]" )
{
    for ( const char *domain : { "radiance", "temperature", "amplitude", "phase", "displacement",
                                 "probability", "features", "count", "vector", "table" } )
    {
        INFO( "domain " << domain );
        CHECK( domainProjectsToUnknown( domain ) );
        ScientificPlan plan;
        plan.planId = "plan-degrade";
        plan.goalId = "goal-degrade";
        plan.goalKind = "measurement";
        plan.rulesRevision = kPlannerRulesRevision;
        plan.modeKind = "agent";
        plan.autonomy = "full";
        plan.verdict = "feasible";
        PlannerStep step;
        step.stepId = "step-analyze-01";
        step.role = "analyze";
        step.operatorId = "rs:mndwi";
        step.family = "analysis";
        step.costClass = "low";
        step.expectedTransitions = { ExpectedTransition{ "asset-a", "dn", domain } };
        plan.steps.push_back( step );
        plan.cost = PlanCost{ "low", 0 };
        std::vector<std::string> warnings;
        std::string error;
        const Json::Value doc = projectPlanToIr( plan, &warnings, &error );
        REQUIRE( error.empty() );
        CHECK( doc["nodes"][0]["outputs"][0]["artifact"]["numeric_domain"].asString()
               == std::string( "unknown" ) );
        CHECK_FALSE( warnings.empty() );
    }
}

// ---------------------------------------------------------------------------
// Adversarial inputs (review round): hostile documents must be rejected,
// never partially consumed.
TEST_CASE( "adversarial: hostile JSON shapes are rejected fail-closed",
           "[scientific_planner][review][adversarial]" )
{
    ScientificGoal goal;
    PlanningContext context;
    std::string error;

    Json::Value goalDoc = scientificGoalToJson( waterGoal() );
    // oversized strings
    goalDoc["subject"] = std::string( PlanLimits::kMaxTextChars + 1, 'x' );
    CHECK_FALSE( scientificGoalFromJson( goalDoc, goal, error ) );
    // wrong-typed optional (additive-optional discipline: present but wrong = error)
    goalDoc = scientificGoalToJson( waterGoal() );
    goalDoc["quantity"] = 42;
    CHECK_FALSE( scientificGoalFromJson( goalDoc, goal, error ) );
    // array where object belongs
    goalDoc = scientificGoalToJson( waterGoal() );
    goalDoc["temporal_scope"] = Json::Value( Json::arrayValue );
    CHECK_FALSE( scientificGoalFromJson( goalDoc, goal, error ) );

    // context: duplicate refs, negative bounds
    const PlanningContext sample = twoDnAssets();
    Json::Value ctxDoc = planningContextToJson( sample );
    ctxDoc["assets"][1]["ref"] = "asset-a";
    CHECK_FALSE( planningContextFromJson( ctxDoc, context, error ) );
    ctxDoc = planningContextToJson( sample );
    ctxDoc["resource_budget"]["max_estimated_ram_mb"] = -5;
    CHECK_FALSE( planningContextFromJson( ctxDoc, context, error ) );

    // whole-block wrong typing is a typed error, never a silent skip
    ctxDoc = planningContextToJson( sample );
    ctxDoc["resource_budget"] = "high";
    CHECK_FALSE( planningContextFromJson( ctxDoc, context, error ) );
    ctxDoc = planningContextToJson( sample );
    ctxDoc["mode"] = Json::Value( Json::arrayValue );
    CHECK_FALSE( planningContextFromJson( ctxDoc, context, error ) );

    // absent assets array is a typed error; an EMPTY array is a legal
    // boundary (the core answers it with a typed question, see the
    // zero-ready-assets adversarial test below)
    ctxDoc = planningContextToJson( sample );
    ctxDoc.removeMember( "assets" );
    CHECK_FALSE( planningContextFromJson( ctxDoc, context, error ) );
}

TEST_CASE( "review regression: producer bounds hold (assets, candidates, cost classes)",
           "[scientific_planner][review][p0]" )
{
    // (a) >8 ready assets: aggregated-overflow question, plan still readable.
    ScientificGoal goal = waterGoal();
    goal.kind = "measurement";
    PlanningContext context;
    for ( int i = 0; i < 11; ++i )
    {
        PlannerAssetFacts asset;
        asset.ref = "asset-" + std::to_string( 100 + i );
        asset.kind = "raster";
        asset.numericDomain = "reflectance";
        asset.state = "ready";
        context.assets.push_back( asset );
    }
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "analysis", capability( "rs:ndvi", "low", "", "index" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    const ScientificPlan &plan = *result.primary();
    bool overflowQuestion = false;
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.blocking && question.detail.find( "exceed the 8-input step bound" )
                 != std::string::npos )
            overflowQuestion = true;
    }
    CHECK( overflowQuestion );
    for ( const auto &step : plan.steps )
        CHECK( static_cast<int>( step.inputs.size() ) <= PlanLimits::kMaxInputsPerStep );
    ScientificPlan reparsed;
    std::string error;
    REQUIRE( scientificPlanFromJson( scientificPlanToJson( plan ), reparsed, error ) );
    CHECK( validateScientificPlan( reparsed ).empty() );

    // (b) >8 lawful analysis candidates: capped at 8 with a typed overflow note.
    PlanningContext pairContext = twoDnAssets();
    // Nine CONTRACT-REGISTERED analysis operators (8 spectral-index family +
    // rs:change) so every fact survives the contracts gate; the cap, not the
    // authority check, is what under test here.
    FakeProvider manyProvider;
    for ( const char *op : { "rs:mndwi", "rs:ndwi", "rs:ndbi", "rs:evi", "rs:savi",
                             "rs:ndvi", "rs:spectral_index", "rs:band_ratio" } )
        manyProvider.add( "analysis", capability( op, "low", "", "index" ) );
    manyProvider.add( "analysis", capability( "rs:change", "high", "features", "none" ) );
    manyProvider.add( "publication", capability( "io:translate", "low", "", "" ) );
    ScientificGoal pairGoal = waterGoal();
    pairGoal.acceptanceCriteria.clear();
    const PlanningResult capped =
        planScientificWork( pairGoal, pairContext, PlannerProviders{ &manyProvider } );
    REQUIRE( static_cast<int>( capped.candidates.size() ) == PlanLimits::kMaxCandidates );
    bool candidateOverflow = false;
    for ( const auto &question : capped.primary()->openQuestions )
    {
        if ( question.detail.find( "9 lawful analysis candidates" ) != std::string::npos )
            candidateOverflow = true;
    }
    CHECK( candidateOverflow );
    for ( const auto &candidate : capped.candidates )
    {
        ScientificPlan candidateReparse;
        std::string candidateError;
        REQUIRE( scientificPlanFromJson( scientificPlanToJson( candidate ), candidateReparse,
                                         candidateError ) );
        CHECK( validateScientificPlan( candidateReparse ).empty() );
    }

    // (c) calibration fact with unknown cost class: excluded, never planned.
    PlanningContext dnContext = twoDnAssets();
    FakeProvider lyingBridge;
    lyingBridge.add( "calibration", capability( "rs:brdf_normalization", "bogus_cost", "dn", "reflectance" ) );
    lyingBridge.add( "analysis", capability( "rs:ace", "low", "reflectance", "probability" ) );
    lyingBridge.add( "publication", capability( "io:translate", "low", "", "" ) );
    ScientificGoal detection = waterGoal();
    detection.kind = "measurement";
    detection.acceptanceCriteria.clear();
    const PlanningResult bridgeless =
        planScientificWork( detection, dnContext, PlannerProviders{ &lyingBridge } );
    for ( const auto &step : bridgeless.primary()->steps )
        CHECK( step.operatorId != "rs:brdf_normalization" );
    bool unbridgeableTyped = false;
    for ( const auto &question : bridgeless.primary()->openQuestions )
    {
        if ( question.blocking && question.detail.find( "no contracts-verified calibration" )
                 != std::string::npos )
            unbridgeableTyped = true;
    }
    CHECK( unbridgeableTyped );
}

TEST_CASE( "review regression: forbidden operators and determinism bind every stage",
           "[scientific_planner][review][p0]" )
{
    ScientificGoal goal = waterGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoDnAssets();
    context.assets[0].crs = "EPSG:32648";
    context.assets[0].resolutionM = 10.0;
    context.assets[1].crs = "EPSG:32647"; // grid mismatch → alignment stage wanted
    context.assets[1].resolutionM = 10.0;
    context.constraints.forbiddenOperators = { "rs:align" };
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "alignment", capability( "rs:align", "low", "", "" ) );
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    const ScientificPlan &plan = *result.primary();
    for ( const auto &step : plan.steps )
        CHECK( step.operatorId != "rs:align" ); // never planned despite the gate
    bool alignmentQuestion = false;
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.blocking && question.detail.find( "alignment capability" )
                 != std::string::npos )
            alignmentQuestion = true;
    }
    CHECK( alignmentQuestion );
    CHECK( plan.verdict == "feasible_with_gaps" );
    // and the emitted plan must survive the proposal validator (no self-reject)
    PlanningContext validatorContext = context;
    validatorContext.constraints.forbiddenOperators.clear();
    FakeProvider validatorProvider = provider;
    const auto outcome =
        validateProposal( scientificPlanToJson( plan ), goal, validatorContext,
                          PlannerProviders{ &validatorProvider } );
    INFO( "rejection: " << outcome.rejection.code );
    CHECK( outcome.accepted );
}

TEST_CASE( "adversarial: zero ready assets yields an honest plan, not a crash or a fake",
           "[scientific_planner][review][adversarial]" )
{
    const ScientificGoal goal = waterGoal();
    PlanningContext context; // no assets at all
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    const ScientificPlan &plan = *result.primary();
    // analysis still lawful (its input domain is unconstrained) but it has no
    // consumable inputs; the publish chain stays visible and typed
    CHECK( plan.verdict == "feasible_with_gaps" );
    CHECK( validateScientificPlan( plan ).empty() );
}
