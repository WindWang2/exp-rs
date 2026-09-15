// src/processing/algorithms/brdf_normalization.h — view-angle (BRDF)
// normalization seam (radiometric-physics-11, work package E).
//
// The platform's optical chain had no view-geometry concept at all: every
// product was compared across dates/swaths as if the sensor always looked
// straight down, which multi-temporal compositing quietly punishes with
// several-percent per-band anisotropy. This module provides the two
// complementary normalization families the mission names, each with its
// angle-metadata preconditions stated and enforced:
//
//   * Kernel-driven — the MODIS-style Ross-Thick (volumetric) +
//     Li-Sparse-Reciprocal (geometric) kernel pair (Lucht, Schaaf & Strahler
//     2000, IEEE TGRS 38(2):977-989; Roujean et al. 1992 for k_vol's
//     one-parameter form). The forward anisotropy factor for one geometry is
//
//         f(θs,θv,Δφ) = 1 + f_vol·k_vol + f_geo·k_geo
//
//     and normalization maps an observation at geometry G to a reference
//     geometry G₀ (default: nadir view, same sun) by the ratio f(G₀)/f(G).
//     The kernel *weights* f_vol/f_geo are band- and canopy-specific: they
//     are ALWAYS caller-provided here (per band). Estimating them needs
//     multi-angle observations — a single scene cannot — so a caller without
//     published weights must use the empirical pair method below instead of
//     inventing weights. Weight domain: finite; the factor denominator is
//     checked to stay positive (a nonphysical 1 + Σf·k ≤ 0 refuses).
//
//   * Empirical pair normalization (mean-preserving c-factor leveling) —
//     for one date pair of the same sensor/grid, c is the ratio of the clear-
//     sample means, c = mean(ref) / mean(target) (y over x), and the
//     correction rho₂' = c·rho₂ levels date 2 onto date 1's mean radiometry
//     without any angles. Mean preservation is exact by construction:
//     mean(c·x) = mean(y). Validity conditions, enforced here: ≥ minimum
//     usable positive finite pairs, positive target mean, positive c.
//
// Angle conventions (platform-wide): zeniths from vertical in degrees
// [0,90), azimuths [0,360) from north clockwise, Δφ = sun azimuth − view
// azimuth. Sun geometry is validated with the SolarGeometry validators; a
// below-horizon sun or a ≥90° view zenith refuses (fail-closed, Oracle 2).
//
// All kernels are pure functions over doubles (radians converted internally
// with one documented constant) so tests can verify published special
// values: k_vol = 0 and k_geo = −1 exactly at (θs=0, θv=0), reciprocity in
// (θs, θv), and the Li-Sparse-R (h/b) = 2 convention.
#pragma once

#include <QString>

#include <cstddef>

namespace BrdfNormalization
{

/// Ross-Thick volumetric kernel k_vol(θs, θv, Δφ), degrees in, unitless out.
/// Symmetric in (θs, θv); 0 exactly at nadir sun + nadir view.
double rossThick( double sunZenithDeg, double viewZenithDeg, double relativeAzimuthDeg );

/// Li-Sparse-Reciprocal geometric kernel k_geo with the standard (h/b) = 2,
/// (b/r) = 1 crown shape (Lucht, Schaaf & Strahler 2000):
///
///   k_geo = O(theta_s, theta_v, phi) - sec(theta_s) - sec(theta_v)
///           + 0.5 * (1 + cos(xi)) * sec(theta_s) * sec(theta_v)
///
/// (O = the mutual-overlap integral, xi the phase angle). Symmetric in
/// (theta_s, theta_v); 0 exactly at nadir sun + nadir view — both kernels
/// vanish there, which is why f_iso is the nadir BRF in the MODIS literature.
double liSparseReciprocal( double sunZenithDeg, double viewZenithDeg,
                           double relativeAzimuthDeg );

/// Forward anisotropy factor f = 1 + f_vol·k_vol + f_geo·k_geo for one
/// geometry. Returns false (typed message) when the value would be
/// nonphysical (≤ 0) or the geometry is outside the validated domain.
bool anisotropyFactor( double sunZenithDeg, double viewZenithDeg,
                       double relativeAzimuthDeg, double fVol, double fGeo,
                       double *out, QString *errorMessage = nullptr );

/**
 * Kernel-driven normalization of one pixel value from observation geometry
 * to reference geometry (default nadir view, unchanged sun — pass explicit
 * reference angles for a different target).
 *
 * rho_ref = rho_obs · f(G_ref) / f(G_obs)
 *
 * Non-finite @p value passes through as NaN. Unusable geometries / weights
 * refuse (false) without writing @p out — the caller then flags or aborts.
 */
bool normalizeKernelDriven( float value, double sunZenithDeg, double viewZenithDeg,
                            double relativeAzimuthDeg, double fVol, double fGeo,
                            float *out, QString *errorMessage = nullptr,
                            double refSunZenithDeg = -1.0, double refViewZenithDeg = 0.0,
                            double refRelativeAzimuthDeg = 0.0 );

/// Pair-sample accumulator for the empirical c-factor. Collects clear
/// paired pixels; add() refuses non-finite or non-positive pairs (the
/// reflectance domain — a multiplicative leveling must not mix signs).
class PairStatistics
{
  public:
    void add( double date2Value, double date1Value );
    size_t count() const { return m_count; }

    /// Mean-preserving leveling factor c = mean(y)/mean(x) = Σy/Σx over the
    /// accumulated pairs (y = reference date-1, x = target date-2).
    /// @param minPairs  minimum usable pairs (caller policy; the house
    ///                  operator passes 30).
    /// @return false (typed message, *c untouched) when count < max(minPairs, 2),
    ///         Σx ≤ 0 or non-finite, or c ≤ 0.
    bool fitCFactor( size_t minPairs, double *c, QString *errorMessage = nullptr ) const;

  private:
    size_t m_count = 0;
    double m_sx = 0.0, m_sy = 0.0;
};

/// Empirical pair normalization: rho₂' = c·rho₂ with the mean-preserving
/// c from PairStatistics::fitCFactor (NaN passes through).
float applyPairNormalization( float date2Value, double cFactor );

} // namespace BrdfNormalization
