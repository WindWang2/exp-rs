/***************************************************************************
 * rs_sar_geocode_operator.h — SAR Range-Doppler geocoding (8.0, package A)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_geocode — forward Range-Doppler geocoding of a calibrated SAR scene
 * onto a DEM map grid, under the DECLARED ORBIT CONTRACT
 * (SICNU_SAR_ORBIT_STATES / SICNU_SAR_AZIMUTH_START_UTC / SICNU_SAR_PRF /
 * SICNU_SAR_RANGE_WINDOW / SICNU_SAR_RANGE_RATE — Scientific Processing 8.0).
 *
 * Chain per DEM cell: forward range-Doppler (ground → zero-Doppler azimuth
 * time + slant range, bounded bisection solver) → source SAR position →
 * bilinear/nearest radiometry resample → real-line-of-sight geometry
 * (reference and terrain-facet incidence, layover/shadow classes) →
 * optional radiometric-terrain correction gamma0 = sigma0 · sin θ0 / sin θL
 * (Ulander 1996 area factor from REAL per-pixel geometry).
 *
 * Products (fixed five-band Float32 output; order also declared in the
 * result and as SICNU_SAR_GEOCODE_BANDS output metadata):
 *   1 backscatter      resampled radiometry in the input's own domain
 *   2 gamma0           backscatter × sin θ0 / sin θL (RTC)
 *   3 incidence        reference (ellipsoid) incidence θ0, degrees
 *   4 local_incidence  terrain-facet incidence θL, degrees
 *   5 layover_shadow   0 normal, 1 layover, 2 shadow (NaN NoData)
 *
 * Refusals (typed, never approximated): incomplete/invalid orbit contract,
 * DEM without CRS, rotated DEM grids, unresolvable scene-timing/grid
 * contradictions. Unresolvable/out-of-swath cells are NaN, counted in the
 * result, never fabricated.
 *
 * Parameters:
 *   input     (string, required) calibrated SAR raster (sigma0 power)
 *   dem       (string, required) DEM raster — defines the output grid
 *   output    (string, required) output raster path
 *   band      (int, optional) 1-based SAR band (default 1)
 *   resampling (string, optional) "bilinear" (default) | "nearest"
 */
class RsSarGeocodeOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_geocode"; }
    std::string displayName() const override { return "SAR Range-Doppler Geocode"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Range-Doppler geocoding of a SAR scene onto a DEM map grid with "
               "real-geometry incidence, layover/shadow masks and gamma0 terrain "
               "correction under the declared orbit contract.";
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
