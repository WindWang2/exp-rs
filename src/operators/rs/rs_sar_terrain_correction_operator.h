/***************************************************************************
 * rs_sar_terrain_correction_operator.h — SAR DEM terrain correction product
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Full DEM terrain correction product: terrain-flattened gamma0
 * (sigma0·cosθ0/cosθi from a co-registered DEM, radar geometry) plus an
 * optional Byte layover/shadow validity mask band and an optional local
 * incidence angle band. Band layout: band 1 = gamma0; band 2 = mask when
 * enabled; last band = incidence (degrees) when enabled. Writes the
 * Platform-3.0 SAR metadata block so the product re-ingests with correct
 * observation contracts.
 */
class RsSarTerrainCorrectionOperator : public RSOperator
{
public:
    std::string name() const override { return "rs:sar_terrain_correction"; }
    std::string displayName() const override { return "SAR Terrain Correction"; }
    std::string group() const override { return "sar"; }
    std::string description() const override
    {
        return "DEM terrain correction product: terrain-flattened gamma0 with a "
               "layover/shadow validity mask and the local incidence angle band.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
