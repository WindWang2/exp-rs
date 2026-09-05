/***************************************************************************
 * rs_sar_calibrate_operator.h — SAR radiometric calibration (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Calibrates SAR digital numbers to radiometrically calibrated backscatter
 * (sigma0): sigma0 = (DN² − noiseLinear) / A², with optional constant noise
 * subtraction. Output domain is linear power or dB. Writes the Platform-3.0
 * SAR metadata block (SICNU_MODALITY/SICNU_SAR_CALIBRATION/SICNU_SAR_DOMAIN/
 * SICNU_POLARIZATIONS/SICNU_RADIOMETRIC_STATE) so the product re-ingests with
 * correct observation contracts.
 */
class RsSarCalibrateOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_calibrate"; }
    std::string displayName() const override { return "SAR Radiometric Calibration"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "Calibrate SAR digital numbers (DN) to sigma0 backscatter "
               "(linear power or dB) with optional noise subtraction.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
