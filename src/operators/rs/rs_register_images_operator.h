/***************************************************************************
 * rs_register_images_operator.h — F13 cross-modal image registration
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Cross-modal (optical-SAR / cross-sensor) image registration: match the
 * source to the reference, fit a robust transform, and write the source
 * warped into the reference frame plus an optional quality report sidecar.
 *
 * Failure semantics (F13 contract): structural shortfalls refuse the job
 * (no output written); low-confidence results still write the product but
 * the result carries status=low_confidence with the reason — callers must
 * review before trusting the geometry.
 *
 * Parameters:
 *   source     (string, required)  Moving raster (band 1)
 *   reference  (string, required)  Reference raster defining the output grid
 *   output     (string, required)  Registered output GeoTIFF (Float32)
 *   reportPath (string, optional)  JSON quality report sidecar
 *                                 (exp_rs_registration_quality/1)
 *   metric     (string, optional)  auto|phase_correlation|mutual_information|ncc
 *   maxDim     (int, optional)     Long-side processing cap (default 1024)
 *   resampling (string, optional)  nearest|bilinear|cubic|lanczos
 *
 * Returns: output, status, reason, inlierCount, inlierRmsePx, coverageRatio,
 *          rmsePx, ce90Px, matchGrid (decimated processing dims).
 */
class RsRegisterImagesOperator : public RSOperator {
public:
    std::string name() const override { return "rs:register_images"; }
    std::string displayName() const override { return "Image Registration"; }
    std::string group() const override { return "geometric"; }
    std::string description() const override
    {
        return "Cross-modal (optical-SAR) image registration with refusal semantics: "
               "match, fit, warp into the reference frame, and report CE90/residual-field "
               "quality. Refusals write nothing; low-confidence outputs are flagged.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
