// src/agent_ops/session_surface.cpp
#include "agent_ops/session_surface.h"

#include "agent_ops/delivery_assembler.h"

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
    doc["error"] = result.error;
    return doc;
}

Json::Value sessionSurfaceActions()
{
    Json::Value actions(Json::arrayValue);
    for (const char *a : {"run", "pause", "cancel", "resume", "approve_repair", "export",
                          "timeline", "status"})
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
        request.approvePendingRepair = args.get("approve_pending_repair", false).asBool();
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
        request.approvePendingRepair = args.get("approve_pending_repair", false).asBool();
        doc = sessionSurfaceStatus(
            coordinator.resume(request.journalDirectory, args["session_id"].asString(),
                               request));
        doc["schema"] = kSessionSurfaceSchema;
        doc["action"] = action;
        return doc;
    }
    if (action == "resume_clear_pause")
    {
        coordinator.clearPause();
        doc["ok"] = true;
        return doc;
    }
    if (action == "approve_repair")
    {
        coordinator.setPendingRepairApproval(args.get("approve", true).asBool());
        doc["ok"] = true;
        doc["pending_repair_approval"] = coordinator.isPendingRepairApproval();
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
