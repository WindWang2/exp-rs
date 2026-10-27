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
    // Only non-finite inputs are invalid here (#1687): every production
    // caller resolves the band's DECLARED NoData sentinel to NaN before
    // this kernel runs (the operator's BandSource readTileBand, the
    // providers' readBandData), so a magic negative threshold would only
    // reject valid data — e.g. identical slightly-negative reflectances
    // from atmospheric correction (a == b == -0.01 over dark water), whose
    // normalized difference is a well-defined 0. A zero denominator is
    // safeDiv's contract, not this gate.
    return std::isfinite(a) && std::isfinite(b);
}

bool legacyNegativeSentinelFree(float a, float b)
{
    // Named, documented home for the legacy undeclared-sentinel screen the
    // pre/post index-difference branch (dNBR family) applies ON TOP of
    // finiteness: older pipelines stamped -9999-family band values without
    // declaring NoData, and such values would otherwise surface as extreme
    // (near +/-1) index change. Physically-bounded values — including the
    // slightly-negative reflectances atmospheric correction produces — pass.
    // The general kernels stay sentinel-blind (#1687): declared band NoData
    // is the caller's authority and valid data must never be guessed away.
    return bothNormalizable(a, b) && a > -999.0f && b > -999.0f;
}

bool bothNormalizable(float a, float b, float noDataA, float noDataB)
{
    if (!bothNormalizable(a, b))
        return false;
    // Exact declared-sentinel match (nodata_utils.h discipline: never an
    // epsilon; a non-finite declared sentinel disables that side's check).
    // Merge note (#1722 x #1718, #1687 ruling): the sentinel-aware overloads
    // are KEPT from R24-A, but the undeclared ceiling net is not reapplied
    // here — every caller that holds declared sentinels is already inside
    // the NoData-aware layer, and undeclared -9999-family fills stay the
    // dNBR seam's business (legacyNegativeSentinelFree).
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
        // Non-finite operand (declared sentinels arrive pre-resolved to
        // NaN) or a zero denominator -> NaN, never a saturated value.
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
