// src/agent_ops/recovery_bridge.cpp
#include "agent_ops/recovery_bridge.h"
#include "agent_ops/autonomy_ops_gate.h"
#include "repair_planner/repair_planner.h"
#include "repair_planner/repair_schema.h"

namespace sicnu::agent_ops {
namespace {

/// Findings past this hard bound are dropped by the bridge BEFORE planning
/// (the digest then covers the bounded set): a hostile diagnoser embedding
/// 10^5+ issues must not drive synthesis cost, and the planner's own
/// requirement budget still reports the visible truncation on top.
constexpr std::size_t kMaxBridgeFindings = 1024;

/// The typed finding documents the repair planner consumes, derived from the
/// diagnostic's structured evidence. The preflight report embedded in
/// `sources` is the typed producer in the ops path: its issues already carry
/// the finding code vocabulary the requirement synthesizer maps. Nothing is
/// guessed from prose, and proposals (rule ids) are deliberately NOT turned
/// into findings — they are another layer's output, not evidence. The live
/// run() path embeds the bridged diagnostic under sources["bridge"], so its
/// preflight report is consulted there too (same producer, one indirection).
///
/// Polarity: issues that are not objects or carry no usable code are
/// SKIPPED (they cannot even name a finding); an issue WITH a code but an
/// unusable severity is KEPT and fails synthesis — evidence that names
/// itself is never silently dropped, and the typed no_safe_repair/ask puts
/// the human back in charge.
std::vector<Json::Value> repairFindingsFromDiagnostic(const OpDiagnostic &diagnostic)
{
    std::vector<Json::Value> findings;
    if (!diagnostic.sources.isObject())
        return findings;
    const Json::Value *preflight = nullptr;
    if (diagnostic.sources["preflight"].isObject())
        preflight = &diagnostic.sources["preflight"];
    else if (diagnostic.sources["bridge"].isObject() &&
             diagnostic.sources["bridge"]["sources"].isObject() &&
             diagnostic.sources["bridge"]["sources"]["preflight"].isObject())
        preflight = &diagnostic.sources["bridge"]["sources"]["preflight"];
    if (preflight == nullptr || !(*preflight)["issues"].isArray())
        return findings;
    for (const Json::Value &issue : (*preflight)["issues"])
    {
        if (!issue.isObject() || !issue["code"].isString() || issue["code"].asString().empty())
            continue;
        Json::Value finding(Json::objectValue);
        finding["code"] = issue["code"];
        if (issue["severity"].isString())
            finding["severity"] = issue["severity"];
        if (issue["subject"].isString())
            finding["subject"] = issue["subject"];
        Json::Value evidence(Json::objectValue);
        if (issue.isMember("message"))
            evidence["message"] = issue["message"];
        finding["evidence"] = evidence;
        findings.push_back(finding);
        if (findings.size() >= kMaxBridgeFindings)
            break;
    }
    return findings;
}

/// Typed no-safe-repair envelope for the cases where the bridge cannot plan
/// honestly (no capability knowledge wired, no typed findings, malformed
/// findings). A repair_plan/1.0 document with a counted cause — never a
/// fabricated candidate list.
Json::Value noSafeRepairPlan(const std::string &intent, const std::string &cause,
                             const std::string &detail)
{
    sicnu::repair::RepairPlan plan;
    plan.intent = intent.empty() ? "ops_recovery" : intent;
    plan.status = sicnu::repair::plan_status::kNoSafeRepair;
    plan.resolvesAllBlockers = false;
    plan.provenance["planner"] = sicnu::repair::kPlannerId;
    plan.provenance["source"] = "agent_ops.recovery_bridge";
    Json::Value entry(Json::objectValue);
    entry["requirement_id"] = "";
    entry["cause"] = cause;
    if (!detail.empty())
        entry["detail"] = detail;
    plan.unresolved.push_back(entry);
    plan.noSafeRepair = entry;
    sicnu::repair::assignRepairPlanIdentity(plan);
    Json::Value planDoc = sicnu::repair::repairPlanToJson(plan);
    planDoc["planning_only"] = true;
    return planDoc;
}

} // namespace

RecoveryBridge::RecoveryBridge(const sicnu::repair::RepairCapabilityProvider *provider)
    : mProvider(provider)
{
}

Json::Value RecoveryBridge::projectRepairPlan(const OpDiagnostic &diagnostic,
                                              const RecoveryContext &ctx) const
{
    const std::string intent = ctx.intent.empty() ? "ops_recovery" : ctx.intent;

    if (mProvider == nullptr)
        return noSafeRepairPlan(intent, "no_provider",
                                "no capability provider wired; no candidate is fabricated");

    const std::vector<Json::Value> findings = repairFindingsFromDiagnostic(diagnostic);
    if (findings.empty())
        return noSafeRepairPlan(intent, "no_findings",
                                "the diagnostic carries no typed preflight findings");

    // Autonomy for the plan's policy record comes from the SAME gate the
    // recovery decision uses on the neutral execute key — one authority, no
    // second derivation of "what autonomy allows".
    OpsAutonomyRequest execReq;
    execReq.mutateKind = ops_mutate::kExecute;
    execReq.domain = ctx.domain;
    execReq.role = ctx.role;
    execReq.intent = ctx.intent;
    execReq.actionKey = "ops:run";
    const sicnu::repair::RepairPolicyContext policyContext{
        ctx.domain, ctx.role, gateMutatingOp(ctx.autonomyPolicy, execReq).allowed,
        ctx.humanApprovedRepair};

    sicnu::repair::RepairPlannerOutcome outcome;
    sicnu::repair::RepairError error;
    sicnu::repair::RepairPlannerOptions options;
    options.intent = intent;
    if (!sicnu::repair::planRepairsForFindings(findings, *mProvider, policyContext, options,
                                               outcome, error))
        return noSafeRepairPlan(intent, "invalid_findings", error.message);

    Json::Value planDoc = sicnu::repair::repairPlanToJson(outcome.plan);
    planDoc["planning_only"] = true;
    if (findings.size() >= kMaxBridgeFindings)
        planDoc["provenance"]["findings_dropped_by_bridge"] = true;
    return planDoc;
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

        // The plan is the authority: provider-backed planning over the
        // diagnostic's typed findings. A plan that could not honestly plan
        // (no knowledge, no findings, only refusals) is a human decision —
        // never an auto path, never fabricated candidates.
        out.repairPlan = projectRepairPlan(diagnostic, ctx);
        const std::string planStatus =
            out.repairPlan.isObject() && out.repairPlan["status"].isString()
                ? out.repairPlan["status"].asString()
                : std::string();
        if (planStatus != sicnu::repair::plan_status::kPlanned)
        {
            out.action = recovery_action::kAsk;
            out.reasonCode = "NO_SAFE_REPAIR_PLAN";
            out.needsApproval = true;
            out.autonomyAllowed = true;
            out.autonomyReasonCode = sicnu::agent::autonomy::autonomy_reason_codes::kAllowed;
            return out;
        }

        // The risk class that gates the repair comes from the plan's own
        // candidate contract — the caller's leadingRiskClass claim cannot
        // downgrade a radiometric or science-changing repair into an auto
        // path. The gate reads the MAXIMUM risk over ALL selected
        // candidates: a plan is executed whole, so one radiometric or
        // science-changing candidate makes the whole launch approval-gated,
        // and the autonomy gate always sees the strictest class present.
        std::string leadingRisk;
        bool anyRadiometricOrScience = false;
        const Json::Value &selected = out.repairPlan["selected"];
        if (selected.isArray())
        {
            for (const Json::Value &candidate : selected)
            {
                const std::string risk = candidate.isObject() &&
                                                 candidate["risk_class"].isString()
                                             ? candidate["risk_class"].asString()
                                             : std::string();
                if (risk == "radiometric" || risk == "science_changing")
                    anyRadiometricOrScience = true;
                if (risk == "science_changing")
                    leadingRisk = risk;
                else if (leadingRisk.empty())
                    leadingRisk = risk;
            }
        }
        if (leadingRisk.empty())
            leadingRisk = ctx.leadingRiskClass;

        // An approval is bound to ONE repair science (the findings digest in
        // the plan's provenance). A token minted for a different finding set
        // never satisfies this plan's human gate; a deterministic
        // re-projection of the same findings is the same binding target.
        const std::string planDigest =
            out.repairPlan["provenance"].isObject() &&
                    out.repairPlan["provenance"]["findings_digest"].isString()
                ? out.repairPlan["provenance"]["findings_digest"].asString()
                : std::string();
        bool approvedForThisPlan = ctx.humanApprovedRepair &&
                                   !ctx.approvedFindingsDigest.empty() &&
                                   ctx.approvedFindingsDigest == planDigest;
        if (ctx.humanApprovedRepair && !approvedForThisPlan)
            out.approvalError = "APPROVAL_WRONG_PLAN";

        if (anyRadiometricOrScience && !approvedForThisPlan)
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
        req.riskClass = leadingRisk;
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
        // Exit 0 on a repair is NOT "repaired": fresh preflight and fresh
        // verification must run before any repaired claim. The decision
        // carries that obligation on the wire so no caller can project a
        // repaired outcome from the execution fact alone.
        out.requiresReverification = true;
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
