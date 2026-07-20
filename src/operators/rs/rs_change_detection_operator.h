/***************************************************************************
 * rs_change_detection_operator.h  —  Change detection RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Computes change detection between two co-registered rasters.
 *
 * Methods:
 *   - difference: after - before
 *   - normalized_difference: (after - before) / (after + before)
 *   - change_mask: thresholded binary mask from difference
 *
 * Parameters:
 *   before     (string, required) Before-date raster path
 *   after      (string, required) After-date raster path
 *   output     (string, required) Output raster path
 *   method     (string, optional) One of: difference, normalized_difference, change_mask (default: difference)
 *   threshold  (number, optional) Threshold for change_mask (default: 0.5)
 *   band       (int, optional)    1-based band number to process (default: 1)
 *
 * Returns JSON object with:
 *   output  (string) Output raster path
 *   method  (string) Applied method
 *   mean    (number) Mean of difference (for difference/normalized_difference)
 *   stddev  (number) Stddev of difference
 */
class RsChangeDetectionOperator : public RSOperator {
public:
    std::string name() const override { return "rs:change_detection"; }
    std::string displayName() const override { return "Change Detection"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override {
        return "Detect changes between two co-registered raster images.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
