// src/agent/spatial_tools/geometric_spatial_tool.h
#pragma once

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/**
 * spatial:geometric_registration — the F13-corrected catalog surface for the
 * geometric registration tool family (ADR 0159 + multimodal registration).
 *
 * The underlying rs::agent::GeometricTool has existed since D14 but was only
 * compiled into the agent library — never registered here, so it was
 * invisible to the copilot/CLI/MCP catalog. This adapter bridges the Qt-JSON
 * envelope onto the SpatialTool (jsoncpp) contract. Read-only: every action
 * inspects rasters or points and returns evidence; it never writes products.
 */
class GeometricSpatialTool : public SpatialTool {
  public:
    std::string name() const override { return "spatial:geometric_registration"; }
    std::string displayName() const override { return "Geometric Registration"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute(const Json::Value& input) override;
};

} // namespace sicnu::agent::spatial_tools
