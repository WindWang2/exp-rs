/***************************************************************************
 * rs_spectral_similarity_operator.h  —  SID-SAM hybrid similarity
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * SID-SAM hybrid spectral similarity / classification. Combines the
 * Spectral Angle Mapper (shape) and Spectral Information Divergence
 * (probability distribution) into one similarity measure and labels each
 * pixel to the most similar reference (or emits pairwise scores).
 *
 * Parameters:
 *   input  (string, required) Input multi-band raster
 *   output (string, required) Label raster (UInt16, 0 = unlabelled,
 *          1-based class per reference; value 65535 reserved = invalid)
 *   refs / refsRef / libraryPath (+libraryMaterials) (reference seam)
 *   scoreOut (string, optional) Best-score raster (Float32)
 *   form (enum "product_normalized"|"classic_tan", default product_normalized)
 *
 * Returns JSON with output/refs/meanScore/form and provenance echo.
 */
class RsSpectralSimilarityOperator : public RSOperator {
public:
    std::string name() const override { return "rs:spectral_similarity"; }
    std::string displayName() const override { return "SID-SAM Hybrid Similarity"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Hybrid spectral similarity combining SAM and SID (bounded "
               "product or classic tan form); classifies pixels to the most "
               "similar reference.";
    }

    RSOperatorMemoryPolicy memoryPolicy() const override {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
