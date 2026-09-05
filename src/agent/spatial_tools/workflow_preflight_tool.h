// src/agent/spatial_tools/workflow_preflight_tool.h
#pragma once

// Phase F — static workflow planning surface:
//   workflow:preflight  validate a WorkflowDefinition JSON BEFORE execution
//
// Registered via registerWorkflowPreflightTool() from SpatialToolRegistry::registerBuiltinTools().

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// Registers workflow:preflight. Idempotent.
void registerWorkflowPreflightTool();

} // namespace sicnu::agent::spatial_tools
