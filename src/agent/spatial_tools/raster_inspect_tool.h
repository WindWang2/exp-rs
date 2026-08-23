// src/agent/spatial_tools/raster_inspect_tool.h
#pragma once

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/**
 * spatial:raster_inspect — fast, read-only GDAL raster inspection for
 * agents (ADR 0122): dimensions, CRS, pixel size, extent, nodata, semantic
 * band roles / wavelengths (SICNU_* product metadata), radiometric state,
 * and optional per-band statistics.
 */
class RasterInspectTool : public SpatialTool {
  public:
    std::string name() const override { return "spatial:raster_inspect"; }
    std::string displayName() const override { return "Inspect Raster Dataset"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

} // namespace sicnu::agent::spatial_tools
