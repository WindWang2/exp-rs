// src/processing/algorithms/change_detection.cpp — Change detection algorithms
#include "change_detection.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

#include <opencv2/core.hpp>

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
    stats.validCount = mathStats.validCount;
    stats.min = mathStats.min;
    stats.max = mathStats.max;
    stats.mean = mathStats.mean;
    stats.stddev = mathStats.stddev;

    return stats;
}

bool ratio(const float *before, const float *after, float *out, size_t count)
{
    if (!before || !after || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ratio: null pointer argument");
        return false;
    }
    if (count == 0) return false;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 0; i < count; ++i)
        // #700: negative `before` (e.g. slightly negative reflectance after
        // atmospheric correction over water) produced sign-flipped ratios
        // that downstream Otsu thresholding reads as huge change; the sibling
        // log-ratio clamps negatives — be consistent and emit NaN instead.
        out[i] = (before[i] <= 0.0f) ? nan : after[i] / before[i];

    return true;
}

bool cvaMagnitude(const float *const *beforeBands, const float *const *afterBands,
                  int bandCount, size_t pixels, float *out, QString *errorMessage)
{
    if (!beforeBands || !afterBands || !out || bandCount <= 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cvaMagnitude: null argument or empty band set");
        return false;
    }
    if (pixels == 0) return false;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t p = 0; p < pixels; ++p) {
        double sumSq = 0.0;
        bool hasNan = false;
        for (int b = 0; b < bandCount; ++b) {
            if (!beforeBands[b] || !afterBands[b]) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("cvaMagnitude: null band buffer");
                return false;
            }
            const float d = afterBands[b][p] - beforeBands[b][p];
            if (std::isnan(d)) {
                hasNan = true;
                break;
            }
            sumSq += static_cast<double>(d) * static_cast<double>(d);
        }
        out[p] = hasNan ? nan : static_cast<float>(std::sqrt(sumSq));
    }
    return true;
}

bool otsuThreshold(const float *values, size_t count, float *threshold, int bins)
{
    if (!values || !threshold || count == 0)
        return false;

    // Collect finite values; find the data range.
    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();
    size_t finite = 0;
    for (size_t i = 0; i < count; ++i) {
        const double v = values[i];
        if (!std::isfinite(v))
            continue;
        minVal = std::min(minVal, v);
        maxVal = std::max(maxVal, v);
        ++finite;
    }
    if (finite == 0)
        return false;
    if (minVal == maxVal) {
        *threshold = static_cast<float>(minVal);
        return true;
    }

    bins = std::clamp(bins, 16, 1024);
    const double range = maxVal - minVal;
    std::vector<double> hist(static_cast<size_t>(bins), 0.0);
    for (size_t i = 0; i < count; ++i) {
        const double v = values[i];
        if (!std::isfinite(v))
            continue;
        int bin = static_cast<int>((v - minVal) / range * (bins - 1));
        bin = std::clamp(bin, 0, bins - 1);
        hist[static_cast<size_t>(bin)] += 1.0;
    }

    return otsuThresholdFromHistogram(minVal, maxVal, hist, finite, threshold);
}

bool otsuThresholdFromHistogram(double minVal, double maxVal,
                                const std::vector<double> &hist,
                                size_t finiteCount, float *threshold)
{
    if (!threshold || hist.empty() || finiteCount == 0)
        return false;
    const double range = maxVal - minVal;
    if (range <= 0.0) {
        *threshold = static_cast<float>(minVal);
        return true;
    }
    const int bins = static_cast<int>(hist.size());

    // Otsu: maximize between-class variance over cumulative histogram sums.
    const double total = static_cast<double>(finiteCount);
    double sumAll = 0.0;
    for (int b = 0; b < bins; ++b)
        sumAll += hist[static_cast<size_t>(b)] * b;

    double sumB = 0.0;
    double weightB = 0.0;
    double bestVariance = -1.0;
    // Bins inside an empty gap between two clusters all yield the identical
    // (bitwise-equal) between-class variance; averaging the tied maxima picks
    // the middle of the gap instead of its first edge, which is the robust
    // convention for well-separated bimodal distributions.
    double bestBinSum = 0.0;
    int bestBinCount = 0;
    for (int b = 0; b < bins; ++b) {
        weightB += hist[static_cast<size_t>(b)];
        if (weightB == 0.0)
            continue;
        sumB += hist[static_cast<size_t>(b)] * b;
        const double weightF = total - weightB;
        if (weightF == 0.0)
            break;
        const double meanB = sumB / weightB;
        const double meanF = (sumAll - sumB) / weightF;
        const double between = weightB * weightF * (meanB - meanF) * (meanB - meanF);
        if (between > bestVariance) {
            bestVariance = between;
            bestBinSum = static_cast<double>(b);
            bestBinCount = 1;
        } else if (between == bestVariance) {
            bestBinSum += static_cast<double>(b);
            ++bestBinCount;
        }
    }

    const double bestBin = (bestBinCount > 0) ? bestBinSum / bestBinCount : 0.0;
    *threshold = static_cast<float>(minVal + (bestBin + 0.5) * range / (bins - 1));
    return true;
}

