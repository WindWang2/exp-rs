/***************************************************************************
 * rs_terrain_flow_operator.h — depression fill / D8 flow (Foundation 5.0, F)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:terrain_flow — hydrology foundation products over a DEM (Milestone F).
 *
 * Products (see processing/algorithms/terrain_flow.h):
 *   fill               priority-flood depression filling (NoData = barrier)
 *   flow_direction     D8 steepest-descent codes (ESRI powers of two, 0 = sink)
 *   flow_accumulation  self-inclusive drainage counts (requires the filled
 *                      surface; computed from the directions of this run)
 *
 * Full-frame kernel contract (O(N log N) fill, O(N) routing) — the terrain
 * family's documented memory model; the schema estimate states the bound.
 *
 * Parameters: input (DEM), output, product, cell_size (optional, metres per
 * cell for documentation purposes; routing itself is cell-based).
 */
class RsTerrainFlowOperator : public RSOperator {
public:
    std::string name() const override { return "rs:terrain_flow"; }
    std::string displayName() const override { return "Terrain Flow"; }
    std::string group() const override { return "terrain"; }
    std::string description() const override {
        return "Depression filling (priority-flood), D8 flow directions, and "
               "drainage accumulation over a DEM.";
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
