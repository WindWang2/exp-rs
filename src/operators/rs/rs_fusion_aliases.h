/***************************************************************************
 * rs_fusion_aliases.h  —  Atomic image-fusion method operators
 *
 * rs:fusion_linear / rs:fusion_brovey / rs:fusion_pca / rs:fusion_ihs /
 * rs:fusion_gram_schmidt — atomic method-selector aliases sharing the single
 * ImageFusion::processNativeFusion kernel. The legacy rs:image_fusion
 * operator remains the high-level selector facade.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/// Shared base for the fixed-method fusion aliases: the schema/metadata/run
/// differ only in the method string.
class RsFusionMethodOperator : public RSOperator
{
public:
    std::string name() const override = 0;
    std::string displayName() const override = 0;
    std::string group() const override { return "fusion"; }
    std::string description() const override = 0;
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;

protected:
    /// The fixed fusion method this alias selects ("linear", "brovey", ...).
    virtual std::string methodName() const = 0;
    /// Short purpose text for metadata.
    virtual std::string methodPurpose() const = 0;
};

class RsFusionLinearOperator : public RsFusionMethodOperator
{
public:
    std::string name() const override { return "rs:fusion_linear"; }
    std::string displayName() const override { return "Fusion Linear"; }
    std::string description() const override { return "Linear pan-sharpening fusion."; }
protected:
    std::string methodName() const override { return "linear"; }
    std::string methodPurpose() const override { return "Linear blend of pan and multispectral bands."; }
};

class RsFusionBroveyOperator : public RsFusionMethodOperator
{
public:
    std::string name() const override { return "rs:fusion_brovey"; }
    std::string displayName() const override { return "Fusion Brovey"; }
    std::string description() const override { return "Brovey transform pan-sharpening fusion."; }
protected:
    std::string methodName() const override { return "brovey"; }
    std::string methodPurpose() const override { return "Brovey transform preserving color ratios."; }
};

class RsFusionPcaOperator : public RsFusionMethodOperator
{
public:
    std::string name() const override { return "rs:fusion_pca"; }
    std::string displayName() const override { return "Fusion PCA"; }
    std::string description() const override { return "Principal Component Analysis pan-sharpening fusion."; }
protected:
    std::string methodName() const override { return "pca"; }
    std::string methodPurpose() const override { return "PCA substitution fusion for multi-band sharpening."; }
};

class RsFusionIhsOperator : public RsFusionMethodOperator
{
public:
    std::string name() const override { return "rs:fusion_ihs"; }
    std::string displayName() const override { return "Fusion IHS"; }
    std::string description() const override { return "Intensity-Hue-Saturation pan-sharpening fusion."; }
protected:
    std::string methodName() const override { return "ihs"; }
    std::string methodPurpose() const override { return "IHS fusion for 3-band RGB composites."; }
};

class RsFusionGramSchmidtOperator : public RsFusionMethodOperator
{
public:
    std::string name() const override { return "rs:fusion_gram_schmidt"; }
    std::string displayName() const override { return "Fusion Gram-Schmidt"; }
    std::string description() const override { return "Gram-Schmidt pan-sharpening fusion."; }
protected:
    std::string methodName() const override { return "gram_schmidt"; }
    std::string methodPurpose() const override { return "Gram-Schmidt fusion with minimal spectral distortion."; }
};

} // namespace sicnu::operators::rs
