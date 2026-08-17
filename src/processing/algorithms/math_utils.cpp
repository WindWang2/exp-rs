// math_utils.cpp — Shared math utilities for processing algorithms
#include "math_utils.h"

#include <cmath>
#include <algorithm>

namespace MathUtils
{

static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

float safeDiv(float numerator, float denominator)
{
    return (denominator == 0.0f) ? NaN : (numerator / denominator);
}

double safeDivDouble(double numerator, double denominator)
{
    return (denominator == 0.0) ? 0.0 : (numerator / denominator);
}

Stats computeStats(const float *data, size_t count)
{
    Stats stats;
    if (!data || count == 0) return stats;

    stats.count = count;

    // Find first non-NaN value for initialization
    bool foundValid = false;
    size_t firstValid = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!std::isnan(data[i])) {
            firstValid = i;
            foundValid = true;
            break;
        }
    }

    if (!foundValid) {
        stats.min = 0.0f;
        stats.max = 0.0f;
        stats.mean = 0.0f;
        stats.stddev = 0.0f;
        return stats;
    }

    stats.min = data[firstValid];
    stats.max = data[firstValid];

    // First pass: compute min, max, sum
    double sum = 0.0;
    size_t validCount = 0;
    for (size_t i = 0; i < count; ++i) {
        if (std::isnan(data[i])) continue;
        validCount++;
        sum += data[i];
        if (data[i] < stats.min) stats.min = data[i];
        if (data[i] > stats.max) stats.max = data[i];
    }

    stats.validCount = validCount;
    stats.mean = (validCount > 0) ? static_cast<float>(sum / validCount) : 0.0f;

    // Second pass: compute population stddev
    if (validCount > 1) {
        double sumSq = 0.0;
        for (size_t i = 0; i < count; ++i) {
            if (std::isnan(data[i])) continue;
            double diff = data[i] - stats.mean;
            sumSq += diff * diff;
        }
        stats.stddev = static_cast<float>(std::sqrt(sumSq / validCount));
    } else {
        stats.stddev = 0.0f;
    }

    return stats;
}

Stats computeStatsWithNodata(const float *data, size_t count, float nodata)
{
    Stats stats;
    if (!data || count == 0) return stats;

    stats.count = count;

    // Find first valid value for initialization
    bool foundValid = false;
    size_t firstValid = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!std::isnan(data[i]) && data[i] != nodata) {
            firstValid = i;
            foundValid = true;
            break;
        }
    }

    if (!foundValid) {
        stats.min = 0.0f;
        stats.max = 0.0f;
        stats.mean = 0.0f;
        stats.stddev = 0.0f;
        return stats;
    }

    stats.min = data[firstValid];
    stats.max = data[firstValid];

    // First pass: compute min, max, sum
    double sum = 0.0;
    size_t validCount = 0;
    for (size_t i = 0; i < count; ++i) {
        if (std::isnan(data[i]) || data[i] == nodata) continue;
        validCount++;
        sum += data[i];
        if (data[i] < stats.min) stats.min = data[i];
        if (data[i] > stats.max) stats.max = data[i];
    }

    stats.validCount = validCount;
    stats.mean = (validCount > 0) ? static_cast<float>(sum / validCount) : 0.0f;

    // Second pass: compute population stddev
    if (validCount > 1) {
        double sumSq = 0.0;
        for (size_t i = 0; i < count; ++i) {
            if (std::isnan(data[i]) || data[i] == nodata) continue;
            double diff = data[i] - stats.mean;
            sumSq += diff * diff;
        }
        stats.stddev = static_cast<float>(std::sqrt(sumSq / validCount));
    } else {
        stats.stddev = 0.0f;
    }

    return stats;
}

Stats computeStatsFromAccumulators(const AccumulatorStats &acc)
{
    Stats stats;
    stats.count = acc.count;
    stats.validCount = acc.count;
    stats.min = acc.min;
    stats.max = acc.max;

    if (acc.count == 0) {
        stats.mean = 0.0f;
        stats.stddev = 0.0f;
        return stats;
    }

    stats.mean = static_cast<float>(acc.sum / acc.count);

    // Population stddev (N denominator)
    if (acc.count > 1) {
        double variance = acc.sumSq / acc.count - stats.mean * stats.mean;
        if (!std::isfinite(variance) || variance < 0.0)
            variance = 0.0;
        stats.stddev = static_cast<float>(std::sqrt(variance));
    } else {
        stats.stddev = 0.0f;
    }

    return stats;
}

bool normalizedDifference(const float *a, const float *b, float *out, size_t count)
{
    if (!a || !b || !out) return false;
    if (count == 0) return false;

    for (size_t i = 0; i < count; ++i) {
        out[i] = safeDiv(a[i] - b[i], a[i] + b[i]);
    }

    return true;
}

bool linearScale(const float *in, float *out, size_t count, float gain, float bias)
{
    if (!in || !out || count == 0) return false;
    if (!std::isfinite(gain) || !std::isfinite(bias)) return false;
    for (size_t i = 0; i < count; ++i)
        out[i] = gain * in[i] + bias;
    return true;
}

} // namespace MathUtils
