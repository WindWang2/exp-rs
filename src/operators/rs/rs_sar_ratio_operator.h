/***************************************************************************
 * rs_sar_ratio_operator.h — SAR ratio / log-ratio pair metric (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Pair metric between two co-registered SAR scenes: linear power ratio A/B,
 * log-ratio 10·log10(A/B) (dB) or the absolute log-difference |ΔdB| (dB
 * magnitude of change). Inputs may be linear power or dB (declared domain is
 * converted before the ratio, so the output domain is independent of the
 * input domain). Streams tile-by-tile; O(tile) memory.
 */
class RsSarRatioOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_ratio"; }
    std::string displayName() const override { return "SAR Ratio / Log-Ratio"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "Ratio, log-ratio or absolute log-difference magnitude between "
               "two co-registered SAR scenes (incoherent change pair metric).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
