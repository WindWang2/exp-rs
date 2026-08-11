/***************************************************************************
 * rs_threshold_raster_operator.h  —  Reusable threshold / mask operator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Thresholds a single-band magnitude/score raster into a binary UInt8 mask
 * (0 = below threshold, 1 = at/above threshold, 255 = NoData) with manual,
 * Otsu, percentile, or statistical (mean + k*stddev) strategies, plus
 * optional morphological cleanup and minimum-mapping-unit filtering.
 *
 * Reusable beyond change detection (any score raster can be thresholded) and
 * the atomic building block of the change-mask workflow.
 */
class RsThresholdRasterOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:threshold_raster"; }
    std::string displayName() const override { return "Threshold Raster"; }
    std::string group() const override { return "masking"; }
    std::string description() const override
    {
        return "Threshold a raster into a binary mask (manual/Otsu/percentile/statistical).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
