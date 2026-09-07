/***************************************************************************
 * rs_temporal_monitor_operator.h — CUSUM / EWMA / seasonal Mann-Kendall
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:temporal_monitor — per-pixel temporal monitoring over a spatiotemporal
 * collection (Foundation 5.0, Milestone E).
 *
 * Methods:
 *   cusum       cumulative sum of standardized anomalies z_t = (x−μ)/σ with
 *               μ/σ from the full series (retrospective monitoring); bands =
 *               [S_final, max|S|, argmaxSceneIndex]
 *   ewma        exponentially weighted moving average of z_t with λ
 *               (parameter `lambda` ∈ (0,1]); bands = [Z_final, max|Z|, argmax]
 *   seasonal_mk seasonal Mann-Kendall trend (season = calendar month,
 *               tie-corrected variance); bands = [Z, tau, seasonsUsed]
 *
 * Shared contract with rs:temporal_anomaly: collection input, temporal
 * preflight, optional QA masking, band-role resolution, min_observations,
 * 3-band Float32 NaN-NoData output, atomic TemporalOutputGuard.
 *
 * Complexity guard: seasonal_mk is O(Σ_m n_m²) pairs per pixel; the operator
 * refuses collections beyond `max_pairwork` (schema default documents the
 * bound) instead of running unbounded.
 */
class RsTemporalMonitorOperator : public RSOperator {
public:
    std::string name() const override { return "rs:temporal_monitor"; }
    std::string displayName() const override { return "Temporal Monitor"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override {
        return "Per-pixel temporal monitoring: CUSUM and EWMA of standardized "
               "anomalies, or seasonal Mann-Kendall trend (calendar-month seasons).";
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
