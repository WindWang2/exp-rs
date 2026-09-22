// src/agent_ops/session_surface.cpp
#include "agent_ops/session_surface.h"

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

Json::Value sessionSurfaceApply(OperationsCoordinator &coordinator, const std::string &action,
                                const Json::Value &args)
{
    Json::Value doc(Json::objectValue);
    doc["schema"] = kSessionSurfaceSchema;
    doc["action"] = action;
    if (action == "pause")
    {
        coordinator.requestPause();
        doc["ok"] = true;
    }
    else if (action == "cancel")
    {
        coordinator.requestCancel();
        doc["ok"] = true;
    }
    else if (action == "resume_clear_pause")
    {
        coordinator.clearPause();
        doc["ok"] = true;
    }
    else if (action == "actions")
    {
        return sessionSurfaceActions();
    }
    else
    {
        doc["ok"] = false;
        doc["error"] = "UNKNOWN_ACTION";
        doc["args"] = args;
    }
    return doc;
}

} // namespace sicnu::agent_ops
