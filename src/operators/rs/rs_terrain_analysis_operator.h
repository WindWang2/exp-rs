/***************************************************************************
 * rs_terrain_analysis_operator.h  —  Terrain analysis RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Computes terrain derivatives from a DEM raster.
 *
 * Products:
 *   - slope:     gradient magnitude in degrees [0, 90]
 *   - aspect:    gradient direction in degrees [0, 360), -1 for flat
 *   - hillshade: illumination model [0, 1]
 *   - roughness: local max-min relief in 3x3 window
 *   - tri:       Terrain Ruggedness Index
 *   - tpi:       Topographic Position Index
 *
 * Parameters:
 *   input         (string, required) Input DEM raster path
 *   output        (string, required) Output raster path
 *   product       (string, required) One of: slope, aspect, hillshade, roughness, tri, tpi
 *   cellSize      (number, optional) Pixel size in map units (default: 30.0)
 *   nodata        (number, optional) DEM no-data value (default: -9999)
 *   sunAzimuth    (number, optional) Sun azimuth for hillshade (default: 315.0)
 *   sunElevation  (number, optional) Sun elevation for hillshade (default: 45.0)
 *
 * Returns JSON object with:
 *   output  (string) Output raster path
 *   product (string) Computed product name
 *   width   (int)    Output width
 *   height  (int)    Output height
 */
class RsTerrainAnalysisOperator : public RSOperator {
public:
    std::string name() const override { return "rs:terrain_analysis"; }
    std::string displayName() const override { return "Terrain Analysis"; }
    std::string group() const override { return "terrain"; }
    std::string description() const override {
        return "Compute slope, aspect, hillshade, roughness, TRI, or TPI from a DEM.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
