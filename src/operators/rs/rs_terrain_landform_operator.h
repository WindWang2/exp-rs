/***************************************************************************
 * rs_terrain_landform_operator.h — multiscale TPI, Weiss landform classes,
 * and geomorphons over a DEM (track terrain-hydrology-11, package F).
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:terrain_landform — landform / terrain-position products.
 *
 * Products (see processing/algorithms/terrain_landform.h):
 *   tpi_multiscale  multi-band float raster: TPI per radius ('radii' param)
 *   landform_class  byte raster of Weiss (2001) classes (0 plains … 5 peak,
 *                   255 NoData)
 *   geomorphon      byte raster of geomorphon form classes 0–6 (255 NoData);
 *                   the packed ternary pattern rides in the JSON result
 *                   histogram
 *
 * Full-frame kernels (integral images / line-of-sight scans); capped by
 * SICNU_TERRAIN_MAX_CELLS.
 */
class RsTerrainLandformOperator : public RSOperator {
public:
    std::string name() const override { return "rs:terrain_landform"; }
    std::string displayName() const override { return "Terrain Landform"; }
    std::string group() const override { return "terrain"; }
    std::string description() const override {
        return "Multiscale topographic position (TPI), Weiss landform "
               "classes, and geomorphon form patterns over a DEM.";
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
