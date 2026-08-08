/***************************************************************************
 * rs_obia_segment_operator.h  —  Simple OBIA / teaching segmenter
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_segment
 *
 * Teaching-quality object segmentation without OTB:
 *   Gaussian smooth → intensity quantization → connected components →
 *   merge small regions.
 *
 * Multi-band inputs are reduced to a mean intensity band first.
 * Output is a UInt32 label GeoTIFF (0 = background/nodata).
 *
 * Parameters:
 *   input          (string, required)
 *   output         (string, required)
 *   smoothKernel   (int, optional, odd, default 5)
 *   quantizeBins   (int, optional, default 32)
 *   minRegionSize  (int, optional, default 50)
 *   bands          (array int, optional) 1-based bands for mean intensity
 *
 * Returns: output, segments, width, height
 */
class RsObiaSegmentOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_segment"; }
    std::string displayName() const override { return "OBIA Simple Segmentation"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Segment imagery into objects (smooth + quantize + connected components).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
