// src/agent/tool_catalog/agent_tool_catalog.h
#pragma once

#include "agent_tool.h"
#include "tool_provider.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::agent::tool_catalog {

/**
 * Structured search query for tool filtering.
 */
struct SearchQuery {
  std::string text;                             ///< Free text search (name, description, tags, purpose)
  std::string group;                            ///< Exact/prefix group filter
  std::string tag;                              ///< Specific tag filter
  std::string inputType;                        ///< Port input data type filter (e.g. Raster, Vector, String, etc.)
  std::string outputType;                       ///< Port output data type filter
  std::optional<ToolCategory> category;         ///< Category filter (Processing, Interaction, Data, Custom)
  bool largeRasterSafeOnly = false;             ///< Filter to streaming/multipass safe algorithms

  // --- Capability facets (#701): AND-combined with the filters above so an
  // agent can express "deterministic, large-raster-safe, band-role:red tools"
  // in one ranked query instead of pulling the whole catalog.
  std::string taskFamily;                       ///< Substring match on agentMetadata.taskFamily
  std::optional<bool> deterministic;            ///< agentMetadata.deterministic
  std::optional<bool> gpu;                      ///< agentMetadata.gpuAccelerated
  std::optional<bool> temporal;                 ///< taskFamily/name carries temporal capability
  std::string memoryPolicy;                     ///< agentMetadata.memoryPolicy (e.g. "streaming")
  std::string costClass;                        ///< Substring on agentMetadata.costClass (e.g. "O(tile)")
  std::string modality;                         ///< rsContract dataKind / port-name match (e.g. "optical")
  std::string bandRoles;                        ///< Comma list; ANY role in any port's rsContract bands
};

/**
 * AgentToolCatalog — Canonical unified tool registry for the exp-rs Agent subsystem.
 *
 * Unifies:
 * 1. Processing Tools (AtomicAlgorithmRegistry + RS operators + Provider algorithms)
 * 2. Interaction Tools (Canvas drawing, raster band composites, display stretch, zoom)
 * 3. Data Tools (Layer listing, dataset description, asset lineage)
 *
 * Surfaces unified OpenAI Tool Schema for Copilot, MCP, and CLI.
 */
class AgentToolCatalog {
public:
  static AgentToolCatalog &instance();

  /**
   * Resets the catalog and re-registers the standard default providers:
   * AlgorithmToolProvider, InteractionToolProvider, DataToolProvider.
   */
  void reset();

  /**
   * Idempotent initialization of default providers.
   */
  void initializeDefaults();

  // --- Provider Management ---
  void registerProvider( ToolProviderPtr provider );
  bool unregisterProvider( const std::string &providerName );
  std::vector<ToolProviderPtr> providers() const;
  ToolProviderPtr provider( const std::string &providerName ) const;

  // --- Custom Tool Registration ---
  void registerCustomTool( const AgentTool &tool );
  bool unregisterCustomTool( const std::string &toolName );

  // --- Discovery & Lookup ---
  std::vector<AgentTool> listTools( std::optional<ToolCategory> category = std::nullopt ) const;
  std::optional<AgentTool> findTool( const std::string &name ) const;
  Json::Value getSchema( const std::string &toolName ) const;
  size_t toolCount( std::optional<ToolCategory> category = std::nullopt ) const;

  // --- Search Capabilities ---
  std::vector<AgentTool> searchTools( const std::string &queryText ) const;
  std::vector<AgentTool> searchTools( const SearchQuery &query ) const;

  // --- Duplicate Detection ---
  std::vector<std::string> findDuplicateNames() const;
  bool hasDuplicates() const;

  // --- Exporters ---
  Json::Value exportOpenAiToolDefinitions( const std::vector<AgentTool> &tools = {} ) const;
  Json::Value exportMcpTools( const std::vector<AgentTool> &tools = {} ) const;
  std::string exportSystemPromptCatalog( const std::vector<AgentTool> &tools = {} ) const;
  void invalidateCache();

private:
  AgentToolCatalog();
  ~AgentToolCatalog() = default;

  AgentToolCatalog( const AgentToolCatalog & ) = delete;
  AgentToolCatalog &operator=( const AgentToolCatalog & ) = delete;

  mutable std::mutex mMutex;
  std::vector<ToolProviderPtr> mProviders;
  std::unordered_map<std::string, AgentTool> mCustomTools;
  mutable bool mCacheValid = false;
  // InteractionToolRegistry revision at cache-build time (#701): the registry
  // can register tools at any moment; a bump invalidates the catalog snapshot
  // so post-registration tools are discoverable without a manual reset().
  mutable size_t mCachedInteractionRevision = 0;
  mutable std::vector<AgentTool> mCachedTools;
  mutable Json::Value mCachedOpenAiDefs;
  mutable Json::Value mCachedMcpTools;

  /// True when the cached tool set is still current (mMutex must be held).
  /// Checks both the explicit invalidation flag and the live
  /// InteractionToolRegistry revision.
  bool cacheIsFresh() const;
};

} // namespace sicnu::agent::tool_catalog
