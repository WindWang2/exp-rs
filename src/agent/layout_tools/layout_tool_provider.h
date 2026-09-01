// src/agent/layout_tools/layout_tool_provider.h
#pragma once

#include "agent/tool_catalog/tool_provider.h"

namespace sicnu::agent::layout_tools {

/**
 * Bridges the layout:* SpatialToolRegistry entries into the unified
 * AgentToolCatalog as a ToolProvider (Cartographic Layout Studio): every
 * layout tool is exported with its full input/output schemas so the
 * copilot, CLI, and MCP surface see one catalog under the "layout" group.
 * Execution stays owned by the SpatialToolRegistry.
 */
class LayoutToolProvider : public sicnu::agent::tool_catalog::ToolProvider {
  public:
    std::string providerName() const override { return "LayoutToolProvider"; }
    sicnu::agent::tool_catalog::ToolCategory category() const override;
    std::vector<sicnu::agent::tool_catalog::AgentTool> provideTools() const override;
};

} // namespace sicnu::agent::layout_tools
