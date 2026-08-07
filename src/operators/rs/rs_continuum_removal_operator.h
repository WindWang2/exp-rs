/***************************************************************************
 * rs_continuum_removal_operator.h  —  Continuum-removed reflectance
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:continuum_removal
 *
 * Per-pixel continuum removal on a multi-band raster. Each pixel's spectrum is
 * divided by its upper convex hull (continuum) so absorption features become
 * normalized valleys in (0, 1]. Pure-C++ kernel (no OpenCV dependency).
 *
 * Parameters:
 *   input  (string, required)  multi-band reflectance raster
 *   output (string, required)  continuum-removed raster (same band count)
 */
class RsContinuumRemovalOperator : public RSOperator {
public:
    std::string name() const override { return "rs:continuum_removal"; }
    std::string displayName() const override { return "Continuum Removal"; }
    std::string group() const override { return "hyperspectral"; }
    std::string description() const override {
        return "Normalize each pixel's reflectance spectrum to its convex-hull continuum.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
