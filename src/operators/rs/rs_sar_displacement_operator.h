/***************************************************************************
 * rs_sar_displacement_operator.h — phase-to-displacement conversion
 * (Advanced SAR / PolSAR / InSAR 10.0, package C; DECISIONS D-009)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_displacement — line-of-sight displacement from UNWRAPPED phase:
 * d_los = −λ·φ/(4π) (positive = motion toward the sensor). The wavelength
 * comes from `wavelengthUm` or the scene metadata
 * (SICNU_SAR_WAVELENGTH_UM); missing both is a typed refusal.
 *
 * Honest quality gate (D-009): the Itoh discontinuity ratio
 * (phaseDiscontinuityRatio) is reported in the result; above
 * `warnThreshold` (default 0.02) the operator WARNS — the caller decides.
 * The operator cannot reliably detect "wrapped vs unwrapped" from data
 * alone and refuses to pretend otherwise.
 */
class RsSarDisplacementOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_displacement"; }
    std::string displayName() const override { return "InSAR LOS Displacement"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Convert an unwrapped interferometric phase to line-of-sight "
               "displacement (metres) with an Itoh discontinuity quality "
               "diagnostic.";
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
