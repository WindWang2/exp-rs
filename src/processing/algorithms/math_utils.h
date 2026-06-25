// math_utils.h — Shared math utilities for processing algorithms
#pragma once

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
 * Statistics computed from pre-accumulated sums.
 * Useful for streaming/segment-based statistics where the full array is not available.
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
 * Safe division for doubles: returns 0.0 when denominator is zero.
 * Note: Unlike safeDiv (which returns NaN for float), this returns 0.0
 * because callers in accuracy assessment and terrain analysis expect 0.0
 * as the fallback value for guarded divisions.
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
 * Compute normalized difference: (a - b) / (a + b).
 * Result is NaN when (a + b) == 0.
 *
 * @param a      First input array
 * @param b      Second input array
 * @param out    Output array (same size as inputs)
 * @param count  Number of elements
 * @return true on success
 */
bool normalizedDifference(const float *a, const float *b, float *out, size_t count);

} // namespace MathUtils
