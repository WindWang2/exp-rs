// src/repair_planner/repair_schema.h
#pragma once

//
// RS14-03 Scientific Repair Planner (ADR 0174): explicit, auditable,
// rejectable repair candidate plans over preflight findings.
//
// PLANNING-ONLY: this module never executes a repair, never opens a
// dataset, and never mutates a registry. Its output is a versioned
// document (`kind: "repair_plan"`, `schema_version: "1.0"`) that names
// EXISTING registered capabilities; a caller decides — and stays
// accountable for — whatever happens next.
//
// Representation discipline:
//   - ranking-independent: a plan's meaning does not depend on any score;
//     ordering is by the closed rule table + requirement order only.
//   - deterministic: same inputs -> byte-identical JSON and fingerprint.
//   - fail-closed: unknown codes, unknown operators and malformed inputs
//     surface as typed refusals, never as silent fallbacks.
//
// Wire vocabulary alignment (single-truth discipline, pinned by tests):
//   - risk classes mirror src/agent/harness/workflow_repair.h repair_risk::*
//     byte-for-byte ("shape_preserving" | "radiometric" | "science_changing").
//   - cost rank follows the compiler's decisionCostRank convention: a closed
//     1..9 ordering device, never a wall-clock claim.
//   - action keys reference the harness closed action table (harness_actions)
//     and the preflight preparation whitelist; this module invents none.

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::repair {

/// Wire envelope identity.
inline constexpr const char *kKind = "repair_plan";
inline constexpr const char *kSchemaVersion = "1.0";
/// Provenance marker for documents this module produced.
inline constexpr const char *kPlannerId = "sicnu::repair/1.0";

/// Closed repair risk classes — byte-identical to harness repair_risk::*.
namespace repair_risk {
inline constexpr const char *kShapePreserving = "shape_preserving";
inline constexpr const char *kRadiometric = "radiometric";
inline constexpr const char *kScienceChanging = "science_changing";
} // namespace repair_risk

/// Closed candidate kinds.
namespace action_kind {
inline constexpr const char *kCapabilityRef = "capability_ref";            ///< names a registered operator
inline constexpr const char *kDeclarativeTransform = "declarative_transform"; ///< order/filter/policy step, no kernel
inline constexpr const char *kDecision = "decision";                       ///< a choice to surface, nothing to run
} // namespace action_kind

/// Closed result-level statuses.
namespace plan_status {
inline constexpr const char *kPlanned = "planned";
inline constexpr const char *kNoSafeRepair = "no_safe_repair";
inline constexpr const char *kInvalidInput = "invalid_input";
} // namespace plan_status

/// Typed planner error (machine-readable; never prose-only).
struct RepairError
{
    std::string code;    ///< closed: invalid_document | unsupported_version | invalid_action | invalid_plan
    std::string message;
};

bool isKnownRiskClass( const std::string &riskClass );
bool isKnownActionKind( const std::string &kind );
bool isKnownPlanStatus( const std::string &status );

/// Estimated cost of one candidate. `rank` follows the compiler's closed
/// 1..9 decisionCostRank convention (1 = cheapest wiring, 9 = replace the
/// dataset); 0 means "unset" and is legal only on refusals. `costClass` is
/// the capability knowledge resource vocabulary (light|medium|heavy).
struct RepairCost
{
    int rank = 0;
    std::string costClass;
    std::string notes;
};

/// Declared risk of one candidate beyond its class.
struct RepairRisk
{
    std::string riskClass;    ///< repair_risk::*
    std::string severity;     ///< low | medium | high
    bool irreversible = false;
    std::string notes;
};

/// One explicit repair candidate. A candidate with a non-empty
/// `refusalCause` is a DOCUMENTED refusal: it still carries the full
/// before/after/loss/cost contract so a reviewer can audit why it was
/// rejected — it is never executable.
struct RepairAction
{
    std::string id;             ///< deterministic, e.g. "ra-<requirementId>-<n>"
    std::string ruleId;         ///< closed rule-table key that produced the candidate
    std::string kind;           ///< action_kind::*
    std::string operatorId;     ///< existing registered operator (capability_ref only)
    std::string actionKey;      ///< harness closed action-table key ("" when none applies)
    Json::Value params{Json::objectValue};
    std::string riskClass;      ///< repair_risk::*
    Json::Value beforeState{Json::objectValue}; ///< predicted pre-repair state
    Json::Value afterState{Json::objectValue};  ///< predicted post-repair state
    std::vector<std::string> informationLoss;   ///< closed loss annotations
    std::vector<std::string> assumptions;       ///< declared preconditions
    RepairCost cost;
    RepairRisk risk;
    bool factsSufficient = true;         ///< false => missingFacts explains what is missing
    std::string refusalCause;            ///< closed refusal vocabulary ("" = offerable)
    Json::Value sourceFinding{Json::objectValue}; ///< {code, severity, item_id?}
    Json::Value factsUsed{Json::objectValue};
    Json::Value missingFacts{Json::arrayValue};
};

Json::Value repairActionToJson( const RepairAction &action );
bool repairActionFromJson( const Json::Value &doc, RepairAction &action, RepairError &error );
bool validateRepairAction( const RepairAction &action, RepairError &error );

/// One complete candidate plan: one chosen candidate per requirement, in
/// requirement order. `unresolved` lists requirements for which every
/// candidate was refused.
struct RepairPlan
{
    std::string planId;      ///< "srp-<sha256/16>" over canonical content
    std::string intent;
    std::string subject;
    std::string status;      ///< plan_status::*
    bool resolvesAllBlockers = false;
    std::vector<RepairAction> selected;
    std::vector<Json::Value> unresolved;   ///< {requirement_id, cause, detail}
    Json::Value requirements{Json::arrayValue};
    Json::Value alternatives{Json::arrayValue};
    Json::Value policy{Json::objectValue};
    Json::Value bounds{Json::objectValue};
    Json::Value provenance{Json::objectValue};
    Json::Value noSafeRepair{Json::Value()}; ///< object with typed causes, or null
};

/// Canonical compact serialization (jsoncpp object members iterate in sorted
/// key order, so this is canonical without extra work).
std::string jsonToString( const Json::Value &doc );

/// SHA-256 truncated to 16 lowercase hex chars over the plan's canonical
/// scientific content (plan_id excluded — content addressing, like
/// agent_plan planFingerprint).
std::string repairPlanFingerprint( const RepairPlan &plan );

/// Computes the fingerprint and stores it as planId ("srp-<fp>"). Returns
/// the assigned id. Idempotent: recomputing over unchanged content yields
/// the same id.
std::string assignRepairPlanIdentity( RepairPlan &plan );

Json::Value repairPlanToJson( const RepairPlan &plan );

/// Fail-closed envelope reader: rejects non-objects, wrong kinds and
/// unsupported versions with typed errors. Full-schema validation lives in
/// the provenance slice.
bool readRepairPlan( const Json::Value &doc, RepairPlan &plan, RepairError &error );

} // namespace sicnu::repair
