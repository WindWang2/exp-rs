// src/agent/spatial_tools/vector_inspect_tool.h
#pragma once

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/**
 * spatial:vector_inspect — read-only OGR vector inspection for agents
 * (ADR 0122): layers, geometry types, feature counts, extents, CRS, field
 * schemas, and optional sampled features (with attributes and GeoJSON
 * geometry).
 */
class VectorInspectTool : public SpatialTool {
  public:
    std::string name() const override { return "spatial:vector_inspect"; }
    std::string displayName() const override { return "Inspect Vector Dataset"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

} // namespace sicnu::agent::spatial_tools
