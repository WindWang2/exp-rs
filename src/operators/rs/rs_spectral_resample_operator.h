/***************************************************************************
 * rs_spectral_resample_operator.h  —  Spectral resampling RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Resamples a multi-band raster from its native wavelength grid onto a target
 * wavelength grid by linear interpolation. Source wavelengths are read from
 * each band's WAVELENGTH metadata (written by product stacking, ADR 0065)
 * unless `sourceWavelengths` is given explicitly.
 *
 * Parameters:
 *   input              (string, required) Input multi-band raster
 *   output             (string, required) Output resampled raster (one band per target wavelength)
 *   wavelengths        (array, required)  Target band center wavelengths (nm)
 *   sourceWavelengths  (array, optional)  Source wavelengths (nm); default reads band WAVELENGTH metadata
 *
 * Returns JSON object with:
 *   output     (string) Output raster path
 *   bands      (int)    Number of output bands
 *   sourceWavelengths (array) Resolved source wavelengths
 */
class RsSpectralResampleOperator : public RSOperator {
public:
    std::string name() const override { return "rs:spectral_resample"; }
    std::string displayName() const override { return "Spectral Resampling"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Resample spectra onto a target wavelength grid by linear interpolation.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
