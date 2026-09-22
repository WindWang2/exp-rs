// src/agent_loop/session_policy.cpp
#include "session_policy.h"

namespace sicnu::agent_loop {

bool parseRunMode( const std::string &text, RunMode &out )
{
    if ( text == "dry_run" )
        out = RunMode::DryRun;
    else if ( text == "plan_only" )
        out = RunMode::PlanOnly;
    else if ( text == "execute_with_verify" )
        out = RunMode::ExecuteWithVerify;
    else
        return false;
    return true;
}

std::string runModeToString( RunMode mode )
{
    switch ( mode )
    {
        case RunMode::DryRun:
            return "dry_run";
        case RunMode::PlanOnly:
            return "plan_only";
        case RunMode::ExecuteWithVerify:
            return "execute_with_verify";
    }
    return "execute_with_verify";
}

bool RepairApprovalPolicy::classAutoApproved( const std::string &riskClass ) const
{
    if ( riskClass.empty() )
        return false; // an unclassified repair is never auto-approved
    for ( const std::string &approved : autoApproveRiskClasses )
        if ( approved == riskClass )
            return true;
    return false;
}

SessionPolicy SessionPolicy::defaults()
{
    return SessionPolicy{};
}

bool SessionPolicy::validate( std::string *error ) const
{
    auto fail = [ &error ]( const std::string &field ) {
        if ( error )
            *error = "session policy: invalid " + field;
        return false;
    };
    if ( maxReplans < 0 )
        return fail( "max_replans" );
    if ( noProgressThreshold < 1 )
        return fail( "no_progress_threshold" );
    if ( resourceBudgetMb < 0 )
        return fail( "resource_budget_mb" );
    if ( maxSteps < 1 )
        return fail( "max_steps" );
    if ( executorTimeoutMs < 0 )
        return fail( "executor_timeout_ms" );
    for ( const std::string &riskClass : repairApproval.autoApproveRiskClasses )
        if ( riskClass.empty() )
            return fail( "repair_approval.auto_approve_risk_classes (empty entry)" );
    return true;
}

} // namespace sicnu::agent_loop
