/***************************************************************************
 * rs_local_rx_operator.h  —  dual-window local RX anomaly detection
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Dual-window local RX: per-pixel Mahalanobis distance to the LOCAL
 * background (outer window minus inner guard), for anomalies that a global
 * RX misses because they dominate or blend into the scene-wide statistics.
 *
 * Parameters:
 *   input        (string, required) Input multi-band raster
 *   output       (string, required) Output RX score raster (Float32, 1 band;
 *                NoData = NaN where the pixel is invalid or unscoreable)
 *   qualityOut   (string, optional) Background valid-sample-count raster
 *                (UInt16/Float32 — per-pixel quality plane)
 *   outerWindow  (int, default 5, odd >= 3) background window side
 *   innerWindow  (int, default 3, odd >= 1, < outerWindow) guard window side
 *   covariance   (enum "full"|"diagonal", default "full"; diagonal scales to
 *                very high band counts)
 *   loading      (double, default 1e-3) scaled diagonal loading alpha
 *
 * Returns JSON object with:
 *   output (string)        score raster path
 *   scoredPixels (number)  pixels with a valid score
 *   unscoredPixels (number) invalid or statistically under-sampled pixels
 *   mean / max (number)    score summary over scored pixels
 *   covariance (string)    mode actually used
 */
class RsLocalRxOperator : public RSOperator {
public:
    std::string name() const override { return "rs:local_rx_anomaly"; }
    std::string displayName() const override { return "Local (Dual-Window) RX Anomaly Detection"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Reed-Xiaoli distance to the local background (dual window with "
               "guard), with scaled diagonal loading and per-pixel quality counts.";
    }

    RSOperatorMemoryPolicy memoryPolicy() const override {
        // The kernel enumerates window-minus-guard pixels per pixel over a
        // resident tile; the operator streams the raster in tiles and
        // manages its own outer-window halo via padded reads
        // (readWindowBip), so the framework-level halo stays 0.
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
