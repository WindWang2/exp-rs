// src/planner/planner_constraints.h
#pragma once

//
// RS14-09 Scientific Task Planner — constraints / resources / risks (slice C).
//
// Constraints narrow the candidate set while a lawful sibling exists and turn
// unmet budgets into typed risks + blocking decisions while keeping the plan
// fully visible (no silent truncation: an over-budget plan is annotated, not
// clipped).
//

#include "planner/planning_context.h"
#include "planner/planner_core.h"
#include "planner/provider_interfaces.h"
#include "planner/scientific_plan.h"

#include <string>
#include <vector>

namespace sicnu::planner {

/// True when @p operatorId is forbidden by the context constraints.
bool isOperatorForbidden( const PlanningContext &context, const std::string &operatorId );

/// True when the capability satisfies the required-determinism constraint
/// (a stochastic operator only fails when determinism is REQUIRED).
bool satisfiesDeterminismRequirement( const PlanningContext &context,
                                      const PlannerCapability &capability );

/// True when the capability's family passes the allowed-family allowlist
/// (empty allowlist = unrestricted).
bool satisfiesFamilyAllowlist( const PlanningContext &context, const std::string &family );

/// Narrowing check bundle for one provider capability.
bool isLawfulCandidate( const PlanningContext &context, const PlannerCapability &capability );

/// Applies resource budgets to a BUILT plan in place: appends
/// resource_over_budget risks and typed blocking decision questions for
/// step-count / RAM / cost-class overruns. The plan stays fully visible.
void applyResourceBudget( const PlanningContext &context, ScientificPlan &plan );

/// True when any blocking question was introduced by the budget pass.
bool budgetExceeded( const PlanningContext &context, const ScientificPlan &plan );

} // namespace sicnu::planner
