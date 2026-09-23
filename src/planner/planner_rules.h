// src/planner/planner_rules.h
#pragma once

//
// RS14-09 Scientific Task Planner — deterministic rule table (plan.md D5).
//
// The rule table expresses ONLY "a goal of this kind needs which capability
// family slots, in which stage order, with which analysis-output contract".
// Concrete operator choice is provider-delegated (CapabilityProvider); the
// table carries a revision that travels inside every plan (observability).
// Rules live in code, not JSON: deterministic, reviewable, no data drift.
//

#include <string>
#include <vector>

namespace sicnu::planner {

/// Revision of the rule table below; travels in every ScientificPlan.
inline constexpr const char *kPlannerRulesRevision = "planner-rules/1";

/// One staged slot of the goal-kind spine.
struct RuleStage
{
    const char *role;     ///< kStepRoles member
    const char *family;   ///< kFamilySlots member
    bool required;        ///< true = stage must resolve or the plan gets a
                          ///< blocking question; false = conditional gate the
                          ///< core inserts only when its condition fires
};

/// Ordered stage spine for @p goalKind. Unknown kind → empty list (core
/// treats that as infeasible; the goal reader already rejects unknown kinds).
const std::vector<RuleStage> &ruleStagesForGoalKind( const std::string &goalKind );

/// Analysis output domains a goal kind accepts (contracts numeric-domain
/// members). The core narrows provider analysis candidates to these; an
/// empty list for the kind means any lawful candidate.
const std::vector<std::string> &allowedAnalysisOutputsForGoalKind( const std::string &goalKind );

/// Human-readable one-line rationale why a stage exists (explanations and
/// teaching views; deterministic).
std::string stageRationale( const std::string &goalKind, const std::string &role,
                            const std::string &family );

} // namespace sicnu::planner
