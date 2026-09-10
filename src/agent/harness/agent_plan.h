// src/agent/harness/agent_plan.h
#pragma once

//
// Harness 4.0 AgentPlan v2 (mission Phases 6/7/8).
//
// A versioned, structured plan document that Pi produces by filling slots —
// never free prose. Plans compile to WorkflowDefinition JSON and run through
// the authoritative WorkflowRunCoordinator → TaskCenter stack; the harness
// creates no second scheduler.
//
// Document shape (envelope kind "execution_plan", schema_version "2.0"):
// {
//   plan_id, goal, intent ("ndvi"|"change"|"sar_change"|"classify"|
//                          "phenology"|"" = custom),
//   inputs:  [{ name, ref, entity? }]        — refs resolved at preflight,
//   steps:   [{ id, operator_id, params,
//               inputs: [{step, port, to_port}],
//               verification: "raster"|"vector"|"skip" }],
//   outputs: [{ name, from_step, port, kind }],
//   verification: { enabled, final_map: bool },
//   map_output: { layout_name, mapspec?, export_path? } | null,
//   estimates: { total_ram_mb, per_step: {stepId: mb} }   — filled by planner
// }
//
// v1 documents (3.0 ExecutionPlan: steps + estimates only) are accepted by
// the reader and gain the defaults; one schema, two versions, one compiler.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "harness_error.h"

namespace sicnu::agent::harness {

inline constexpr const char *kAgentPlanSchemaVersion = "2.0";

/// Closed intent vocabulary driving scientific preflight rule packs.
/// "" (empty) means a custom plan with no intent-specific preflight.
bool isKnownIntent( const std::string &intent );

struct AgentPlanIssue {
  HarnessError error;      ///< typed error (code from the stable taxonomy)
  std::string stepId;      ///< offending step, may be empty
  bool repairable = false;
};

struct AgentPlan {
  std::string planId;
  std::string goal;
  std::string intent;
  Json::Value inputs{Json::arrayValue};    ///< [{name, ref}]
  Json::Value steps{Json::arrayValue};     ///< [{id, operator_id, params, inputs, verification}]
  Json::Value outputs{Json::arrayValue};   ///< [{name, from_step, port, kind}]
  Json::Value verification{Json::objectValue};
  Json::Value mapOutput{Json::Value()};    ///< null or object
  Json::Value raw{Json::objectValue};      ///< original document (estimates kept)

  /// Harness 8.0 (Area E): identity pins. Shape:
  /// { "datasets": { "<slot>": { "asset_entity_id"?: "asset-N",
  ///                              "asset_id"?: "<uuid>", "path"?: "...",
  ///                              "revision"?: N } },
  ///   "model"?: "<id>" | "<id>@<version>" }
  /// A pinned slot must exist in `inputs` and — at execute time — resolve to
  /// the SAME dataset; a mismatch is a typed IDENTITY_MISMATCH blocker, so a
  /// silently re-registered or renamed input can never feed the plan.
  Json::Value pins{Json::Value()};
  /// Harness 8.0 (Area E): intermediate-artifact cleanup policy declared for
  /// the run. "keep_all" (default) | "keep_outputs". Validated here and
  /// forwarded into the compiled workflow metadata (engine consumption is an
  /// execution-plane follow-up); harness:explain reports the policy.
  std::string cleanup;                       ///< "" = keep_all
};

/// Harness 8.0 (Area E): deterministic plan fingerprint — SHA-256 (first 16
/// hex chars) over the canonical compact serialization of the plan's
/// scientific content (intent, inputs, steps with operator/params/wiring/
/// verification/role, outputs, verification block). Identical plans →
/// identical fingerprint regardless of plan_id/timestamps; any change to the
/// science changes it. Recorded in plan bindings, the execute_plan response,
/// and every evidence sidecar for reproducibility bookkeeping.
std::string planFingerprint( const AgentPlan &plan );

/// Reads a plan document (v2 or legacy v1). Returns false with a typed error.
bool readAgentPlan( const Json::Value &doc, AgentPlan &plan, HarnessError &error );

/// Structural validation: unique step ids, referential integrity, declared
/// outputs reference existing steps, operator ids resolvable in the
/// registries. Produces typed, repairable issues — never throws.
std::vector<AgentPlanIssue> validateAgentPlan( const AgentPlan &plan );

/// Compiles a plan to WorkflowDefinition JSON — the single bridge to the
/// authoritative engine. Fails (empty string + error) when structural
/// validation finds errors. Resource estimates and verification policies are
/// forwarded; $step.port placeholders in params are preserved verbatim.
std::string compilePlanToWorkflowJson( const AgentPlan &plan, HarnessError &error );

/// Resource estimates per mission Phase 8: per-step RAM (operator-declared,
/// params-aware when the operator implements it) plus the plan aggregate.
/// `0` means the operator declared nothing (TaskCenter applies its own
/// fallback); the aggregate sums only declared values.
Json::Value estimatePlanResources( const AgentPlan &plan );

/// Bounded plan summary for tool responses (no params bodies — those can be
/// large; the agent already knows what it authored).
Json::Value planSummary( const AgentPlan &plan );

} // namespace sicnu::agent::harness
