// src/agent_ops/session_surface.cpp
#include "agent_ops/session_surface.h"

#include "agent_ops/delivery_assembler.h"
#include "agent_ops/repair_approval.h"

namespace sicnu::agent_ops {

Json::Value sessionSurfaceStatus(const OpsRunResult &result)
{
    Json::Value doc(Json::objectValue);
    doc["schema"] = kSessionSurfaceSchema;
    doc["tool"] = kSessionSurfaceTool;
    doc["ok"] = result.ok;
    doc["session_id"] = result.session.sessionId;
    doc["outcome"] = result.delivery.outcome;
    doc["stop_reason"] = result.delivery.stopReason;
    doc["projection"] = result.projection.toJson();
    doc["delivery"] = result.delivery.toJson();
    if (!result.approvalError.empty())
        doc["approval_error"] = result.approvalError;
    if (result.lastDiagnostic)
        doc["diagnostic"] = result.lastDiagnostic->toJson();
    if (result.lastRecovery)
        doc["recovery"] = result.lastRecovery->toJson();
    if (result.trace)
    {
        doc["trace_id"] = result.trace->traceId;
        doc["trace_schema"] = sicnu::agentbench::kAgentTraceSchemaTag;
    }
    if (!result.benchmarkError.empty())
        doc["benchmark_error"] = result.benchmarkError;
    if (!result.checkpointError.empty())
        doc["checkpoint_error"] = result.checkpointError;
    // Resume/restart evidence on the wire: the reconciler's typed verdict
    // (incl. duplicate-submit hazard + submitted run ids) rides with every
    // status so a driver never has to guess whether a restart is safe.
    doc["reconcile"] = result.reconcile.toJson();
    doc["error"] = result.error;
    return doc;
}

Json::Value sessionSurfaceActions()
{
    Json::Value actions(Json::arrayValue);
    for (const char *a : {"run", "pause", "cancel", "clear_pause", "clear_cancel", "resume",
                          "approve_repair", "export", "timeline", "status"})
        actions.append(a);
    Json::Value doc(Json::objectValue);
    doc["schema"] = kSessionSurfaceSchema;
    doc["tool"] = kSessionSurfaceTool;
    doc["actions"] = actions;
    doc["note"] = "Drivers share OperationsCoordinator; no vendor LLM SDK in core.";
    return doc;
}

namespace {

/// Typed failure on the surface wire: ok=false with a machine reason, never
/// a silent success.
Json::Value errorDoc(const std::string &action, const std::string &error)
{
    Json::Value doc(Json::objectValue);
    doc["schema"] = kSessionSurfaceSchema;
    doc["action"] = action;
    doc["ok"] = false;
    doc["error"] = error;
    return doc;
}

/// Reads the run/resume approval arguments into the request. The legacy
/// bare-bool `approve_pending_repair` spelling is refused explicitly: an
/// approval without a bound token is exactly what this surface must never
/// accept. A driver-held token is verified by the coordinator at launch.
std::string readApprovalArgs(const std::string &action, const Json::Value &args,
                             OpsRunRequest &request)
{
    if (args.isObject() && args.isMember("approve_pending_repair"))
        return "APPROVAL_TOKEN_REQUIRED";
    if (!args.isObject())
        return {};
    // A malformed token document is a typed refusal, never a silent drop.
    if (args.isMember("repair_approval") && !args["repair_approval"].isNull() &&
        !args["repair_approval"].isObject())
        return "APPROVAL_MALFORMED";
    // resume() never evaluates the recovery bridge, so an approval could
    // not be consumed honestly there — refuse instead of ignoring.
    if (action == "resume" && args.isMember("repair_approval"))
        return "APPROVAL_TOKEN_REQUIRED";
    if (args["repair_approval"].isObject())
        request.repairApproval = args["repair_approval"];
    if (args["approval_now_ms"].isInt64())
        request.approvalNowMs = args["approval_now_ms"].asInt64();
    return {};
}

} // namespace

Json::Value sessionSurfaceApply(OperationsCoordinator &coordinator, const std::string &action,
                                const Json::Value &args)
{
    Json::Value doc(Json::objectValue);
    doc["schema"] = kSessionSurfaceSchema;
    doc["action"] = action;

    if (action == "run")
    {
        if (!args.isObject() || args.get("goal", "").asString().empty())
            return errorDoc(action, "MISSING_ARGS");
        OpsRunRequest request;
        request.session.goal = args["goal"].asString();
        request.session.intent = args.get("intent", "").asString();
        if (args.isMember("refs"))
            request.session.refs = args["refs"];
        request.journalDirectory = args.get("journal_directory", "").asString();
        request.domain = args.get("domain", "research").asString();
        request.role = args.get("role", "").asString();
        const std::string approvalError = readApprovalArgs(action, args, request);
        if (!approvalError.empty())
            return errorDoc(action, approvalError);
        doc = sessionSurfaceStatus(coordinator.run(request));
        doc["schema"] = kSessionSurfaceSchema;
        doc["action"] = action;
        return doc;
    }
    if (action == "pause")
    {
        coordinator.requestPause();
        doc["ok"] = true;
        // Honest scope: the flag gates the launch of new sessions on this
        // coordinator; the loop state machine is authoritative mid-run.
        doc["effective_scope"] = "session_launch";
        return doc;
    }
    if (action == "cancel")
    {
        coordinator.requestCancel();
        doc["ok"] = true;
        doc["effective_scope"] = "session_launch_and_next_run";
        return doc;
    }
    if (action == "clear_pause")
    {
        coordinator.clearPause();
        doc["ok"] = true;
        return doc;
    }
    if (action == "clear_cancel")
    {
        // Relaunch path: cancel latches (it must survive until the in-flight
        // or next run actually consumed it), so the driver needs an explicit
        // typed way to arm the coordinator for new work after the aborted
        // run was observed. Without this the wire had no disarm at all and
        // every later run on a cancelled coordinator aborted as CANCELLED.
        coordinator.clearCancel();
        doc["ok"] = true;
        return doc;
    }
    if (action == "resume")
    {
        // The loop refuses a resume whose run() does not restate the
        // journalled goal (SESSION_GOAL_MISMATCH) — and a goal-less attempt
        // would overwrite the parked journal with the refused terminal one.
        // Requiring the goal here fails the request before anything runs.
        if (!args.isObject() || args.get("journal_directory", "").asString().empty() ||
            args.get("session_id", "").asString().empty() ||
            args.get("goal", "").asString().empty())
            return errorDoc(action, "MISSING_ARGS");
        OpsRunRequest request;
        request.session.goal = args.get("goal", "").asString();
        request.session.intent = args.get("intent", "").asString();
        request.journalDirectory = args["journal_directory"].asString();
        request.domain = args.get("domain", "research").asString();
        request.role = args.get("role", "").asString();
        const std::string approvalError = readApprovalArgs(action, args, request);
        if (!approvalError.empty())
            return errorDoc(action, approvalError);
        doc = sessionSurfaceStatus(
            coordinator.resume(request.journalDirectory, args["session_id"].asString(),
                               request));
        doc["schema"] = kSessionSurfaceSchema;
        doc["action"] = action;
        return doc;
    }
    if (action == "resume_clear_pause")
    {
        // Legacy name kept for wire compatibility; clear_pause is the
        // advertised spelling.
        coordinator.clearPause();
        doc["ok"] = true;
        return doc;
    }
    if (action == "approve_repair")
    {
        // The human gate mints a token bound to the repair science the
        // driver last saw: (this coordinator, findings digest, expiry
        // window, integrity digest). An approval that binds nothing is
        // refused — never a bare flag.
        const std::string findingsDigest = coordinator.lastProjectedFindingsDigest();
        if (findingsDigest.empty())
            return errorDoc(action, "NO_PENDING_REPAIR_PLAN");
        if (args.isObject() && args.isMember("findings_digest") &&
            (!args["findings_digest"].isString() ||
             args["findings_digest"].asString() != findingsDigest))
            return errorDoc(action, "APPROVAL_WRONG_PLAN");
        const Json::Int64 nowMs = args.isObject() && args["now_ms"].isInt64()
                                      ? args["now_ms"].asInt64()
                                      : 0;
        const Json::Int64 ttlMs = args.isObject() && args["ttl_ms"].isInt64()
                                      ? args["ttl_ms"].asInt64()
                                      : 0;
        Json::Value token = mintRepairApprovalToken(findingsDigest,
                                                    coordinator.instanceId(), nowMs, ttlMs);
        if (token.isNull())
            return errorDoc(action, "APPROVAL_MINTING_FAILED");
        const std::string armError = coordinator.armRepairApproval(token, nowMs);
        if (!armError.empty())
            return errorDoc(action, armError);
        doc["ok"] = true;
        doc["findings_digest"] = findingsDigest;
        doc["repair_approval"] = token;
        doc["expires_at_ms"] = token["expires_at_ms"];
        doc["pending_repair_approval"] = coordinator.hasPendingRepairApproval();
        return doc;
    }
    if (action == "export")
    {
        const auto &last = coordinator.lastResult();
        if (!last)
            return errorDoc(action, "NO_SESSION");
        // The export payload IS the capsule document (drivers save it
        // as-is); only the surface action tag and the response `ok` are
        // added, so the usual driver check keeps working.
        doc = DeliveryAssembler().capsuleExportDocument(last->delivery);
        doc["action"] = action;
        doc["ok"] = true;
        return doc;
    }
    if (action == "timeline")
    {
        const auto &last = coordinator.lastResult();
        if (!last)
            return errorDoc(action, "NO_SESSION");
        doc["projection"] = last->projection.toJson();
        doc["ok"] = last->ok;
        return doc;
    }
    if (action == "status")
    {
        const auto &last = coordinator.lastResult();
        if (!last)
            return errorDoc(action, "NO_SESSION");
        doc = sessionSurfaceStatus(*last);
        doc["schema"] = kSessionSurfaceSchema;
        doc["action"] = action;
        return doc;
    }
    if (action == "actions")
        return sessionSurfaceActions();

    Json::Value unknown = errorDoc(action, "UNKNOWN_ACTION");
    unknown["args"] = args;
    return unknown;
}

} // namespace sicnu::agent_ops
