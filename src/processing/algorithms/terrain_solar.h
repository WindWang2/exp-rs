// terrain_solar.h — solar terrain kernels (track terrain-hydrology-11,
// work package E).
//
// Contracts:
//   * shadowDuration: per-cell fraction of a weighted sun track during which
//     the cell is terrain-shadowed (parallel-ray model, exact per sample:
//     a cell is shadowed for a sample (α, ε) iff some cell beyond it along
//     azimuth α rises above the ray at elevation ε). Azimuths are quantized
//     to 1° sectors so one sweep serves all samples of a sector. Samples
//     with elevation ≤ 0 (night) or weight ≤ 0 are excluded from both
//     numerator and denominator. Curvature is NOT applied to shadows (v1,
//     documented limitation; viewshed carries the pairwise-exact model).
//   * sunPositionDeg: low-precision solar position (Cooper 1981 declination
//     ± the standard hour-angle formula) from local SOLAR time — caller
//     supplies solar (not civil) time, sidestepping equation-of-time and
//     timezone errors. Declared accuracy ≈ ±0.5° declination, ±1° position;
//     it generates sun tracks, it is not an ephemeris.
//
// hillshadeSeries composes the existing TerrainAnalysis::hillshade kernel
// over a sun track (operator side); no second hillshade implementation.
#pragma once

#include <functional>
#include <vector>

namespace TerrainSolar
{

struct SunSample
{
    double azimuthDeg = 0.0;   ///< degrees clockwise from north
    double elevationDeg = 0.0; ///< degrees above horizon; > 0 = daylight
    double weight = 1.0;       ///< relative duration weight (e.g. time step)
};

struct ShadowDurationResult
{
    std::vector<float> shadowFraction; ///< [0,1] per cell; nodata for NoData cells
    double weightSum = 0.0;            ///< total accepted weight
    std::size_t sampleCount = 0;       ///< accepted (daylight, weighted) samples
    std::size_t sectorsUsed = 0;       ///< distinct 1° azimuth sectors swept
};

bool shadowDuration( const float *dem, int width, int height, float nodata,
                     double cellSizeX, double cellSizeY,
                     const std::vector<SunSample> &track,
                     ShadowDurationResult *out,
                     const std::function<bool()> &cancelled = {} );

/// Solar position for @p dayOfYear (1–365) at @p solarHour (0–24 local
/// SOLAR time) and @p latitudeDeg. Returns false for out-of-range inputs.
bool sunPositionDeg( int dayOfYear, double solarHour, double latitudeDeg,
                     double *azimuthDeg, double *elevationDeg );

} // namespace TerrainSolar
