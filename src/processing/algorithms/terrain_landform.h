// terrain_landform.h — landform / multiscale terrain position kernels
// (track terrain-hydrology-11, work package F).
//
// Contracts:
//   * tpiMultiscale: topographic position index over square (2r+1)² windows
//     for a set of radii in cells: TPI = z_center − mean(window, centre
//     excluded), standardised TPI = TPI / SD(window). Square windows are a
//     documented v1 choice (circular kernels = follow-up); they make every
//     scale an O(N) integral-image pass with closed-form behaviour on
//     analytic surfaces.
//   * landformClasses: Weiss (2001) 6-class terrain-position classification
//     from standardised TPI at an inner and an outer radius plus the Horn
//     slope (reuses TerrainAnalysis::slope — no second slope implementation):
//       plains < flatSlopeDeg ≤ …
//       stdTPI_outer > 1σ   → peak (inner > 1σ) / upper slope
//       stdTPI_outer ≥ −1σ  → upper (inner > 1σ) / middle / lower slope
//       stdTPI_outer < −1σ  → valley (inner < −1σ) / lower slope
//     Codes: 0 plains, 1 valley, 2 lower slope, 3 middle slope,
//     4 upper slope, 5 peak.
//   * geomorphon: Jasiewicz & Stepinski (2013) ternary pattern: for each of
//     the 8 compass directions the nearest-cell line-of-sight scan (skipping
//     flatRadiusCells, up to searchRadiusCells) yields zenith/nadir angles;
//     the direction codes +1 (terrain significantly higher), −1
//     (significantly lower) or 0 (flat within flatThreshDeg). Pattern packed
//     base-3 over the fixed direction order N, NE, E, SE, S, SW, W, NW.
//     Form classes (v1, unambiguous subset — full 10-class J&S table is a
//     documented follow-up):
//       0 flat (all directions 0)              3 ridge (− arcs only, no +)
//       1 peak (all −1: surroundings lower)    4 valley (+ arcs only, no −)
//       2 pit  (all +1: surroundings higher)   5 slope (exactly one + and
//                                                        one − arc)
//       6 other (everything else: shoulder/spur/hollow/footslope patterns)
//     Sign convention: +1 means terrain HIGHER than the centre along that
//     direction, so a pit cell is all +1 and a peak cell all −1.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace TerrainLandform
{

struct MultiscaleTPI
{
    int radiusCells = 0;         ///< this scale's window radius in cells
    std::vector<float> tpi;      ///< z − window mean (centre excluded)
    std::vector<float> stdTpi;   ///< TPI / window SD (0 where SD == 0)
};

/// @p radiiCells must be positive and sorted (not enforced, but larger
/// radii cost a full pass each). NoData cells carry @p nodata in both
/// outputs; windows ignore NoData cells and a window with < 1 valid
/// neighbour yields TPI 0.
bool tpiMultiscale( const float *dem, int width, int height, float nodata,
                    const std::vector<int> &radiiCells,
                    std::vector<MultiscaleTPI> *out,
                    const std::function<bool()> &cancelled = {} );

struct LandformClassParams
{
    int innerRadiusCells = 3;
    int outerRadiusCells = 15;
    double flatSlopeDeg = 5.0; ///< slope below this → plains (code 0)
};

bool landformClasses( const float *dem, int width, int height, float nodata,
                      double cellSizeX, double cellSizeY,
                      const LandformClassParams &params,
                      std::vector<std::uint8_t> *classes,
                      const std::function<bool()> &cancelled = {} );

struct GeomorphonParams
{
    int searchRadiusCells = 20;  ///< line-of-sight reach per direction
    int flatRadiusCells = 2;     ///< cells skipped before angle comparison
    double flatThreshDeg = 2.0;  ///< |angle| below this counts as flat
};

struct GeomorphonResult
{
    std::vector<std::uint16_t> pattern; ///< packed base-3 ternary code (0..6560)
    std::vector<std::uint8_t> form;     ///< class codes 0–6 (see header)
};

bool geomorphon( const float *dem, int width, int height, float nodata,
                 double cellSizeX, double cellSizeY, const GeomorphonParams &params,
                 GeomorphonResult *out,
                 const std::function<bool()> &cancelled = {} );

} // namespace TerrainLandform
