// src/agent/harness/plan_tools.h
#pragma once

#include "agent_plan.h"

#include "../spatial_tools/spatial_tool.h"

#include <workflow/workflow_run.h>

#include <json/json.h>

#include <memory>

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

/// Builds the harness:run_status result document for a run: per-step
/// states, the aggregate verification verdict over the run's completed
/// outputs (FAIL can never surface as success), and the evidence sidecar
/// report. `persistEvidence` (default true) controls the evidence sidecar
/// writes, the ledger rebind, and map confirmation — observation surfaces
/// (harness:explain) pass false so they stay strictly read-only
/// (adversarial review P1). Declared here so the document contract is
/// directly testable.
Json::Value runResultDocument( const std::shared_ptr<sicnu::workflow::WorkflowRun> &run,
                               const AgentPlan *plan, bool persistEvidence = true );

} // namespace sicnu::agent::harness