bool percentileThresholdFromHistogram(double minVal, double maxVal,
                                      const std::vector<double> &hist,
                                      size_t finiteCount, double percentile,
                                      float *threshold)
{
    if (!threshold || hist.empty() || finiteCount == 0)
        return false;
    const double range = maxVal - minVal;
    if (range <= 0.0) {
        *threshold = static_cast<float>(minVal);
        return true;
    }
    const int bins = static_cast<int>(hist.size());
    // Bin width must match the histogram builder, which bins with
    // (v - minVal) / range * (bins - 1) — i.e. width range/(bins-1), not
    // range/bins. The old /bins reconstruction biased every percentile low
    // by up to one bin (#700).
    const double binWidth = bins > 1 ? range / (bins - 1) : range;
    const double p = std::clamp(static_cast<double>(percentile), 0.0, 100.0);
    // Nearest-rank index over the sorted finite values (p == 0 -> minimum).
    const double rank = std::max(1.0,
        std::ceil(p / 100.0 * static_cast<double>(finiteCount))) - 1.0;

    double cum = 0.0;
    for (int b = 0; b < bins; ++b) {
        const double prev = cum;
        cum += hist[static_cast<size_t>(b)];
        if (rank < cum || b == bins - 1) {
            // The rank falls inside this bin; interpolate linearly to the bin
            // lower edge + fractional offset. Histogram-estimated, so a value
            // near the true sorted percentile rather than an exact sample.
            const double frac = (cum > prev)
                ? (rank - prev) / (cum - prev)
                : 0.0;
            *threshold = static_cast<float>(
                minVal + (static_cast<double>(b) + std::clamp(frac, 0.0, 1.0)) * binWidth);
            return true;
        }
    }
    *threshold = static_cast<float>(maxVal);
    return true;
}

bool percentileThreshold(const float *values, size_t count, float percentile, float *threshold)
{
    if (!values || !threshold || count == 0)
        return false;

    std::vector<float> finite;
    finite.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (std::isfinite(values[i]))
            finite.push_back(values[i]);
    }
    if (finite.empty())
        return false;

    const double p = std::clamp(static_cast<double>(percentile), 0.0, 100.0);
    std::sort(finite.begin(), finite.end());
    // nearest-rank index; p == 0.0 must yield the minimum (index 0), not a
    // size_t underflow that wraps to the maximum.
    const size_t size = finite.size();
    const size_t ceilRank = static_cast<size_t>(
        std::ceil(p / 100.0 * static_cast<double>(size)));
    const size_t rank = ceilRank > 0 ? std::min(ceilRank, size) - 1 : 0;
    *threshold = finite[rank];
    return true;
}

namespace {

/// One 3x3 pass over a 0/1 mask; 255 (NoData) cells never change.
void erodePass(const uint8_t *src, uint8_t *dst, int width, int height)
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t v = src[static_cast<size_t>(y) * width + x];
            if (v != 1) {
                dst[static_cast<size_t>(y) * width + x] = v;
                continue;
            }
            bool all = true;
            for (int dy = -1; dy <= 1 && all; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue; // border: keep the pixel
                    if (src[static_cast<size_t>(ny) * width + nx] != 1) {
                        all = false;
                        break;
                    }
                }
            }
            dst[static_cast<size_t>(y) * width + x] = all ? 1 : 0;
        }
    }
}

void dilatePass(const uint8_t *src, uint8_t *dst, int width, int height)
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t v = src[static_cast<size_t>(y) * width + x];
            if (v == 1) {
                dst[static_cast<size_t>(y) * width + x] = 1;
                continue;
            }
            if (v == 255) {
                dst[static_cast<size_t>(y) * width + x] = 255;
                continue;
            }
            bool any = false;
            for (int dy = -1; dy <= 1 && !any; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    if (src[static_cast<size_t>(ny) * width + nx] == 1) {
                        any = true;
                        break;
                    }
                }
            }
            dst[static_cast<size_t>(y) * width + x] = any ? 1 : 0;
        }
    }
}

} // namespace

void morphologicalCleanup(uint8_t *mask, int width, int height, int iterations, MorphOp op)
{
    if (!mask || width <= 0 || height <= 0 || iterations <= 0 || op == MorphOp::None)
        return;

    std::vector<uint8_t> scratch(static_cast<size_t>(width) * height);
    for (int it = 0; it < iterations; ++it) {
        switch (op) {
        case MorphOp::Erode:
            erodePass(mask, scratch.data(), width, height);
            std::copy(scratch.begin(), scratch.end(), mask);
            break;
        case MorphOp::Dilate:
            dilatePass(mask, scratch.data(), width, height);
            std::copy(scratch.begin(), scratch.end(), mask);
            break;
        case MorphOp::Open:
            erodePass(mask, scratch.data(), width, height);
            dilatePass(scratch.data(), mask, width, height);
            break;
        case MorphOp::Close:
            dilatePass(mask, scratch.data(), width, height);
            erodePass(scratch.data(), mask, width, height);
            break;
        case MorphOp::None:
            break;
        }
    }
}

namespace {

// Node-table policies for connectedComponentFilter (#648). Both drive the
// same union-find scan; they differ only in where parent/area live.
struct ccUnionFindDenseStore
{
    std::vector<int> parent;
    std::vector<size_t> area;
    explicit ccUnionFindDenseStore(size_t count)
        : parent(count, -1), area(count, 0) {}
    void makeNode(int i) { parent[i] = i; area[i] = 1; }
    int parentOf(int i) const { return parent[i]; }
    void setParent(int i, int p) { parent[i] = p; }
    void mergeArea(int ra, int rb) { area[ra] += area[rb]; }
    size_t areaOf(int i) const { return area[i]; }
};

struct ccUnionFindSparseStore
{
    // One entry per foreground pixel (~40-48 B/entry incl. hashing).
    std::unordered_map<int, int> parent;
    std::unordered_map<int, size_t> area;
    void makeNode(int i) { parent[i] = i; area[i] = 1; }
    int parentOf(int i) const { return parent.find(i)->second; }
    void setParent(int i, int p) { parent.find(i)->second = p; }
    void mergeArea(int ra, int rb) { area[ra] += area[rb]; }
    size_t areaOf(int i) const { return area.find(i)->second; }
};

// Shared 8-neighbourhood union-find + minimum-mapping-unit drop. The store
// sees exactly the same operations in exactly the same order for a given
// mask, so both policies produce byte-identical output.
template <typename Store>
void ccLabelScan(uint8_t *mask, int width, int height, size_t minArea, Store &store)
{
    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; ++i) {
        if (mask[i] == 1)
            store.makeNode(static_cast<int>(i));
    }

