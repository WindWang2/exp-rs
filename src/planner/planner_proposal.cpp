// src/planner/planner_proposal.cpp
#include "planner/planner_proposal.h"

#include "planner/json_util.h"
#include "planner/planner_constraints.h"
#include "planner/sha256_util.h"

#include <algorithm>

namespace sicnu::planner {

namespace {

constexpr const char *kSchemaCode = "planner:proposal_schema_invalid";
constexpr const char *kStructureCode = "planner:proposal_structure_invalid";
constexpr const char *kGoalCode = "planner:proposal_goal_mismatch";
constexpr const char *kModeCode = "planner:proposal_mode_mismatch";
constexpr const char *kUnknownOperatorCode = "planner:proposal_unknown_operator";
constexpr const char *kForbiddenOperatorCode = "planner:proposal_forbidden_operator";
constexpr const char *kDeterminismCode = "planner:proposal_determinism_violation";
constexpr const char *kFamilyCode = "planner:proposal_family_mismatch";
constexpr const char *kTransitionCode = "planner:proposal_unverified_transition";
constexpr const char *kUnknownAssetCode = "planner:proposal_unknown_asset";
constexpr const char *kPreconditionCode = "planner:proposal_unmet_precondition";
constexpr const char *kBudgetCode = "planner:proposal_over_budget";
constexpr const char *kVerdictCode = "planner:proposal_inconsistent_verdict";

} // namespace

ProposalOutcome validateProposal( const Json::Value &proposalDoc, const ScientificGoal &goal,
                                  const PlanningContext &context,
                                  const PlannerProviders &providers )
{
    ProposalOutcome outcome;
    ProposalRejection rejection;

    // --- envelope / structure ------------------------------------------------
    ScientificPlan plan;
    std::string error;
    if ( !scientificPlanFromJson( proposalDoc, plan, error ) )
    {
        const bool versionProblem = error.rfind( "unsupported_version", 0 ) == 0
                                    || error.rfind( "invalid_document", 0 ) == 0;
        rejection.code = versionProblem ? kSchemaCode : kStructureCode;
        rejection.reasons.push_back( error );
        outcome.rejection = std::move( rejection );
        return outcome;
    }

    // --- goal / mode alignment ----------------------------------------------
    if ( plan.goalId != goal.goalId || plan.goalKind != goal.kind )
    {
        rejection.code = kGoalCode;
        rejection.reasons.push_back( "proposal targets goal \"" + plan.goalId + "\" ("
                                     + plan.goalKind + ") but validation is for \"" + goal.goalId
                                     + "\" (" + goal.kind + ")" );
        outcome.rejection = std::move( rejection );
        return outcome;
    }
    if ( plan.modeKind != context.mode.kind )
    {
        rejection.code = kModeCode;
        rejection.reasons.push_back( "proposal mode \"" + plan.modeKind
                                     + "\" does not match the context mode \""
                                     + context.mode.kind + "\"" );
        outcome.rejection = std::move( rejection );
        return outcome;
    }

    // --- structural self-consistency ------------------------------------------
    // Verdict consistency is NOT part of the structural check here: it has
    // its own dedicated rejection code below.
    std::vector<std::string> problems;
    for ( const auto &problem : validateScientificPlan( plan ) )
    {
        if ( problem.find( "feasible verdict with blocking" ) != std::string::npos )
            continue;
        problems.push_back( problem );
    }
    if ( !problems.empty() )
    {
        rejection.code = kStructureCode;
        rejection.reasons = problems;
        outcome.rejection = std::move( rejection );
        return outcome;
    }

    // --- operator-level facts (provider + constraints + contracts) -------------
    if ( providers.capability == nullptr )
    {
        // Fail-closed parity with the baseline planner: a missing seam means
        // nothing can be verified, so nothing is accepted.
        rejection.code = kUnknownOperatorCode;
        rejection.reasons.push_back(
            "fail-closed: capability provider seam is not wired; proposal operators cannot be "
            "verified" );
        outcome.rejection = std::move( rejection );
        return outcome;
    }
    {
        for ( const auto &step : plan.steps )
        {
            if ( step.operatorId.empty() )
                continue;
            if ( isOperatorForbidden( context, step.operatorId ) )
            {
                rejection.code = kForbiddenOperatorCode;
                rejection.reasons.push_back( "step " + step.stepId + " uses forbidden operator "
                                             + step.operatorId );
                break;
            }
            bool known = false;
            for ( const auto &capability :
                  providers.capability->capabilitiesForFamily( step.family ) )
            {
                if ( capability.operatorId == step.operatorId )
                {
                    known = capability.family == step.family;
                    if ( known && !satisfiesDeterminismRequirement( context, capability ) )
                    {
                        rejection.code = kDeterminismCode;
                        rejection.reasons.push_back(
                            "step " + step.stepId + " uses stochastic operator "
                            + step.operatorId + " while the context requires determinism" );
                    }
                    break;
                }
            }
            if ( !rejection.code.empty() )
                break;
            if ( !known )
            {
                rejection.code = rejection.code.empty() ? kUnknownOperatorCode : rejection.code;
                rejection.reasons.push_back( "step " + step.stepId + " names operator "
                                             + step.operatorId + " unknown to the provider in family "
                                             + step.family );
                break;
            }
            if ( !satisfiesFamilyAllowlist( context, step.family ) )
            {
                rejection.code = kFamilyCode;
                rejection.reasons.push_back( "step " + step.stepId + " uses family "
                                             + step.family + " outside the allowed families" );
                break;
            }
            const auto *operatorContract = contractForOperator( step.operatorId );
            if ( !operatorContract )
            {
                // Parity with the baseline planner: a provider-known operator
                // without a scientific contract has no verifiable semantics.
                rejection.code = kTransitionCode;
                rejection.reasons.push_back( "step " + step.stepId + " names operator "
                                             + step.operatorId
                                             + " which has no scientific contract; its state "
                                               "transitions are unverifiable" );
                break;
            }
            // state transitions must agree with the linked contracts registry
            for ( const auto &transition : step.expectedTransitions )
            {
                const auto *contract = operatorContract;
                if ( contract && !transition.toDomain.empty()
                     && !contract->outputDomain.empty()
                     && transition.toDomain != contract->outputDomain )
                {
                    rejection.code = kTransitionCode;
                    rejection.reasons.push_back( "step " + step.stepId + " claims transition to "
                                                 + transition.toDomain + " but the contracts record for "
                                                 + step.operatorId + " produces "
                                                 + contract->outputDomain );
                    break;
                }
                if ( contract && !transition.fromDomain.empty()
                     && !contract->inputDomain.empty()
                     && contract->inputDomain != "any"
                     && transition.fromDomain != contract->inputDomain )
                {
                    rejection.code = kTransitionCode;
                    rejection.reasons.push_back( "step " + step.stepId + " claims transition from "
                                                 + transition.fromDomain + " but the contracts record for "
                                                 + step.operatorId + " consumes "
                                                 + contract->inputDomain );
                    break;
                }
            }
            if ( !rejection.code.empty() )
                break;
        }
    }
    if ( !rejection.code.empty() )
    {
        outcome.rejection = std::move( rejection );
        return outcome;
    }

    // --- assets + preconditions ------------------------------------------------
    for ( const auto &step : plan.steps )
    {
        for ( const auto &input : step.inputs )
        {
            if ( !input.assetRef.empty() && !findContextAsset( context, input.assetRef ) )
            {
                rejection.code = kUnknownAssetCode;
                rejection.reasons.push_back( "step " + step.stepId + " consumes unknown asset "
                                             + input.assetRef );
                break;
            }
        }
        if ( !rejection.code.empty() )
            break;
        for ( const auto &precondition : step.preconditions )
        {
            if ( !precondition.assetRef.empty()
                 && !findContextAsset( context, precondition.assetRef ) )
            {
                rejection.code = kUnknownAssetCode;
                rejection.reasons.push_back( "step " + step.stepId
                                             + " precondition references unknown asset "
                                             + precondition.assetRef );
                break;
            }
            if ( precondition.kind == "asset_state" && !precondition.assetRef.empty() )
            {
                const PlannerAssetFacts *asset =
                    findContextAsset( context, precondition.assetRef );
                if ( asset && !isHardFactAssetState( asset->state ) )
                {
                    rejection.code = kPreconditionCode;
                    rejection.reasons.push_back( "step " + step.stepId
                                                 + " asserts a hard asset_state precondition on \""
                                                 + precondition.assetRef + "\" which is "
                                                 + asset->state );
                    break;
                }
            }
        }
        if ( !rejection.code.empty() )
            break;
    }
    if ( !rejection.code.empty() )
    {
        outcome.rejection = std::move( rejection );
        return outcome;
    }

    // --- budgets ------------------------------------------------------------------
    const int stepBudget = context.resourceBudget.maxSteps > 0
                               ? context.resourceBudget.maxSteps
                               : context.constraints.maxSteps;
    if ( stepBudget > 0 && static_cast<int>( plan.steps.size() ) > stepBudget )
    {
        rejection.code = kBudgetCode;
        rejection.reasons.push_back( "proposal needs " + std::to_string( plan.steps.size() )
                                     + " steps beyond the budget of "
                                     + std::to_string( stepBudget ) );
    }
    if ( rejection.code.empty() && context.resourceBudget.maxEstimatedRamMb > 0
         && plan.cost.totalEstimatedRamMb > context.resourceBudget.maxEstimatedRamMb )
    {
        rejection.code = kBudgetCode;
        rejection.reasons.push_back( "proposal RAM estimate "
                                     + std::to_string( plan.cost.totalEstimatedRamMb )
                                     + " MB exceeds the budget" );
    }
    if ( rejection.code.empty() && !context.resourceBudget.maxCostClass.empty()
         && costClassRank( plan.cost.aggregateCostClass )
                > costClassRank( context.resourceBudget.maxCostClass ) )
    {
        rejection.code = kBudgetCode;
        rejection.reasons.push_back( "proposal aggregate cost class "
                                     + plan.cost.aggregateCostClass + " exceeds the budget" );
    }
    if ( !rejection.code.empty() )
    {
        outcome.rejection = std::move( rejection );
        return outcome;
    }

    // --- self-consistent verdict ------------------------------------------------------
    {
        const bool blocking =
            std::any_of( plan.openQuestions.begin(), plan.openQuestions.end(),
                         []( const PlanOpenQuestion &q ) { return q.blocking; } );
        if ( plan.verdict == "feasible" && blocking )
        {
            rejection.code = kVerdictCode;
            rejection.reasons.push_back( "proposal claims a feasible verdict while carrying "
                                         "blocking open questions" );
        }
        else if ( plan.verdict == "infeasible" && !plan.steps.empty() )
        {
            rejection.code = kVerdictCode;
            rejection.reasons.push_back( "proposal claims infeasible while carrying executable "
                                         "steps" );
        }
    }
    if ( !rejection.code.empty() )
    {
        outcome.rejection = std::move( rejection );
        return outcome;
    }

    // --- accepted: identity re-minted from content --------------------------------
    plan.planId = "plan-" + scientificPlanFingerprint( plan );
    outcome.accepted = true;
    outcome.plan = std::move( plan );
    return outcome;
}

} // namespace sicnu::planner
