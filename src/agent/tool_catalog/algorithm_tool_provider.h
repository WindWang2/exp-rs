// src/agent/tool_catalog/algorithm_tool_provider.h
#pragma once

#include "tool_provider.h"
#include "processing/framework/atomic_algorithm_registry.h"

namespace sicnu::agent::tool_catalog {

/**
 * Tool provider that exposes processing algorithms and RS operators
 * from AtomicAlgorithmRegistry into the unified AgentToolCatalog.
 */
class AlgorithmToolProvider : public ToolProvider {
public:
  AlgorithmToolProvider() = default;
  ~AlgorithmToolProvider() override = default;

  std::string providerName() const override { return "AlgorithmToolProvider"; }
  ToolCategory category() const override { return ToolCategory::Processing; }

  std::vector<AgentTool> provideTools() const override;
  std::optional<AgentTool> findTool( const std::string &name ) const override;
};

} // namespace sicnu::agent::tool_catalog
