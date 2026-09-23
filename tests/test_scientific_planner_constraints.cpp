// tests/test_scientific_planner_constraints.cpp — slice C:
// constraints narrow candidates while a lawful sibling exists; budgets
// annotate overruns with typed risks + blocking decisions while keeping the
// plan fully visible (no silent truncation).
#include <catch2/catch_test_macros.hpp>

#include "planner/planner_constraints.h"
#include "planner/planner_core.h"

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
                              bool deterministic = true, long long ramMb = 256 )
{
    PlannerCapability capability;
    capability.operatorId = operatorId;
    capability.costClass = costClass;
    capability.inputDomain = inputDomain;
    capability.outputDomain = outputDomain;
    capability.deterministic = deterministic;
    capability.estimatedRamMb = ramMb;
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

FakeProvider twoCandidateProvider()
{
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    // reflectance → features bridge so the rs:change variant is lawful too
    provider.add( "calibration", capability( "rs:band_math", "medium", "reflectance", "features" ) );
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "analysis", capability( "rs:change", "high", "features", "none", true, 4096 ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );
    return provider;
}
} // namespace

TEST_CASE( "cost-class budget: over-budget candidate annotated, plan fully visible",
           "[scientific_planner][constraints]" )
{
    ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.resourceBudget.maxCostClass = "low";
    FakeProvider provider = twoCandidateProvider();

    const PlanningResult result = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 2 );
    const ScientificPlan &primary = *result.primary();
    // the primary (all-low) fits the budget
    CHECK( primary.verdict == "feasible" );
    CHECK_FALSE( budgetExceeded( ctx, primary ) );

    // the expensive candidate is annotated, not clipped
    const ScientificPlan &expensive = result.candidates[1];
    CHECK( budgetExceeded( ctx, expensive ) );
    CHECK( expensive.cost.aggregateCostClass == "high" );
    CHECK( expensive.steps.size() == 4 ); // full plan still visible (import→calibrate→analyze→publish)
    bool riskSeen = false;
    bool questionSeen = false;
    for ( const auto &risk : expensive.risks )
    {
        if ( risk.kind == "resource_over_budget"
             && risk.detail.find( "cost class" ) != std::string::npos )
            riskSeen = true;
    }
    for ( const auto &question : expensive.openQuestions )
    {
        if ( question.kind == "decision_required" && question.blocking
             && question.detail.find( "cost-class budget" ) != std::string::npos )
            questionSeen = true;
    }
    CHECK( riskSeen );
    CHECK( questionSeen );
    CHECK( expensive.verdict == "feasible_with_gaps" );
}

TEST_CASE( "step budget overrun: typed risk + blocking decision, no truncation",
           "[scientific_planner][constraints]" )
{
    ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.resourceBudget.maxSteps = 2;
    FakeProvider provider = twoCandidateProvider();

    const PlanningResult result = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    const ScientificPlan &plan = result.candidates[1];
    CHECK( plan.steps.size() == 4 );
    CHECK( budgetExceeded( ctx, plan ) );
    bool riskSeen = false;
    for ( const auto &risk : plan.risks )
    {
        if ( risk.kind == "resource_over_budget" && risk.detail.find( "steps" ) != std::string::npos )
            riskSeen = true;
    }
    CHECK( riskSeen );
    CHECK( plan.verdict == "feasible_with_gaps" );
}

TEST_CASE( "RAM budget overrun annotated", "[scientific_planner][constraints]" )
{
    ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.resourceBudget.maxEstimatedRamMb = 1024;
    FakeProvider provider = twoCandidateProvider();

    const PlanningResult result = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    const ScientificPlan &plan = result.candidates[1];
    CHECK( plan.cost.totalEstimatedRamMb > 1024 );
    CHECK( budgetExceeded( ctx, plan ) );
    bool seen = false;
    for ( const auto &risk : plan.risks )
    {
        if ( risk.kind == "resource_over_budget" && risk.detail.find( "RAM" ) != std::string::npos )
            seen = true;
    }
    CHECK( seen );
}

