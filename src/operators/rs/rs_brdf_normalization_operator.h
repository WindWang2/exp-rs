/***************************************************************************
 * rs_brdf_normalization_operator.h — radiometric-physics-11 (E/G)
 *
 * Kernel-driven view-angle normalization of reflectance rasters to a
 * reference geometry (default: nadir view, unchanged sun). Angles come from
 * the parameters or from the SICNU_SUN_* / SICNU_VIEW_* dataset metadata
 * (stamped by rs:solar_geometry / the provider); missing angles are a typed
 * refusal, never defaults. Per-band kernel weights (f_vol, f_geo) are
 * caller-provided. Ross-Thick + Li-Sparse-Reciprocal per
 * BrdfNormalization; state metadata preserved.
 ***************************************************************************/
#pragma once
#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

class RsBrdfNormalizationOperator : public RSOperator {
public:
    std::string name() const override { return "rs:brdf_normalization"; }
    std::string displayName() const override { return "BRDF Normalization"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Normalize optical reflectance from the observed sun/view geometry to a reference "
               "geometry (nadir view by default) with the Ross-Thick + Li-Sparse-Reciprocal "
               "kernels. Requires per-band kernel weights and sun/view angles (parameters or "
               "SICNU_* metadata); refuses when the geometry or weights are missing/nonphysical.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run(const Json::Value &params, RSOperatorContext &context) override;
};

} // namespace sicnu::operators::rs
