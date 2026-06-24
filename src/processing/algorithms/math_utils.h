// math_utils.h — Shared math utilities for processing algorithms
#pragma once

#include <cstddef>
#include <cstdint>

namespace MathUtils
{

/**
 * Statistics computed over a float array.
 */
struct Stats {
    size_t count = 0;      // Total number of values (including NaN)
    size_t validCount = 0; // Number of non-NaN values
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;
    float stddev = 0.0f;
};

/**
 * Safe division: returns NaN when denominator is zero.
 */
float safeDiv(float numerator, float denominator);

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
