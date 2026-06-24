// src/processing/algorithms/change_detection.cpp — Change detection algorithms
#include "change_detection.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <cmath>
#include <algorithm>
#include <limits>

namespace ChangeDetection
{

bool difference(const float *before, const float *after, float *out, size_t count)
{
    if (!before || !after || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "difference: null pointer argument");
        return false;
    }
    if (count == 0) return false;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Change detection (difference): %1 pixels" ).arg( count ) );

    for (size_t i = 0; i < count; ++i)
        out[i] = std::abs(after[i] - before[i]);

    return true;
}

bool normalizedDifference(const float *before, const float *after, float *out, size_t count)
{
    if (!before || !after || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "normalizedDifference: null pointer argument");
        return false;
    }
    if (count == 0) return false;

    return MathUtils::normalizedDifference(after, before, out, count);
}

bool changeMask(const float *diff, uint8_t *mask, size_t count, float threshold)
{
    if (!diff || !mask) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "changeMask: null pointer argument");
        return false;
    }
    if (count == 0) return false;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Change mask: %1 pixels, threshold=%2" ).arg( count ).arg( threshold ) );

    for (size_t i = 0; i < count; ++i) {
        if (std::isnan(diff[i]))
            mask[i] = 255; // No data
        else
            mask[i] = (diff[i] >= threshold) ? 1 : 0;
    }

    return true;
}

ChangeStats statistics(const float *diff, size_t count)
{
    ChangeStats stats;
    if (!diff) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "statistics: null pointer argument");
        return stats;
    }
    if (count == 0) return stats;

    // Use shared MathUtils::computeStats
    MathUtils::Stats mathStats = MathUtils::computeStats(diff, count);

    stats.count = mathStats.count;
    stats.min = mathStats.min;
    stats.max = mathStats.max;
    stats.mean = mathStats.mean;
    stats.stddev = mathStats.stddev;

    return stats;
}

}
