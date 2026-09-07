/***************************************************************************
 * gdal_pansharpen_operator.h — gdal_pansharpen.py CLI RSOperator
 *
 * Thin-client migration of the fusion dialog's inline gdal_pansharp lambda
 * (Desktop Workbench UX 4.0): external subprocess execution, cooperative
 * cancellation, output cleanup, and typed errors at the operator seam.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::gdal {

class GdalPanSharpenOperator : public RSOperator {
public:
    std::string name() const override { return "gdal:pansharpen"; }
    std::string displayName() const override { return "GDAL Pan-Sharpen"; }
    std::string group() const override { return "fusion"; }
    std::string description() const override {
        return "Pan-sharpen via the gdal_pansharpen.py utility (bilinear, LZW GTiff).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::gdal
