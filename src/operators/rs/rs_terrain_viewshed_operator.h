/***************************************************************************
 * rs_terrain_viewshed_operator.h — viewshed / cumulative viewshed over a
 * DEM (track terrain-hydrology-11, package D).
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:terrain_viewshed — line-of-sight visibility from DEM-mounted observers.
 *
 * Products (see processing/algorithms/terrain_viewshed.h):
 *   viewshed    byte raster: 1 visible, 0 not, 255 NoData
 *   cumulative  uint16 raster: number of observers seeing each cell
 *               (65535 = NoData)
 *
 * Full-frame kernel contract (~22 bytes/cell working set); fail-closed
 * above SICNU_TERRAIN_MAX_CELLS. Curvature/refraction requires a projected
 * (metric) CRS — geographic inputs are refused with InvalidParameter.
 */
class RsTerrainViewshedOperator : public RSOperator {
public:
    std::string name() const override { return "rs:terrain_viewshed"; }
    std::string displayName() const override { return "Terrain Viewshed"; }
    std::string group() const override { return "terrain"; }
    std::string description() const override {
        return "Line-of-sight viewshed (single or cumulative observers) over a "
               "DEM with optional Earth-curvature/refraction correction.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::FullRaster;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
