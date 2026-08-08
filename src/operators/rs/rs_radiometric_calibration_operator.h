/***************************************************************************
 * rs_radiometric_calibration_operator.h  —  Radiometric calibration RSOperator
 ***************************************************************************/
#pragma once
#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Convert raw DN values to physical units (radiance, TOA reflectance, or
 * brightness temperature) using sensor metadata coefficients.
 *
 * Resolves the previously-dangling rs:radiometric_calibration operator id and
 * activates the tool.rs.radiometric_calibration workflow definition.
 */
class RsRadiometricCalibrationOperator : public RSOperator {
public:
    std::string name() const override { return "rs:radiometric_calibration"; }
    std::string displayName() const override { return "Radiometric Calibration"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Convert DN to radiance, TOA reflectance, or brightness temperature "
               "from Landsat MTL / Sentinel-2 MTD / generic GDAL scale-offset metadata.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value &params, RSOperatorContext &context) override;
};

} // namespace sicnu::operators::rs
