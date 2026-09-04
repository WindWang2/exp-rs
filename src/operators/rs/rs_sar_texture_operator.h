/***************************************************************************
 * rs_sar_texture_operator.h — SAR GLCM texture measures (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Computes Haralick GLCM texture measures (contrast, dissimilarity,
 * homogeneity, energy/ASM, entropy, mean, stddev, correlation) per pixel
 * over a sliding window of a SAR intensity raster, writing one Float32
 * band per requested measure in list order.
 */
class RsSarTextureOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_texture"; }
    std::string displayName() const override { return "SAR GLCM Texture"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "Compute GLCM (Haralick) texture measures over a sliding window of "
               "a SAR intensity raster; one Float32 output band per measure.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    // Per-window quantization/accumulation differs from a full-frame pass at
    // last-ULP level (ADR 0124).
    std::string determinismGrade() const override { return "tolerance"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
