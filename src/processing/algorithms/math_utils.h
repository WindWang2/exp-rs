// math_utils.h — Shared math utilities for processing algorithms
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace MathUtils
{

/**
 * Statistics computed over a float array.
 */
struct Stats {
    size_t count = 0;      // Total number of values (including NaN/nodata)
    size_t validCount = 0; // Number of valid (non-NaN, non-nodata) values
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;
    float stddev = 0.0f;  // Population stddev (N denominator)
};

/**
 * Compute statistics from pre-accumulated sums.
 * Uses population stddev (N denominator).
 *
 * Count semantics (#1687): validCount mirrors acc.count verbatim — the
 * number of samples the CALLER fed into the accumulator. This function
 * cannot re-filter non-finite values (they are no longer distinguishable
 * once summed); feeding paths must pre-filter, unlike computeStats /
 * computeStatsWithNodata which re-scan and exclude per element.
 *
 * Precision contract (#1684): the mean is kept in double through the
 * variance formula (sumSq/N - mean²), which is exact by the Sterbenz
 * lemma whenever mean² is within a factor of two of sumSq/N. Two error
 * floors remain, whichever dominates:
 *   - the formula's own cancellation, ~eps·(mean²/σ²) — dominates only
 *     above condition number mean²/σ² ~ 1/eps (≈4.5e15), i.e. far beyond
 *     reflectance/DN magnitudes;
 *   - the CALLER's accumulation of sum/sumSq: naive accumulation carries
 *     ~sqrt(N)·eps·sumSq, which dominates for realistic large-offset
 *     small-spread populations (mean/σ > ~1e6 at N=1e6).
 * Accumulators feeding this struct over many pixels should sum values
 * shifted by a representative offset or use Kahan compensation
 * (see rs_segment_features.cpp).
 */
struct AccumulatorStats {
    size_t count = 0;
    double sum = 0.0;
    double sumSq = 0.0;
    float min = 0.0f;
    float max = 0.0f;
};

/**
 * Safe division: returns NaN when denominator is zero.
 */
float safeDiv(float numerator, float denominator);

/**
 * Safe division for doubles: returns NaN when the denominator is zero —
 * same zero-denominator contract as safeDiv. Callers that need a neutral
 * 0.0 for a degenerate denominator must guard it explicitly (see
 * rs_accuracy_assessment per-class metrics).
 */
double safeDivDouble(double numerator, double denominator);

/**
 * Compute min, max, mean, stddev over a float array.
 * NaN values are skipped in all computations.
 *
 * @param data   Input array
 * @param count  Number of elements
 * @return Stats structure
 */
Stats computeStats(const float *data, size_t count);

/**
 * Compute min, max, mean, stddev over a float array, treating a specific
 * value as nodata (in addition to NaN).
 *
 * @param data    Input array
 * @param count   Number of elements
 * @param nodata  Value to treat as invalid (in addition to NaN)
 * @return Stats structure
 */
Stats computeStatsWithNodata(const float *data, size_t count, float nodata);

/**
 * Compute statistics from pre-accumulated sums.
 * Uses population stddev (N denominator).
 *
 * @param acc  Accumulator with sum, sumSq, min, max, count
 * @return Stats structure (stddev uses population formula)
 */
Stats computeStatsFromAccumulators(const AccumulatorStats &acc);

/**
 * Undeclared negative-DN sentinel ceiling for the index kernels: no
 * legitimate index-domain input (reflectance [-1,1], DN non-negative) is
 * at or below this value, while the common undeclared fill values are.
 * Not a NoData authority — a band's DECLARED sentinel routes through the
 * sentinel-aware overloads below or the operators' pre-normalization
 * (nodata_utils.h owns that policy).
 */
inline constexpr float kUndeclaredSentinelCeiling = -999.0f;

/**
 * Compute normalized difference: (a - b) / (a + b).
 * Result is NaN when (a + b) == 0 or an operand is invalid.
 *
 * @param a      First input array
 * @param b      Second input array
 * @param out    Output array (same size as inputs)
 * @param count  Number of elements
 * @return true on success
 */
bool normalizedDifference(const float *a, const float *b, float *out, size_t count);

/**
 * Sentinel-aware variant: an operand is additionally invalid when it
 * exactly equals that band's declared NoData value (non-finite declared
 * sentinels disable the check; matching is exact, never an epsilon —
 * nodata_utils.h discipline). Reserved for callers that hold per-band
 * declared sentinels (kernel-level composition); today's production
 * operators achieve the identical invalid mask by pre-normalizing
 * declared sentinels to NaN before calling the 4-argument form
 * (rs_spectral_index_operator's BandSource) — do not add a second
 * normalization layer on top of that path.
 */
bool normalizedDifference(const float *a, const float *b, float *out, size_t count,
                          float noDataA, float noDataB);

/**
 * The exact sample-level predicate normalizedDifference() applies — finite
 * operands and neither at/below the undeclared negative-sentinel ceiling.
 * Equal negative operands are VALID ((a-b)/(a+b) evaluates to 0; slight
 * negative reflectance after atmospheric correction is legitimate input,
 * #1687). Fused kernels (e.g. dNBR) share it so their validity mask
 * cannot drift from the staged form.
 */
bool bothNormalizable(float a, float b);

/**
 * Sentinel-aware predicate: the 2-argument form plus exact rejection of
 * declared per-band NoData values.
 */
bool bothNormalizable(float a, float b, float noDataA, float noDataB);

/**
 * Apply linear scaling: out[i] = gain * in[i] + bias.
 *
 * This is the single kernel for DN-to-radiance / radiometric calibration
 * gain-bias conversion, shared by AtmosphericCorrection::dnToRadiance and
 * RadiometricCalibration::toRadiance.
 *
 * @param in     Input values
 * @param out    Output buffer
 * @param count  Number of elements
 * @param gain   Multiplicative gain
 * @param bias   Additive bias
 * @return true on success; false on null pointers, zero count, or NaN gain/bias
 */
bool linearScale(const float *in, float *out, size_t count, float gain, float bias);

/**
 * WGS84 geodetic arc lengths in metres per degree of latitude/longitude at
 * @p latitudeDeg (Snyder's expansion to the 4th/3rd harmonic). The single
 * owner of the degree→metre conversion every geographic-CRS kernel
 * consumes (terrain cell sizes, change areas, SAR masks); callers pass the
 * scene-centre latitude and multiply their own pixel sizes. Sub-metre
 * accuracy versus full geodesics at pixel scales, deterministic, and
 * identical at every call site by construction.
 */
struct Wgs84ArcMeters
{
    double perDegLat;  ///< metres per degree of latitude at the given latitude
    double perDegLon;  ///< metres per degree of longitude at the given latitude
};
Wgs84ArcMeters wgs84ArcAtLatitudeDeg( double latitudeDeg );

/**
 * Scene-centre latitude in degrees of a north-up geotransform:
 * gt[3] + (height / 2) * gt[5]. All geographic-CRS conversions in the
 * platform evaluate the arc lengths at this single latitude per raster.
 */
double sceneCentreLatitudeDeg( const std::array<double, 6> &geoTransform, int height );

} // namespace MathUtils
