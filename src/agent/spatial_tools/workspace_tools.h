// src/agent/spatial_tools/workspace_tools.h
#pragma once

// Phase B/C — workspace-facing inspection SpatialTools:
//   spatial:workspace_summary  full WorkspaceState document (bounded)
//   spatial:layer_summary      one asset/layer deep-dive (bounded)
//
// Registered via registerWorkspaceTools() from SpatialToolRegistry::registerBuiltinTools().

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// Registers spatial:workspace_summary and spatial:layer_summary. Idempotent.
void registerWorkspaceTools();

} // namespace sicnu::agent::spatial_tools
