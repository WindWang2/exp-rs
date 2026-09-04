/***************************************************************************
 * rs_sar_backscatter_operator.h — SAR backscatter conversion (Platform 3.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Converts SAR backscatter between radiometric states (sigma0/gamma0/beta0)
 * and/or numeric domains (linear power / dB), using a constant scene incidence
 * angle or a per-pixel local incidence raster. DN input is rejected: calibrate
 * with rs:sar_calibrate first. Writes the Platform-3.0 SAR metadata block
 * (SICNU_SAR_CALIBRATION/SICNU_SAR_DOMAIN/SICNU_RADIOMETRIC_STATE) so the
 * product re-ingests with correct observation contracts.
 */
class RsSarBackscatterOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_backscatter"; }
    std::string displayName() const override { return "SAR Backscatter Conversion"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "Convert SAR backscatter between sigma0/gamma0/beta0 states and "
               "linear power or dB using a constant or per-pixel incidence angle.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
