// src/agent/spatial_tools/spatial_tool_provider.h
#pragma once

#include "agent/tool_catalog/tool_provider.h"

namespace sicnu::agent::spatial_tools {

/**
 * Bridges the SpatialToolRegistry into the unified AgentToolCatalog as a
 * ToolProvider (ADR 0122): every registered spatial tool is exported with
 * its full input/output schemas so the copilot, CLI, and MCP surface see
 * one catalog. Execution stays owned by the registry.
 */
class SpatialToolProvider : public sicnu::agent::tool_catalog::ToolProvider {
  public:
    std::string providerName() const override { return "SpatialToolProvider"; }
    sicnu::agent::tool_catalog::ToolCategory category() const override;
    std::vector<sicnu::agent::tool_catalog::AgentTool> provideTools() const override;
};

} // namespace sicnu::agent::spatial_tools
