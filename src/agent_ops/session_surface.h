// src/agent_ops/session_surface.h
#pragma once

//
// Feature I: MCP / Pi session surface over the same ops spine.
// Extends the scientific agent_session concept without embedding a vendor
// LLM SDK. Drivers (MCP tools, Pi bridge) call these pure projections.
//

#include "agent_ops/operations_coordinator.h"

#include <string>

namespace sicnu::agent_ops {

inline constexpr const char *kSessionSurfaceTool = "scientific:agent_session";
inline constexpr const char *kSessionSurfaceSchema = "sicnu.agent_ops.session_surface/v1";

/// Wire document a MCP/Pi driver may return for tools/call parity.
Json::Value sessionSurfaceStatus(const OpsRunResult &result);

/// Enumerate supported actions for the session surface (discovery).
Json::Value sessionSurfaceActions();

/// Apply a named control action to a live coordinator (pause/cancel/resume
/// are cooperative; export returns delivery JSON).
Json::Value sessionSurfaceApply(OperationsCoordinator &coordinator,
                                const std::string &action,
                                const Json::Value &args = Json::objectValue);

} // namespace sicnu::agent_ops
