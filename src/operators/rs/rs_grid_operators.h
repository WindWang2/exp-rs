/***************************************************************************
 * rs_grid_operators.h — Foundation 6.0, Milestone D: grid & resampling
 * foundation operators over the authoritative GDAL warp seam.
 *
 *   rs:resample — resolution change on the raster's own CRS (explicit
 *                 resampling kernel, categorical safety pin).
 *   rs:align    — snap a raster onto a reference raster's exact grid
 *                 (CRS + origin + resolution + extent), the preflight
 *                 harmonization step the grid-compat contract points to
 *                 (docs/processing/grid-and-radiometric-policy.md §1.3:
 *                 "pre-align via the grid-harmonization seam").
 *
 * Both delegate the resampling math to sicnu::geo::warpRaster
 * (gdalwarp semantics: staged, validated, atomically published, bounded
 * warp memory). No kernel is reimplemented here.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

class RsResampleOperator : public RSOperator {
public:
    std::string name() const override { return "rs:resample"; }
    std::string displayName() const override { return "Resample Raster"; }
    std::string group() const override { return "raster"; }
    std::string description() const override {
        return "Change the raster resolution on the input's own CRS with an "
               "explicit resampling kernel (GDAL warp); categorical rasters "
               "are pinned to nearest-neighbour unless 'mode' is chosen.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

class RsAlignOperator : public RSOperator {
public:
    std::string name() const override { return "rs:align"; }
    std::string displayName() const override { return "Align Raster To Reference"; }
    std::string group() const override { return "raster"; }
    std::string description() const override {
        return "Warp a raster exactly onto a reference raster's grid (CRS, "
               "origin, resolution and extent) so multi-input operators "
               "accept the pair; identical grids publish a lossless copy.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
