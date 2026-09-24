// src/agent_ops/recovery_bridge.cpp
#include "agent_ops/recovery_bridge.h"
#include "agent_ops/autonomy_ops_gate.h"
#include "repair_planner/repair_policy.h"
#include "repair_planner/repair_schema.h"

#include <map>

namespace sicnu::agent_ops {
namespace {

/// Risk classes are fail-closed: only the three known classes pass through;
/// anything unknown or missing resolves to the STRICTEST class. A projected
/// plan may understate nothing — a wrong "shape_preserving" label here was
/// the fail-open path that would let a science-changing rule look harmless
/// to a downstream consumer of the plan document.
std::string failClosedRiskClass(const std::string &candidate)
{
    if (candidate == sicnu::repair::repair_risk::kShapePreserving)
        return candidate;
    if (candidate == sicnu::repair::repair_risk::kRadiometric)
        return candidate;
    return sicnu::repair::repair_risk::kScienceChanging;
}

std::string severityForRiskClass(const std::string &riskClass)
{
    if (riskClass == sicnu::repair::repair_risk::kShapePreserving)
        return "low";
    if (riskClass == sicnu::repair::repair_risk::kRadiometric)
        return "medium";
    return "high";
}

} // namespace

Json::Value RecoveryBridge::projectRepairPlan(const OpDiagnostic &diagnostic,
                                              const std::string &intent) const
{
    return projectRepairPlan(diagnostic, intent, PlanHints{});
}

Json::Value RecoveryBridge::projectRepairPlan(const OpDiagnostic &diagnostic,
                                              const std::string &intent,
                                              const PlanHints &hints) const
{
    // Per-proposal risk evidence recorded by the diagnostic sources.
    std::map<std::string, std::string> detailRisk;
    for (const auto &detail : diagnostic.proposalDetails)
    {
        if (!detail.isObject() || !detail.isMember("rule_id") || !detail["rule_id"].isString())
            continue;
        if (detail.isMember("risk_class") && detail["risk_class"].isString())
            detailRisk[detail["rule_id"].asString()] = detail["risk_class"].asString();
    }

    sicnu::repair::RepairPlan plan;
    plan.intent = intent.empty() ? "ops_recovery" : intent;
    plan.subject = diagnostic.rootCauseCode;
    plan.status = diagnostic.proposals.empty() ? sicnu::repair::plan_status::kNoSafeRepair
                                               : sicnu::repair::plan_status::kPlanned;
    plan.resolvesAllBlockers = !diagnostic.proposals.empty();
    plan.provenance["planner"] = sicnu::repair::kPlannerId;
    plan.provenance["source"] = "agent_ops.recovery_bridge";
    // Planning-only context: the recorded approval and the teaching context
    // ride along as AUDIT — evaluateRepairPolicy, not this projection,
    // decides what a caller may do with each action.
    plan.policy["science_change_approved"] = hints.scienceChangeApproved;
    plan.policy["auto_executable"] = false;

    sicnu::repair::RepairPolicyContext policyContext;
    policyContext.domain = hints.domain;
    policyContext.role = hints.role;
    policyContext.scienceChangeApproved = hints.scienceChangeApproved;
    policyContext.allowAutonomousExec = false; // ops projections never auto-execute

    int n = 0;
    for (const auto &ruleId : diagnostic.proposals)
    {
        sicnu::repair::RepairAction action;
        action.id = "ra-ops-" + std::to_string(++n);
        action.ruleId = ruleId;
        action.kind = sicnu::repair::action_kind::kCapabilityRef;
        action.operatorId = ruleId.find(':') != std::string::npos ? ruleId : ("rs:" + ruleId);
        // No executable action key is known for an ops-projected rule id; ""
        // keeps evaluateRepairPolicy from ever calling this auto-executable.
        action.actionKey = "";
        auto detail = detailRisk.find(ruleId);
        const std::string riskClass =
            failClosedRiskClass(detail != detailRisk.end()
                                    ? detail->second
                                    : hints.leadingRiskClass);
        action.riskClass = riskClass;
        action.risk.riskClass = action.riskClass;
        action.risk.severity = severityForRiskClass(action.riskClass);
        action.cost.rank = 2;
        action.cost.costClass = "light";
        action.sourceFinding["code"] = diagnostic.rootCauseCode;
        plan.selected.push_back(action);

        // The policy authority's own verdict for this action, recorded in
        // the plan (planning-only; a caller decides, nothing executes).
        const auto decision = sicnu::repair::evaluateRepairPolicy(action, policyContext);
        Json::Value entry(Json::objectValue);
        entry["action_id"] = action.id;
        entry["rule_id"] = action.ruleId;
        entry["risk_class"] = action.riskClass;
        entry["decision"] = decision.decision;
        entry["reason_code"] = decision.reasonCode;
        plan.policy["actions"].append(entry);
    }
    if (plan.selected.empty())
    {
        plan.noSafeRepair = Json::objectValue;
        plan.noSafeRepair["cause"] = "no_proposals";
        plan.noSafeRepair["root_cause"] = diagnostic.rootCauseCode;
    }
    sicnu::repair::assignRepairPlanIdentity(plan);
    return sicnu::repair::repairPlanToJson(plan);
}

RecoveryDecision RecoveryBridge::decide(const OpDiagnostic &diagnostic,
                                        const RecoveryContext &ctx) const
{
    RecoveryDecision out;
    out.attempt = ctx.attempt;
    out.replanCount = ctx.replanCount;
    out.repairCount = ctx.repairCount;
    out.retryCount = ctx.retryCount;
    out.diagnostic = diagnostic.toJson();
    out.budgets["max_retries"] = ctx.budgets.maxRetries;
    out.budgets["max_replans"] = ctx.budgets.maxReplans;
    out.budgets["max_repairs"] = ctx.budgets.maxRepairs;
    out.budgets["no_progress_threshold"] = ctx.budgets.noProgressThreshold;
    out.budgets["wall_clock_ms"] = static_cast<Json::Int64>(ctx.budgets.wallClockMs);
    out.budgets["elapsed_ms"] = static_cast<Json::Int64>(ctx.elapsedMs);

    auto abortWith = [&](const std::string &reason) {
        out.action = recovery_action::kAbort;
        out.reasonCode = reason;
        out.autonomyAllowed = false;
        out.autonomyReasonCode = reason;
        return out;
    };

    if (ctx.cancelRequested)
        return abortWith("CANCELLED");

    if (ctx.budgets.wallClockMs > 0 && ctx.elapsedMs >= ctx.budgets.wallClockMs)
        return abortWith("WALL_CLOCK_EXCEEDED");

    if (ctx.identicalFailureCount >= ctx.budgets.noProgressThreshold)
        return abortWith("NO_PROGRESS");

    const std::string preferred =
        diagnostic.advisoryNext.empty() ? recovery_action::kReplan : diagnostic.advisoryNext;

    if (preferred == recovery_action::kAsk)
    {
        out.action = recovery_action::kAsk;
        out.reasonCode = "NEEDS_HUMAN";
        out.needsApproval = true;
        out.autonomyAllowed = true; // ask is non-mutating
        out.autonomyReasonCode = sicnu::agent::autonomy::autonomy_reason_codes::kAllowed;
        return out;
    }

    if (preferred == recovery_action::kAbort)
        return abortWith(diagnostic.rootCauseCode.empty() ? "DIAGNOSTIC_ABORT"
                                                          : diagnostic.rootCauseCode);

    if (preferred == recovery_action::kRetry)
    {
        if (ctx.retryCount >= ctx.budgets.maxRetries)
            return abortWith("MAX_RETRIES");
        OpsAutonomyRequest req;
        req.mutateKind = ops_mutate::kExecute;
        req.domain = ctx.domain;
        req.role = ctx.role;
        req.intent = ctx.intent;
        req.actionKey = "ops:retry";
        const auto gate = gateMutatingOp(ctx.autonomyPolicy, req);
        out.autonomyAllowed = gate.allowed;
        out.autonomyReasonCode = gate.reasonCode;
        if (!gate.allowed)
            return abortWith(gate.reasonCode);
        out.action = recovery_action::kRetry;
        out.reasonCode = "RETRYABLE";
        return out;
    }

    if (preferred == recovery_action::kRepair)
    {
        if (ctx.repairCount >= ctx.budgets.maxRepairs)
            return abortWith("MAX_REPAIRS");
        RecoveryBridge::PlanHints hints;
        hints.leadingRiskClass = ctx.leadingRiskClass;
        hints.scienceChangeApproved = ctx.humanApprovedRepair;
        hints.domain = ctx.domain;
        hints.role = ctx.role;
        out.repairPlan = projectRepairPlan(diagnostic, ctx.intent, hints);
        const bool scienceChanging =
            ctx.leadingRiskClass == "radiometric" || ctx.leadingRiskClass == "science_changing";
        if (scienceChanging && !ctx.humanApprovedRepair)
        {
            out.action = recovery_action::kAsk;
            out.reasonCode = "REPAIR_NEEDS_APPROVAL";
            out.needsApproval = true;
            out.autonomyAllowed = true;
            out.autonomyReasonCode = sicnu::agent::autonomy::autonomy_reason_codes::kAllowed;
            return out;
        }
        OpsAutonomyRequest req;
        req.mutateKind = ops_mutate::kRepair;
        req.domain = ctx.domain;
        req.role = ctx.role;
        req.intent = ctx.intent;
        req.actionKey = "ops:repair";
        req.riskClass = ctx.leadingRiskClass;
        const auto gate = gateMutatingOp(ctx.autonomyPolicy, req);
        out.autonomyAllowed = gate.allowed;
        out.autonomyReasonCode = gate.reasonCode;
        if (!gate.allowed)
        {
            out.action = recovery_action::kAsk;
            out.reasonCode = gate.reasonCode;
            out.needsApproval = true;
            return out;
        }
        out.action = recovery_action::kRepair;
        out.reasonCode = "REPAIR_PROCEED";
        return out;
    }

    // Default: replan
    if (ctx.replanCount >= ctx.budgets.maxReplans)
        return abortWith("MAX_REPLANS");
    OpsAutonomyRequest req;
    req.mutateKind = ops_mutate::kWorkflowMutate;
    req.domain = ctx.domain;
    req.role = ctx.role;
    req.intent = ctx.intent;
    req.actionKey = "ops:replan";
    const auto gate = gateMutatingOp(ctx.autonomyPolicy, req);
    out.autonomyAllowed = gate.allowed;
    out.autonomyReasonCode = gate.reasonCode;
    if (!gate.allowed)
    {
        out.action = recovery_action::kAsk;
        out.reasonCode = gate.reasonCode;
        out.needsApproval = true;
        return out;
    }
    out.action = recovery_action::kReplan;
    out.reasonCode = "REPLAN";
    return out;
}

} // namespace sicnu::agent_ops
