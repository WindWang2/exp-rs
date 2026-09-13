/***************************************************************************
 * rs_sar_unwrap_operator.h — reference quality-guided phase unwrapping
 * (Advanced SAR / PolSAR / InSAR 10.0, package C; DECISIONS D-001)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_unwrap — BUILT-IN reference phase unwrapper (quality-guided
 * flood fill, deterministic): seeds at the highest-quality valid pixel and
 * unwraps outward through quality-descending neighbors under the Itoh
 * condition. Honest limits (sar_insar.h): not residue-aware, not a global
 * optimum (no SNAPHU/MCF semantics); dense residue fields produce wrong
 * 2π branches exactly like any greedy unwrapper.
 *
 * The `provider` parameter (default "builtin") is the external-provider
 * seam: any other value is a typed refusal (UNWRAP_PROVIDER_UNAVAILABLE) —
 * the built-in never masquerades as a named external tool.
 *
 * Memory: the unwrapper is deliberately single-scale (tiling breaks global
 * phase connectivity). The whole phase/quality planes are materialized
 * behind a fixed 2 GiB budget; larger rasters are refused with
 * MEMORY_BUDGET_EXCEEDED (use a smaller AOI or an external provider).
 */
class RsSarUnwrapOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_unwrap"; }
    std::string displayName() const override { return "InSAR Phase Unwrap"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Reference quality-guided flood-fill phase unwrapping of a "
               "complex interferogram, with an explicit external-provider "
               "seam and a plane memory budget.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