    const auto find = [&store](int x) {
        int root = x;
        while (store.parentOf(root) != root)
            root = store.parentOf(root);
        while (store.parentOf(x) != root) {
            const int next = store.parentOf(x);
            store.setParent(x, root);
            x = next;
        }
        return root;
    };
    const auto unite = [&](int a, int b) {
        const int ra = find(a);
        const int rb = find(b);
        if (ra != rb) {
            store.setParent(rb, ra);
            store.mergeArea(ra, rb);
        }
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (mask[idx] != 1)
                continue;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dy == 0 && dx == 0)
                        continue;
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    const size_t nidx = static_cast<size_t>(ny) * width + nx;
                    if (mask[nidx] == 1)
                        unite(static_cast<int>(idx), static_cast<int>(nidx));
                }
            }
        }
    }

    // Drop components below the minimum mapping unit.
    for (size_t i = 0; i < count; ++i) {
        if (mask[i] == 1 && store.areaOf(find(static_cast<int>(i))) < minArea)
            mask[i] = 0;
    }
}

} // namespace

bool connectedComponentFilter(uint8_t *mask, int width, int height,
                              size_t minArea)
{
    if (!mask || width <= 0 || height <= 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms,
                        "connectedComponentFilter: invalid arguments");
        return false;
    }
    if (minArea == 0) return true; // no-op

    const size_t count = static_cast<size_t>(width) * height;
    if (count == 0) return true;
    // The union-find stores node ids as int; refuse pathological rasters
    // instead of wrapping indices into out-of-bounds access.
    if (count > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;

    // Union-find over the 1-pixels with 8-neighbourhood connectivity (#648).
    // The node tables used to be allocated for EVERY pixel (4 + 8 B/px =
    // ~30 GB at 50k x 50k). Two bounds fix the realistic sparse-change case
    // while keeping the worst case at the old ceiling:
    //   - an all-foreground mask is one component: decided without
    //     union-find at all;
    //   - otherwise the store is picked by the measured foreground fraction:
    //     hash tables (~40-48 B per foreground entry) below 25%, dense
    //     tables (the old 12 B/px ceiling) above.
    // Both stores drive the SAME ccLabelScan template, so the output is
    // identical regardless of which path is taken (fuzz-verified over 405
    // randomized + edge masks: dense == sparse == hybrid).
    size_t foreground = 0;
    for (size_t i = 0; i < count; ++i) {
        if (mask[i] == 1)
            ++foreground;
    }
    if (foreground == count) {
        // One 8-connected component spans the scene.
        if (minArea > count)
            std::fill(mask, mask + count, static_cast<uint8_t>(0));
        return true;
    }

    if (foreground * 4 < count) {
        ccUnionFindSparseStore store;
        ccLabelScan(mask, width, height, minArea, store);
    } else {
        ccUnionFindDenseStore store(count);
        ccLabelScan(mask, width, height, minArea, store);
    }
    return true;
}

namespace {

/// SVD-based symmetric square-root inverse; eigenvalues <= 1e-12 are zeroed.
/// Shared by madFinalize() (and the legacy madChange() via the same path).
cv::Mat madSqrtInv(const cv::Mat& M)
{
    cv::Mat w, u, vt;
    cv::SVD::compute(M, w, u, vt);
    cv::Mat wInvSqrt = cv::Mat::zeros(M.rows, M.cols, CV_64F);
    for (int i = 0; i < M.rows; ++i) {
        const double val = w.at<double>(i);
        wInvSqrt.at<double>(i, i) = (val > 1e-12) ? (1.0 / std::sqrt(val)) : 0.0;
    }
    return u * wInvSqrt * vt;
}

} // namespace

