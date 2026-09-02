// src/agent/tool_catalog/interaction_tool_provider.h
#pragma once

#include "tool_provider.h"
#include <mutex>
#include <unordered_map>

namespace sicnu::agent::tool_catalog {

/**
 * Tool provider that exposes Canvas and Interaction tools
 * (such as drawing ROIs, configuring raster RGB composites, setting contrast stretches, zooming, etc.)
 */
class InteractionToolProvider : public ToolProvider {
public:
  InteractionToolProvider();
  ~InteractionToolProvider() override = default;

  std::string providerName() const override { return "InteractionToolProvider"; }
  ToolCategory category() const override { return ToolCategory::Interaction; }

  std::vector<AgentTool> provideTools() const override;
  std::optional<AgentTool> findTool( const std::string &name ) const override;

  /// Registers an additional interaction tool dynamically
  void registerTool( const AgentTool &tool );

  /// Unregisters an interaction tool by name
  bool unregisterTool( const std::string &name );

  /// Restores default builtin interaction tools
  void resetDefaults();

private:
  mutable std::mutex mMutex;
  // mutable so the const provideTools()/findTool() can live-sync from the
  // InteractionToolRegistry (#701) — every mutation still happens under
  // mMutex.
  mutable std::unordered_map<std::string, AgentTool> mTools;
};

} // namespace sicnu::agent::tool_catalog
