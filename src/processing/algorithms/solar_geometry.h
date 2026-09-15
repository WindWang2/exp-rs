// src/processing/algorithms/solar_geometry.h — sun position and earth-sun
// geometry from acquisition date/time (radiometric-physics-11, work package B).
//
// Fills the one hole in the optical calibration chain: everything downstream
// (TOA reflectance 1/sin(θe), BRDF kernels, topographic correction) consumes
// sun angles, but nothing on the platform computed them — sun elevation was
// only ever read from scene metadata (Landsat MTL SUN_ELEVATION) or refused.
// This module derives the geometry from the acquisition instant itself, so
// scenes without angle metadata can still be calibrated through an explicit,
// documented computation instead of a silent 90° default.
//
// Formulas (Spencer 1971, "Fourier series representation of the position of
// the sun", Search 2(5):172; NOAA Solar Calculator general form):
//
//   Γ = 2π·(n − 1 + (h − 12)/24)/365          fractional year, n = day of year
//   EoT = 229.18·(0.000075 + 0.001868·cosΓ − 0.032077·sinΓ
//                 − 0.014615·cos2Γ − 0.040849·sin2Γ)        minutes
//   δ = 0.006918 − 0.399912·cosΓ + 0.070257·sinΓ − 0.006758·cos2Γ
//       + 0.000907·sin2Γ − 0.002697·cos3Γ + 0.00148·sin3Γ      radians
//   E₀ = 1.000110 + 0.034221·cosΓ + 0.001280·sinΓ
//        + 0.000719·cos2Γ + 0.000077·sin2Γ     inverse-square d⁻² factor
//
//   time_offset = EoT + 4·longitude   (UTC input ⇒ timezone term = 0) minutes
//   tst = 60·h + time_offset          true solar time, minutes
//   ha  = tst/4 − 180                 hour angle, degrees
//   cos z = sin φ·sin δ + cos φ·cos δ·cos ha
//   azimuth (from north, clockwise, [0,360)):
//     atan2( −cosδ·sin ha, sinδ·cosφ − cosδ·sinφ·cos ha ), +360 when negative
//
// Accuracy: Spencer's series carry ≈0.0006 rad (0.035°) declination and
// ≈0.6 s equation-of-time error, so derived positions are good to ~0.05°
// plus the 365-day series approximation (leap years ignored — the standard
// RS convention; sub-0.01° effect). Atmospheric refraction is NOT applied:
// calibration geometry uses the true geometric position, and applying the
// ~0.57° horizon refraction would corrupt the 1/sin(θe) factor.
//
// Series are evaluated in double and angles clamped before asin/acos so a
// degenerate input can never produce NaN silently — it refuses instead.
#pragma once

#include <QString>
#include <QTime>

class QDate;

namespace SolarGeometry
{

/// Sun position and geometry for one acquisition instant. All angles degrees;
/// azimuth measured from geographic north, increasing eastward (clockwise on
/// a map), matching the TopographicCorrection illumination-cosine convention.
struct SunPosition
{
    double zenithDeg = 0.0;          ///< [0, 180); 90 when the sun is on the horizon
    double elevationDeg = 0.0;       ///< 90 − zenith; negative below horizon
    double azimuthDeg = 0.0;         ///< [0, 360) from north, clockwise
    double declinationDeg = 0.0;     ///< solar declination (Spencer series)
    double hourAngleDeg = 0.0;       ///< true solar hour angle (−180, 180]
    double equationOfTimeMin = 0.0;  ///< equation of time, minutes
    double earthSunDistanceAu = 1.0; ///< earth-sun distance in AU (= 1/√E₀)
    bool sunAboveHorizon = false;    ///< elevation > 0
};

/// 1-based day of year of @p date (Jan 1 → 1). Returns 0 for an invalid date.
int dayOfYear( const QDate &date );

/**
 * Spencer (1971) inverse-square earth-sun factor E₀ = d⁻² for @p dayOfYear
 * (1..366) — the divisor in the ESUN form of TOA reflectance:
 * ρ = π·L·d² / (ESUN·cos θz) = π·L / (E₀·ESUN·cos θz).
 * Range ≈ [0.9661, 1.0351] (aphelion/perihelion).
 * @return false when @p dayOfYear is outside 1..366 (typed refusal, *out untouched).
 */
bool earthSunFactor( int dayOfYear, double *inverseSquareFactor,
                     QString *errorMessage = nullptr );

/// Convenience: earth-sun distance in AU = 1/√E₀ (perihelion ≈ 0.9832,
/// aphelion ≈ 1.0171). Same refusal contract as earthSunFactor().
bool earthSunDistanceAu( int dayOfYear, double *distanceAu,
                         QString *errorMessage = nullptr );

/**
 * Solar position for a UTC acquisition instant and geographic location.
 *
 * @param date      acquisition date (must be valid — invalid dates refuse)
 * @param utcTime   acquisition time of day, UTC (must be valid)
 * @param latitudeDeg  [-90, 90]
 * @param longitudeDeg [-180, 180] (east positive)
 * @param out       filled on success only
 * @return false with @p errorMessage set on any invalid input; never emits a
 *         silent default position (fail-closed contract, GOAL Oracle #2).
 */
bool solarPosition( const QDate &date, const QTime &utcTime,
                    double latitudeDeg, double longitudeDeg,
                    SunPosition *out, QString *errorMessage = nullptr );

/**
 * Scene-geometry guard shared by the BRDF / topographic consumers: validates
 * a sun elevation/azimuth pair read from metadata or computed here.
 * Elevation must lie in (0, 90] (a below-horizon sun cannot calibrate
 * anything), azimuth in [0, 360). @return false with a reason naming the
 * offending value; also false for non-finite inputs.
 */
bool validateSunGeometryDeg( double sunElevationDeg, double sunAzimuthDeg,
                             QString *errorMessage = nullptr );

/// Validates a sensor view geometry pair for BRDF use: view zenith [0, 90)
/// (nadir included, straight-up sensor refused), view azimuth [0, 360).
bool validateViewGeometryDeg( double viewZenithDeg, double viewAzimuthDeg,
                              QString *errorMessage = nullptr );

} // namespace SolarGeometry