TEST_CASE( "budget questions carry deterministic ids and re-read cleanly",
           "[scientific_planner][constraints]" )
{
    ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.resourceBudget.maxSteps = 2;
    FakeProvider provider = twoCandidateProvider();

    const PlanningResult result = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    const ScientificPlan &plan = result.candidates[1];
    CHECK_FALSE( plan.openQuestions.empty() );
    for ( const auto &question : plan.openQuestions )
    {
        INFO( "question " << question.questionId );
        CHECK_FALSE( question.questionId.empty() );
    }
    ScientificPlan reparsed;
    std::string error;
    REQUIRE( scientificPlanFromJson( scientificPlanToJson( plan ), reparsed, error ) );
    CHECK( validateScientificPlan( reparsed ).empty() );
    const PlanningResult replay = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    REQUIRE( replay.candidates.size() == result.candidates.size() );
    for ( size_t i = 0; i < result.candidates.size(); ++i )
    {
        REQUIRE( replay.candidates[i].openQuestions.size()
                 == result.candidates[i].openQuestions.size() );
        for ( size_t q = 0; q < result.candidates[i].openQuestions.size(); ++q )
            CHECK( replay.candidates[i].openQuestions[q].questionId
                   == result.candidates[i].openQuestions[q].questionId );
    }
}

TEST_CASE( "forbidden operator: lawful sibling wins; forbidden-only plan is infeasible",
           "[scientific_planner][constraints]" )
{
    ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.constraints.forbiddenOperators = { "rs:mndwi" };
    FakeProvider provider = twoCandidateProvider();

    const PlanningResult result = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 1 );
    const ScientificPlan &plan = *result.primary();
    bool usesForbidden = false;
    for ( const auto &step : plan.steps )
    {
        if ( step.operatorId == "rs:mndwi" )
            usesForbidden = true;
    }
    CHECK_FALSE( usesForbidden );
    CHECK( plan.verdict == "feasible" );

    // forbid BOTH lawful candidates → infeasible with blocking decision
    ctx.constraints.forbiddenOperators = { "rs:mndwi", "rs:change" };
    const PlanningResult blocked = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    REQUIRE( blocked.candidates.size() == 1 );
    CHECK( blocked.primary()->verdict == "infeasible" );
    bool blockingDecision = false;
    for ( const auto &question : blocked.primary()->openQuestions )
    {
        if ( question.blocking && question.detail.find( "excluded by constraints" )
                 != std::string::npos )
            blockingDecision = true;
    }
    CHECK( blockingDecision );
}

TEST_CASE( "required determinism excludes stochastic candidates with typed visibility",
           "[scientific_planner][constraints]" )
{
    ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.constraints.requiredDeterminism = true;
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:classify", "low", "", "none", false ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    CHECK( result.primary()->verdict == "infeasible" );
    bool exclusion = false;
    for ( const auto &question : result.primary()->openQuestions )
    {
        if ( question.blocking && question.detail.find( "rs:classify" ) != std::string::npos )
            exclusion = true;
    }
    CHECK( exclusion );
}

TEST_CASE( "allowed-families allowlist narrows candidates", "[scientific_planner][constraints]" )
{
    ScientificGoal goal = changeGoal();
    PlanningContext ctx = context();
    ctx.constraints.allowedFamilies = { "analysis", "publication" };
    FakeProvider provider = twoCandidateProvider();
    // data_import no longer allowed: import stage raises its typed question
    const PlanningResult result = planScientificWork( goal, ctx, PlannerProviders{ &provider } );
    const ScientificPlan &plan = *result.primary();
    CHECK( plan.verdict == "feasible_with_gaps" );
    bool importQuestion = false;
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.detail.find( "data_import" ) != std::string::npos )
            importQuestion = true;
    }
    CHECK( importQuestion );
}
