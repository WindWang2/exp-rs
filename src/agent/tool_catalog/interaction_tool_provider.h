// src/agent/tool_catalog/interaction_tool_provider.h
#pragma once

#include "tool_provider.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>

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
  /// Live re-sync from the InteractionToolRegistry: removal-aware against
  /// mRegistrySourcedNames only. const: the state it mutates is mutable by
  /// design (live-sync from the const provideTools()/findTool()). Callers
  /// hold mMutex.
  void mergeRegistryTools() const;

  mutable std::mutex mMutex;
  // mutable so the const provideTools()/findTool() can live-sync from the
  // InteractionToolRegistry (#701) — every mutation still happens under
  // mMutex.
  mutable std::unordered_map<std::string, AgentTool> mTools;
  /// Names present in mTools because the registry merge added them (#1056).
  /// INSTANCE-local: a function-static set was shared by every provider
  /// instance, so one instance's rebuild erased an explicit registerTool()
  /// override recorded as registry-sourced by an earlier merge — the
  /// override silently reverted to the registry definition on the next
  /// merge. The merge is removal-aware against this set only.
  mutable std::unordered_set<std::string> mRegistrySourcedNames;
};

} // namespace sicnu::agent::tool_catalog
