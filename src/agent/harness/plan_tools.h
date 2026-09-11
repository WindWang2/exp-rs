// src/agent/harness/plan_tools.h
#pragma once

#include "agent_plan.h"

#include "../spatial_tools/spatial_tool.h"

namespace sicnu::agent::harness {

/// Registers the plan lifecycle tools (mission Phases 5-10, 13):
///   harness:preflight     deterministic scientific preflight
///   harness:plan          validate + compile a plan (workflow JSON + estimates)
///   harness:execute_plan  preflight -> compile -> authoritative workflow run
///   harness:run_status    observe the real run state + verify outputs
///   harness:explain       evidence-driven explanation of a run (8.0)
/// Idempotent.
void registerPlanTools();

/// Harness 8.0 (Area E): validates a plan's identity pins against the
/// resolved datasets. Empty error = all pins hold; otherwise a typed
/// blocking error (IDENTITY_MISMATCH or the resolution failure).
HarnessError validatePlanIdentity( const AgentPlan &plan );

} // namespace sicnu::agent::harness
