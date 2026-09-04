/***************************************************************************
 * rs_obia_label_operator.h  —  ROI majority segment labeling
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_label
 *
 * Label the segments of an existing label raster from training polygons by
 * pixel majority (issue #663: replaces the OBIA GUI's hand-rolled
 * rasterize-and-vote import, which duplicated the kernel). Delegates to the
 * canonical analysis-layer RsRoiLabeler (ADR 0060): center-of-pixel
 * rasterization, class-field fallback chain (classField → "class" → "id"),
 * ties → smaller class id, minLabelPixels floor.
 *
 * Parameters:
 *   input          (string, required) — source raster (georeferences the polygons)
 *   labels         (string, required) — segment label raster (UInt32)
 *   training       (string, required) — training polygons with an integer class field
 *   output         (string, required) — destination CSV (segment_id,class_id)
 *   classField     (string, default "class_id")
 *   minLabelPixels (int, default 3)   — min ROI pixels to label a segment
 *
 * Returns: output, labeled (rows written), skipped (segments below the floor)
 */
class RsObiaLabelOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_label"; }
    std::string displayName() const override { return "OBIA ROI Labeling"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Label objects by pixel majority of training polygons (CSV output).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
