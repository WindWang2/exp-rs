// src/agentbench/failure_taxonomy.h
#pragma once

//
// Closed outcome-failure taxonomy for graded agent trajectories.
//
// Every graded run receives exactly one class; "none" is reserved for passing
// runs. The vocabulary is closed: classification (evaluator) may only emit
// values from this table, and case documents may only *expect* values from
// this table. Never rename a wire string — bump the case schema instead.
//

#include <string>
#include <vector>

namespace sicnu::agentbench
{

/// Closed failure classes, in table order.
std::vector<std::string> allFailureClasses();

/// True when `failureClass` is part of the closed taxonomy.
bool isValidFailureClass( const std::string &failureClass );

namespace failure_classes
{
inline constexpr const char *kNone = "none";                       // passing run
inline constexpr const char *kNotStarted = "not_started";          // empty trajectory
inline constexpr const char *kIncomplete = "incomplete";           // stopped early, goal unmet
inline constexpr const char *kScopeViolation = "scope_violation";  // used disallowed tools / escaped scope
inline constexpr const char *kBudgetExhausted = "budget_exhausted"; // ran out of calls/tokens
inline constexpr const char *kInvalidScience = "invalid_science";  // violated scientific invariants
inline constexpr const char *kVerificationFailed = "verification_failed"; // evidence checks failed
inline constexpr const char *kSilentFailure = "silent_failure";    // claimed success over failed work
inline constexpr const char *kRecoveryFailed = "recovery_failed";  // mishandled an injected fault
inline constexpr const char *kClaimMismatch = "claim_mismatch";    // outcome claim contradicts evidence
inline constexpr const char *kImpossibleTask = "impossible_task";  // proceeded despite an impossible goal
} // namespace failure_classes

} // namespace sicnu::agentbench
