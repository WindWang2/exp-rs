/***************************************************************************
 * rs_quality_mosaic_operator.h — F15: quality mosaic operator (ADR 0163)
 *
 * rs:quality_mosaic — production-grade multi-scene mosaic with a scene/grid
 * plan (Package A), robust inter-scene radiometric balancing (B), DP
 * seamline placement (C), NoData-safe feather/multiband blending (D),
 * quality-score compositing with per-pixel provenance (E), and tiled
 * COG-friendly atomic output with optional overviews and a JSON report (G).
 *
 * The legacy rs:mosaic keeps its first/last-valid contract untouched; this
 * operator is additive and composes the same registration seams.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

class RsQualityMosaicOperator : public RSOperator {
public:
    std::string name() const override { return "rs:quality_mosaic"; }
    std::string displayName() const override { return "Quality Mosaic"; }
    std::string group() const override { return "composition"; }
    std::string description() const override {
        return "Seam-lined, radiometrically balanced quality mosaic with per-pixel "
               "provenance, feather blending, tiled atomic output, overviews and a "
               "quality report.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
