/***************************************************************************
 * rs_topographic_correction_operator.h — illumination topographic correction
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Illumination-angle topographic correction of optical reflectance over a
 * co-registered DEM (Milestone B.1).
 *
 * Corrects the slope-driven illumination variation of TOA/surface
 * reflectance after atmospheric correction:
 *   - cosine:       Lambertian L·cosθz/cos_i (self-shadowed → NaN)
 *   - c_correction: empirical C (Teillet 1982), also the SCS+C form when c
 *                   is estimated from the radiance~illumination regression
 *   - minnaert:     L·(cosθz/cosi)^k (Smith et al. 1980, log-log fit)
 *
 * The regression pass and the apply pass are both tile-streamed
 * (O(tile) memory; full-scene per-band fits accumulated in double).
 *
 * Parameters:
 *   input         (string, required) Reflectance raster (multi-band)
 *   dem           (string, required) DEM raster on the same grid (typed
 *                 refusal on grid mismatch — no hidden resampling)
 *   output        (string, required) Output corrected raster path
 *   method        (string, required) cosine | c_correction | minnaert
 *   solar_zenith  (number, required) Solar zenith in degrees from vertical [0, 90)
 *   solar_azimuth (number, required) Solar azimuth in degrees clockwise from north [0, 360)
 *
 * Returns JSON object with: output, method, bandCount, width, height and the
 * per-band fitted parameters (a/b/c or k).
 */
class RsTopographicCorrectionOperator : public RSOperator {
public:
    std::string name() const override { return "rs:topographic_correction"; }
    std::string displayName() const override { return "Topographic Correction"; }
    std::string group() const override { return "optical"; }
    std::string description() const override {
        return "Topographic correction of reflectance over a co-registered DEM "
               "(cosine, C/SCS+C, or Minnaert illumination models).";
    }
    // Two full tile-streamed passes (regression + apply): O(tile) memory.
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
