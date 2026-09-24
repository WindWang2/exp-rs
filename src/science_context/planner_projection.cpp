// src/science_context/planner_projection.cpp
#include "science_context/planner_projection.h"

namespace sicnu::science_context {

PlanningContext projectPlanningContext( const ScientificContextBundle &bundle )
{
    PlanningContext ctx;
    ctx.goal = bundle.goal;
    ctx.intent = bundle.intent;
    if ( !bundle.recipes.empty() )
        ctx.recipeId = bundle.recipes.front().recipeId;

    ctx.inputFacts = bundle.planner.inputFacts;
    ctx.missingFacts = bundle.planner.missingFacts;
    ctx.limitations = bundle.planner.limitations;
    ctx.openQuestions = Json::Value( Json::arrayValue );
    for ( const auto &q : bundle.openQuestions )
        ctx.openQuestions.append( q );

    Json::Value alts( Json::arrayValue );
    for ( const auto &c : bundle.capabilities )
    {
        Json::Value a( Json::objectValue );
        a["kind"] = "capability";
        a["id"] = c.capabilityId;
        a["status"] = c.status;
        a["score"] = c.score;
        alts.append( a );
    }
    for ( const auto &r : bundle.recipes )
    {
        Json::Value a( Json::objectValue );
        a["kind"] = "recipe";
        a["id"] = r.recipeId;
        a["score"] = r.score;
        alts.append( a );
    }
    ctx.alternatives = alts;

    // Autonomy: L2 and below never allow autonomous execution.
    if ( !bundle.constraints.allowAutonomousExec )
    {
        // Structural candidates may exist, but execution stays blocked for L2.
        // Only block *autonomous* exec marker — planner may still draft.
        ctx.limitations.append( "autonomy_forbids_autonomous_exec:" +
                                bundle.constraints.autonomyLevel );
    }

    bool blocked = false;
    std::string reason;
    for ( const auto &a : bundle.assets )
    {
        if ( a.evidence == EvidenceBucket::Conflicted )
        {
            blocked = true;
            reason = "asset_evidence_conflicted:" + a.assetId;
            break;
        }
    }
    // Impossibility is not monotone across capability candidates: one operator
    // being impossible does not block the goal while another candidate is
    // direct or prep. Blocking requires every candidate to be unusable.
    bool anyImpossible = false;
    std::string impossibleReason;
    bool anyFeasible = false;
    for ( const auto &c : bundle.capabilities )
    {
        if ( c.status == "impossible" )
        {
            anyImpossible = true;
            if ( impossibleReason.empty() )
                impossibleReason = "capability_impossible:" + c.capabilityId;
        }
        else if ( c.status == "unavailable" )
        {
            // unavailable = needs prep; draft ok but note limitation
            ctx.missingFacts.append( c.reasons.empty() ? c.capabilityId : c.reasons.front() );
        }
        else if ( c.status == "prep" )
        {
            for ( const auto &p : c.prepActions )
                ctx.missingFacts.append( p );
        }
        if ( c.status == "direct" || c.status == "prep" )
            anyFeasible = true;
    }
    if ( anyImpossible && !anyFeasible )
    {
        blocked = true;
        if ( reason.empty() )
            reason = impossibleReason;
    }
    if ( bundle.constraints.offline )
        ctx.limitations.append( "offline_mode" );

    ctx.executionBlocked = blocked;
    ctx.blockReason = reason;
    return ctx;
}

Json::Value planningContextToCompileRequest( const PlanningContext &ctx )
{
    Json::Value req( Json::objectValue );
    req["goal"] = ctx.goal;
    req["intent"] = ctx.intent;
    req["recipe_id"] = ctx.recipeId;
    req["recipe_bindings"] = ctx.recipeBindings;
    req["input_facts"] = ctx.inputFacts;
    req["model_contracts"] = ctx.modelContracts;
    req["apply_repairs"] = ctx.applyRepairs;
    req["execution_blocked"] = ctx.executionBlocked;
    req["block_reason"] = ctx.blockReason;
    req["missing_facts"] = ctx.missingFacts;
    req["limitations"] = ctx.limitations;
    req["open_questions"] = ctx.openQuestions;
    req["alternatives"] = ctx.alternatives;
    return req;
}

void applyPlannerProjection( ScientificContextBundle &bundle )
{
    PlanningContext ctx = projectPlanningContext( bundle );
    bundle.planner.goal = ctx.goal;
    bundle.planner.intent = ctx.intent;
    bundle.planner.recipeId = ctx.recipeId;
    bundle.planner.inputFacts = ctx.inputFacts;
    bundle.planner.missingFacts = ctx.missingFacts;
    bundle.planner.limitations = ctx.limitations;
    bundle.planner.openQuestions = ctx.openQuestions;
    bundle.planner.executionBlocked = ctx.executionBlocked;
    bundle.planner.blockReason = ctx.blockReason;
}

} // namespace sicnu::science_context
