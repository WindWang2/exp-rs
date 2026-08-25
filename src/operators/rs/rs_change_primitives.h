/***************************************************************************
 * rs_change_primitives.h  —  Atomic change-detection metric operators
 *
 * rs:change_difference / rs:change_normalized_difference / rs:change_ratio /
 * rs:change_cva / rs:change_mad — single-responsibility change metrics that
 * share the memory-bounded tile-streaming kernel (rs_change_streaming.h).
 * The legacy multi-method facade rs:change_detection remains the high-level
 * selector; these primitives are the independently callable/composable steps
 * an Agent can chain (e.g. change_difference → threshold_raster).
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

class RsChangeDifferenceOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_difference"; }
    std::string displayName() const override { return "Change Difference"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Pixel-wise difference between two co-registered rasters (after - before).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeNormalizedDifferenceOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_normalized_difference"; }
    std::string displayName() const override { return "Change Normalized Difference"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Normalized difference change (after - before) / (after + before).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeRatioOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_ratio"; }
    std::string displayName() const override { return "Change Ratio"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Ratio change after / before (NaN where before is 0).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeCvaOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_cva"; }
    std::string displayName() const override { return "Change Vector Analysis"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Change Vector Analysis magnitude across all bands "
               "(sqrt of summed squared band deltas).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::MultiPassStreaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeMadOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_mad"; }
    std::string displayName() const override { return "Multivariate Alteration Detection"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Multivariate Alteration Detection change magnitude (canonical "
               "correlation analysis, multi-pass streaming).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::MultiPassStreaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeCvaAngleOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_cva_angle"; }
    std::string displayName() const override { return "Change Vector Analysis Angle & Quadrant"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Change Vector Analysis 2-band directional angle (radians) and semantic quadrant classification.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeSamOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_sam"; }
    std::string displayName() const override { return "Spectral Angle Mapper Change"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Spectral Angle Mapper (SAM) change angle (radians) across multi-spectral bands.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeLogRatioOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_log_ratio"; }
    std::string displayName() const override { return "Change Log Ratio"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Log-Ratio change metric ln(after + eps) - ln(before + eps).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class RsChangeIrMadOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:change_irmad"; }
    std::string displayName() const override { return "Iteratively Reweighted MAD"; }
    std::string group() const override { return "temporal"; }
    std::string description() const override
    {
        return "Iteratively Reweighted Multivariate Alteration Detection (IR-MAD) with Chi-Square sample weights.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::MultiPassStreaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
