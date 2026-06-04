// src/processing/algorithms/change_detection.cpp — Change detection algorithms
#include "change_detection.h"

#include <cmath>
#include <algorithm>
#include <limits>

namespace ChangeDetection
{

bool difference(const float *before, const float *after, float *out, size_t count)
{
    if (!before || !after || !out || count == 0)
        return false;

    for (size_t i = 0; i < count; ++i)
        out[i] = std::abs(after[i] - before[i]);

    return true;
}

bool normalizedDifference(const float *before, const float *after, float *out, size_t count)
{
    if (!before || !after || !out || count == 0)
        return false;

    for (size_t i = 0; i < count; ++i) {
        float sum = after[i] + before[i];
        if (sum == 0.0f)
            out[i] = std::numeric_limits<float>::quiet_NaN();
        else
            out[i] = (after[i] - before[i]) / sum;
    }

    return true;
}

bool changeMask(const float *diff, uint8_t *mask, size_t count, float threshold)
{
    if (!diff || !mask || count == 0)
        return false;

    for (size_t i = 0; i < count; ++i)
        mask[i] = (diff[i] >= threshold) ? 1 : 0;

    return true;
}

ChangeStats statistics(const float *diff, size_t count)
{
    ChangeStats stats;
    if (!diff || count == 0)
        return stats;

    stats.count = count;
    stats.min = diff[0];
    stats.max = diff[0];

    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += diff[i];
        stats.min = std::min(stats.min, diff[i]);
        stats.max = std::max(stats.max, diff[i]);
    }
    stats.mean = static_cast<float>(sum / count);

    double sqSum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double d = diff[i] - stats.mean;
        sqSum += d * d;
    }
    stats.stddev = static_cast<float>(std::sqrt(sqSum / count));

    return stats;
}

}
