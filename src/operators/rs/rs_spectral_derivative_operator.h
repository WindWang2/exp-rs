/***************************************************************************
 * rs_spectral_derivative_operator.h — per-pixel spectral derivatives
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * First/second spectral derivatives along the wavelength axis (Milestone B3).
 *
 * Input bands must carry per-band WAVELENGTH metadata (nm) — an explicit
 * `wavelengths` array may override it. The wavelength axis must be strictly
 * ascending; duplicate or descending axes are typed refusals (the derivative
 * against a band *index* is not a spectral derivative).
 *
 * Output band count: order=1 → B−1 bands (each stamped with its midpoint
 * wavelength), order=2 → B−2 bands.
 *
 * Parameters:
 *   input       (string, required) Multi-band reflectance raster
 *   output      (string, required) Output raster path
 *   order       (integer, required) 1 or 2
 *   wavelengths (array of numbers, optional, nm) explicit axis; default reads
 *               band WAVELENGTH metadata
 */
class RsSpectralDerivativeOperator : public RSOperator {
public:
    std::string name() const override { return "rs:spectral_derivative"; }
    std::string displayName() const override { return "Spectral Derivative"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "First or second spectral derivative along the wavelength axis "
               "(finite differences; requires a wavelength axis).";
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
