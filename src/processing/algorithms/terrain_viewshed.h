// terrain_viewshed.h — viewshed and horizon kernels (track
// terrain-hydrology-11, work package D).
//
// Contracts:
//   * viewshedR3: ring-sweep line-of-sight viewshed from a single observer.
//     Cells are visited in order of increasing map distance from the
//     observer; a cell is visible iff the elevation angle of its surface
//     (+ targetHeight) exceeds the maximum terrain angle along every path
//     to it. The path maximum is merged from the cell's already-visited
//     ring predecessors (the standard permissive R3-family merge; slightly
//     optimistic on convex ridgelines, deterministic, O(N)).
//   * cumulativeViewshed: per-observer viewshedR3 runs accumulated into a
//     visibility count per cell (multi-observer analysis).
//   * horizonProfile: for one observer, the maximum terrain elevation angle
//     per azimuth sector (DDA march along each azimuth, nearest-cell
//     sampling) — the horizon line used by the solar package and the
//     terrain agent tool.
//
// Conventions (terrain family):
//   * Cell coordinates: col ∈ [0,width), row ∈ [0,height); +row = south.
//   * Azimuth: degrees clockwise from north.
//   * Angles: degrees above the observer's local horizon plane.
//   * Curvature/refraction: @p curvatureFactor = (1−k)/(2·R_earth) with the
//     standard refraction k = 0.13 (factor ≈ 6.83e-8 1/m); every elevation
//     is lowered by factor·d² at map distance d before the line-of-sight
//     math (effective-earth-radius model). 0 disables. Meaningful only for
//     metric projected DEMs — callers must refuse geographic CRS inputs.
//   * NoData cells are opaque barriers: rays never pass them; terrain
//     behind a NoData region is invisible and blocks nothing beyond (the
//     NoData edge is the horizon there).
//   * Determinism: fixed neighbour order, strict comparisons; identical
//     inputs give byte-identical outputs.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace TerrainVisibility
{

struct ViewshedParams
{
    double obsCol = 0.0;        ///< observer cell (rounded to nearest cell)
    double obsRow = 0.0;
    double observerHeight = 0.0; ///< m above the DEM surface
    double targetHeight = 0.0;   ///< m above the DEM surface for targets
    double radius = 0.0;         ///< analysis radius in map units; <= 0 → full frame
    double curvatureFactor = 0.0; ///< (1−k)/(2R) per metre; 0 = flat Earth
};

/// Single-observer viewshed. @a visible holds 1 (visible), 0 (not), and
/// @p nodataByte for DEM-NoData cells. Returns false on null/empty inputs,
/// an observer outside the grid or on a NoData cell, or when cancelled.
bool viewshedR3( const float *dem, int width, int height, float nodata,
                 double cellSizeX, double cellSizeY, const ViewshedParams &params,
                 std::vector<std::uint8_t> *visible, std::uint8_t nodataByte = 0,
                 const std::function<bool()> &cancelled = {} );

/// Cumulative visibility counts over @p observers (each run identical to
/// viewshedR3). Cells that are NoData carry @p nodataWord. Returns false on
/// the same conditions as viewshedR3 (checked once per observer).
bool cumulativeViewshed( const float *dem, int width, int height, float nodata,
                         double cellSizeX, double cellSizeY,
                         const std::vector<ViewshedParams> &observers,
                         std::vector<std::uint16_t> *counts,
                         std::uint16_t nodataWord = 0xFFFF,
                         const std::function<bool()> &cancelled = {} );

struct HorizonProfile
{
    double azimuthStepDeg = 1.0;               ///< sector width used
    std::vector<double> azimuths;              ///< sector centres, degrees CW from north
    std::vector<double> angles;                ///< max terrain angle per sector, degrees (−90 = open)
};

/// Horizon line of one observer: for each azimuth sector, the maximum
/// terrain elevation angle found by marching away from the observer up to
/// @p maxDistance map units (<= 0 → full frame; curvatureFactor applies).
bool horizonProfile( const float *dem, int width, int height, float nodata,
                     double cellSizeX, double cellSizeY, double obsCol, double obsRow,
                     double observerHeight, double maxDistance, double curvatureFactor,
                     double azimuthStepDeg, HorizonProfile *out,
                     const std::function<bool()> &cancelled = {} );

} // namespace TerrainVisibility
