// src/planner/planner_constraints.cpp
#include "planner/planner_constraints.h"

#include <algorithm>

namespace sicnu::planner {

bool isOperatorForbidden( const PlanningContext &context, const std::string &operatorId )
{
    return std::find( context.constraints.forbiddenOperators.begin(),
                      context.constraints.forbiddenOperators.end(),
                      operatorId )
           != context.constraints.forbiddenOperators.end();
}

bool satisfiesDeterminismRequirement( const PlanningContext &context,
                                      const PlannerCapability &capability )
{
    return !context.constraints.requiredDeterminism || capability.deterministic;
}

bool satisfiesFamilyAllowlist( const PlanningContext &context, const std::string &family )
{
    return context.constraints.allowedFamilies.empty()
           || std::find( context.constraints.allowedFamilies.begin(),
                         context.constraints.allowedFamilies.end(), family )
                  != context.constraints.allowedFamilies.end();
}

bool isLawfulCandidate( const PlanningContext &context, const PlannerCapability &capability )
{
    return !isOperatorForbidden( context, capability.operatorId )
           && satisfiesDeterminismRequirement( context, capability )
           && satisfiesFamilyAllowlist( context, capability.family );
}

void applyResourceBudget( const PlanningContext &context, ScientificPlan &plan )
{
    const int stepCount = static_cast<int>( plan.steps.size() );
    const int maxSteps = context.resourceBudget.maxSteps > 0
                             ? context.resourceBudget.maxSteps
                             : ( context.constraints.maxSteps > 0 ? context.constraints.maxSteps : 0 );
    const long long maxRam = context.resourceBudget.maxEstimatedRamMb;

    if ( maxSteps > 0 && stepCount > maxSteps )
    {
        plan.risks.push_back(
            PlanRisk{ "resource_over_budget",
                      "plan needs " + std::to_string( stepCount )
                          + " steps but the budget allows " + std::to_string( maxSteps ),
                      "" } );
        plan.openQuestions.push_back( PlanOpenQuestion{
            "", "decision_required", true,
            "plan exceeds the step budget (" + std::to_string( stepCount ) + " > "
                + std::to_string( maxSteps ) + "); the full plan is shown but must be accepted "
                "explicitly",
            "raise the budget or accept the over-budget plan" } );
    }
    if ( maxRam > 0 && plan.cost.totalEstimatedRamMb > maxRam )
    {
        plan.risks.push_back(
            PlanRisk{ "resource_over_budget",
                      "estimated RAM " + std::to_string( plan.cost.totalEstimatedRamMb )
                          + " MB exceeds the budget of " + std::to_string( maxRam ) + " MB",
                      "" } );
        plan.openQuestions.push_back( PlanOpenQuestion{
            "", "decision_required", true,
            "plan exceeds the RAM budget (" + std::to_string( plan.cost.totalEstimatedRamMb )
                + " > " + std::to_string( maxRam ) + " MB); the full plan is shown but must be "
                "accepted explicitly",
            "raise the budget or choose a lighter candidate" } );
    }
    if ( !context.resourceBudget.maxCostClass.empty()
         && costClassRank( plan.cost.aggregateCostClass )
                > costClassRank( context.resourceBudget.maxCostClass ) )
    {
        plan.risks.push_back(
            PlanRisk{ "resource_over_budget",
                      "aggregate cost class " + plan.cost.aggregateCostClass
                          + " exceeds the budget of " + context.resourceBudget.maxCostClass,
                      "" } );
        plan.openQuestions.push_back( PlanOpenQuestion{
            "", "decision_required", true,
            "plan exceeds the cost-class budget (" + plan.cost.aggregateCostClass + " > "
                + context.resourceBudget.maxCostClass + "); the full plan is shown but must be "
                "accepted explicitly",
            "raise the budget or choose a cheaper candidate" } );
    }
    // A plan that only gained non-blocking questions keeps feasible_with_gaps;
    // blocking budget questions must not sit under a bare "feasible".
    if ( plan.verdict == "feasible" )
    {
        const bool blocking =
            std::any_of( plan.openQuestions.begin(), plan.openQuestions.end(),
                         []( const PlanOpenQuestion &q ) { return q.blocking; } );
        if ( blocking )
            plan.verdict = "feasible_with_gaps";
    }
}

bool budgetExceeded( const PlanningContext &context, const ScientificPlan &plan )
{
    const int stepCount = static_cast<int>( plan.steps.size() );
    const int maxSteps = context.resourceBudget.maxSteps > 0
                             ? context.resourceBudget.maxSteps
                             : ( context.constraints.maxSteps > 0 ? context.constraints.maxSteps : 0 );
    if ( maxSteps > 0 && stepCount > maxSteps )
        return true;
    if ( context.resourceBudget.maxEstimatedRamMb > 0
         && plan.cost.totalEstimatedRamMb > context.resourceBudget.maxEstimatedRamMb )
        return true;
    if ( !context.resourceBudget.maxCostClass.empty()
         && costClassRank( plan.cost.aggregateCostClass )
                > costClassRank( context.resourceBudget.maxCostClass ) )
        return true;
    return false;
}

} // namespace sicnu::planner
