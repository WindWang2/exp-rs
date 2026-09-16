/***************************************************************************
 * rs_terrain_solar_operator.h — shadow duration and hillshade series over a
 * DEM (track terrain-hydrology-11, package E).
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:terrain_solar — sun-track terrain products.
 *
 * Products (see processing/algorithms/terrain_solar.h):
 *   shadow_duration   float raster [0,1]: weighted fraction of daylight
 *                     track samples during which the cell is shadowed
 *   hillshade_series  float raster, one band per track sample (the
 *                     existing TerrainAnalysis::hillshade kernel per
 *                     sun position — no second hillshade implementation)
 *
 * The sun track comes from 'sun_track' ("az,elev[,weight];…" degrees) or is
 * generated for day_of_year/latitude between start_hour and end_hour
 * (local SOLAR time) with step_hours. Full-frame kernel contract; capped by
 * SICNU_TERRAIN_MAX_CELLS.
 */
class RsTerrainSolarOperator : public RSOperator {
public:
    std::string name() const override { return "rs:terrain_solar"; }
    std::string displayName() const override { return "Terrain Solar"; }
    std::string group() const override { return "terrain"; }
    std::string description() const override {
        return "Terrain shadow duration over a weighted sun track and "
               "hillshade series; sun positions from an explicit track or "
               "generated from date/latitude in local solar time.";
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
