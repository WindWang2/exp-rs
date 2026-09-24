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

/// Apply a named control action to a live coordinator. Every advertised
/// action is implemented: run/resume take typed args (run requires goal;
/// resume requires journal_directory, session_id AND the journalled goal —
/// the loop refuses a goal mismatch); status/timeline/export project the
/// last result (typed NO_SESSION before the first run; export returns the
/// capsule export document itself); pause/cancel gate the session launch;
/// approve_repair records one-shot pending approval consumed by the next
/// launch. Failures are typed docs (ok=false + machine reason), never
/// silent successes.
Json::Value sessionSurfaceApply(OperationsCoordinator &coordinator,
                                const std::string &action,
                                const Json::Value &args = Json::objectValue);

} // namespace sicnu::agent_ops
