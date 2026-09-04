/***************************************************************************
 * rs_sar_speckle_operator.h — SAR speckle filtering (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Filters speckle from a SAR intensity raster with one of the classic
 * adaptive kernels (Lee, enhanced Lee, Frost, Kuan, Gamma-MAP, refined Lee)
 * or the Quegan-style multitemporal filter over co-registered companion
 * scenes. Streams tile-by-tile on the input grid; the band's declared
 * NoData is treated as NaN so window statistics stay physical.
 */
class RsSarSpeckleOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_speckle"; }
    std::string displayName() const override { return "SAR Speckle Filter"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "Despeckle a SAR intensity raster with Lee, enhanced Lee, Frost, "
               "Kuan, Gamma-MAP, refined Lee or multitemporal filtering.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    // Streaming window sums differ from a full-frame (SAT) implementation at
    // last-ULP level (ADR 0124).
    std::string determinismGrade() const override { return "tolerance"; }
    RSOperatorDeterminism determinism() const override { return RSOperatorDeterminism::Tolerance; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
