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
    return (denominator == 0.0)
        ? std::numeric_limits<double>::quiet_NaN()
        : (numerator / denominator);
}

Stats computeStats(const float *data, size_t count)
{
    Stats stats;
    if (!data || count == 0) return stats;

    stats.count = count;

    // Find first finite value for initialization (excludes inf/NaN)
    bool foundValid = false;
    size_t firstValid = 0;
    for (size_t i = 0; i < count; ++i) {
        if (std::isfinite(data[i])) {
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
        if (!std::isfinite(data[i])) continue;
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
            if (!std::isfinite(data[i])) continue;
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

    // Find first valid value for initialization (finite and not nodata)
    bool foundValid = false;
    size_t firstValid = 0;
    for (size_t i = 0; i < count; ++i) {
        if (std::isfinite(data[i]) && data[i] != nodata) {
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
        if (!std::isfinite(data[i]) || data[i] == nodata) continue;
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
            if (!std::isfinite(data[i]) || data[i] == nodata) continue;
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

    // The mean must stay double through the variance formula (#1684):
    // truncating it to float first let the rounding error of mean² dominate
    // sumSq/N for large-offset small-spread populations (10000.3 ± 0.5
    // reported σ +17%; 1e6 ± 0.25 reported 64 instead of 0.25). stats.mean
    // is still published as float, but the cancellation happens in double.
    // Precision floor of this two-sum form: ~2·ε·(mean²/σ²) relative, i.e.
    // ~0.7% for mean=1e6 σ=0.25; that is a property of the AccumulatorStats
    // contract (sum/sumSq only), not of this function.
    const double mean = acc.sum / static_cast<double>(acc.count);
    stats.mean = static_cast<float>(mean);

    // Population stddev (N denominator)
    if (acc.count > 1) {
        double variance = acc.sumSq / static_cast<double>(acc.count) - mean * mean;
        if (!std::isfinite(variance) || variance < 0.0)
            variance = 0.0;
        stats.stddev = static_cast<float>(std::sqrt(variance));
    } else {
        stats.stddev = 0.0f;
    }

    return stats;
}

bool bothNormalizable(float a, float b)
{
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;
    // Undeclared negative-DN sentinel net (#1687): reflectance domains are
    // [-1,1] and DN domains non-negative, so no legitimate index input is
    // <= this ceiling, while common undeclared fills (-9999, -32768) all
    // are. Files that omit the NoData tag still get their fill pixels
    // invalidated (see rs_temporal_index_operator's probe note); DECLARED
    // sentinels above the ceiling (e.g. -99) are the caller's to pass via
    // the sentinel-aware overloads or operator pre-normalization.
    if (a <= kUndeclaredSentinelCeiling || b <= kUndeclaredSentinelCeiling)
        return false;
    // Equal negative operands are legitimate post-correction reflectance
    // (dark water/shadow): (a-b)/(a+b) = 0/(2a) is well-defined and
    // evaluates to 0. The pre-R24 form rejected the pair and emitted NaN,
    // silently invalidating every equal-negative pixel (#1687).
    return true;
}

bool bothNormalizable(float a, float b, float noDataA, float noDataB)
{
    if (!bothNormalizable(a, b))
        return false;
    // Exact declared-sentinel match (nodata_utils.h discipline: never an
    // epsilon; a non-finite declared sentinel disables that side's check).
    // Catches declared values above the undeclared ceiling, e.g. -99.
    if (std::isfinite(noDataA) && a == noDataA)
        return false;
    if (std::isfinite(noDataB) && b == noDataB)
        return false;
    return true;
}

bool normalizedDifference(const float *a, const float *b, float *out, size_t count)
{
    if (!a || !b || !out) return false;
    if (count == 0) return false;

    for (size_t i = 0; i < count; ++i) {
        if (!bothNormalizable(a[i], b[i])) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
        } else {
            out[i] = safeDiv(a[i] - b[i], a[i] + b[i]);
        }
    }

    return true;
}

bool normalizedDifference(const float *a, const float *b, float *out, size_t count,
                          float noDataA, float noDataB)
{
    if (!a || !b || !out) return false;
    if (count == 0) return false;

    for (size_t i = 0; i < count; ++i) {
        if (!bothNormalizable(a[i], b[i], noDataA, noDataB)) {
            out[i] = std::numeric_limits<float>::quiet_NaN();
        } else {
            out[i] = safeDiv(a[i] - b[i], a[i] + b[i]);
        }
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



Wgs84ArcMeters wgs84ArcAtLatitudeDeg( double latitudeDeg )
{
    const double phiRad = latitudeDeg * M_PI / 180.0;
    const double cosPhi = std::cos( phiRad );
    Wgs84ArcMeters arc;
    arc.perDegLat =
        111132.92 - 559.82 * std::cos( 2 * phiRad ) + 1.175 * std::cos( 4 * phiRad );
    arc.perDegLon = 111412.84 * cosPhi - 93.5 * std::cos( 3 * phiRad );
    return arc;
}

double sceneCentreLatitudeDeg( const std::array<double, 6> &geoTransform, int height )
{
    return geoTransform[3] + ( height / 2.0 ) * geoTransform[5];
}

} // namespace MathUtils
