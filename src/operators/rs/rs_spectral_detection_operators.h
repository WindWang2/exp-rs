/***************************************************************************
 * rs_spectral_detection_operators.h — matched filter + ACE detectors
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:matched_filter — signed matched filter (t−μ)ᵀΣ⁻¹(x−μ) of a target
 * spectrum against the streamed scene background (Milestone C).
 *
 * rs:ace — adaptive coherence/cosine estimator, squared whitened cosine
 * between target and pixel, output in [0, 1] (Milestone C).
 *
 * Shared contract:
 *   input  (string, required)        Multi-band input raster
 *   output (string, required)        Single-band score raster (Float32)
 *   target (array of numbers, req.)  Target spectrum, one value per band
 *
 * Background statistics (mean, covariance) stream from the input with the
 * RX operator's valid-pixel predicate (non-finite or declared-NoData pixels
 * excluded); three passes, O(tile + bands²) memory, bit-exact grade.
 * Thresholding is a downstream step (rs:threshold_raster).
 */
class RsMatchedFilterOperator : public RSOperator {
public:
    std::string name() const override { return "rs:matched_filter"; }
    std::string displayName() const override { return "Matched Filter"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Matched filter detection of a target spectrum against the scene "
               "background: signed whitened projection per pixel.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

class RsAceOperator : public RSOperator {
public:
    std::string name() const override { return "rs:ace"; }
    std::string displayName() const override { return "ACE Detector"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Adaptive coherence estimator: squared whitened cosine between a "
               "target spectrum and each pixel, in [0, 1].";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
