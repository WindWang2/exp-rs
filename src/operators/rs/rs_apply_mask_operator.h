/***************************************************************************
 * rs_apply_mask_operator.h  —  Apply a QA mask to a product raster
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Applies a binary quality mask (1 = masked/obscured, 0 = clear) to a
 * multi-band product raster: masked pixels are set to NoData in every band,
 * producing an analysis-ready raster that excludes cloud / shadow / snow.
 *
 * The output preserves the input's grid, band count, data types, band
 * semantics (SICNU_BAND_ROLE / wavelength metadata are copied through) and
 * per-band NoData values. When the mask grid differs from the input grid
 * (e.g. a 20 m Sentinel-2 SCL against a 10 m product), `align_mask` (default
 * true) nearest-neighbor resamples the mask onto the input grid — safe for
 * integer classification masks; pixels outside the mask extent are treated as
 * clear. Same-CRS grid differences are auto-aligned; CRS mismatches are always
 * an error (align CRSs first). Processing is block-streaming
 * (RSOperatorMemoryPolicy::Streaming) and cancellable.
 *
 * Parameters:
 *   input      (string, required) Product raster path (multi-band)
 *   mask       (string, required) Mask raster path (band 1: value > 0 = masked)
 *   output     (string, required) Output raster path
 *   no_data    (double, optional) NoData value used for bands without one;
 *                                required when an input band defines none
 *   align_mask (bool, optional)   Auto-align same-CRS mask grid to the input
 *                                grid (default: true)
 *
 * Returns JSON object with:
 *   output        (string) Output raster path
 *   maskedPixels  (int)    Number of masked (set-to-NoData) pixels
 *   totalPixels   (int)    Evaluated pixel count (input grid)
 *   maskedPercent (double) Percentage of masked pixels
 *   aligned       (bool)   Whether the mask was resampled onto the input grid
 */
class RsApplyMaskOperator : public RSOperator {
public:
    std::string name() const override { return "rs:apply_mask"; }
    std::string displayName() const override { return "Apply Mask"; }
    std::string group() const override { return "qa"; }
    std::string description() const override {
        return "Set masked pixels (cloud / shadow / snow) to NoData in every "
               "band of a product raster, yielding analysis-ready imagery.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
