/***************************************************************************
 * rs_rasterize_operator.h — vector → raster burn (8.0, package D)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:rasterize — burns vector geometries onto a reference raster grid.
 *
 * The reference raster defines the output grid, CRS and geotransform
 * (north-up). Features stream through the geospatial VectorReader contract
 * with the reader's declared CRS transform into the raster CRS; pixels are
 * burned through the shared windowed rasterization seam (rs_raster_vector)
 * — the SAME pixel-membership implementation rs:zonal_stats uses, so the
 * two operators can never disagree about what a geometry covers.
 *
 * Semantics:
 *   * unburned pixels are NaN (declared NoData);
 *   * overlapping geometries: LAST wins (feature order);
 *   * pixel-center membership by default, ALL_TOUCHED with allTouched=true;
 *   * burn values: constant `value` (default 1) or a numeric `field`
 *     (non-numeric values are typed refusals, never silently coerced);
 *   * geometryless features and geometries outside the grid are counted,
 *     not fatal.
 *
 * Parameters:
 *   input      (string, required) reference raster (grid + CRS donor)
 *   vector     (string, required) vector dataset (OGR)
 *   layer      (string, optional) layer name/index (default first)
 *   field      (string, optional) numeric attribute to burn
 *   value      (number, optional) constant burn value when no field (1)
 *   allTouched (bool, optional) ALL_TOUCHED pixel selection (false)
 *   output     (string, required) output raster path
 */
class RsRasterizeOperator : public RSOperator {
public:
    std::string name() const override { return "rs:rasterize"; }
    std::string displayName() const override { return "Rasterize Vector"; }
    std::string group() const override { return "raster-vector"; }
    std::string description() const override {
        return "Burn vector geometries onto a reference raster grid (constant "
               "value or numeric attribute), last-wins on overlap, NaN NoData.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
