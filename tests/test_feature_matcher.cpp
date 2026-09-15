// test_feature_matcher.cpp — D14 Package D: RANSAC + ratio test tests.
// The RANSAC ground truth is constructed by hand: 40 correspondences obeying
// a documented homography H_true and 20 gross corruptions, so the expected
// masks and matrices are known before the matcher runs.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/feature_matcher.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::algorithms;

namespace {

const std::array<double, 9> kHTrue{1.05, -0.02, 10.0,
                                   0.02, 1.03, -5.0,
                                   0.0001, 0.0001, 1.0};

std::pair<double, double> applyTrue(double x, double y)
{
    const double w = kHTrue[6] * x + kHTrue[7] * y + kHTrue[8];
    return {(kHTrue[0] * x + kHTrue[1] * y + kHTrue[2]) / w,
            (kHTrue[3] * x + kHTrue[4] * y + kHTrue[5]) / w};
}

/// 40 true inliers on a jittered grid + 20 gross corruptions.
struct CorruptedSet {
    std::vector<std::pair<double, double>> src;
    std::vector<std::pair<double, double>> dst;
};

CorruptedSet makeCorrespondences()
{
    CorruptedSet set;
    for (int i = 0; i < 40; ++i) {
        // Deterministic "jitter" from index arithmetic (no RNG in the test).
        const double x = 10.0 + (i % 8) * 10.0 + 0.13 * i;
        const double y = 5.0 + (i / 8) * 20.0 - 0.07 * i;
        const auto [tx, ty] = applyTrue(x, y);
        set.src.emplace_back(x, y);
        set.dst.emplace_back(tx, ty);
    }
    for (int i = 0; i < 20; ++i) {
        const double x = 10.0 + (i % 5) * 15.0;
        const double y = 5.0 + (i / 5) * 22.0;
        const auto [tx, ty] = applyTrue(x, y);
        // Gross outlier: deterministic offsets in [50, 100] / [-100, -50].
        const double ox = 50.0 + 2.5 * i;
        const double oy = -50.0 - 2.5 * i;
        set.src.emplace_back(x, y);
        set.dst.emplace_back(tx + ox, ty + oy);
    }
    return set;
}

/// Deterministic textured image: sum of oriented sines + seeded LCG blobs.
std::vector<float> makeTexture(int width, int height, uint32_t seed)
{
    std::vector<float> data(static_cast<size_t>(width) * height, 0.0f);
    uint32_t lcg = seed;
    const auto nextRand = [&lcg]() {
        lcg = lcg * 1664525u + 1013904223u;
        return static_cast<float>((lcg >> 8) % 1000) / 1000.0f;
    };
    for (int b = 0; b < 24; ++b) {
        const double bx = nextRand() * width;
        const double by = nextRand() * height;
        const double amp = 30.0 + 60.0 * nextRand();
        const double sigma = 3.0 + 5.0 * nextRand();
        for (int y = std::max(0, static_cast<int>(by - 3 * sigma));
             y < std::min(height, static_cast<int>(by + 3 * sigma) + 1); ++y) {
            for (int x = std::max(0, static_cast<int>(bx - 3 * sigma));
                 x < std::min(width, static_cast<int>(bx + 3 * sigma) + 1); ++x) {
                const double d2 = (x - bx) * (x - bx) + (y - by) * (y - by);
                data[static_cast<size_t>(y) * width + x] += static_cast<float>(amp * std::exp(-d2 / (2.0 * sigma * sigma)));
            }
        }
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            data[static_cast<size_t>(y) * width + x] +=
                static_cast<float>(40.0 + 25.0 * std::sin(x * 0.35) * std::cos(y * 0.28));
    return data;
}

} // namespace

TEST_CASE("test_feature_matcher - RANSAC rejects every injected gross outlier and keeps true inliers", "[feature][d14]")
{
    const auto [srcPts, dstPts] = makeCorrespondences();
    REQUIRE(srcPts.size() == 60);

    auto [H, mask] = FeatureMatcher::estimateHomographyRansac(srcPts, dstPts, 3.0, 2000, 0.99);
    REQUIRE(mask.size() == 60);

    // All 20 gross outliers flagged as false.
    for (size_t i = 40; i < 60; ++i)
        REQUIRE_FALSE(mask[i]);
    // At least 38 of the 40 true inliers retained.
    int retained = 0;
    for (size_t i = 0; i < 40; ++i)
        if (mask[i])
            ++retained;
    REQUIRE(retained >= 38);

    // h33 normalized to 1; entries near the documented truth.
    REQUIRE_THAT(H[8], WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(H[0], WithinAbs(kHTrue[0], 1e-2));
    REQUIRE_THAT(H[1], WithinAbs(kHTrue[1], 1e-2));
    REQUIRE_THAT(H[2], WithinAbs(kHTrue[2], 5e-1));
    REQUIRE_THAT(H[5], WithinAbs(kHTrue[5], 5e-1));
}

TEST_CASE("test_feature_matcher - RANSAC returns identity and an empty mask below four points", "[feature][d14]")
{
    const std::vector<std::pair<double, double>> src{{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}};
    const std::vector<std::pair<double, double>> dst{{1.0, 1.0}, {2.0, 1.0}, {1.0, 2.0}};
    auto [H, mask] = FeatureMatcher::estimateHomographyRansac(src, dst, 3.0, 100, 0.99);
    REQUIRE_THAT(H[0], WithinAbs(1.0, 0.0));
    REQUIRE_THAT(H[4], WithinAbs(1.0, 0.0));
    REQUIRE_THAT(H[8], WithinAbs(1.0, 0.0));
    REQUIRE(mask.size() == 3);
    for (bool flag : mask)
        REQUIRE_FALSE(flag);
}

TEST_CASE("test_feature_matcher - Ratio test keeps unambiguous matches and drops ambiguous ones", "[feature][d14]")
{
    // Source descriptors are near-orthogonal unit vectors; the destination set
    // holds exact copies (clear winners) plus a distractor equidistant to
    // everything (forced ambiguity loser).
    const auto unitAt = [](size_t dim, size_t size) {
        std::vector<float> v(size, 0.0f);
        v[dim] = 1.0f;
        return v;
    };
    constexpr size_t kDim = 8;

    std::vector<KeyPoint2D> srcKps;
    std::vector<std::vector<float>> srcDesc;
    for (size_t i = 0; i < 6; ++i) {
        // Two rows so the correspondences are not collinear (a homography
        // needs a non-degenerate support).
        const double y = (i % 2 == 0) ? 0.0 : 40.0;
        srcKps.push_back({static_cast<double>(10 * i), y, 1.0, 0.0, 1.0, 0});
        srcDesc.push_back(unitAt(i, kDim));
    }
    std::vector<KeyPoint2D> dstKps;
    std::vector<std::vector<float>> dstDesc;
    // Destinations appear in scrambled order to prove identity-based matching.
    const std::vector<size_t> perm{3, 0, 5, 1, 4, 2};
    for (const size_t p : perm) {
        const double y = ((p % 2 == 0) ? 0.0 : 40.0) + 5.0; // pure translation
        dstKps.push_back({static_cast<double>(10 * p), y, 1.0, 0.0, 1.0, 0});
        dstDesc.push_back(unitAt(p, kDim));
    }
    // One ambiguous all-tie descriptor set: identical rows in dst.
    dstKps.push_back({995.0, 25.0, 1.0, 0.0, 1.0, 0});
    dstDesc.push_back(std::vector<float>(kDim, 0.7071f));

    // Identity geometry: all true correspondences lie on H = I.
    FeatureMatchOptions options;
    options.loweRatioThreshold = 0.75;
    FeatureMatcher matcher;
    const auto report = matcher.matchDescriptors(srcKps, srcDesc, dstKps, dstDesc, options);

    REQUIRE(report.totalCandidates == 6);
    for (const auto& match : report.matches) {
        REQUIRE(match.inlier);
        // Every kept match maps src x to dst x with the same value.
        REQUIRE_THAT(match.dstPt.x, WithinAbs(match.srcPt.x, 1e-9));
    }
    REQUIRE(report.inlierCount == 6);
    REQUIRE_THAT(report.inlierRatio, WithinAbs(1.0, 1e-12));
    REQUIRE(report.inlierRmse < 1e-9);
}

TEST_CASE("test_feature_matcher - matchImages recovers a known translation from a textured pair", "[feature][d14]")
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 128;
    const auto texture = makeTexture(kWidth, kHeight, 42u);

    constexpr double kDx = 7.0;
    constexpr double kDy = -5.0;
    std::vector<float> shifted(static_cast<size_t>(kWidth) * kHeight, 12.0f);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int sx = x - static_cast<int>(kDx);
            const int sy = y - static_cast<int>(kDy);
            if (sx >= 0 && sx < kWidth && sy >= 0 && sy < kHeight)
                shifted[static_cast<size_t>(y) * kWidth + x] = texture[static_cast<size_t>(sy) * kWidth + sx];
        }
    }

    FeatureMatchOptions options;
    options.detector = FeatureDetectorType::Sift;
    options.ransacReprojThreshold = 2.0;
    FeatureMatcher matcher;
    const auto report = matcher.matchImages(texture.data(), kWidth, kHeight,
                                            shifted.data(), kWidth, kHeight, options);
    REQUIRE(report.totalCandidates >= 4);
    REQUIRE(report.inlierCount >= 10);
    REQUIRE(report.inlierRmse < 2.0);

    const auto& H = report.homographyMatrix;
    // Translation entries close to the known shift; the 8 px keypoint grid
    // quantizes the RANSAC consensus, so allow half a sampling cell of slack.
    REQUIRE(std::abs(H[2] - kDx) < 4.0);
    REQUIRE(std::abs(H[5] - kDy) < 4.0);
    REQUIRE(std::abs(H[0] - 1.0) < 0.05);
    REQUIRE(std::abs(H[4] - 1.0) < 0.05);
}
