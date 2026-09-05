/***************************************************************************
 * rs_feature_stack_operator.h — Multimodal feature cube builder (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Builds a self-describing multimodal feature cube from multiple co-registered
 * raster bands (optical indices, SAR backscatter/texture, DEM derivatives,
 * temporal metrics). Every output band is a FEATURE with a declared identity —
 * id, semantic role, unit, modality, time semantics and provenance — persisted
 * as the FeatureCubeContract (SICNU_FEATURE_CUBE dataset metadata) so
 * downstream consumers (rs:infer model matching, rs:feature_normalize,
 * rs:feature_select) see one consistent feature identity. Inputs must already
 * share a grid: grid mismatches are errors, never hidden resampling.
 */
class RsFeatureStackOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:feature_stack"; }
    std::string displayName() const override { return "Feature Cube Builder"; }
    std::string group() const override { return "features"; }
    std::string description() const override
    {
        return "Stack multiple co-registered raster bands into a self-describing "
               "multimodal feature cube (optical indices, SAR backscatter/texture, "
               "DEM derivatives, temporal metrics) with per-band feature identity "
               "metadata for model-input matching.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::MultiPassStreaming; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
