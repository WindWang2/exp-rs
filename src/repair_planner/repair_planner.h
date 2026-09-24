// src/repair_planner/repair_planner.h
#pragma once

//
// RS14-03 completion slice E: the deterministic planner.
//
// findings -> requirements -> provider-driven candidates -> policy decisions
// -> a versioned repair_plan/1.0 document plus a repair_result/1.0 receipt.
//
// Determinism contract: the same findings, provider answers, context and
// options always produce byte-identical documents. Requirement order is the
// synthesis order (severity desc, code asc, subject asc); candidate order is
// (cost rank asc, operator id asc); ids are positional. No wall-clock, no
// randomness.
//
// Fail-closed contract: unknown finding codes surface as typed unresolved
// entries (cause "unsupported_finding"); kinds the provider cannot serve as
// cause "no_candidate"; entries without a usable cost as documented refusals
// (refusal_cause "cost_unknown"); budget overruns as counted truncation.
// Nothing unavailable is ever presented as success.
//
// PLANNING-ONLY: the planner names existing capabilities and records what a
// caller may do; it never executes a repair, opens a dataset, or mutates a
// registry.

#include <json/json.h>
#include <string>
#include <vector>

#include "repair_provider.h"
#include "repair_policy.h"
#include "repair_schema.h"

namespace sicnu::repair {

/// Closed typed causes for requirements that resolved to nothing offerable.
namespace unresolved_cause {
inline constexpr const char *kNoCandidate = "no_candidate";
inline constexpr const char *kUnsupportedFinding = "unsupported_finding";
inline constexpr const char *kBudgetExhausted = "budget_exhausted";
inline constexpr const char *kAllCandidatesRefused = "all_candidates_refused";
} // namespace unresolved_cause

struct RepairPlannerOptions
{
    std::string intent;                 ///< caller intent, carried verbatim
    int maxRequirements = 64;           ///< deterministic cap, counted when hit
    int maxCandidatesPerRequirement = 8;
    int maxTotalCandidates = 128;       ///< across the whole plan
};

struct RepairPlannerOutcome
{
    RepairPlan plan;     ///< versioned repair_plan/1.0 content
    Json::Value result;  ///< repair_result/1.0 receipt (planning outcome)
};

/// Plans repairs for the given finding documents. Returns false with a typed
/// error for malformed input (empty findings, non-object findings).
bool planRepairsForFindings( const std::vector<Json::Value> &findings,
                             const RepairCapabilityProvider &provider,
                             const RepairPolicyContext &context,
                             const RepairPlannerOptions &options,
                             RepairPlannerOutcome &out, RepairError &error );

/// sha256/16 over the canonical serialization of the finding documents — the
/// digest the plan and result carry so state can link them tamper-evidently.
std::string findingsDigest( const std::vector<Json::Value> &findings );

/// Extracts one requirement's slice of a finished plan as a
/// repair_fragment/1.0 document: the requirement, its selected candidate and
/// its alternatives, linked to the plan id and fingerprint. Deterministic.
/// Unknown requirement ids are typed invalid_input, never an empty success.
bool repairPlanFragment( const RepairPlan &plan, const std::string &requirementId,
                         Json::Value &fragment, RepairError &error );

} // namespace sicnu::repair
