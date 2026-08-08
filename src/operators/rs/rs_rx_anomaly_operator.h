/***************************************************************************
 * rs_rx_anomaly_operator.h  —  Reed-Xiaoli anomaly detection RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Reed-Xiaoli (RX) anomaly detector: per-pixel Mahalanobis distance to the
 * global background statistics. High scores indicate pixels that do not fit
 * the scene background (anomalies) — the standard unsupervised detector for
 * hyperspectral imagery.
 *
 * Parameters:
 *   input  (string, required) Input multi-band raster
 *   output (string, required) Output RX score raster (Float32, single band)
 *
 * Returns JSON object with:
 *   output (string) Output raster path
 *   mean   (double) Mean RX score
 *   max    (double) Maximum RX score
 */
class RsRxAnomalyOperator : public RSOperator {
public:
    std::string name() const override { return "rs:rx_anomaly"; }
    std::string displayName() const override { return "RX Anomaly Detection"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Reed-Xiaoli anomaly detection (Mahalanobis distance to scene background).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        // Two-pass streaming (stats + per-tile score) — O(tile + bands^2) memory,
        // not the O(width*height*bands) of the prior full-raster materialization.
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
