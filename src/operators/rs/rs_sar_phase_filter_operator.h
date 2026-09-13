/***************************************************************************
 * rs_sar_phase_filter_operator.h — Goldstein-Werner phase filtering
 * (Advanced SAR / PolSAR / InSAR 10.0, package C)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_phase_filter — Goldstein-Werner spectral filtering of a complex
 * interferogram (spatial form: Z_f = Σ|z|^α·z / Σ|z|^α over the window;
 * output = the filtered UNIT phasor per pixel, encoded as a complex
 * raster so the chain stays in the complex artifact domain).
 *
 * Output: CFloat32 single band |Z_f| = 1 (NaN where the window has no
 * valid sample). Consume with rs:sar_unwrap.
 */
class RsSarPhaseFilterOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_phase_filter"; }
    std::string displayName() const override { return "InSAR Phase Filter"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Goldstein-Werner spatial phase filtering of a complex "
               "interferogram (window size and alpha exponent configurable).";
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
