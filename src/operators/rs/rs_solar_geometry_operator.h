/***************************************************************************
 * rs_solar_geometry_operator.h — radiometric-physics-11 (work package B/G)
 *
 * Computes sun position and earth-sun geometry from an acquisition instant
 * (SolarGeometry, Spencer 1971 / NOAA closed forms) and optionally stamps
 * the scene metadata with SICNU_SUN_* / SICNU_EARTH_SUN_* keys so downstream
 * calibration, BRDF and topographic operators can consume real angles
 * instead of refusing (or defaulting) when import metadata lacks them.
 *
 * No pixel I/O: metadata-only in-place update, or a pure calculation whose
 * result JSON carries the full provenance.
 ***************************************************************************/
#pragma once
#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

class RsSolarGeometryOperator : public RSOperator {
public:
    std::string name() const override { return "rs:solar_geometry"; }
    std::string displayName() const override { return "Solar Geometry"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Compute sun elevation/azimuth, solar declination, earth-sun distance and the "
               "inverse-square earth-sun factor from acquisition date/time and scene centre "
               "(Spencer 1971 / NOAA closed forms). Optionally writes SICNU_SUN_ZENITH, "
               "SICNU_SUN_AZIMUTH, SICNU_SUN_ELEVATION, SICNU_EARTH_SUN_FACTOR and "
               "SICNU_EARTH_SUN_DISTANCE_AU dataset metadata onto the input raster.";
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
