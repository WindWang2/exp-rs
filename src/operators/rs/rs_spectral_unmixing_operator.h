/***************************************************************************
 * rs_spectral_unmixing_operator.h  —  Linear spectral unmixing RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Linear spectral unmixing: estimates per-pixel endmember abundances from a
 * set of user-supplied endmember spectra (least squares + clip + unit-sum
 * renormalization). Outputs one abundance band per endmember and an optional
 * per-pixel reconstruction-error band.
 *
 * Parameters:
 *   input       (string, required) Input multi-band raster
 *   output      (string, required) Output abundance raster (one band per endmember)
 *   endmembers  (array, required)  Array of endmember spectra (arrays of band-count floats)
 *   bands       (int, optional)    1-based band subset (reserved; default all)
 *   errorOut    (string, optional) Optional per-pixel reconstruction-error raster
 *
 * Returns JSON object with:
 *   output      (string) Output raster path
 *   endmembers  (int)    Number of endmembers
 *   meanError   (double) Mean per-pixel reconstruction error
 */
class RsSpectralUnmixingOperator : public RSOperator {
public:
    std::string name() const override { return "rs:spectral_unmixing"; }
    std::string displayName() const override { return "Linear Spectral Unmixing"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Estimate per-pixel endmember abundances by linear spectral unmixing.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
