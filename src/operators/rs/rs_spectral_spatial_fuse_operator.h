/***************************************************************************
 * rs_spectral_spatial_fuse_operator.h — spatial consistency fusion of
 * spectral score rasters (Spectral Intelligence 12.0, work package B).
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:spectral_spatial_fuse — fuses a single-band spectral score raster with
 * its local spatial context: fused = (1−β)·s + β·aggregate(s, window).
 * NoData/non-finite scores are excluded from every aggregate and stay NaN in
 * the output (no leak, no zero-fill bias). Streamed with an r-pixel halo, so
 * interior results are identical to a whole-plane pass.
 *
 *   input  (string, required)   Single-band score raster (e.g. detection output)
 *   output (string, required)   Single-band fused score raster (Float32)
 *   radius (int, optional)      Window half-side, >= 0, default 1
 *   beta   (number, optional)   Spatial weight in [0, 1], default 0.5
 *   method (enum, optional)     'mean' (default) or 'bilateral'
 *   sigmaRange (number, opt.)   Range sigma for the bilateral method, > 0
 */
class RsSpectralSpatialFuseOperator : public RSOperator {
public:
    std::string name() const override { return "rs:spectral_spatial_fuse"; }
    std::string displayName() const override { return "Spectral-Spatial Fuse"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Fuse a spectral score raster with its local spatial consistency: "
               "a NoData-aware convex combination of the score and the valid "
               "window mean (or edge-preserving bilateral aggregate).";
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
