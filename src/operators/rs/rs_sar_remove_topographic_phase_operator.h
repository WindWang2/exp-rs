/***************************************************************************
 * rs_sar_remove_topographic_phase_operator.h — rigorous DEM/orbit
 * topographic phase removal (Advanced InSAR 11.0, package B; DECISIONS
 * D-002)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_remove_topographic_phase — geometric topographic phase of a
 * co-registered interferogram pair from a DEM and BOTH scene orbits, and
 * its removal from the complex interferogram.
 *
 * CHAIN (sar_topographic_phase.h): per interferogram pixel — map center →
 * WGS84 geodetic (GDAL/OSR authority), DEM height bilinear at that map
 * location, zero-Doppler ranges of master/slave orbits at the ground
 * point, φ_topo = wrap(−4π(r_m − r_s)/λ); removal rotates the complex
 * sample by e^{−iφ_topo} (amplitude preserved, φ_residual = wrap(
 * φ_ifg − φ_topo)). This is the true range-difference phase, NOT the
 * B⊥ approximation and NOT the low-order ramp fit of rs:sar_interferogram.
 *
 * Fail-closed preflights (typed, never approximated):
 *   TOPO_PHASE_METADATA_MISSING  wavelength (param nor
 *                                SICNU_SAR_WAVELENGTH_UM metadata) or an
 *                                orbit string missing/invalid
 *   GRID_CRS_MISSING             interferogram without CRS/geotransform
 *   DEM_CRS_MISMATCH             DEM CRS differs from the interferogram
 *                                (remedy: warp the DEM first)
 *   DEM_GRID_UNSUPPORTED         rotated/sheared geotransform on either
 *                                raster (north-up axis-aligned grids only)
 *   DEM_EXTENT_INSUFFICIENT      DEM does not fully cover the
 *                                interferogram extent
 *   COMPLEX_BANDS_REQUIRED       non-CFloat32 interferogram band
 *   TOPO_PHASE_ORBIT_COVERAGE    not a single pixel got a computable
 *                                phase (DEM area outside both orbit
 *                                windows / CRS transform failed)
 *
 * Heights are metres ABOVE THE WGS84 ELLIPSOID; geoid-attached DEMs must
 * be converted upstream (the data cannot declare its vertical datum
 * reliably — the operator states this instead of guessing).
 *
 * Streaming: O(tile) memory; the phase kernel is two zero-Doppler solves
 * per pixel (forwardRangeDoppler authority). Cancel-safe via the context
 * probe; outputs are written per tile through the streaming output seam.
 */
class RsSarRemoveTopographicPhaseOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_remove_topographic_phase"; }
    std::string displayName() const override { return "InSAR Topographic Phase Removal"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Remove the DEM/orbit topographic phase from a complex "
               "interferogram using the rigorous per-pixel range-difference "
               "geometry of both scene orbits (not an approximation).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
