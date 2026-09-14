// feature_matcher.cpp — D14 Package D implementation (ADR 0159).
#include "processing/algorithms/feature_matcher.h"

#include "processing/algorithms/detail/linalg.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>

namespace rs::algorithms {

namespace {

using detail::dltHomography;
using detail::homographyReprojection;

constexpr int kDescriptorDim = 32;       // 2x2 cells x 8 orientations (Sift-like)
constexpr int kOrbPatchRadius = 4;       // 9x9 intensity patch (Orb-like)
constexpr double kGridSpacing = 8.0;     // keypoint sampling grid step

double l2(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return std::sqrt(sum);
}

void l2Normalize(std::vector<float>& v)
{
    double sum = 0.0;
    for (const float value : v)
        sum += static_cast<double>(value) * value;
    const double norm = std::sqrt(sum);
    if (norm < 1e-12)
        return;
    for (float& value : v)
        value = static_cast<float>(value / norm);
}

/// Deterministic synthetic texture: sums of oriented sines plus a seeded LCG
/// blob pattern. Used by matchImages tests but shipped here so the sampling
/// seam stays exercised end to end.
double gradientMagnitudeAt(const float* data, int width, int height, int x, int y)
{
    if (x <= 0 || y <= 0 || x >= width - 1 || y >= height - 1)
        return 0.0;
    const double dx = data[y * width + x + 1] - data[y * width + x - 1];
    const double dy = data[(y + 1) * width + x] - data[(y - 1) * width + x];
    return std::hypot(dx, dy);
}

/// Simplified Sift-like descriptor: gradient orientation histogram over a
/// 2x2 cell grid (8 orientations each), L2-normalized.
std::vector<float> siftLikeDescriptor(const float* data, int width, int height, int cx, int cy)
{
    std::vector<float> desc(kDescriptorDim, 0.0f);
    const int radius = 2 * kOrbPatchRadius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x <= 0 || y <= 0 || x >= width - 1 || y >= height - 1)
                continue;
            const double gx = data[y * width + x + 1] - data[y * width + x - 1];
            const double gy = data[(y + 1) * width + x] - data[(y - 1) * width + x];
            const double magnitude = std::hypot(gx, gy);
            if (magnitude < 1e-12)
                continue;
            double angle = std::atan2(gy, gx);
            if (angle < 0.0)
                angle += 2.0 * std::numbers::pi;
            const int bin = std::min(7, static_cast<int>(angle / (2.0 * std::numbers::pi) * 8.0));
            const int cell = (dy > 0 ? 1 : 0) * 2 + (dx > 0 ? 1 : 0);
            desc[cell * 8 + bin] += static_cast<float>(magnitude);
        }
    }
    l2Normalize(desc);
    return desc;
}

/// Simplified Orb-like descriptor: mean-centered intensity patch, L2-normalized.
std::vector<float> orbLikeDescriptor(const float* data, int width, int height, int cx, int cy)
{
    const int side = 2 * kOrbPatchRadius + 1;
    std::vector<float> desc(static_cast<size_t>(side) * side, 0.0f);
    double mean = 0.0;
    int count = 0;
    for (int dy = -kOrbPatchRadius; dy <= kOrbPatchRadius; ++dy) {
        for (int dx = -kOrbPatchRadius; dx <= kOrbPatchRadius; ++dx) {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || y < 0 || x >= width || y >= height)
                continue;
            mean += data[y * width + x];
            ++count;
        }
    }
    if (count == 0)
        return desc;
    mean /= count;
    size_t index = 0;
    for (int dy = -kOrbPatchRadius; dy <= kOrbPatchRadius; ++dy) {
        for (int dx = -kOrbPatchRadius; dx <= kOrbPatchRadius; ++dx) {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || y < 0 || x >= width || y >= height) {
                ++index;
                continue;
            }
            desc[index++] = static_cast<float>(data[y * width + x] - mean);
        }
    }
    l2Normalize(desc);
    return desc;
}

} // namespace

FeatureMatcher::DescriptorHit FeatureMatcher::ratioMatch(const std::vector<float>& query,
                                                         const std::vector<std::vector<float>>& trainSet)
{
    DescriptorHit hit;
    double best = std::numeric_limits<double>::infinity();
    double second = std::numeric_limits<double>::infinity();
    for (size_t j = 0; j < trainSet.size(); ++j) {
        const double d = l2(query, trainSet[j]);
        if (d < best) {
            second = best;
            best = d;
            hit.best = static_cast<int>(j);
        } else if (d < second) {
            second = d;
        }
    }
    hit.bestDist = best;
    hit.secondDist = second;
    return hit;
}

