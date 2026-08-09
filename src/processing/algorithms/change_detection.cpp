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

    // Otsu: maximize between-class variance over cumulative histogram sums.
    const double total = static_cast<double>(finite);
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

    // Step 1: Collect valid finite pixels across all bands
    std::vector<size_t> validIndices;
    validIndices.reserve(pixels);
    for (size_t i = 0; i < pixels; ++i) {
        bool valid = true;
        for (int b = 0; b < bandCount; ++b) {
            if (!std::isfinite(beforeBands[b][i]) || !std::isfinite(afterBands[b][i])) {
                valid = false;
                break;
            }
        }
        if (valid) {
            validIndices.push_back(i);
        }
    }

    const size_t N = validIndices.size();
    if (N < static_cast<size_t>(bandCount + 2)) {
        std::fill_n(out, pixels, std::numeric_limits<float>::quiet_NaN());
        if (errorMessage) *errorMessage = QStringLiteral("Insufficient valid pixels for MAD calculation.");
        return false;
    }

    // Step 2: Compute means for Before (X) and After (Y)
    std::vector<double> meanX(bandCount, 0.0);
    std::vector<double> meanY(bandCount, 0.0);
    for (size_t idx : validIndices) {
        for (int b = 0; b < bandCount; ++b) {
            meanX[b] += beforeBands[b][idx];
            meanY[b] += afterBands[b][idx];
        }
    }
    for (int b = 0; b < bandCount; ++b) {
        meanX[b] /= N;
        meanY[b] /= N;
    }

    // Step 3: Build centered observation matrices X_mat and Y_mat using OpenCV
    cv::Mat X_mat(static_cast<int>(N), bandCount, CV_64F);
    cv::Mat Y_mat(static_cast<int>(N), bandCount, CV_64F);

    for (size_t r = 0; r < N; ++r) {
        size_t idx = validIndices[r];
        double* rowX = X_mat.ptr<double>(static_cast<int>(r));
        double* rowY = Y_mat.ptr<double>(static_cast<int>(r));
        for (int b = 0; b < bandCount; ++b) {
            rowX[b] = static_cast<double>(beforeBands[b][idx]) - meanX[b];
            rowY[b] = static_cast<double>(afterBands[b][idx]) - meanY[b];
        }
    }

    // Covariance matrices: S_XX = (X^T * X) / (N-1), S_YY = (Y^T * Y) / (N-1), S_XY = (X^T * Y) / (N-1)
    double scale = 1.0 / static_cast<double>(N - 1);
    cv::Mat S_XX = scale * (X_mat.t() * X_mat);
    cv::Mat S_YY = scale * (Y_mat.t() * Y_mat);
    cv::Mat S_XY = scale * (X_mat.t() * Y_mat);

    // Add trace-scaled regularization to diagonal for numerical stability
    // across both DN-valued (1e+3..1e+4) and reflectance-valued (0..1) imagery
    const double epsXX = 1e-6 * cv::trace(S_XX)[0] / bandCount;
    const double epsYY = 1e-6 * cv::trace(S_YY)[0] / bandCount;
    for (int b = 0; b < bandCount; ++b) {
        S_XX.at<double>(b, b) += std::max(epsXX, 1e-12);
        S_YY.at<double>(b, b) += std::max(epsYY, 1e-12);
    }

    // Compute S_XX^(-1/2) and S_YY^(-1/2) using SVD
    const auto computeSqrtInv = [](const cv::Mat& M) -> cv::Mat {
        cv::Mat w, u, vt;
        cv::SVD::compute(M, w, u, vt);
        cv::Mat w_inv_sqrt = cv::Mat::zeros(M.rows, M.cols, CV_64F);
        for (int i = 0; i < M.rows; ++i) {
            double val = w.at<double>(i);
            w_inv_sqrt.at<double>(i, i) = (val > 1e-12) ? (1.0 / std::sqrt(val)) : 0.0;
        }
        return u * w_inv_sqrt * vt;
    };

    cv::Mat S_XX_inv_sqrt = computeSqrtInv(S_XX);
    cv::Mat S_YY_inv_sqrt = computeSqrtInv(S_YY);

    // Transformed matrix H = S_XX^(-1/2) * S_XY * S_YY^(-1/2)
    cv::Mat H = S_XX_inv_sqrt * S_XY * S_YY_inv_sqrt;

    // SVD of H: H = U_h * D * V_h^T
    cv::Mat D, U_h, V_h_t;
    cv::SVD::compute(H, D, U_h, V_h_t);

    // Canonical coefficients: A = S_XX^(-1/2) * U_h, B = S_YY^(-1/2) * V_h
    cv::Mat A = S_XX_inv_sqrt * U_h;
    cv::Mat B = S_YY_inv_sqrt * V_h_t.t();

    // Ensure canonical variate pairs are positively correlated: A[:, k]^T * S_XY * B[:, k] > 0
    for (int k = 0; k < bandCount; ++k) {
        cv::Mat ak = A.col(k);
        cv::Mat bk = B.col(k);
        cv::Mat cov_k = ak.t() * S_XY * bk;
        if (cov_k.at<double>(0, 0) < 0.0) {
            B.col(k) *= -1.0;
        }
    }

    // Canonical correlations lambda_i and MAD variate variances
    std::vector<double> lambda(bandCount, 0.0);
    std::vector<double> varMad(bandCount, 0.0);
    for (int i = 0; i < bandCount; ++i) {
        lambda[i] = std::clamp(D.at<double>(i), 0.0, 1.0);
        varMad[i] = std::max(2.0 * (1.0 - lambda[i]), 1e-6);
    }

    // Step 4: Pre-compute all canonical variates via matrix multiplication: O(N × B)
    // U_all = X_mat * A  (N × B),  V_all = Y_mat * B  (N × B)
    cv::Mat U_all = X_mat * A;
    cv::Mat V_all = Y_mat * B;

    // Compute Chi-Square change magnitude Z for each pixel
    std::fill_n(out, pixels, std::numeric_limits<float>::quiet_NaN());

    for (size_t r = 0; r < N; ++r) {
        const double* rowU = U_all.ptr<double>(static_cast<int>(r));
        const double* rowV = V_all.ptr<double>(static_cast<int>(r));
        double chiSquare = 0.0;
        for (int k = 0; k < bandCount; ++k) {
            const double m_k = rowU[k] - rowV[k]; // MAD variate
            chiSquare += (m_k * m_k) / varMad[k];
        }
        out[validIndices[r]] = static_cast<float>(chiSquare);
    }

    return true;
}

} // namespace ChangeDetection
