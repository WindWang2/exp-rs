// src/agent/spatial_tools/model_catalog_tool.h
#pragma once

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/**
 * spatial:list_models — exposes the ModelCatalog (ADR 0122) to agents:
 * available model runtimes with task, input/output contract, framework, GPU
 * requirement, accuracy, and local weight path.
 */
class ModelCatalogTool : public SpatialTool {
  public:
    std::string name() const override { return "spatial:list_models"; }
    std::string displayName() const override { return "List Registered Models"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

} // namespace sicnu::agent::spatial_tools
