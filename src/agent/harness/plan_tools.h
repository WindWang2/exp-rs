// src/agent/harness/plan_tools.h
#pragma once

#include "spatial_tools/spatial_tool.h"

namespace sicnu::agent::harness {

/// Registers the plan lifecycle tools (mission Phases 5-10, 13):
///   harness:preflight     deterministic scientific preflight
///   harness:plan          validate + compile a plan (workflow JSON + estimates)
///   harness:execute_plan  preflight -> compile -> authoritative workflow run
///   harness:run_status    observe the real run state + verify outputs
/// Idempotent.
void registerPlanTools();

} // namespace sicnu::agent::harness