FeatureMatchReport FeatureMatcher::matchDescriptors(const std::vector<KeyPoint2D>& srcKps,
                                                    const std::vector<std::vector<float>>& srcDesc,
                                                    const std::vector<KeyPoint2D>& dstKps,
                                                    const std::vector<std::vector<float>>& dstDesc,
                                                    const FeatureMatchOptions& options)
{
    FeatureMatchReport report;
    report.homographyMatrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (srcKps.empty() || dstKps.empty())
        return report;
    if (srcKps.size() != srcDesc.size() || dstKps.size() != dstDesc.size())
        return report;

    // Sortable candidate order keeps maxFeatures deterministic (best response first).
    std::vector<size_t> order(srcKps.size());
    std::iota(order.begin(), order.end(), 0);
    const int maxFeatures = std::max(0, options.maxFeatures);
    if (static_cast<int>(order.size()) > maxFeatures) {
        std::ranges::sort(order, [&](size_t a, size_t b) {
            return srcKps[a].response > srcKps[b].response;
        });
        order.resize(static_cast<size_t>(maxFeatures));
    }

    std::vector<std::vector<float>> normalizedDst = dstDesc;
    for (auto& d : normalizedDst)
        l2Normalize(d);

    std::vector<std::pair<double, double>> srcPts;
    std::vector<std::pair<double, double>> dstPts;
    for (const size_t i : order) {
        std::vector<float> query = srcDesc[i];
        l2Normalize(query);
        const DescriptorHit hit = ratioMatch(query, normalizedDst);
        if (hit.best < 0)
            continue;
        if (hit.secondDist > 0.0 && hit.bestDist / hit.secondDist >= options.loweRatioThreshold)
            continue; // ambiguous correspondence
        MatchPair pair;
        pair.srcPt = srcKps[i];
        pair.dstPt = dstKps[static_cast<size_t>(hit.best)];
        pair.distance = hit.bestDist;
        report.matches.push_back(pair);
        srcPts.emplace_back(pair.srcPt.x, pair.srcPt.y);
        dstPts.emplace_back(pair.dstPt.x, pair.dstPt.y);
    }
    report.totalCandidates = static_cast<int>(report.matches.size());
    if (report.totalCandidates < 4)
        return report;

    auto [H, mask] = estimateHomographyRansac(srcPts, dstPts, options.ransacReprojThreshold,
                                              options.ransacMaxIters, options.ransacConfidence);
    report.homographyMatrix = H;

    double sumSq = 0.0;
    for (size_t i = 0; i < report.matches.size(); ++i) {
        report.matches[i].inlier = mask[i];
        if (mask[i]) {
            ++report.inlierCount;
            const double err = homographyReprojection(H, srcPts[i], dstPts[i]);
            sumSq += err * err;
        }
    }
    report.inlierRatio = static_cast<double>(report.inlierCount) / report.matches.size();
    report.inlierRmse = report.inlierCount > 0 ? std::sqrt(sumSq / report.inlierCount) : 0.0;
    return report;
}

