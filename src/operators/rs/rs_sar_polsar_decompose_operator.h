/***************************************************************************
 * rs_sar_polsar_decompose_operator.h — full-polarimetric decomposition
 * (Advanced SAR / PolSAR / InSAR 10.0, package B)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_polsar_decompose — polarimetric decomposition of a FULL-POL
 * complex (CFloat32) scattering raster: HH/HV/VV channels under the
 * reciprocity contract (sar_complex.h channel identity, DECISIONS D-002),
 * an ensemble window (D-003), and one decomposition product per run.
 *
 * Products (fixed band orders, declared as SICNU_SAR_POLSAR_BANDS):
 *   pauli          4 bands: odd_bounce, double_bounce, volume, span
 *   h_alpha        7 bands: entropy, anisotropy, alpha_deg, dominance,
 *                  lambda1, lambda2, lambda3 (Cloude-Pottier)
 *   freeman_durden 4 bands: surface, double_bounce, volume, span
 *   yamaguchi      5 bands: surface, double_bounce, volume, helix, span
 *
 * MODE CONTRACT: dual-pol detected inputs are refused
 * (POLARIZATION_MISMATCH) — no dual-pol approximation is relabeled as a
 * quad-pol decomposition (sar_polsar.h honesty rule).
 *
 * Refusals (typed): non-CFloat32 bands (COMPLEX_BANDS_REQUIRED),
 * unresolvable channel identity (POLARIZATION_MISMATCH), even window
 * (InvalidParameter), 4-channel input with assumeReciprocity=false
 * (NON_RECIPROCAL_CHANNELS).
 */
class RsSarPolsarDecomposeOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_polsar_decompose"; }
    std::string displayName() const override { return "PolSAR Decomposition"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Full-polarimetric decomposition (Pauli, H/A/alpha, "
               "Freeman-Durden, Yamaguchi) of complex HH/HV/VV SLC channels "
               "under the reciprocity contract, with ensemble window averaging.";
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
