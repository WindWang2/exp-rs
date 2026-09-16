// sar_topographic_phase.h — rigorous DEM/orbit topographic phase
// (Advanced InSAR 11.0, package B; DECISIONS D-002).
//
// CHAIN (the honest geometric truth, no B⊥ approximation): for a ground
// point P (WGS84 geodetic, height h above the ellipsoid) and the two
// partner orbits of a pair:
//   r_m = zero-Doppler slant range of the MASTER orbit at P      (metres)
//   r_s = zero-Doppler slant range of the SLAVE orbit at P      (metres)
//   φ_topo = wrap( −(4π/λ)·(r_m − r_s) )                        (radians)
// where wrap maps to (−π, π]. This is exactly the geometric contribution
// the DEM height imprints on the interferogram φ = arg(s_m·conj(s_s))
// (s_m·conj(s_s) phase = −(4π/λ)(r_m − r_s) + displacement + atmosphere):
// removal is φ_residual = wrap(φ_ifg − φ_topo).
//
// Sign conventions (pinned by tests, do not "fix" silently):
//   * φ carries a MINUS sign vs range difference (two-way propagation
//     phase = −4π·range/λ), consistent with sar_insar.h displacement
//     d_los = −λ·φ/(4π) (positive = motion toward the sensor).
//   * r_m is the MASTER range (the interferogram convention s_master·conj(
//     s_slave)); swapping master/slave flips the sign of φ_topo.
//   * Heights are metres ABOVE THE WGS84 ELLIPSOID (geoid-attached DEMs
//     must be converted upstream — the kernel cannot know the datum).
//
// The rejected alternative (documented in DECISIONS.md D-002) is the
// B⊥-approximation φ ≈ (4π/λ)·B⊥·h/(r·sinθ): it degenerates at nadir,
// needs a per-pixel incidence/baseline model, and diverges on relief —
// this module evaluates the true range difference instead, at the cost of
// two zero-Doppler solves per point (forwardRangeDoppler authority,
// O(orbit states) each).
//
// Failure semantics: every function refuses (false + domain-coded error)
// when the geometry is not computable — no zero-Doppler crossing in an
// orbit segment (BASELINE_NO_ZERO_DOPPLER_*), failed interpolation, or a
// non-finite input. NaN heights/phases propagate as NaN outputs (masked,
// never clamped). Grid semantics, CRS conversion and DEM sampling are
// operator-seam concerns (rs:sar_remove_topographic_phase); the kernel
// consumes geodetic points directly.
#pragma once

#include "sar_orbit.h"

#include <QString>

namespace sicnu::sar
{

/// Wraps a phase (radians) into (−π, π]. NaN passthrough.
double wrapPhaseRad( double phaseRad );

/// Rigorous topographic phase at one ground point for a scene pair:
/// φ_topo = wrap( −(4π/λ)·(r_master − r_slave) ) with the zero-Doppler
/// ranges of both orbits at @a ground (height = ellipsoidal metres).
/// @param wavelengthM metres (> 0). Refuses (typed, domain-coded) when
/// either orbit has no zero-Doppler crossing at the point.
bool topographicPhaseAtGround( const OrbitSegment &masterOrbit,
                               const OrbitSegment &slaveOrbit,
                               double wavelengthM,
                               const GeodeticPoint &ground,
                               double *phaseRad, QString *error = nullptr );

/// Elementwise residual: out[i] = wrap( ifgPhase[i] − topoPhase[i] ).
/// NaN wherever either input is NaN (counted in @a nanCount when given).
/// @p n elements per plane (row-major).
void removeTopographicPhase( const double *ifgPhase, const double *topoPhase,
                             long long n, double *out, long long *nanCount = nullptr );

/// Statistics of a plane removal (operator seam reporting).
struct TopoPhaseRemovalStats
{
    long long evaluated = 0;   ///< elements processed
    long long nanOutput = 0;   ///< elements that came out NaN (invalid inputs)
};

} // namespace sicnu::sar
