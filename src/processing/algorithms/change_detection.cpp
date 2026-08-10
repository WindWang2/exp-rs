// src/processing/algorithms/change_detection.cpp — Change detection algorithms
#include "change_detection.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>
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
        out[i] = (before[i] == 0.0f) ? nan : after[i] / before[i];

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
    int bestBin = 0;
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
            bestBin = b;
        }
    }

    *threshold = static_cast<float>(minVal + (bestBin + 0.5) * range / bins);
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
                minVal + (static_cast<double>(b) + std::clamp(frac, 0.0, 1.0)) * range / bins);
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

    // Union-find over the 1-pixels with 8-neighbourhood connectivity.
    std::vector<int> parent(count, -1);
    std::vector<size_t> area(count, 0);

    for (size_t i = 0; i < count; ++i) {
        if (mask[i] == 1) {
            parent[i] = static_cast<int>(i);
            area[i] = 1;
        }
    }

    const auto find = [&parent](int x) {
        int root = x;
        while (parent[root] != root)
            root = parent[root];
        while (parent[x] != root) {
            const int next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    };
    const auto unite = [&](int a, int b) {
        int ra = find(a);
        const int rb = find(b);
        if (ra != rb) {
            parent[rb] = ra;
            area[ra] += area[rb];
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
        if (mask[i] == 1 && area[find(static_cast<int>(i))] < minArea)
            mask[i] = 0;
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

} // namespace ChangeDetection
