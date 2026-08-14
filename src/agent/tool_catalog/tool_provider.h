// src/agent/tool_catalog/tool_provider.h
#pragma once

#include "agent_tool.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent::tool_catalog {

/**
 * Interface for Tool Providers.
 * Enables modular expansion of tool sources (Algorithms, Interactions, Data, Plugins)
 * without bundling all logic into a monolithic registry.
 */
class ToolProvider {
public:
  virtual ~ToolProvider() = default;

  /// Unique provider name (e.g. "AlgorithmToolProvider", "InteractionToolProvider", "DataToolProvider")
  virtual std::string providerName() const = 0;

  /// Default category of tools produced by this provider
  virtual ToolCategory category() const = 0;

  /// Lists all tools provided by this provider
  virtual std::vector<AgentTool> provideTools() const = 0;

  /// Looks up a single tool by name from this provider
  virtual std::optional<AgentTool> findTool( const std::string &name ) const
  {
    const auto all = provideTools();
    for ( const auto &t : all )
    {
      if ( t.name == name )
        return t;
    }
    return std::nullopt;
  }
};

using ToolProviderPtr = std::shared_ptr<ToolProvider>;

} // namespace sicnu::agent::tool_catalog
