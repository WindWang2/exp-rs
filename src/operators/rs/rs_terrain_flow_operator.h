/***************************************************************************
 * rs_terrain_flow_operator.h — depression fill / D8 flow (Foundation 5.0, F)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:terrain_flow — hydrology products over a DEM (Foundation 5.0 F +
 * terrain-hydrology-11).
 *
 * Products (see processing/algorithms/terrain_flow.h and terrain_hydrology.h):
 *   fill               priority-flood depression filling (NoData = barrier)
 *   flow_direction     D8 steepest-descent codes (ESRI powers of two, 0 = sink)
 *   flow_accumulation  self-inclusive drainage counts
 *   watershed          pour-point basin labels
 *   flat_resolve       epsilon-gradient flat resolution (fills and drains)
 *   flow_direction_inf D∞ angles, degrees clockwise from north (-1 undecided)
 *   stream_network     threshold extraction + Strahler orders (result JSON
 *                      carries connectivity stats and optional link polylines)
 *   outlets            D8 direction-0 cells (sinks/rim outflows) as a mask
 *
 * Full-frame kernel contract (O(N log N) fill, O(N) routing) — the terrain
 * family's documented memory model; the schema estimate states the bound and
 * SICNU_TERRAIN_MAX_CELLS caps the cell count fail-closed.
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
