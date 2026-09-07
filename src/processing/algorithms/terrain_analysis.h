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

    /// Compute TRI (Terrain Ruggedness Index) after Riley et al. (1999):
    /// sqrt of the sum of squared differences from the center cell to the
    /// 8 neighbors in a 3x3 window (comparable with GDAL/ArcGIS/GRASS).
    static bool tri( const float *dem, float *out, int width, int height,
                     float nodata );

    /// Compute TPI (Topographic Position Index): center cell minus mean
    /// of 8 neighbors in 3x3 window.
    static bool tpi( const float *dem, float *out, int width, int height,
                     float nodata );

    // --- Foundation 5.0 (Milestone F) products ------------------------------
    // Curvature conventions (convexity-positive): a bowl z = (x²+y²)/2
    // yields profile = plan = total = +1. Formulas are the
    // Zevenbergen-Thorne (1987) surface-fit second derivatives over the
    // 3×3 stencil (a b c / d e f / g h i, row-major):
    //   zxx = (d+f−2e)/csx², zyy = (b+h−2e)/csy², zxy = (g+i−a−c)/(4 csx csy)
    //   zx = (f−d)/(2 csx), zy = (h−b)/(2 csy)
    //   profile = (zx²·zxx + 2 zx·zy·zxy + zy²·zyy)/(zx²+zy²)
    //   plan    = (zy²·zxx − 2 zx·zy·zxy + zx²·zyy)/(zx²+zy²)
    //   total   = (zxx + zyy)/2
    // Flat cells (zx = zy = 0) give profile = plan = 0 (no slope line).

    /// Profile curvature along the slope line (convexity-positive).
    static bool curvatureProfile( const float *dem, float *out, int width, int height,
                                  float cellSizeX, float cellSizeY, float nodata );

    /// Plan (across-slope) curvature, same sign convention as profile.
    static bool curvaturePlan( const float *dem, float *out, int width, int height,
                               float cellSizeX, float cellSizeY, float nodata );

    /// Mean principal second derivative ((zxx + zyy)/2).
    static bool curvatureTotal( const float *dem, float *out, int width, int height,
                                float cellSizeX, float cellSizeY, float nodata );

    /// Multidirectional hillshade: mean of the eight 45°-step azimuth
    /// hillshades at @a sunElevation. Flat cells give cos(zenith).
    static bool hillshadeMultidirectional( const float *dem, float *out, int width, int height,
                                           float cellSizeX, float cellSizeY, float nodata,
                                           float sunElevation );

    /// Local relief: max − min within the 3×3 neighbourhood (centre included).
    static bool localRelief( const float *dem, float *out, int width, int height,
                             float nodata );

  private:
    /// Get DEM value at (row, col), returning nodata for out-of-bounds.
    static float getCell( const float *dem, int width, int height,
                          int row, int col, float nodata );
};
