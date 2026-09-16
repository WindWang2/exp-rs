/***************************************************************************
 * rs_sar_network_inversion_operator.h — small-baseline linear network
 * inversion (Advanced InSAR 11.0, package F; DECISIONS D-006)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_network_inversion — SBAS-style LINEAR small-baseline inversion
 * over a pair network of per-pair LOS displacement rasters: per-epoch
 * displacement stack (relative to the reference epoch), linear velocity,
 * and the fit RMS residual, with per-pixel missing-data handling
 * (sar_network_inversion.h). HONEST SCOPE: no atmospheric separation, no
 * PS selection — this is not PSI and must not be presented as PSI.
 *
 * Pair-level weights (e.g. per-pair mean coherence²) enter the normal
 * equations as a stack property; a per-pixel scalar quality weight is
 * mathematically inert and is not an input. maskStrategy:
 *   perpixel  — NaN pairs drop their rows; epochs outside the reference
 *               component come out NaN (the honest default)
 *   intersect — a pixel missing in ANY pair is dropped entirely (uniform
 *               coverage, documented degradation)
 */
class RsSarNetworkInversionOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_network_inversion"; }
    std::string displayName() const override { return "InSAR Network Inversion"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Solve per-epoch displacement and linear velocity from a "
               "connected pair network of unwrapped LOS displacement "
               "rasters (small-baseline linear inversion).";
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
