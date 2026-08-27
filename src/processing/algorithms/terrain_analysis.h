// terrain_analysis.h — Phase 11.2: Terrain analysis algorithms.
//
// Pure C++ implementations of DEM-based terrain analysis:
//   - Slope (gradient magnitude in degrees)
//   - Aspect (gradient direction in degrees, 0-360)
//   - Hillshade (illumination model)
//   - Roughness (local relief)
//
// All functions operate on float arrays with nodata support.
// No external dependencies beyond <cmath> and <algorithm>.
#pragma once

#include <QVector>

class TerrainAnalysis
{
  public:
    /// Compute slope from DEM using 3x3 window (Horn 1981).
    /// Input: elevation values (row-major), output: slope in degrees [0, 90].
    /// cellSize: pixel size in map units.
    /// nodata: value to treat as missing.
    static bool slope( const float *dem, float *out, int width, int height,
                       float cellSize, float nodata );

    /// Slope with separate x/y pixel sizes (map units per pixel). Needed for
    /// anisotropic pixels and for geographic (degree) DEMs auto-converted to
    /// metres per degree at scene-centre latitude (#612): dz/dx uses
    /// cellSizeX, dz/dy uses cellSizeY.
    static bool slope( const float *dem, float *out, int width, int height,
                       float cellSizeX, float cellSizeY, float nodata );

    /// Compute aspect from DEM using 3x3 window (Horn 1981).
    /// Output: aspect in degrees [0, 360), clockwise from north.
    /// Flat areas (slope == 0) get aspect = -1.
    static bool aspect( const float *dem, float *out, int width, int height,
                        float cellSize, float nodata );

    /// Aspect with separate x/y pixel sizes (see the slope overload).
    static bool aspect( const float *dem, float *out, int width, int height,
                        float cellSizeX, float cellSizeY, float nodata );

    /// Compute hillshade from DEM.
    /// sunAzimuth: sun direction in degrees clockwise from north (default 315).
    /// sunElevation: sun altitude in degrees above horizon (default 45).
    /// Output: hillshade [0, 1] (0 = shadow, 1 = fully lit).
    static bool hillshade( const float *dem, float *out, int width, int height,
                           float cellSize, float nodata,
                           float sunAzimuth = 315.0f, float sunElevation = 45.0f );

    /// Hillshade with separate x/y pixel sizes (see the slope overload).
    /// sunAzimuth/sunElevation are explicit here (no defaults) so the
    /// isotropic overload stays unambiguous for legacy call sites.
    static bool hillshade( const float *dem, float *out, int width, int height,
                           float cellSizeX, float cellSizeY, float nodata,
                           float sunAzimuth, float sunElevation );

    /// Compute roughness: local max-min elevation difference in 3x3 window.
    /// Output: roughness in map units.
    static bool roughness( const float *dem, float *out, int width, int height,
                           float nodata );

    /// Compute TRI (Terrain Ruggedness Index): mean absolute difference
    /// from center cell to 8 neighbors in 3x3 window.
    static bool tri( const float *dem, float *out, int width, int height,
                     float nodata );

    /// Compute TPI (Topographic Position Index): center cell minus mean
    /// of 8 neighbors in 3x3 window.
    static bool tpi( const float *dem, float *out, int width, int height,
                     float nodata );

  private:
    /// Get DEM value at (row, col), returning nodata for out-of-bounds.
    static float getCell( const float *dem, int width, int height,
                          int row, int col, float nodata );
};
