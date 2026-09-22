// src/agent_ops/recovery_bridge.cpp
#include "agent_ops/recovery_bridge.h"
#include "agent_ops/autonomy_ops_gate.h"
#include "repair_planner/repair_schema.h"

namespace sicnu::agent_ops {

Json::Value RecoveryBridge::projectRepairPlan(const OpDiagnostic &diagnostic,
                                              const std::string &intent) const
{
    sicnu::repair::RepairPlan plan;
    plan.intent = intent.empty() ? "ops_recovery" : intent;
    plan.subject = diagnostic.rootCauseCode;
    plan.status = diagnostic.proposals.empty() ? sicnu::repair::plan_status::kNoSafeRepair
                                               : sicnu::repair::plan_status::kPlanned;
    plan.resolvesAllBlockers = !diagnostic.proposals.empty();
    plan.provenance["planner"] = sicnu::repair::kPlannerId;
    plan.provenance["source"] = "agent_ops.recovery_bridge";
    int n = 0;
    for (const auto &ruleId : diagnostic.proposals)
    {
        sicnu::repair::RepairAction action;
        action.id = "ra-ops-" + std::to_string(++n);
        action.ruleId = ruleId;
        action.kind = sicnu::repair::action_kind::kCapabilityRef;
        action.operatorId = ruleId.find(':') != std::string::npos ? ruleId : ("rs:" + ruleId);
        action.riskClass = sicnu::repair::repair_risk::kShapePreserving;
        action.risk.riskClass = action.riskClass;
        action.risk.severity = "low";
        action.cost.rank = 2;
        action.cost.costClass = "light";
        action.sourceFinding["code"] = diagnostic.rootCauseCode;
        plan.selected.push_back(action);
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
        out.repairPlan = projectRepairPlan(diagnostic, ctx.intent);
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
