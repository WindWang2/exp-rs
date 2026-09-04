/***************************************************************************
 * rs_obia_segment_operator.h  —  OBIA segmentation (teaching + OTB engines)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_segment
 *
 * Object segmentation with a selectable engine (issue #663: the OBIA GUI's
 * prefer-OTB / teaching-fallback policy lives here, not in frontends):
 *
 *   engine=simple — teaching segmenter (default, no OTB required):
 *     Gaussian smooth → intensity quantization → connected components →
 *     merge small regions (RsSimpleSegmenter, ADR 0060).
 *   engine=otb    — OTB MeanShift via RsOtbSegmenter (ADR 0058 raster
 *     dialect). Fail-closed when the OTB CLI is unavailable.
 *   engine=auto   — OTB MeanShift when available, teaching fallback with a
 *     warning otherwise (the OBIA GUI policy).
 *
 * Multi-band inputs are reduced to a mean intensity band first (simple
 * engine). Output is a UInt32 label GeoTIFF (0 = background/nodata).
 *
 * Parameters:
 *   input          (string, required)
 *   output         (string, required)
 *   engine         (enum: simple|otb|auto, default simple)
 *   smoothKernel   (int, odd, default 5)      — simple
 *   quantizeBins   (int, default 32)          — simple
 *   minRegionSize  (int, default 50)          — both engines
 *   spatialRadius  (int, default 5)           — otb
 *   rangeRadius    (number, default 15.0)     — otb
 *   maxIterations  (int, default 100)         — otb
 *   threshold      (number, default 0.1)      — otb
 *   bands          (array int, optional) 1-based bands — simple
 *
 * Returns: output, segments, engine (engine that actually ran), width, height
 */
class RsObiaSegmentOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_segment"; }
    std::string displayName() const override { return "OBIA Segmentation"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Segment imagery into objects (teaching segmenter or OTB MeanShift).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
