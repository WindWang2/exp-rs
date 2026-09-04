/***************************************************************************
 * rs_feature_select_operator.h — Feature cube band subsetting (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Subsets a feature cube's bands by id, semantic role and/or 1-based index,
 * preserving the self-describing contract (band numbers renumbered 1..k;
 * the normalization section is dropped — stored stats no longer match the
 * subset). This is the model-input preparation step: select exactly a model
 * manifest's input.band_roles, then feed rs:infer.
 */
class RsFeatureSelectOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:feature_select"; }
    std::string displayName() const override { return "Feature Band Selection"; }
    std::string group() const override { return "features"; }
    std::string description() const override
    {
        return "Keep (or with complement, drop) feature cube bands by id, semantic role "
               "or 1-based index, renumbering the bands and keeping the feature "
               "contract intact for model-input matching.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
