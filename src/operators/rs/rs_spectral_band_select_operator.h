/***************************************************************************
 * rs_spectral_band_select_operator.h — band selection / bad-band exclusion
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:spectral_band_select — select or exclude bands of a multi-band raster,
 * by explicit index or by wavelength ranges, with wavelength-unit
 * normalization (WAVELENGTH_UNITS respected, always written as nm) and
 * metadata propagation. The composable missing piece of the spectral
 * preprocessing chain (continuum removal / derivatives / resampling already
 * exist as operators).
 *
 * Exactly one selection mode:
 *   bands            1-based band list (works without wavelength metadata)
 *   wavelengthMin/Max inclusive nm window over band centers (requires
 *                    WAVELENGTH metadata on every band)
 *   excludeRanges    array of {minNm, maxNm} ranges to drop (requires
 *                    WAVELENGTH metadata)
 * Selecting zero bands is a typed refusal (naming the filter and the
 * available wavelength extent), never an empty raster.
 */
class RsSpectralBandSelectOperator : public RSOperator {
public:
    std::string name() const override { return "rs:spectral_band_select"; }
    std::string displayName() const override { return "Spectral Band Select"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Select or exclude raster bands by index or wavelength range; "
               "wavelength metadata is normalized to nm and propagated.";
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
