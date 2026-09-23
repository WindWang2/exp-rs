// tests/test_scientific_planner_proposal.cpp — slice E:
// the deterministic validator gates untrusted proposal plans: closed typed
// rejection codes, no silent fallback, identity re-minted from content.
#include <catch2/catch_test_macros.hpp>

#include "planner/planner_proposal.h"

#include <json/json.h>

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

ScientificGoal changeGoal()
{
    ScientificGoal goal;
    goal.goalId = "goal-water";
    goal.kind = "change";
    goal.subject = "water change";
    return goal;
}

PlanningContext context()
{
    PlanningContext context;
    PlannerAssetFacts asset;
    asset.ref = "asset-a";
    asset.kind = "raster";
    asset.numericDomain = "reflectance";
    asset.state = "ready";
    context.assets = { asset };
    context.mode = ModePolicy{ "agent", "full", false };
    return context;
}

Json::Value proposalDoc()
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = "scientific_plan";
    doc["schema_version"] = "1.0";
    doc["plan_id"] = "plan-from-llm";
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
    Json::Value inputs( Json::arrayValue );
    Json::Value input( Json::objectValue );
    input["asset_ref"] = "asset-a";
    input["as"] = "input";
    inputs.append( input );
    analyze["inputs"] = inputs;

    Json::Value steps( Json::arrayValue );
    steps.append( analyze );
    doc["steps"] = steps;

    Json::Value cost( Json::objectValue );
    cost["aggregate_cost_class"] = "low";
    doc["cost"] = cost;
    return doc;
}
} // namespace

TEST_CASE( "a lawful proposal is accepted and re-minted from content",
           "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    const PlanningContext ctx = context();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );

    Json::Value doc = proposalDoc();
    const ProposalOutcome outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    REQUIRE( outcome.accepted );
    CHECK( outcome.plan.planId.rfind( "plan-", 0 ) == 0 );
    // identity re-minted: the wire id never survives
    CHECK( outcome.plan.planId != "plan-from-llm" );
    CHECK( outcome.plan.planId == "plan-" + scientificPlanFingerprint( outcome.plan ) );
}

TEST_CASE( "schema and structure problems reject with typed codes",
           "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    const PlanningContext ctx = context();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );

    Json::Value doc = proposalDoc();
    doc["schema_version"] = "9.9";
    auto outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_schema_invalid" );

    doc = proposalDoc();
    doc["kind"] = "shopping_list";
    outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_schema_invalid" );

    doc = proposalDoc();
    doc["steps"][0]["role"] = "party";
    outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_structure_invalid" );
}

TEST_CASE( "goal/mode mismatches reject with typed codes", "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    const PlanningContext ctx = context();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );

    Json::Value doc = proposalDoc();
    doc["goal_id"] = "some-other-goal";
    auto outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_goal_mismatch" );

    doc = proposalDoc();
    doc["mode"]["kind"] = "teaching";
    outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_mode_mismatch" );
}

TEST_CASE( "unknown and forbidden operators reject with typed codes",
           "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );

    Json::Value doc = proposalDoc();
    doc["steps"][0]["operator_id"] = "rs:hallucinated_op";
    auto outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_unknown_operator" );

    ctx.constraints.forbiddenOperators = { "rs:mndwi" };
    doc = proposalDoc();
    outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_forbidden_operator" );
}

