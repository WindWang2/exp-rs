// sar_geocoding.h — forward Range-Doppler geocoding onto a DEM map grid
// (Scientific Processing 8.0, capability package A).
//
// Companion to sar_orbit.h (orbit contract, Hermite interpolation,
// zero-Doppler geolocation, forward range-Doppler) and
// sar_terrain_geometry.h (constant-geometry subset). 7.0 shipped the
// BACKWARD product (per-SAR-pixel geolocation under `local_incidence_orbit`)
// and explicitly refused forward output-grid geocoding; this module
// implements the forward chain under the same declared-contract rules:
// nothing is approximated when the metadata cannot support it.
//
// Chain per output ground cell (lon/lat/height from the DEM grid):
//   forward range-Doppler (ground → zero-Doppler azimuth time + slant range)
//   → source SAR position (row = (t − AZIMUTH_START_UTC)·PRF,
//     col = (ρ − RANGE_WINDOW_START·c/2) / (c / (2·RANGE_RATE)))
//   → real line of sight Psat(t) − P (orbit-interpolated, ECEF)
//   → ellipsoid reference incidence θ0, terrain-facet local incidence θL
//     (Horn gradients dz/dE, dz/dN rotated into the ENU frame)
//   → layover/shadow classes from the REAL per-pixel look elevation
//     θe = asin(ŝ·û), evaluated on the slope α along the BEAM-TRAVEL
//     horizontal direction (away from the sensor — the direction slant
//     range folds in): LAYOVER when α > 90° − θe; SHADOW when α < −θe —
//     the exact constant-geometry conditions of sar_terrain_geometry.h
//     with the real per-pixel θe in place of 90° − θi.
//   → radiometric-terrain correction factor sin θL / sin θ0 (Ulander 1996;
//     Small 2011 eq. 5): γ0 = σ0 · factor; 1 on flat ground, < 1 on the
//     beam-facing flank the flat-earth image over-brightens (θL < θ0),
//     > 1 on back flanks, NaN when sin θL ≤ 0 (facet at/past grazing).
//
// All math is double precision and deterministic; the solvers inside
// forwardRangeDoppler/geolocateZeroDoppler are bounded (sign-scan +
// bracketed bisection / ≤60-iteration Newton), so per-pixel cost is bounded
// regardless of input.
#pragma once

#include "sar_orbit.h"
#include "sar_terrain_geometry.h" // TerrainMaskClass (shared class vocabulary)

#include <QString>
#include <functional>
#include <limits>

namespace sicnu::sar
{

inline double geocodeNaN() { return std::numeric_limits<double>::quiet_NaN(); }

/// A parsed, validated SAR scene timing contract — the same declared keys
/// and validation rules the backward orbit product enforces (the backward
/// path in rs_sar_terrain_masks_operator.cpp revalidates independently and
/// is pinned to the same tolerances by its own tests).
struct SarSceneContract
{
    OrbitSegment orbit;
    double azimuthStartSeconds = 0.0; ///< azimuth time of row 0 (scene time base)
    double prfHz = 0.0;               ///< rows per second
    double rangeStartM = 0.0;         ///< slant range of column 0 (one-way path length, m)
    double rangeSampleSpacingM = 0.0; ///< slant-range metres per column (c / (2·rate))
    int imageWidth = 0;
    int imageHeight = 0;

    double azimuthTimeOfRow( double rowF ) const { return azimuthStartSeconds + rowF / prfHz; }
    double slantRangeOfCol( double colF ) const { return rangeStartM + colF * rangeSampleSpacingM; }
    double rowOfAzimuthTime( double t ) const { return ( t - azimuthStartSeconds ) * prfHz; }
    double colOfSlantRange( double rho ) const { return ( rho - rangeStartM ) / rangeSampleSpacingM; }
};

/// Reads and validates the declared orbit contract through @a metaItem (a
/// dataset/band metadata accessor returning "" when a key is absent —
/// SICNU_SAR_ORBIT_STATES, SICNU_SAR_AZIMUTH_START_UTC, SICNU_SAR_PRF,
/// SICNU_SAR_RANGE_WINDOW, SICNU_SAR_RANGE_RATE). The window must cover the
/// raster columns within the same 2-sample tolerance the backward product
/// enforces. Returns false with a human-readable @a error for every
/// malformed/contradictory declaration — callers refuse, never approximate.
bool parseSarSceneContract( const std::function<QString( const char * )> &metaItem,
                            int imageWidth, int imageHeight,
                            SarSceneContract *out, QString *error = nullptr );

/// Per-ground-cell geocoding geometry. Every angle is from the REAL orbit
/// line of sight at the cell's own azimuth time; NaN marks a quantity the
/// metadata+DEM cannot support at that cell (never a fabricated value).
struct GeocodeGeometry
{
    bool resolved = false;   ///< forward range-Doppler found a zero-Doppler crossing
    double rowF = 0.0;       ///< source SAR row (floating; outside [0, imageHeight) → out of image)
    double colF = 0.0;       ///< source SAR column (same rule against imageWidth)
    double incidenceDeg = geocodeNaN();     ///< ellipsoid reference incidence θ0
    double localIncidenceDeg = geocodeNaN(); ///< terrain-facet incidence θL (NaN on degenerate gradients)
    double rtcFactor = geocodeNaN();        ///< sin θL / sin θ0 (NaN when unphysical)
    double lookElevationDeg = geocodeNaN(); ///< REAL LOS elevation above the local horizontal
    TerrainMaskClass maskClass = TerrainMaskClass::Normal;
};

/// Forward-geocodes one ground cell. @a latDeg/@a lonDeg is the cell center
/// (geodetic WGS84), @a heightM the DEM height above the ellipsoid,
/// @a dzdE/@a dzdN the terrain gradients in metres per metre (east/north);
/// NaN gradients mark a degenerate facet neighborhood (geometry products
/// that need the facet normal come out NaN, the class stays Normal and the
/// caller masks). Returns false only when @a out is null — per-cell
/// unresolvability is reported through GeocodeGeometry::resolved so callers
/// can count NoData causes.
bool geocodeGroundCell( const SarSceneContract &contract,
                        double latDeg, double lonDeg, double heightM,
                        double dzdE, double dzdN,
                        GeocodeGeometry *out );

/// Bilinear sample of @a buf (w×h row-major) at (xF, yF). False when any of
/// the four taps is non-finite or the position falls outside [0, w−1]×[0, h−1]
/// (the caller's window padding decides out-of-image semantics, not this
/// function).
bool bilinearSample( const float *buf, int w, int h, double xF, double yF, float *out );

/// Nearest sample of @a buf (w×h row-major) at (xF, yF) (round-half-up).
/// Same tap-validity rule as bilinearSample.
bool nearestSample( const float *buf, int w, int h, double xF, double yF, float *out );

} // namespace sicnu::sar