bool madAccumulateSums(const float *beforeBip, const float *afterBip,
                       size_t tilePixels, int bandCount, MadStreamingState *s)
{
    if (!beforeBip || !afterBip || !s || bandCount <= 0)
        return false;
    if (s->bandCount == 0) {
        s->bandCount = bandCount;
        s->sumX.assign(static_cast<size_t>(bandCount), 0.0);
        s->sumY.assign(static_cast<size_t>(bandCount), 0.0);
    } else if (s->bandCount != bandCount) {
        return false;
    }

    const size_t B = static_cast<size_t>(bandCount);
    for (size_t p = 0; p < tilePixels; ++p) {
        bool valid = true;
        for (size_t b = 0; b < B; ++b) {
            if (!std::isfinite(beforeBip[p * B + b]) || !std::isfinite(afterBip[p * B + b])) {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;
        ++s->validCount;
        for (size_t b = 0; b < B; ++b) {
            s->sumX[b] += beforeBip[p * B + b];
            s->sumY[b] += afterBip[p * B + b];
        }
    }
    return true;
}

bool madFinalizeMeans(MadStreamingState *s, QString *errorMessage)
{
    if (!s || s->bandCount <= 0)
        return false;
    if (s->validCount < static_cast<size_t>(s->bandCount + 2)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Insufficient valid pixels for MAD calculation.");
        return false;
    }

    const size_t B = static_cast<size_t>(s->bandCount);
    const double invN = 1.0 / static_cast<double>(s->validCount);
    s->meanX.assign(B, 0.0);
    s->meanY.assign(B, 0.0);
    for (size_t b = 0; b < B; ++b) {
        s->meanX[b] = s->sumX[b] * invN;
        s->meanY[b] = s->sumY[b] * invN;
    }
    s->xx.assign(B * B, 0.0);
    s->yy.assign(B * B, 0.0);
    s->xy.assign(B * B, 0.0);
    s->meansReady = true;
    return true;
}

bool madAccumulateCentered(const float *beforeBip, const float *afterBip,
                           size_t tilePixels, int bandCount, MadStreamingState *s)
{
    if (!beforeBip || !afterBip || !s || bandCount <= 0)
        return false;
    if (!s->meansReady || s->bandCount != bandCount)
        return false;

    const size_t B = static_cast<size_t>(bandCount);
    std::vector<double> cx(B), cy(B);
    for (size_t p = 0; p < tilePixels; ++p) {
        bool valid = true;
        for (size_t b = 0; b < B; ++b) {
            if (!std::isfinite(beforeBip[p * B + b]) || !std::isfinite(afterBip[p * B + b])) {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;
        for (size_t b = 0; b < B; ++b) {
            cx[b] = static_cast<double>(beforeBip[p * B + b]) - s->meanX[b];
            cy[b] = static_cast<double>(afterBip[p * B + b]) - s->meanY[b];
        }
        for (size_t a = 0; a < B; ++a) {
            for (size_t b = 0; b < B; ++b) {
                s->xx[a * B + b] += cx[a] * cx[b];
                s->yy[a * B + b] += cy[a] * cy[b];
                s->xy[a * B + b] += cx[a] * cy[b];
            }
        }
    }
    return true;
}

bool madFinalize(MadStreamingState *s, QString *errorMessage)
{
    if (!s || s->bandCount <= 0 || !s->meansReady) {
        if (errorMessage)
            *errorMessage = QStringLiteral("MAD statistics are not ready.");
        return false;
    }

    const int B = s->bandCount;
    const size_t BB = static_cast<size_t>(B) * B;
    if (s->validCount < static_cast<size_t>(B + 2)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Insufficient valid pixels for MAD calculation.");
        return false;
    }

    // Covariance matrices: S_XX = sum(x-xbar)(x-xbar)^T / (N-1), etc.
    cv::Mat SXX(B, B, CV_64F), SYY(B, B, CV_64F), SXY(B, B, CV_64F);
    const double scale = 1.0 / static_cast<double>(s->validCount - 1);
    for (int r = 0; r < B; ++r) {
        for (int c = 0; c < B; ++c) {
            const size_t i = static_cast<size_t>(r) * B + c;
            SXX.at<double>(r, c) = s->xx[i] * scale;
            SYY.at<double>(r, c) = s->yy[i] * scale;
            SXY.at<double>(r, c) = s->xy[i] * scale;
        }
    }

    // Trace-scaled diagonal regularization for numerical stability across
    // both DN-valued (1e+3..1e+4) and reflectance-valued (0..1) imagery.
    const double epsXX = 1e-6 * cv::trace(SXX)[0] / B;
    const double epsYY = 1e-6 * cv::trace(SYY)[0] / B;
    for (int b = 0; b < B; ++b) {
        SXX.at<double>(b, b) += std::max(epsXX, 1e-12);
        SYY.at<double>(b, b) += std::max(epsYY, 1e-12);
    }

    const cv::Mat SXXInvSqrt = madSqrtInv(SXX);
    const cv::Mat SYYInvSqrt = madSqrtInv(SYY);

    // Transformed matrix H = S_XX^(-1/2) * S_XY * S_YY^(-1/2); SVD H = U_h * D * V_h^T.
    const cv::Mat H = SXXInvSqrt * SXY * SYYInvSqrt;
    cv::Mat D, Uh, VhT;
    cv::SVD::compute(H, D, Uh, VhT);

    // Canonical coefficients: A = S_XX^(-1/2) * U_h, B = S_YY^(-1/2) * V_h^T.
    cv::Mat A = SXXInvSqrt * Uh;
    cv::Mat Bmat = SYYInvSqrt * VhT.t();

    // Ensure canonical variate pairs are positively correlated:
    // A[:, k]^T * S_XY * B[:, k] > 0; otherwise negate B[:, k].
    for (int k = 0; k < B; ++k) {
        const cv::Mat covK = A.col(k).t() * SXY * Bmat.col(k);
        if (covK.at<double>(0, 0) < 0.0)
            Bmat.col(k) *= -1.0;
    }

    // Canonical correlations lambda_i and MAD variate variances.
    s->A.assign(BB, 0.0);
    s->B.assign(BB, 0.0);
    s->varMad.assign(static_cast<size_t>(B), 0.0);
    for (int r = 0; r < B; ++r) {
        const double lambda = std::clamp(D.at<double>(r), 0.0, 1.0);
        s->varMad[static_cast<size_t>(r)] = std::max(2.0 * (1.0 - lambda), 1e-6);
        for (int c = 0; c < B; ++c) {
            s->A[static_cast<size_t>(r) * B + c] = A.at<double>(r, c);
            s->B[static_cast<size_t>(r) * B + c] = Bmat.at<double>(r, c);
        }
    }

    s->covReady = true;
    s->ready = true;
    return true;
}

void madTransformTile(const float *beforeBip, const float *afterBip,
                      size_t tilePixels, int bandCount, const MadStreamingState &s,
                      float *out)
{
    if (!beforeBip || !afterBip || !out || bandCount <= 0 || !s.ready)
        return;

    const size_t B = static_cast<size_t>(bandCount);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<double> u(B), v(B);
    for (size_t p = 0; p < tilePixels; ++p) {
        bool valid = true;
        for (size_t b = 0; b < B; ++b) {
            if (!std::isfinite(beforeBip[p * B + b]) || !std::isfinite(afterBip[p * B + b])) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            out[p] = nan;
            continue;
        }
        // u = A^T (x - xbar), v = B^T (y - ybar); A/B are row-major B x B.
        for (size_t k = 0; k < B; ++k) {
            double uk = 0.0, vk = 0.0;
            for (size_t a = 0; a < B; ++a) {
                uk += s.A[a * B + k] * (static_cast<double>(beforeBip[p * B + a]) - s.meanX[a]);
                vk += s.B[a * B + k] * (static_cast<double>(afterBip[p * B + a]) - s.meanY[a]);
            }
            u[k] = uk;
            v[k] = vk;
        }
        double chiSquare = 0.0;
        for (size_t k = 0; k < B; ++k) {
            const double m = u[k] - v[k]; // MAD variate
            chiSquare += (m * m) / s.varMad[k];
        }
        out[p] = static_cast<float>(chiSquare);
    }
}

bool madChange(const float *const *beforeBands, const float *const *afterBands,
               int bandCount, size_t pixels, float *out,
               QString *errorMessage)
{
    if (!beforeBands || !afterBands || !out || bandCount <= 0 || pixels == 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid input pointers or zero dimensions for MAD change detection.");
        return false;
    }
    for (int b = 0; b < bandCount; ++b) {
        if (!beforeBands[b] || !afterBands[b]) {
            if (errorMessage) *errorMessage = QStringLiteral("Null band pointer for band %1").arg(b + 1);
            return false;
        }
    }

    // Legacy full-scene entry point: pack the band-major buffers into one
    // BIP "tile" and run the exact same streaming pipeline, so there is a
    // single source of the MAD math. Packing is O(pixels * bandCount), which
    // is fine for this API (the caller already holds all bands in memory);
    // the memory-bounded path is the operator's per-tile streaming.
    const size_t B = static_cast<size_t>(bandCount);
    std::vector<float> beforeBip(pixels * B), afterBip(pixels * B);
    for (size_t p = 0; p < pixels; ++p) {
        for (size_t b = 0; b < B; ++b) {
            beforeBip[p * B + b] = beforeBands[b][p];
            afterBip[p * B + b] = afterBands[b][p];
        }
    }

    MadStreamingState state;
    const auto failNaN = [&]() {
        std::fill_n(out, pixels, std::numeric_limits<float>::quiet_NaN());
    };

    if (!madAccumulateSums(beforeBip.data(), afterBip.data(), pixels, bandCount, &state)) {
        failNaN();
        if (errorMessage) *errorMessage = QStringLiteral("MAD accumulation failed.");
        return false;
    }
    if (!madFinalizeMeans(&state, errorMessage)) {
        failNaN();
        return false;
    }
    if (!madAccumulateCentered(beforeBip.data(), afterBip.data(), pixels, bandCount, &state)) {
        failNaN();
        if (errorMessage) *errorMessage = QStringLiteral("MAD covariance accumulation failed.");
        return false;
    }
    if (!madFinalize(&state, errorMessage)) {
        failNaN();
        return false;
    }

    madTransformTile(beforeBip.data(), afterBip.data(), pixels, bandCount, state, out);
    return true;
}

bool kittlerIllingworthThresholdFromHistogram(double minVal, double maxVal,
                                              const std::vector<double> &hist,
                                              size_t finiteCount, float *threshold)
{
    if (!threshold || hist.empty() || finiteCount == 0)
        return false;
    const double range = maxVal - minVal;
    if (range <= 0.0) {
        *threshold = static_cast<float>(minVal);
        return true;
    }
    const int bins = static_cast<int>(hist.size());
    if (bins < 2) {
        *threshold = static_cast<float>(minVal);
        return true;
    }

    const double total = static_cast<double>(finiteCount);
    std::vector<double> p(bins);
    for (int i = 0; i < bins; ++i) {
        p[i] = hist[static_cast<size_t>(i)] / total;
    }

    // Cumulative weights, sums, squared sums
    std::vector<double> P(bins, 0.0);
    std::vector<double> S(bins, 0.0);
    std::vector<double> SS(bins, 0.0);

    P[0] = p[0];
    S[0] = 0.0;
    SS[0] = 0.0;
    for (int i = 1; i < bins; ++i) {
        P[i] = P[i - 1] + p[i];
        S[i] = S[i - 1] + static_cast<double>(i) * p[i];
        SS[i] = SS[i - 1] + static_cast<double>(i * i) * p[i];
    }

    const double totalP = P[bins - 1];
    const double totalS = S[bins - 1];
    const double totalSS = SS[bins - 1];

    double bestCost = std::numeric_limits<double>::infinity();
    int bestBin = 0;

    for (int t = 0; t < bins - 1; ++t) {
        const double p1 = P[t];
        const double p2 = totalP - p1;
        if (p1 < 1e-12 || p2 < 1e-12)
            continue;

        const double mu1 = S[t] / p1;
        const double mu2 = (totalS - S[t]) / p2;

        const double var1 = std::max((SS[t] / p1) - mu1 * mu1, 1e-6);
        const double var2 = std::max(((totalSS - SS[t]) / p2) - mu2 * mu2, 1e-6);

        // J(T) = 1 + 2*(P1*ln(sigma1) + P2*ln(sigma2)) - 2*(P1*ln(P1) + P2*ln(P2))
        //      = 1 + P1*ln(var1) + P2*ln(var2) - 2*(P1*ln(P1) + P2*ln(P2))
        const double cost = 1.0 + p1 * std::log(var1) + p2 * std::log(var2)
                            - 2.0 * (p1 * std::log(p1) + p2 * std::log(p2));

        if (cost < bestCost) {
            bestCost = cost;
            bestBin = t;
        }
    }

    if (std::isinf(bestCost)) {
        *threshold = static_cast<float>(minVal + 0.5 * range);
    } else {
        *threshold = static_cast<float>(minVal + (bestBin + 0.5) * range / (bins - 1));
    }
    return true;
}

bool kittlerIllingworthThreshold(const float *values, size_t count, float *threshold, int bins)
{
    if (!values || !threshold || count == 0)
        return false;

    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();
    size_t finite = 0;
    for (size_t i = 0; i < count; ++i) {
        const double v = values[i];
        if (!std::isfinite(v))
            continue;
        minVal = std::min(minVal, v);
        maxVal = std::max(maxVal, v);
        ++finite;
    }
    if (finite == 0)
        return false;
    if (minVal == maxVal) {
        *threshold = static_cast<float>(minVal);
        return true;
    }

    bins = std::clamp(bins, 16, 1024);
    const double range = maxVal - minVal;
    std::vector<double> hist(static_cast<size_t>(bins), 0.0);
    for (size_t i = 0; i < count; ++i) {
        const double v = values[i];
        if (!std::isfinite(v))
            continue;
        int bin = static_cast<int>((v - minVal) / range * (bins - 1));
        bin = std::clamp(bin, 0, bins - 1);
        hist[static_cast<size_t>(bin)] += 1.0;
    }

    return kittlerIllingworthThresholdFromHistogram(minVal, maxVal, hist, finite, threshold);
}

bool cvaMagnitudeAndAngle(const float *beforeBand1, const float *beforeBand2,
                          const float *afterBand1, const float *afterBand2,
                          size_t pixels, float *outMagnitude, float *outAngle,
                          QString *errorMessage)
{
    if (!beforeBand1 || !beforeBand2 || !afterBand1 || !afterBand2 || !outMagnitude || !outAngle) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cvaMagnitudeAndAngle: null pointer argument");
        return false;
    }
    if (pixels == 0) return false;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 0; i < pixels; ++i) {
        const float b1 = beforeBand1[i];
        const float b2 = beforeBand2[i];
        const float a1 = afterBand1[i];
        const float a2 = afterBand2[i];

        if (!std::isfinite(b1) || !std::isfinite(b2) || !std::isfinite(a1) || !std::isfinite(a2)) {
            outMagnitude[i] = nan;
            outAngle[i] = nan;
            continue;
        }

        const double d1 = static_cast<double>(a1) - static_cast<double>(b1);
        const double d2 = static_cast<double>(a2) - static_cast<double>(b2);

        const double mag = std::sqrt(d1 * d1 + d2 * d2);
        const double angle = std::atan2(d2, d1);

        outMagnitude[i] = static_cast<float>(mag);
        outAngle[i] = static_cast<float>(angle);
    }
    return true;
}

bool cvaQuadrant(const float *beforeBand1, const float *beforeBand2,
                 const float *afterBand1, const float *afterBand2,
                 size_t pixels, uint8_t *outQuadrant,
                 QString *errorMessage)
{
    if (!beforeBand1 || !beforeBand2 || !afterBand1 || !afterBand2 || !outQuadrant) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cvaQuadrant: null pointer argument");
        return false;
    }
    if (pixels == 0) return false;

    for (size_t i = 0; i < pixels; ++i) {
        const float b1 = beforeBand1[i];
        const float b2 = beforeBand2[i];
        const float a1 = afterBand1[i];
        const float a2 = afterBand2[i];

        if (!std::isfinite(b1) || !std::isfinite(b2) || !std::isfinite(a1) || !std::isfinite(a2)) {
            outQuadrant[i] = 255; // NoData
            continue;
        }

        const float d1 = a1 - b1;
        const float d2 = a2 - b2;

        if (d1 > 0.0f) {
            outQuadrant[i] = (d2 > 0.0f) ? 1 : 4;
        } else {
            outQuadrant[i] = (d2 > 0.0f) ? 2 : 3;
        }
    }
    return true;
}

bool samChangeAngle(const float *const *beforeBands, const float *const *afterBands,
                    int bandCount, size_t pixels, float *outAngleRadians,
                    QString *errorMessage)
{
    if (!beforeBands || !afterBands || !outAngleRadians || bandCount <= 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("samChangeAngle: null argument or invalid band count");
        return false;
    }
    if (pixels == 0) return false;
    for (int b = 0; b < bandCount; ++b) {
        if (!beforeBands[b] || !afterBands[b]) {
            if (errorMessage)
                *errorMessage = QStringLiteral("samChangeAngle: null band buffer for band %1").arg(b + 1);
            return false;
        }
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t p = 0; p < pixels; ++p) {
        double dot = 0.0;
        double normSqX = 0.0;
        double normSqY = 0.0;
        bool valid = true;

        for (int b = 0; b < bandCount; ++b) {
            const float bx = beforeBands[b][p];
            const float by = afterBands[b][p];
            if (!std::isfinite(bx) || !std::isfinite(by)) {
                valid = false;
                break;
            }
            const double x = static_cast<double>(bx);
            const double y = static_cast<double>(by);
            dot += x * y;
            normSqX += x * x;
            normSqY += y * y;
        }

        if (!valid) {
            outAngleRadians[p] = nan;
            continue;
        }

        const double denom = std::sqrt(normSqX * normSqY);
        if (denom <= 1e-12) {
            if (normSqX <= 1e-12 && normSqY <= 1e-12) {
                outAngleRadians[p] = 0.0f;
            } else {
                outAngleRadians[p] = static_cast<float>(M_PI / 2.0);
            }
            continue;
        }

        const double cosAlpha = std::clamp(dot / denom, -1.0, 1.0);
        outAngleRadians[p] = static_cast<float>(std::acos(cosAlpha));
    }
    return true;
}

bool logRatio(const float *before, const float *after, float *out,
              size_t count, float epsilon)
{
    if (!before || !after || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "logRatio: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    if (epsilon <= 0.0f) epsilon = 1e-4f;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 0; i < count; ++i) {
        const float b = before[i];
        const float a = after[i];
        if (!std::isfinite(b) || !std::isfinite(a)) {
            out[i] = nan;
            continue;
        }
        const double valBefore = std::max(static_cast<double>(b), 0.0) + static_cast<double>(epsilon);
        const double valAfter = std::max(static_cast<double>(a), 0.0) + static_cast<double>(epsilon);
        out[i] = static_cast<float>(std::log(valAfter) - std::log(valBefore));
    }
    return true;
}

namespace {

inline double chiSquareUpperCdf(double k, double x)
{
    if (x <= 0.0) return 1.0;
    if (k <= 0.0) return 0.0;
    const double a = k * 0.5;
    const double z = x * 0.5;
    if (k == 2.0) {
        return std::exp(-z);
    }
    if (k == 1.0) {
        return std::erfc(std::sqrt(z));
    }
    if (z < a + 1.0) {
        double sum = 1.0 / a;
        double term = 1.0 / a;
        for (int n = 1; n < 100; ++n) {
            term *= z / (a + n);
            sum += term;
            if (term < sum * 1e-12) break;
        }
        double lower = sum * std::exp(-z + a * std::log(z) - std::lgamma(a));
        return std::clamp(1.0 - lower, 0.0, 1.0);
    } else {
        double b = z + 1.0 - a;
        double c = 1.0 / 1e-30;
        double d = 1.0 / b;
        double h = d;
        for (int n = 1; n < 100; ++n) {
            double an = -static_cast<double>(n) * (static_cast<double>(n) - a);
            b += 2.0;
            d = an * d + b;
            if (std::abs(d) < 1e-30) d = 1e-30;
            c = b + an / c;
            if (std::abs(c) < 1e-30) c = 1e-30;
            d = 1.0 / d;
            double delta = d * c;
            h *= delta;
            if (std::abs(delta - 1.0) < 1e-12) break;
        }
        double q = std::exp(-z + a * std::log(z) - std::lgamma(a)) * h;
        return std::clamp(q, 0.0, 1.0);
    }
}

} // namespace

bool irMadChange(const float *const *beforeBands, const float *const *afterBands,
                 int bandCount, size_t pixels, float *outChiSquare,
                 int maxIterations, double convThreshold,
                 QString *errorMessage)
{
    if (!beforeBands || !afterBands || !outChiSquare || bandCount <= 0 || pixels == 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid input pointers or zero dimensions for IR-MAD.");
        return false;
    }
    for (int b = 0; b < bandCount; ++b) {
        if (!beforeBands[b] || !afterBands[b]) {
            if (errorMessage) *errorMessage = QStringLiteral("Null band pointer for band %1").arg(b + 1);
            return false;
        }
    }

    const size_t B = static_cast<size_t>(bandCount);
    std::vector<size_t> validIndices;
    validIndices.reserve(pixels);

    for (size_t p = 0; p < pixels; ++p) {
        bool valid = true;
        for (size_t b = 0; b < B; ++b) {
            if (!std::isfinite(beforeBands[b][p]) || !std::isfinite(afterBands[b][p])) {
                valid = false;
                break;
            }
        }
        if (valid)
            validIndices.push_back(p);
    }

    const size_t N = validIndices.size();
    if (N < B + 2) {
        std::fill_n(outChiSquare, pixels, std::numeric_limits<float>::quiet_NaN());
        if (errorMessage) *errorMessage = QStringLiteral("Insufficient valid pixels for IR-MAD calculation.");
        return false;
    }

    std::vector<double> weights(N, 1.0);
    std::vector<double> prevRho(B, 0.0);
    cv::Mat A_final, B_final;
    std::vector<double> varMad_final(B, 1.0);
    std::vector<double> meanX_final(B, 0.0), meanY_final(B, 0.0);

    maxIterations = std::clamp(maxIterations, 1, 100);
    if (convThreshold <= 0.0) convThreshold = 1e-4;

    for (int iter = 0; iter < maxIterations; ++iter) {
        // Step 1: Weighted means
        double sumW = 0.0;
        double sumW2 = 0.0;
        std::vector<double> sumX(B, 0.0), sumY(B, 0.0);

        for (size_t i = 0; i < N; ++i) {
            const double w = weights[i];
            sumW += w;
            sumW2 += w * w;
            const size_t p = validIndices[i];
            for (size_t b = 0; b < B; ++b) {
                sumX[b] += w * static_cast<double>(beforeBands[b][p]);
                sumY[b] += w * static_cast<double>(afterBands[b][p]);
            }
        }

        if (sumW <= 1e-12) break;

        const double invSumW = 1.0 / sumW;
        std::vector<double> meanX(B), meanY(B);
        for (size_t b = 0; b < B; ++b) {
            meanX[b] = sumX[b] * invSumW;
            meanY[b] = sumY[b] * invSumW;
        }

        // Step 2: Weighted covariance matrices
        double denom = sumW - (sumW2 / sumW);
        if (denom < 1.0) denom = 1.0;
        const double covScale = 1.0 / denom;

        cv::Mat SXX = cv::Mat::zeros(static_cast<int>(B), static_cast<int>(B), CV_64F);
        cv::Mat SYY = cv::Mat::zeros(static_cast<int>(B), static_cast<int>(B), CV_64F);
        cv::Mat SXY = cv::Mat::zeros(static_cast<int>(B), static_cast<int>(B), CV_64F);

        std::vector<double> cx(B), cy(B);
        for (size_t i = 0; i < N; ++i) {
            const double w = weights[i];
            const size_t p = validIndices[i];
            for (size_t b = 0; b < B; ++b) {
                cx[b] = static_cast<double>(beforeBands[b][p]) - meanX[b];
                cy[b] = static_cast<double>(afterBands[b][p]) - meanY[b];
            }
            for (size_t r = 0; r < B; ++r) {
                for (size_t c = 0; c < B; ++c) {
                    SXX.at<double>(static_cast<int>(r), static_cast<int>(c)) += w * cx[r] * cx[c];
                    SYY.at<double>(static_cast<int>(r), static_cast<int>(c)) += w * cy[r] * cy[c];
                    SXY.at<double>(static_cast<int>(r), static_cast<int>(c)) += w * cx[r] * cy[c];
                }
            }
        }

        SXX *= covScale;
        SYY *= covScale;
        SXY *= covScale;

        // Trace-scaled diagonal regularization
        const double epsXX = 1e-6 * cv::trace(SXX)[0] / static_cast<double>(B);
        const double epsYY = 1e-6 * cv::trace(SYY)[0] / static_cast<double>(B);
        for (size_t b = 0; b < B; ++b) {
            SXX.at<double>(static_cast<int>(b), static_cast<int>(b)) += std::max(epsXX, 1e-12);
            SYY.at<double>(static_cast<int>(b), static_cast<int>(b)) += std::max(epsYY, 1e-12);
        }

        const cv::Mat SXXInvSqrt = madSqrtInv(SXX);
        const cv::Mat SYYInvSqrt = madSqrtInv(SYY);
        if (cv::countNonZero(SXXInvSqrt) == 0 || cv::countNonZero(SYYInvSqrt) == 0) {
            break;
        }

        const cv::Mat H = SXXInvSqrt * SXY * SYYInvSqrt;
        cv::Mat D, Uh, VhT;
        cv::SVD::compute(H, D, Uh, VhT);

        cv::Mat A = SXXInvSqrt * Uh;
        cv::Mat Bmat = SYYInvSqrt * VhT.t();

        // Ensure canonical variate pairs are positively correlated
        for (size_t k = 0; k < B; ++k) {
            const cv::Mat covK = A.col(static_cast<int>(k)).t() * SXY * Bmat.col(static_cast<int>(k));
            if (covK.at<double>(0, 0) < 0.0)
                Bmat.col(static_cast<int>(k)) *= -1.0;
        }

        std::vector<double> curRho(B);
        std::vector<double> curVarMad(B);
        for (size_t k = 0; k < B; ++k) {
            curRho[k] = std::clamp(D.at<double>(static_cast<int>(k)), 0.0, 1.0);
            curVarMad[k] = std::max(2.0 * (1.0 - curRho[k]), 1e-6);
        }

        A_final = A;
        B_final = Bmat;
        varMad_final = curVarMad;
        meanX_final = meanX;
        meanY_final = meanY;

        // Check convergence
        double maxDeltaRho = 0.0;
        for (size_t k = 0; k < B; ++k) {
            maxDeltaRho = std::max(maxDeltaRho, std::abs(curRho[k] - prevRho[k]));
        }
        prevRho = curRho;

        if (iter > 0 && maxDeltaRho < convThreshold) {
            break; // Converged!
        }

        if (iter + 1 < maxIterations) {
            // Update Chi-square weights for next iteration
            for (size_t i = 0; i < N; ++i) {
                const size_t p = validIndices[i];
                double chiSquare = 0.0;
                for (size_t k = 0; k < B; ++k) {
                    double uk = 0.0, vk = 0.0;
                    for (size_t a = 0; a < B; ++a) {
                        uk += A.at<double>(static_cast<int>(a), static_cast<int>(k))
                              * (static_cast<double>(beforeBands[a][p]) - meanX[a]);
                        vk += Bmat.at<double>(static_cast<int>(a), static_cast<int>(k))
                              * (static_cast<double>(afterBands[a][p]) - meanY[a]);
                    }
                    const double m = uk - vk;
                    chiSquare += (m * m) / curVarMad[k];
                }
                weights[i] = chiSquareUpperCdf(static_cast<double>(B), chiSquare);
            }
        }
    }

    if ( A_final.empty() || B_final.empty() || varMad_final.empty() || meanX_final.empty() || meanY_final.empty() )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Algorithms, "IR-MAD failed: degenerate weights or empty transformation matrix" );
        if ( errorMessage )
            *errorMessage = QStringLiteral( "IR-MAD failed: degenerate weights or empty transformation matrix" );
        return false;
    }

    // Final transformation
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::fill_n(outChiSquare, pixels, nan);

    for (size_t i = 0; i < N; ++i) {
        const size_t p = validIndices[i];
        double chiSquare = 0.0;
        for (size_t k = 0; k < B; ++k) {
            double uk = 0.0, vk = 0.0;
            for (size_t a = 0; a < B; ++a) {
                uk += A_final.at<double>(static_cast<int>(a), static_cast<int>(k))
                      * (static_cast<double>(beforeBands[a][p]) - meanX_final[a]);
                vk += B_final.at<double>(static_cast<int>(a), static_cast<int>(k))
                      * (static_cast<double>(afterBands[a][p]) - meanY_final[a]);
            }
            const double m = uk - vk;
            chiSquare += (m * m) / varMad_final[k];
        }
        outChiSquare[p] = static_cast<float>(chiSquare);
    }

    return true;
}

} // namespace ChangeDetection
