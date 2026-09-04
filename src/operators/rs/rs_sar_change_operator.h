/***************************************************************************
 * rs_sar_change_operator.h — SAR change detection (log-ratio -> mask)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * SAR change detection: computes the log-difference magnitude (dB) between
 * two co-registered scenes (stage 1, streamed to a work-dir temporary) and
 * derives a binary change mask from it via a manual / Otsu / percentile /
 * statistical threshold with optional morphological cleanup and minimum
 * mapping unit filtering (stage 2, the shared change-streaming mask
 * machinery behind rs:threshold_raster). Optionally keeps the dB magnitude
 * raster. Statistical thresholds adapt to the data, hence the tolerance
 * determinism grade.
 */
class RsSarChangeOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_change"; }
    std::string displayName() const override { return "SAR Change Detection"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "Detect change between two co-registered SAR scenes: log-ratio "
               "magnitude (dB) thresholded into a change mask (manual, Otsu, "
               "percentile or statistical).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::MultiPassStreaming; }
    std::string determinismGrade() const override { return "tolerance"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
