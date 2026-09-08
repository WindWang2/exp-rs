// topographic_correction.h — illumination-angle topographic correction of
// optical bands over a co-registered DEM (Scientific Algorithm Foundation
// 5.0, Milestone B).
//
// Position in the optical chain: applied to TOA/surface reflectance AFTER
// atmospheric correction, using a same-grid DEM. The correction removes the
// illumination-driven radiance variation on slopes so like pixels have like
// values across aspects.
//
// Geometry contract:
//   * Slope/aspect come from the Horn (1981) 3×3 gradients of the DEM,
//     matching TerrainAnalysis::slope/aspect conventions exactly (same
//     dzdx/dzdy kernels, aspect = atan2(−dzdx, dzdy) in degrees [0,360),
//     flat = −1) so both families read identically on the same DEM.
//   * Illumination cosine (Riano et al. 2003 form):
//         cos_i = cos(θz)·cos(s) + sin(θz)·sin(s)·cos(φs − φa)
//     with θz the solar zenith (from vertical), φs the solar azimuth and
//     φa the slope aspect, all in degrees. Flat cells (aspect −1) have
//     cos_i = cos(θz).
//   * Cell sizes are horizontal METRES. Geographic (degree) DEMs must be
//     converted to metres-per-degree at scene centre by the caller — the
//     rs:topographic_correction operator applies the same WGS84 arc-length
//     conversion as the terrain operator (#612).
//
// Methods (Teillet et al. 1982; Smith et al. 1980; Riano et al. 2003):
//   * Cosine      — Lambertian:      L·cosθz / cos_i. Self-shadowed pixels
//                   (cos_i <= 0) are NaN.
//   * CCorrection — empirical C:     L·(cosθz + c)/(cos_i + c), c = a/b from
//                   the OLS fit L = a + b·cos_i over the scene. The SCS+C
//                   variant (Soenen et al. 2005) coincides with this form
//                   when c is estimated from the same regression, so this
//                   one kernel serves both (declared in the operator docs).
//   * Minnaert    — L·(cosθz/cosi)^k, k from the log-log OLS
//                   ln L = a − k·ln cos_i over (cos_i > 0, L > 0) pixels.
//                   Pixels outside that domain are NaN.
//
// All fits accumulate in double over the valid pairs (finite L, finite
// cos_i); NaN/sentinel exclusion is the caller's job (operators normalize
// through the shared masked-read convention first). A fit with < 2 usable
// pairs or a near-zero slope is "not usable" and the caller must refuse the
// band rather than silently passing data through.
#pragma once

#include <cstddef>

namespace TopographicCorrection
{

enum class Method
{
    Cosine,
    CCorrection,
    Minnaert,
};

/// Stable names for the operator schema / metadata round-trip.
const char *methodName( Method m );
/// @return false (and leaves @a out untouched) on an unknown token.
bool parseMethod( const char *token, Method *out );

/**
 * Horn (1981) central gradients from the 3×3 DEM neighbourhood
 * @a k = {a,b,c,d,e,f,g,h,i} in row-major order (e = centre). The caller
 * supplies a replicate-filled or validity-resolved neighbourhood (the
 * streaming tile kernel reads through a 1-pixel halo; interior pixels match
 * TerrainAnalysis exactly, raster-edge pixels use the replicated border —
 * the platform's documented streaming edge policy).
 */
void hornGradient( const float *k9, double cellSizeX, double cellSizeY,
                   double *dzdx, double *dzdy );

/// Slope in degrees [0, 90] and platform-convention aspect in degrees
/// [0, 360) (−1 when the cell is flat: |dzdx|, |dzdy| < 1e-10 — the same
/// epsilon TerrainAnalysis uses).
void slopeAspectDeg( double dzdx, double dzdy, double *slopeDeg, double *aspectDeg );

/// Illumination cosine from platform slope/aspect (degrees).
double illuminationCosine( double slopeDeg, double aspectDeg,
                           double solarZenithDeg, double solarAzimuthDeg );

/// OLS y = a + b·x accumulated in double (numerically stable sum forms).
/// add() refuses non-finite pairs — callers may rely on count() == pairs.
class OlsRegression
{
  public:
    void add( double x, double y );
    size_t count() const { return m_count; }
    /// @return false when count < 2 (or degenerate x spread).
    bool fit( double *a, double *b ) const;

  private:
    size_t m_count = 0;
    double m_sx = 0.0, m_sy = 0.0, m_sxx = 0.0, m_sxy = 0.0;
};

/// log-log regression ln(y) = a − k·ln(x) for the Minnaert exponent
/// (valid domain x > 0, y > 0 enforced here — add() refuses other pairs).
class MinnaertRegression
{
  public:
    void add( double cosIllumination, double value );
    size_t count() const { return m_count; }
    /// @return k > 0 when count >= 2 and the fit is usable, else false.
    bool fit( double *k ) const;

  private:
    size_t m_count = 0;
    double m_sx = 0.0, m_sy = 0.0, m_sxx = 0.0, m_sxy = 0.0;
};

/// Fitted correction parameters for one band.
struct BandFit
{
    bool usable = false;
    double a = 0.0;    ///< OLS intercept (CCorrection)
    double b = 0.0;    ///< OLS slope (CCorrection)
    double c = 0.0;    ///< C factor a/b (CCorrection)
    double k = 1.0;    ///< Minnaert exponent (Minnaert)
    double cosZenith = 1.0; ///< cos(θz)
};

/// Fits @a method for one band. For CCorrection, @a c = a/b requires
/// |b| >= 1e-6 (a flat regression carries no illumination signal — the
/// caller refuses with a typed error instead of passing data through).
BandFit fitBand( Method method, double solarZenithDeg,
                 const OlsRegression &ols, const MinnaertRegression &minnaert );

/// Corrects one pixel value. NaN (or any non-finite) @a value passes through
/// as NaN. Method domain violations (self-shadowed cosine, cos_i + c <= 0,
/// Minnaert domain) yield NaN — the operator writes NaN-NoData for them.
float correctPixel( Method method, float value, double cosIllumination,
                    const BandFit &fit );

} // namespace TopographicCorrection
