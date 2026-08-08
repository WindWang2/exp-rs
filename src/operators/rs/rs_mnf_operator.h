/***************************************************************************
 * rs_mnf_operator.h  —  Minimum Noise Fraction RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Minimum Noise Fraction (MNF) transform: PCA of noise-whitened data, with
 * components ordered by signal-to-noise ratio. The standard dimensionality
 * reduction for hyperspectral imagery (noise covariance estimated from
 * lagged differences).
 *
 * Parameters:
 *   input         (string, required) Input multi-band raster
 *   output        (string, required) Output MNF components raster
 *   numComponents (int, optional)    Number of components (0 = all bands)
 *
 * Returns JSON object with:
 *   output        (string) Output raster path
 *   numComponents (int)    Components written
 *   width/height  (int)    Output dimensions
 */
class RsMnfOperator : public RSOperator {
public:
    std::string name() const override { return "rs:mnf"; }
    std::string displayName() const override { return "MNF (Minimum Noise Fraction)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Minimum Noise Fraction transform for hyperspectral dimensionality reduction.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