std::pair<std::array<double, 9>, std::vector<bool>>
FeatureMatcher::estimateHomographyRansac(const std::vector<std::pair<double, double>>& srcPts,
                                         const std::vector<std::pair<double, double>>& dstPts,
                                         double reprojThreshold,
                                         int maxIters,
                                         double confidence)
{
    std::array<double, 9> identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (srcPts.size() != dstPts.size() || srcPts.size() < 4)
        return {identity, std::vector<bool>(srcPts.size(), false)};

    // Degenerate guard: 4 collinear anchors can never define H.
    std::mt19937 rng(42u);
    std::uniform_int_distribution<size_t> pick(0, srcPts.size() - 1);

    std::array<double, 9> bestH = identity;
    std::vector<bool> bestMask(srcPts.size(), false);
    int bestCount = 0;
    int iterBudget = std::max(1, maxIters);
    int iter = 0;
    std::vector<size_t> sample(4);

    while (iter < iterBudget) {
        ++iter;
        // Sample 4 distinct indices.
        sample.clear();
        while (sample.size() < 4) {
            const size_t idx = pick(rng);
            if (std::find(sample.begin(), sample.end(), idx) == sample.end())
                sample.push_back(idx);
        }
        std::vector<std::pair<double, double>> hs, hd;
        for (const size_t idx : sample) {
            hs.push_back(srcPts[idx]);
            hd.push_back(dstPts[idx]);
        }
        std::array<double, 9> H = identity;
        double kappa = 0.0;
        if (!dltHomography(hs, hd, H, kappa))
            continue;

        std::vector<bool> mask(srcPts.size(), false);
        int count = 0;
        for (size_t i = 0; i < srcPts.size(); ++i) {
            if (homographyReprojection(H, srcPts[i], dstPts[i]) <= reprojThreshold) {
                mask[i] = true;
                ++count;
            }
        }
        if (count > bestCount) {
            bestCount = count;
            bestMask = mask;
            bestH = H;
            // Adaptive iteration cap: K = ln(1-p)/ln(1-(1-n)⁴), s = 4.
            const double w = static_cast<double>(count) / static_cast<double>(srcPts.size());
            const double denom = 1.0 - std::pow(w, 4.0);
            if (denom <= std::numeric_limits<double>::epsilon()) {
                iterBudget = iter; // all-inlier sample found
            } else {
                const double needed = std::log(1.0 - std::clamp(confidence, 0.0, 0.9999)) /
                                      std::log(std::clamp(denom, 1e-12, 1.0));
                iterBudget = std::min(std::max(1, maxIters), std::max(iter, static_cast<int>(std::ceil(needed))));
            }
        }
    }

    if (bestCount < 4)
        return {identity, std::vector<bool>(srcPts.size(), false)};

    // Least-squares refit on all inliers, then a final mask refresh.
    std::vector<std::pair<double, double>> inSrc, inDst;
    for (size_t i = 0; i < srcPts.size(); ++i) {
        if (bestMask[i]) {
            inSrc.push_back(srcPts[i]);
            inDst.push_back(dstPts[i]);
        }
    }
    std::array<double, 9> refined = bestH;
    double kappa = 0.0;
    if (dltHomography(inSrc, inDst, refined, kappa))
        bestH = refined;

    std::vector<bool> mask(srcPts.size(), false);
    int count = 0;
    for (size_t i = 0; i < srcPts.size(); ++i) {
        if (homographyReprojection(bestH, srcPts[i], dstPts[i]) <= reprojThreshold) {
            mask[i] = true;
            ++count;
        }
    }
    return {bestH, mask};
}

FeatureMatchReport FeatureMatcher::matchImages(const float* srcData, int srcWidth, int srcHeight,
                                               const float* dstData, int dstWidth, int dstHeight,
                                               const FeatureMatchOptions& options)
{
    FeatureMatchReport report;
    report.homographyMatrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (!srcData || !dstData || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0)
        return report;

    const auto collectKeyPoints = [&](const float* data, int width, int height) {
        std::vector<KeyPoint2D> kps;
        std::vector<std::vector<float>> desc;
        const int step = static_cast<int>(kGridSpacing);
        for (int y = 2 * kOrbPatchRadius; y < height - 2 * kOrbPatchRadius; y += step) {
            for (int x = 2 * kOrbPatchRadius; x < width - 2 * kOrbPatchRadius; x += step) {
                KeyPoint2D kp;
                kp.x = x;
                kp.y = y;
                kp.size = 2.0 * kOrbPatchRadius;
                kp.response = gradientMagnitudeAt(data, width, height, x, y);
                if (kp.response < 1e-6)
                    continue;
                kp.octave = 0;
                kps.push_back(kp);
                desc.push_back(options.detector == FeatureDetectorType::Sift
                                   ? siftLikeDescriptor(data, width, height, x, y)
                                   : orbLikeDescriptor(data, width, height, x, y));
            }
        }
        return std::make_pair(kps, desc);
    };

    const auto [srcKps, srcDesc] = collectKeyPoints(srcData, srcWidth, srcHeight);
    const auto [dstKps, dstDesc] = collectKeyPoints(dstData, dstWidth, dstHeight);
    return matchDescriptors(srcKps, srcDesc, dstKps, dstDesc, options);
}

} // namespace rs::algorithms
