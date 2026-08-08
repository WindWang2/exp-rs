/***************************************************************************
 * rs_segment_stats_operator.h  —  Per-segment spectral / area statistics
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:segment_stats
 *
 * Given a multi-band image and a label raster (from rs:obia_segment or similar),
 * compute per-segment mean (per band) and area (pixel count). Writes CSV.
 *
 * Parameters:
 *   input   (string, required)  Multi-band image
 *   labels  (string, required)  Segment / class label raster
 *   output  (string, required)  CSV path
 *   bands   (array int, optional) 1-based bands (default all)
 *
 * CSV columns: segment_id, area_pixels, mean_b1, mean_b2, ...
 */
class RsSegmentStatsOperator : public RSOperator {
public:
    std::string name() const override { return "rs:segment_stats"; }
    std::string displayName() const override { return "Segment Statistics"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Compute per-segment mean spectra and area from a label raster.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
