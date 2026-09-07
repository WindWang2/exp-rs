/***************************************************************************
 * rs_image_enhancement_operator.h — rs:image_enhancement
 *
 * Thin-client migration of the image enhancement panel's streaming dispatch
 * (Desktop Workbench UX 4.0). Every pixel formula stays in
 * ImageEnhancementStreaming; this operator owns the file-level orchestration
 * the dialog used to hand-roll, so GUI/CLI/MCP share one execution path.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

class RsImageEnhancementOperator : public RSOperator {
public:
    std::string name() const override { return "rs:image_enhancement"; }
    std::string displayName() const override { return "Image Enhancement"; }
    std::string group() const override { return "enhancement"; }
    std::string description() const override {
        return "Contrast stretch, spatial filter, band ratio/IHS, or SAR speckle filter.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
