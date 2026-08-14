// src/agent/tool_catalog/data_tool_provider.h
#pragma once

#include "tool_provider.h"
#include <mutex>
#include <unordered_map>

namespace sicnu::agent::tool_catalog {

/**
 * Tool provider that exposes Data and Workspace exploration tools
 * (such as listing loaded layers, describing dataset metadata, querying asset lineage, etc.)
 */
class DataToolProvider : public ToolProvider {
public:
  DataToolProvider();
  ~DataToolProvider() override = default;

  std::string providerName() const override { return "DataToolProvider"; }
  ToolCategory category() const override { return ToolCategory::Data; }

  std::vector<AgentTool> provideTools() const override;
  std::optional<AgentTool> findTool( const std::string &name ) const override;

  /// Registers an additional data tool dynamically
  void registerTool( const AgentTool &tool );

  /// Unregisters a data tool by name
  bool unregisterTool( const std::string &name );

  /// Restores default builtin data tools
  void resetDefaults();

private:
  mutable std::mutex mMutex;
  std::unordered_map<std::string, AgentTool> mTools;
};

} // namespace sicnu::agent::tool_catalog
