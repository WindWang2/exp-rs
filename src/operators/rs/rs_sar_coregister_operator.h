/***************************************************************************
 * rs_sar_coregister_operator.h — global complex shift estimation + resample
 * (Advanced SAR / PolSAR / InSAR 10.0, package C; DECISIONS D-005)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_coregister — FINE global-shift co-registration refinement for a
 * same-grid complex SLC pair: magnitude-domain patch NCC with parabolic
 * sub-pixel refinement and a robust (median) global translation model,
 * applied to the slave by bilinear complex resampling.
 *
 * HONEST SCOPE: translation only — no affine/polynomial warp, no DEM-based
 * coregistration, no range-Doppler refinement. Same-grid preflight applies
 * (GRID_MISMATCH refusal); the operator refines residual offsets, it does
 * not align different grids.
 */
class RsSarCoregisterOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_coregister"; }
    std::string displayName() const override { return "SAR Coregistration"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Estimate the residual global shift between a same-grid "
               "complex SLC pair (patch NCC, sub-pixel) and resample the "
               "slave onto the master by bilinear complex interpolation.";
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
