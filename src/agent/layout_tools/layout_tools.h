// src/agent/layout_tools/layout_tools.h
#pragma once

//
// Agent-facing layout/cartography tools (Cartographic Layout Studio).
//
// All tools are SpatialTool implementations registered in the
// SpatialToolRegistry with the "layout:" prefix and are therefore available
// headlessly through the MCP surface and the Pi extension. Mutations go
// through LayoutService, which uses the same QgsLayoutItem setters + undo
// commands as the GUI property inspector.
//

#include "../spatial_tools/spatial_tool.h"

namespace sicnu::agent::layout_tools {

/// Registers all layout:* tools in the SpatialToolRegistry (idempotent).
void registerBuiltinLayoutTools();

} // namespace sicnu::agent::layout_tools
