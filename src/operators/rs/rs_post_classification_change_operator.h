/***************************************************************************
 * rs_post_classification_change_operator.h  —  Post-classification change
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Post-classification comparison of two thematic (classification) rasters.
 *
 * Counts per-pixel class transitions into a row-major transition matrix
 * (rows = before class, columns = after class) and writes a change-type map:
 * output value = beforeClass * classCount + afterClass, UInt16 NoData where
 * either input is NoData. Two-pass block streaming
 * (RSOperatorMemoryPolicy::MultiPassStreaming): pass 1 counts transitions and
 * probes the observed class range, pass 2 writes the change map. O(tile)
 * memory plus O(classCount^2) for the matrix.
 *
 * Parameters:
 *   before       (string, required) Before-date thematic raster
 *   after        (string, required) After-date thematic raster
 *   output       (string, required) Change-type map path (UInt16)
 *   band         (int, optional)    1-based class band on both rasters (1)
 *   beforeBand   (int, optional)    1-based band on before (overrides band)
 *   afterBand    (int, optional)    1-based band on after (overrides band)
 *   class_count  (int, optional)    Number of classes; 0 = auto from the
 *                                   maximum observed class + 1 (<= 255)
 *   class_labels (array, optional)  Optional class names echoed in the result
 *
 * Returns JSON object with:
 *   output           (string) Change-type map path
 *   classCount       (int)    Effective class count
 *   transitionMatrix (array)  classCount x classCount counts (row = before,
 *                             column = after)
 *   fromTotals       (array)  Per-before-class totals (row sums)
 *   toTotals         (array)  Per-after-class totals (column sums)
 *   netChange        (array)  toTotals - fromTotals per class
 *   changedPixels    (int)    Pixels whose class changed
 *   unchangedPixels  (int)    Pixels whose class stayed
 *   totalPixels      (int)    Evaluated (valid) pixel count
 *   changedPercent   (double) Percentage of evaluated pixels that changed
 */
class RsPostClassificationChangeOperator : public RSOperator {
public:
    std::string name() const override { return "rs:post_classification_change"; }
    std::string displayName() const override { return "Post-Classification Change"; }
    std::string group() const override { return "change-detection"; }
    std::string description() const override {
        return "Compare two thematic rasters and report the per-class transition "
               "matrix, gains/losses, and a change-type map.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
