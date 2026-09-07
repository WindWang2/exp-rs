/***************************************************************************
 * rs_sar_terrain_masks_operator.h — SAR terrain geometry masks (D.3)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_terrain_masks — local incidence angle and geometric layover/shadow
 * masks for SAR scenes over a co-registered DEM, under the DECLARED
 * CONSTANT-GEOMETRY contract (incidence + heading metadata or explicit
 * parameters). Foundation 5.0, Milestone D.3.
 *
 * This is the rigorous executable subset: both products follow from the DEM
 * surface normal and the look vector (see sar_terrain_geometry.h). FULL
 * range-Doppler terrain correction / geocoding requires orbit state vectors
 * and sensor timing that the generic product metadata contract does not
 * carry — this operator does not approximate it, and the extension contract
 * (SICNU_SAR_ORBIT_STATES etc.) is documented in sar_terrain_geometry.h for
 * future providers.
 *
 * Parameters:
 *   dem       (string, required) DEM raster
 *   output    (string, required) Output raster path
 *   product   (string, required) local_incidence (deg, Float32) |
 *             layover_shadow_mask (Byte: 0 normal, 1 layover, 2 shadow, 255 NoData)
 *   incidence (number, required unless SICNU_SAR_INCIDENCE_DEG declared) deg from vertical, (0, 90)
 *   heading   (number, required unless SICNU_SAR_HEADING_DEG declared) look azimuth deg from north, [0, 360)
 */
class RsSarTerrainMasksOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_terrain_masks"; }
    std::string displayName() const override { return "SAR Terrain Masks"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Local incidence angle and geometric layover/shadow masks from a "
               "DEM under declared constant SAR geometry (not full range-Doppler).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