TEST_CASE( "determinism requirement and family relabeling reject", "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.constraints.requiredDeterminism = true;
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:classify", "medium", "", "none", false ) );

    Json::Value doc = proposalDoc();
    doc["steps"][0]["operator_id"] = "rs:classify";
    doc["steps"][0]["cost_class"] = "medium";
    auto outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_determinism_violation" );

    // family relabeling: the provider knows the operator under a DIFFERENT family
    PlanningContext plainCtx = context();
    FakeProvider otherFamilyProvider;
    otherFamilyProvider.add( "verification", capability( "rs:mndwi", "low", "", "index" ) );
    doc = proposalDoc();
    outcome = validateProposal( doc, goal, plainCtx, PlannerProviders{ &otherFamilyProvider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_unknown_operator" );
}

TEST_CASE( "unknown assets and unmet hard preconditions reject", "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    const PlanningContext ctx = context();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );

    Json::Value doc = proposalDoc();
    doc["steps"][0]["inputs"][0]["asset_ref"] = "asset-ghost";
    auto outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_unknown_asset" );

    // hard asset_state precondition on a non-ready asset
    PlanningContext offlineCtx = context();
    offlineCtx.assets[0].state = "offline";
    doc = proposalDoc();
    Json::Value precondition( Json::objectValue );
    precondition["kind"] = "asset_state";
    precondition["asset_ref"] = "asset-a";
    Json::Value preconditions( Json::arrayValue );
    preconditions.append( precondition );
    doc["steps"][0]["preconditions"] = preconditions;
    outcome = validateProposal( doc, goal, offlineCtx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_unmet_precondition" );
}

TEST_CASE( "transitions contradicting contracts reject", "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    const PlanningContext ctx = context();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );

    Json::Value doc = proposalDoc();
    Json::Value transition( Json::objectValue );
    transition["asset_ref"] = "asset-a";
    transition["from_domain"] = "reflectance";
    transition["to_domain"] = "temperature"; // contracts: rs:mndwi produces index
    Json::Value transitions( Json::arrayValue );
    transitions.append( transition );
    doc["steps"][0]["expected_transitions"] = transitions;
    const auto outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_unverified_transition" );
}

TEST_CASE( "budget overruns and inconsistent verdicts reject", "[scientific_planner][proposal]" )
{
    const ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.resourceBudget.maxSteps = 1;
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    Json::Value doc = proposalDoc();
    // a second (publish) step pushes the proposal past the 1-step budget
    Json::Value publish( Json::objectValue );
    publish["step_id"] = "step-publish-01";
    publish["role"] = "publish";
    publish["operator_id"] = "io:translate";
    publish["family"] = "publication";
    publish["cost_class"] = "low";
    publish["inputs"] = Json::Value( Json::arrayValue );
    Json::Value publishInput( Json::objectValue );
    publishInput["from_step_id"] = "step-analyze-01";
    publishInput["as"] = "input";
    doc["steps"][0]["inputs"] = Json::Value( Json::arrayValue );
    Json::Value assetInput( Json::objectValue );
    assetInput["asset_ref"] = "asset-a";
    assetInput["as"] = "input";
    doc["steps"][0]["inputs"].append( assetInput );
    doc["steps"].append( publish );
    auto outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_over_budget" );

    // feasible verdict + blocking question = self-inconsistent
    ctx = context();
    doc = proposalDoc();
    Json::Value question( Json::objectValue );
    question["question_id"] = "q-1";
    question["kind"] = "insufficient_data";
    question["blocking"] = true;
    question["detail"] = "missing scene";
    Json::Value questions( Json::arrayValue );
    questions.append( question );
    doc["open_questions"] = questions;
    outcome = validateProposal( doc, goal, ctx, PlannerProviders{ &provider } );
    CHECK_FALSE( outcome.accepted );
    CHECK( outcome.rejection.code == "planner:proposal_inconsistent_verdict" );
}

TEST_CASE( "rejection codes are the closed sorted vocabulary", "[scientific_planner][proposal]" )
{
    // every emitted code in this suite was a kProposalRejectionCodes member
    // (checked implicitly above); here the vocabulary itself is pinned.
    CHECK( isKnownProposalRejectionCode( "planner:proposal_schema_invalid" ) );
    CHECK( isKnownProposalRejectionCode( "planner:proposal_inconsistent_verdict" ) );
    CHECK( std::is_sorted( kProposalRejectionCodes.begin(), kProposalRejectionCodes.end() ) );
}
