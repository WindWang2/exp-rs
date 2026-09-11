/***************************************************************************
 * rs_sar_temporal_stats_operator.h — multi-date SAR statistics
 * (Scientific Processing 8.0, package B)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_temporal_stats — per-pixel statistics over N >= 2 co-registered
 * SAR scenes (Scientific Processing 8.0, package B).
 *
 * Kernels compute in LINEAR POWER (sar_temporal.h): dB-declared scenes are
 * converted once, so the output domain never depends on the input
 * declaration. A sample is valid iff finite and strictly positive.
 * Nonpositive power is outside the physical domain — NaN, never clamped.
 *
 * Robust change products are anchored at the per-pixel MEDIAN linear
 * baseline; deviations are log-domain dB magnitudes, where the multiplicative
 * speckle process is additive (speckle-robust — unlike linear
 * mean-based deviations).
 *
 * Output bands (fixed order; also declared as SICNU_SAR_TEMPORAL_BANDS):
 *   1 mean_db              10·log10(mean linear power)
 *   2 mean_linear          mean linear power
 *   3 std_dev_linear       population stddev of linear power
 *   4 cv                   std_dev_linear / mean_linear (speckle dispersion)
 *   5 min_db               10·log10(min)
 *   6 max_db               10·log10(max)
 *   7 argmax_date          0-based index of the first maximum
 *   8 baseline_db          10·log10(median linear)
 *   9 max_log_deviation_db robust change magnitude vs the baseline
 *   10 changed_dates       dates with |deviation| >= changeThresholdDb
 *   11 valid_count         valid observations (always written)
 *
 * Pixels with validCount < minValid are NaN on every band except
 * valid_count.
 *
 * Parameters:
 *   inputs            (array string, required) scene paths, N >= 2
 *   output            (string, required) output raster path
 *   band              (int, optional) 1-based band per scene (default 1)
 *   inputDomain       (string, optional) "declared" (default) | "linear_power" | "db";
 *                     declared scenes must agree, mixed declarations are
 *                     typed refusals
 *   changeThresholdDb (number, optional) robust-change counting threshold (6 dB)
 *   minValid          (int, optional) minimum valid observations (default 2)
 */
class RsSarTemporalStatsOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_temporal_stats"; }
    std::string displayName() const override { return "SAR Temporal Statistics"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Multi-date SAR statistics: linear-domain mean/dispersion, dB "
               "reporting, and robust log-domain change against the median "
               "baseline over N co-registered scenes.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
