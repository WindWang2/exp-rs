// src/agent/interaction_tool_registry.h
#pragma once

#include <json/json.h>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::agent {

class ViewControlService;
class RasterDisplayService;

/**
 * @brief Definition of an interaction tool (view controls, layer management, canvas ROI).
 */
struct InteractionToolDefinition {
  std::string name;             ///< Unique tool name, e.g. "view:get_state", "roi:set"
  std::string displayName;      ///< Human-readable tool name
  std::string category;         ///< Tool category: "view", "roi", "canvas", "layer"
  std::string description;      ///< Description for LLM agent discovery
  Json::Value inputSchema;      ///< JSON Schema for tool parameters
  std::function<Json::Value( const Json::Value &parameters )> handler;
};

/**
 * @brief Registry for agent interaction tools (GUI view, layer, and canvas interaction capabilities).
 *
 * Analogous to AtomicAlgorithmRegistry for processing algorithms, this registry
 * manages interactive GIS tools, parameter schemas, and execution handlers.
 */
class InteractionToolRegistry {
public:
  static InteractionToolRegistry &instance();

  /**
   * @brief Clears all registered interaction tools. Useful for test isolation.
   */
  void reset();

  /**
   * @brief Registers an interaction tool definition.
   */
  void registerTool( InteractionToolDefinition toolDef );

  /**
   * @brief Unregisters a tool by name. Returns true if found and removed.
   */
  bool unregisterTool( const std::string &name );

  /**
   * @brief Finds a tool definition by name.
   */
  std::optional<InteractionToolDefinition> findTool( const std::string &name ) const;

  /**
   * @brief Checks if a tool with given name is registered.
   */
  bool hasTool( const std::string &name ) const;

  /**
   * @brief Lists all currently registered interaction tools.
   */
  std::vector<InteractionToolDefinition> listTools() const;

  /**
   * @brief Returns the total number of registered interaction tools.
   */
  size_t toolCount() const;

  /**
   * @brief Executes a registered interaction tool with parsed JSON arguments.
   *
   * If the tool is not found, returns a JSON object with status "error" and errorMessage.
   */
  Json::Value execute( const std::string &name, const Json::Value &parameters ) const;

  /**
   * @brief Registers built-in GIS interaction tools wired to the provided ViewControlService.
   *
   * Registers:
   * - view:get_state
   * - view:set_extent
   * - view:zoom_to_layer
   * - view:zoom_to_asset
   * - view:fit_all
   * - view:set_scale
   * - roi:set
   * - roi:clear
   * - canvas:draw_roi (backward compatibility alias)
   * - raster tools (when rasterService is non-null)
   */
  void registerBuiltinTools( ViewControlService *service, RasterDisplayService *rasterService = nullptr );

  /**
   * @brief Registers raster display and visualization tools wired to the provided RasterDisplayService.
   *
   * Registers:
   * - raster:get_display
   * - raster:set_band_composite
   * - raster:set_stretch
   * - raster:reset_display
   */
  void registerRasterTools( RasterDisplayService *service );

  /**
   * @brief Exports registered interaction tools into OpenAI / Qwen Tool Call Function format JSON array.
   */
  Json::Value exportOpenAiToolDefinitions() const;

  /**
   * @brief Exports registered tools into a Markdown system prompt catalog table.
   */
  std::string exportSystemPromptCatalog() const;

private:
  InteractionToolRegistry();
  ~InteractionToolRegistry() = default;

  InteractionToolRegistry( const InteractionToolRegistry & ) = delete;
  InteractionToolRegistry &operator=( const InteractionToolRegistry & ) = delete;

  mutable std::mutex m_mutex;
  std::unordered_map<std::string, InteractionToolDefinition> m_tools;
};

} // namespace sicnu::agent
