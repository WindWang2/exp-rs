// src/agent/tool_catalog/agent_tool.h
#pragma once

#include <json/json.h>
#include <optional>
#include <string>
#include <vector>

#include "processing/framework/algorithm_descriptor.h"

namespace sicnu::agent::tool_catalog {

/**
 * High-level tool category classifications.
 */
enum class ToolCategory {
  Processing,   ///< Algorithm & RS processing operators (rs:, gdal:, otb:, qgis:, etc.)
  Interaction,  ///< UI, Canvas, and visualization actions (canvas:draw_roi, raster:set_band_composite, etc.)
  Data,         ///< Layer, dataset, catalog & lineage queries (data:list_layers, describe_dataset, etc.)
  Custom        ///< User-defined / plugin extension tools
};

std::string toolCategoryToString( ToolCategory category );
ToolCategory toolCategoryFromString( const std::string &catStr );
std::optional<ToolCategory> tryParseToolCategory( const std::string &catStr );

/**
 * Unified Agent Tool Descriptor.
 * Encapsulates name, category, group, description, tags, parameter schema,
 * and metadata across all tool types in the system.
 */
struct AgentTool {
  std::string name;             ///< Canonical ID, e.g. "rs:spectral_index", "canvas:draw_roi", "data:list_layers"
  std::string displayName;      ///< Human-readable title
  ToolCategory category = ToolCategory::Processing;
  std::string group;            ///< Sub-group / module, e.g. "spectral", "canvas", "display", "data"
  std::string description;      ///< Detailed description of capability
  std::vector<std::string> tags;///< Semantic tags for discovery (e.g. "show raster", "ndvi", "roi")

  Json::Value inputSchema;      ///< JSON Schema for input parameters ({type: "object", properties: {...}, required: [...]})
  Json::Value outputSchema;     ///< Optional JSON Schema for output results

  std::vector<sicnu::processing::PortDescriptor> inputs;  ///< Port-level inputs when available
  std::vector<sicnu::processing::PortDescriptor> outputs; ///< Port-level outputs when available

  sicnu::processing::AgentMetadata agentMetadata;         ///< Execution hints, memory policy, and planning metadata

  /// Converts this tool to OpenAI / Qwen Function Calling format:
  /// { "type": "function", "function": { "name": "...", "description": "...", "parameters": { ... } } }
  Json::Value toOpenAiToolDefinition() const;

  /// Converts this tool to MCP tool format:
  /// { "category": "...", "name": "...", "description": "...", "schema": { ... } }
  Json::Value toMcpToolDefinition() const;

  /// Converts this tool to a comprehensive JSON representation
  Json::Value toJson() const;
};

} // namespace sicnu::agent::tool_catalog
