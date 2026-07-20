/***************************************************************************
 * rs_image_fusion_operator.h  —  Image fusion / pan-sharpening RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Fuses a high-resolution panchromatic image with low-resolution multispectral data.
 *
 * Methods:
 *   - linear:  weighted average fusion
 *   - brovey:  ratio-based Brovey transform
 *   - pca:     principal component substitution
 *   - ihs:     intensity-hue-saturation substitution (requires 3 MS bands as RGB)
 *
 * Parameters:
 *   pan       (string, required) Panchromatic raster path
 *   ms        (string, required) Multispectral raster path
 *   output    (string, required) Output fused raster path
 *   method    (string, optional) One of: linear, brovey, pca, ihs (default: linear)
 *   panWeight (number, optional) Weight for panchromatic in linear fusion (default: 0.5)
 *
 * Returns JSON object with:
 *   output  (string) Output raster path
 *   method  (string) Applied method
 *   bands   (int)    Number of output bands
 */
class RsImageFusionOperator : public RSOperator {
public:
    std::string name() const override { return "rs:image_fusion"; }
    std::string displayName() const override { return "Image Fusion"; }
    std::string group() const override { return "enhancement"; }
    std::string description() const override {
        return "Fuse panchromatic and multispectral imagery (pan-sharpening).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
