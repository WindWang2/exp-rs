// src/planner/planner_core.h
#pragma once

//
// RS14-09 Scientific Task Planner — deterministic baseline planner (slice B).
//
// planScientificWork: (ScientificGoal, PlanningContext, PlannerProviders) →
// ranked candidate ScientificPlans. Deterministic: same inputs ⇒ same plans,
// byte-identical JSON, same fingerprints. Planning ONLY — this never executes
// an operator, never opens a dataset, never touches TaskCenter.
//
// Behavior contract (slices B–D):
//   - staged spine per goal kind (planner_rules), with conditional gates:
//     cross-scene grid mismatch inserts an alignment stage; a numeric-domain
//     gap inserts a calibration bridge ONLY when a contracts-verified
//     calibration capability maps asset domain → analysis input domain;
//     otherwise a typed blocking question is raised — no silent fallback.
//   - goal acceptance criteria land as verifier targets on the verify stage.
//   - every unmet need (missing scene, unreachable asset, missing capability
//     family, unbridgeable domain) is a typed open question.
//   - every lawful analysis variant becomes a ranked candidate plan; the
//     primary carries alternatives[] with why/whyNot + candidateIndex.
//

#include "contracts/scientific_contract.h"
#include "planner/planning_context.h"
#include "planner/provider_interfaces.h"
#include "planner/scientific_goal.h"
#include "planner/scientific_plan.h"

#include <string>
#include <vector>

namespace sicnu::planner {

struct PlanningResult
{
    /// Ranked lawful candidates (best first). Never empty: an unlawful/
    /// infeasible situation yields one infeasible plan carrying the typed
    /// reasons, so consumers always have an explainable document.
    std::vector<ScientificPlan> candidates;

    /// @returns the primary (best-ranked) plan; nullptr when candidates empty
    /// (cannot happen through planScientificWork).
    const ScientificPlan *primary() const
    {
        return candidates.empty() ? nullptr : &candidates.front();
    }
};

/// Deterministic baseline planner. `providers.capability` must be non-null —
/// a missing provider seam is a typed infeasible plan (fail-closed), never a
/// default capability table.
PlanningResult planScientificWork( const ScientificGoal &goal, const PlanningContext &context,
                                   const PlannerProviders &providers );

/// Shared guards used by the proposal validator too (same rules for external
/// plans and self-generated ones).
/// @returns the contract for @p operatorId via the linked contracts registry,
/// or nullptr when the operator has no scientific contract.
const sicnu::contracts::ScientificContract *contractForOperator(
    const std::string &operatorId );

/// True when the provider capability's declared domains agree with the linked
/// contracts registry for the same operator (the single truth source check).
bool capabilityAgreesWithContracts( const PlannerCapability &capability );

} // namespace sicnu::planner
