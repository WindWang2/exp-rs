// src/planner/plan_ir_projection.h
#pragma once

//
// RS14-09 Scientific Task Planner — WorkflowIR projection (slice F).
//
// Emits a workflow_ir 1.0-SHAPED document (kind/schema_version/inputs/nodes/
// outputs/expectations) so a future adapter can hand a plan to the existing
// lowering chain (readWorkflowIr → AgentPlan v2 → engine JSON). Data-level
// conformance only: the planner is Qt-free and cannot link the harness to
// run readWorkflowIr itself; conformance is pinned by fixtures and drift
// tests, and the harness reader remains the final authority.
//
// The contracts numeric-domain axis and the harness artifact_facts axis both
// exist in this repo; the projection carries an EXPLICIT, fully covering map
// between them. Domains without an honest artifact_facts token project to
// "unknown" WITH an explicit warning — downstream checks degrade, they never
// fake a verdict.
//

#include "planner/scientific_plan.h"

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::planner {

/// Projects @p plan into a workflow_ir 1.0-shaped document.
/// @param warnings optional sink for honest-degradation warnings (e.g.
///   "radiance has no artifact_facts token — projected as unknown").
/// @param error typed failure reason (closed prefix invalid_document /
///   unsupported_projection) when projection is refused: a plan without
///   steps, a plan failing structural validation, or a step without any
///   state basis (no operator and no expected transitions).
/// @returns the projected document; null on refusal.
Json::Value projectPlanToIr( const ScientificPlan &plan, std::vector<std::string> *warnings,
                             std::string *error );

/// The explicit contracts→artifact_facts domain map (single definition).
/// @returns the artifact_facts token for a contracts numeric domain, or ""
/// when the domain carries no raster-surface fact ("none"/"any").
/// Unknown domains (must not happen: readers gate on the contracts
/// vocabulary) return "" too — the projection never invents tokens.
std::string artifactFactsTokenForDomain( const std::string &contractsDomain );

/// True when the domain map intentionally degrades to "unknown" (and thus
/// carries a warning when projected).
bool domainProjectsToUnknown( const std::string &contractsDomain );

/// The goal-kind → harness-intent map (honest: kinds with no lawful intent
/// entry project to "" plus a warning).
std::string harnessIntentForGoalKind( const std::string &goalKind );

/// The contracts numeric domains the projection map knows. Drift contract:
/// this set must equal the linked sicnu::contracts::kNumericDomains — a
/// domain added to contracts without a map entry fails the drift test.
std::vector<std::string> knownProjectionDomains();

} // namespace sicnu::planner
