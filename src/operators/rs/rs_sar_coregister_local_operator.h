/***************************************************************************
 * rs_sar_coregister_local_operator.h — local offset-field co-registration
 * (Advanced InSAR 11.0, package C; DECISIONS D-003)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_coregister_local — multi-scale LOCAL offset-field co-registration
 * for a same-grid complex SLC pair: per-patch magnitude NCC on a lattice
 * (parabolic sub-pixel, median-filtered), a confidence-masked offset field
 * (dx, dy, peakRatio), and bilinear resampling of the slave by the field.
 * Unconfident lattice nodes fall back to the global shift model
 * (rs:sar_coregister's semantics); the fallback count is reported.
 *
 * HONEST SCOPE (D-003): translation-field model — no affine/polynomial
 * warp, no DEM-based coregistration. The estimate needs BOTH full planes
 * behind the 2 GiB budget gate (MEMORY_BUDGET_EXCEEDED beyond).
 */
class RsSarCoregisterLocalOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_coregister_local"; }
    std::string displayName() const override { return "SAR Local Coregistration"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Estimate a local offset field between same-grid complex SLC "
               "rasters (patch NCC lattice with sub-pixel refinement and "
               "median filtering) and resample the slave onto the master.";
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
