// src/agent/spatial_tools/terrain_spatial_tools.h
#pragma once

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/**
 * spatial:terrain_profile — read-only elevation profile along a pixel line
 * over a DEM (nearest-cell sampling, map-unit distances from the
 * geotransform).
 */
class TerrainProfileTool : public SpatialTool {
  public:
    std::string name() const override { return "spatial:terrain_profile"; }
    std::string displayName() const override { return "DEM Elevation Profile"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/**
 * spatial:terrain_viewshed_inspect — quick single-observer viewshed summary
 * (visible-cell counts and horizon sectors) without writing a raster.
 */
class TerrainViewshedInspectTool : public SpatialTool {
  public:
    std::string name() const override { return "spatial:terrain_viewshed_inspect"; }
    std::string displayName() const override { return "Viewshed Quick Inspect"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

// Registered via registerTerrainTools() from
// SpatialToolRegistry::registerBuiltinTools().
void registerTerrainTools();

} // namespace sicnu::agent::spatial_tools
