/***************************************************************************
 * rs_band_tools_operators.h — Band ratio / extract-bands / contrast-stretch
 * RSOperators (Desktop Workbench UX 4.0 thin-client migration).
 *
 * The kernels were promoted verbatim from src/app dialog lambdas into
 * processing/algorithms/band_tools.* — these operators are thin JSON adapters
 * at the authoritative seam so GUI/CLI/MCP share one execution path.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/// rs:band_ratio — numerator/denominator band ratio, or RGB -> IHS
/// decomposition (mode "ratio" | "ihs").
class RsBandRatioOperator : public RSOperator {
public:
    std::string name() const override { return "rs:band_ratio"; }
    std::string displayName() const override { return "Band Ratio / IHS"; }
    std::string group() const override { return "enhancement"; }
    std::string description() const override {
        return "Band ratio (numerator/denominator) or RGB-to-IHS color transform.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/// rs:extract_bands — copy the listed (1-based) bands into a new Float32
/// raster, preserving order and georeference.
class RsExtractBandsOperator : public RSOperator {
public:
    std::string name() const override { return "rs:extract_bands"; }
    std::string displayName() const override { return "Extract Bands"; }
    std::string group() const override { return "enhancement"; }
    std::string description() const override {
        return "Extract one or more bands into a new multi-band raster.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/// rs:contrast_stretch — per-band contrast stretch (linear, percent clip,
/// stddev, histogram equalize, piecewise) with declared-NoData masking.
class RsContrastStretchOperator : public RSOperator {
public:
    std::string name() const override { return "rs:contrast_stretch"; }
    std::string displayName() const override { return "Contrast Stretch"; }
    std::string group() const override { return "enhancement"; }
    std::string description() const override {
        return "Stretch each band (linear / percent-clip / stddev / histogram / piecewise).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
