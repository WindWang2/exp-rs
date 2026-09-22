// src/agent_loop/session_state.cpp
#include "session_state.h"

#include <array>
#include <string_view>

namespace sicnu::agent_loop {
namespace {

constexpr std::array< std::string_view, 10 > kStages = {
    stages::kGoalNormalization, stages::kDataStateSnapshot, stages::kPlanRequest,
    stages::kPreflight,         stages::kRepairApproval,   stages::kExecute,
    stages::kVerify,            stages::kDiagnose,         stages::kReplan,
    stages::kDelivery
};

constexpr std::array< std::string_view, 3 > kTerminalStates = {
    terminal_states::kDelivered, terminal_states::kRefused, terminal_states::kAborted
};

/// The legal edges, as (from, to) pairs. Everything not listed is illegal —
/// including the tempting shortcuts (plan → execute, verify → execute).
struct Edge {
    std::string_view from;
    std::string_view to;
};

constexpr std::array< Edge, 38 > kEdges = { {
    // The happy chain.
    { stages::kGoalNormalization, stages::kDataStateSnapshot },
    { stages::kDataStateSnapshot, stages::kPlanRequest },
    { stages::kPlanRequest, stages::kPreflight },
    { stages::kPreflight, stages::kRepairApproval },
    { stages::kPreflight, stages::kExecute },
    { stages::kPreflight, stages::kDelivery }, // dry_run reports and stops
    { stages::kRepairApproval, stages::kExecute },
    { stages::kRepairApproval, stages::kPlanRequest },
    { stages::kRepairApproval, stages::kReplan }, // approved repairs re-plan
    { stages::kRepairApproval, stages::kDelivery }, // plan_only: converged plan
    { stages::kExecute, stages::kVerify },
    { stages::kVerify, stages::kDelivery },
    { stages::kVerify, stages::kDiagnose },
    { stages::kExecute, stages::kDiagnose },
    { stages::kDiagnose, stages::kReplan },
    { stages::kReplan, stages::kPlanRequest },
    { stages::kDelivery, terminal_states::kDelivered },
    // A failed execution is diagnosed, never silently retried.
    // (execute → diagnose above.)
    // Refusals and aborts are reachable from every running stage,
    // delivery included (a cancel while the evidence summary is written).
    { stages::kGoalNormalization, terminal_states::kRefused },
    { stages::kGoalNormalization, terminal_states::kAborted },
    { stages::kDataStateSnapshot, terminal_states::kRefused },
    { stages::kDataStateSnapshot, terminal_states::kAborted },
    { stages::kPlanRequest, terminal_states::kRefused },
    { stages::kPlanRequest, terminal_states::kAborted },
    { stages::kPreflight, terminal_states::kRefused },
    { stages::kPreflight, terminal_states::kAborted },
    { stages::kRepairApproval, terminal_states::kRefused },
    { stages::kRepairApproval, terminal_states::kAborted },
    { stages::kExecute, terminal_states::kRefused },
    { stages::kExecute, terminal_states::kAborted },
    { stages::kVerify, terminal_states::kRefused },
    { stages::kVerify, terminal_states::kAborted },
    { stages::kDiagnose, terminal_states::kRefused },
    { stages::kDiagnose, terminal_states::kAborted },
    { stages::kReplan, terminal_states::kRefused },
    { stages::kReplan, terminal_states::kAborted },
    { stages::kDelivery, terminal_states::kRefused },
    { stages::kDelivery, terminal_states::kAborted },
} };

} // namespace

bool isKnownStage( const std::string &stage )
{
    for ( const std::string_view s : kStages )
        if ( s == stage )
            return true;
    return false;
}

bool isKnownTerminalState( const std::string &state )
{
    for ( const std::string_view s : kTerminalStates )
        if ( s == state )
            return true;
    return false;
}

bool isKnownStageOrTerminal( const std::string &stage )
{
    return isKnownStage( stage ) || isKnownTerminalState( stage );
}

bool isLegalTransition( const std::string &from, const std::string &to )
{
    if ( !isKnownStageOrTerminal( from ) || !isKnownStageOrTerminal( to ) )
        return false;
    if ( isKnownTerminalState( from ) )
        return false; // terminal is absorbing
    for ( const Edge &e : kEdges )
        if ( e.from == from && e.to == to )
            return true;
    return false;
}

SessionStageMachine::SessionStageMachine() : mStage( stages::kGoalNormalization ) {}

TransitionResult SessionStageMachine::advance( const std::string &to )
{
    TransitionResult result;
    result.from = mStage;
    result.to = to;
    if ( terminal() )
    {
        result.error = { error_codes::kAlreadyTerminal,
                         "session is terminal (" + mTerminal + "); no further transitions" };
        return result;
    }
    if ( !isLegalTransition( mStage, to ) )
    {
        result.error = { error_codes::kIllegalTransition,
                         "illegal transition " + mStage + " -> " + to };
        return result;
    }
    mStage = to;
    if ( isKnownTerminalState( to ) )
        mTerminal = to; // advancing into a terminal state ends the session
    else if ( to == stages::kReplan )
        ++mReplans;
    result.ok = true;
    return result;
}

TransitionResult SessionStageMachine::terminate( const std::string &terminalState,
                                                 const std::string &stopReason )
{
    TransitionResult result;
    result.from = mTerminal.empty() ? mStage : mTerminal;
    result.to = terminalState;
    result.stopReason = stopReason;
    if ( terminal() )
    {
        result.error = { error_codes::kAlreadyTerminal,
                         "session is already terminal (" + mTerminal + ")" };
        return result;
    }
    if ( !isKnownTerminalState( terminalState ) )
    {
        result.error = { error_codes::kUnknownTerminalState,
                         "unknown terminal state '" + terminalState + "'" };
        return result;
    }
    if ( !isLegalTransition( mStage, terminalState ) )
    {
        result.error = { error_codes::kIllegalTransition,
                         "cannot terminate from " + mStage };
        return result;
    }
    mTerminal = terminalState;
    result.ok = true;
    return result;
}

TransitionResult SessionStageMachine::rewindTo( const std::string &stage, int replanCount )
{
    TransitionResult result;
    result.from = mStage;
    result.to = stage;
    if ( terminal() )
    {
        result.error = { error_codes::kAlreadyTerminal,
                         "session is terminal (" + mTerminal + "); cannot rewind" };
        return result;
    }
    if ( !isKnownStage( stage ) )
    {
        result.error = { error_codes::kUnknownStage,
                         "cannot rewind to '" + stage + "'" };
        return result;
    }
    mStage = stage;
    mReplans = replanCount < 0 ? 0 : replanCount;
    result.ok = true;
    return result;
}

} // namespace sicnu::agent_loop
