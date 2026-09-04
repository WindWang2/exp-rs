/***************************************************************************
 * rs_feature_normalize_operator.h — Feature cube normalization (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Standardizes a feature cube's bands (zscore | minmax | robust) and persists
 * the fit statistics INTO the cube contract (SICNU_FEATURE_CUBE normalization
 * section), so the exact same stats ride the file into training and inference
 * — train/inference consistency without a side stats file. 'inverse' applies
 * the EXISTING stored statistics backwards instead of fitting new ones.
 * Two streaming passes: a stats pass (Welford mean/variance + min/max, plus a
 * 512-bin histogram pass for the robust percentile range) and an apply pass;
 * floating-point accumulation order makes the grade "tolerance".
 */
class RsFeatureNormalizeOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:feature_normalize"; }
    std::string displayName() const override { return "Feature Normalization"; }
    std::string group() const override { return "features"; }
    std::string description() const override
    {
        return "Standardize feature cube bands (zscore, minmax or robust) and store the "
               "fit statistics inside the cube contract so training and inference share "
               "the exact same normalization; supports inverting stored stats.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::MultiPassStreaming; }
    std::string determinismGrade() const override { return "tolerance"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
