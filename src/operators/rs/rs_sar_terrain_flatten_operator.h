/***************************************************************************
 * rs_sar_terrain_flatten_operator.h — SAR radiometric terrain flattening
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Radiometric terrain flattening: sigma0 -> gamma0 = sigma0·cosθ0/cosθi using
 * a co-registered DEM in radar geometry (plane-fit RTC model). Writes a
 * two-band product (band 1 = gamma0, band 2 = validity mask 1/0/255) plus the
 * Platform-3.0 SAR metadata block (SICNU_SAR_CALIBRATION/SICNU_SAR_DOMAIN/
 * SICNU_RADIOMETRIC_STATE) so the product re-ingests with correct observation
 * contracts. Facets with a local incidence angle >= 85° are masked as
 * layover/shadow. rs:sar_terrain_correction is the 3-band product
 * (gamma0 + mask + local incidence).
 */
class RsSarTerrainFlattenOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_terrain_flatten"; }
    std::string displayName() const override { return "SAR Terrain Flattening"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "Radiometric terrain flattening: sigma0 to gamma0 "
               "(sigma0·cosθ0/cosθi) using a co-registered DEM in radar geometry. "
               "Writes band 1 = gamma0 and band 2 = validity mask (1/0/255).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
