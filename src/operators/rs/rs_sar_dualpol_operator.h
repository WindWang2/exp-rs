/***************************************************************************
 * rs_sar_dualpol_operator.h — dual-pol feature RSOperator (Milestone D.2)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_dualpol_features — single-feature dual-polarization derived raster
 * (Foundation 5.0, Milestone D.2).
 *
 * Features (see processing/algorithms/sar/sar_dualpol.h for the formulas):
 *   ratio, normalized_difference, log_ratio, rvi (dual-pol Sentinel-1
 *   approximation — NOT quad-pol RVI), span.
 *
 * Domain contract: inputs may be linear power or dB — the declared
 * SICNU_SAR_DOMAIN wins, the explicit `domain` parameter overrides, and dB
 * inputs are converted to linear before the kernel. Negative/nonpositive
 * out-of-domain values become NaN (never clamped).
 *
 * Parameters:
 *   input    (string, required) 2-band (or more) calibrated SAR raster
 *   output   (string, required) Output raster path
 *   feature  (string, required) one of the feature ids above
 *   vv_band  (integer, optional, default 1) 1-based VV (co-pol) band
 *   vh_band  (integer, optional, default 2) 1-based VH (cross-pol) band
 *   domain   (string, optional) linear|db|auto (default auto = declared
 *            metadata, falling back to linear with a logged warning)
 */
class RsSarDualPolOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_dualpol_features"; }
    std::string displayName() const override { return "SAR Dual-Pol Features"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Dual-polarization feature rasters (ratio, normalized difference, "
               "log ratio, dual-pol RVI, span) from VV/VH calibrated backscatter.";
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
