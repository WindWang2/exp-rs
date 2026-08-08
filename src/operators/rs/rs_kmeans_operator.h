/***************************************************************************
 * rs_kmeans_operator.h  —  Unsupervised K-Means classification RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:kmeans_classification — full-raster unsupervised K-Means clustering.
 *
 * Builds a pixel feature matrix from selected bands, runs OpenCV kmeans,
 * and writes a single-band Byte class map (class IDs 1..k).
 *
 * Parameters:
 *   input       (string, required)  Input multi-band raster
 *   output      (string, required)  Output class map GeoTIFF
 *   k           (int, optional)     Number of clusters (default 3)
 *   maxSamples  (int, optional)     Max pixels used for centroid fit
 *                                   (default 100000; 0 = use all)
 *   bands       (array of int, optional) 1-based band indices (default: all)
 *
 * Returns:
 *   output, k, width, height, samplesUsed
 */
class RsKmeansOperator : public RSOperator {
public:
    std::string name() const override { return "rs:kmeans_classification"; }
    std::string displayName() const override { return "K-Means Classification"; }
    std::string group() const override { return "classification"; }
    std::string description() const override {
        return "Unsupervised K-Means clustering of multi-band raster pixels.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
