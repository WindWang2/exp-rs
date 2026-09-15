// test_multimodal_matcher.cpp — F13 Packages A + B: cross-modal matcher
// known-answer tests. All ground-truth offsets are documented constants and
// the synthetic scenes are composed from closed-form texture + deterministic
// LCG speckle. Nothing is derived from the code under test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/registration/multimodal_matcher.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace sicnu::registration;

namespace {

constexpr int kDim = 256;

/// Deterministic grain in [-1, 1] (splitmix-style hash; identical on every
/// platform and every run).
float grain(int x, int y)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                      + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
}

/// Smooth multi-frequency texture with a little grain — enough structure for
/// phase correlation and a multi-modal histogram for MI.
float texture(int x, int y)
{
    const double fx = static_cast<double>(x);
    const double fy = static_cast<double>(y);
    const double v = 50.0 + 20.0 * std::sin(fx / 9.3) * std::sin(fy / 7.7)
                     + 10.0 * std::sin((fx + fy) / 5.1) + 8.0 * std::sin(fx / 3.7)
                     + 3.0 * grain(x, y);
    return static_cast<float>(v);
}

std::vector<float> makeScene(int dim)
{
    std::vector<float> img(static_cast<std::size_t>(dim) * dim);
    for (int y = 0; y < dim; ++y)
        for (int x = 0; x < dim; ++x)
            img[static_cast<std::size_t>(y) * dim + x] = texture(x, y);
    return img;
}

/// Bilinear sample of img shifted by (dx, dy): out(x, y) = img(x - dx, y - dy).
float sampleShifted(const std::vector<float>& img, int dim, double dx, double dy, int x, int y)
{
    const double sx = static_cast<double>(x) - dx;
    const double sy = static_cast<double>(y) - dy;
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const double fx = sx - x0;
    const double fy = sy - y0;
    auto at = [&](int xx, int yy) -> double {
        const int cx = std::max(0, std::min(dim - 1, xx));
        const int cy = std::max(0, std::min(dim - 1, yy));
        return img[static_cast<std::size_t>(cy) * dim + cx];
    };
    return static_cast<float>((1 - fx) * (1 - fy) * at(x0, y0) + fx * (1 - fy) * at(x0 + 1, y0)
                              + (1 - fx) * fy * at(x0, y0 + 1) + fx * fy * at(x0 + 1, y0 + 1));
}

/// Median of (dst - src) offsets over inlier points — the observed shift.
std::pair<double, double> observedShift(const MultimodalMatchReport& rep)
{
    std::vector<double> ox, oy;
    for (const auto& p : rep.points) {
        if (!p.inlier)
            continue;
        ox.push_back(p.dstX - p.srcX);
        oy.push_back(p.dstY - p.srcY);
    }
    auto med = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    };
    return {med(ox), med(oy)};
}

} // namespace

TEST_CASE("multimodal: integer translation recovered exactly", "[f13][matcher]")
{
    const auto src = makeScene(kDim);
    std::vector<float> dst(static_cast<std::size_t>(kDim) * kDim);
    constexpr double kDx = 7.0, kDy = -5.0;
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x)
            dst[static_cast<std::size_t>(y) * kDim + x] = sampleShifted(src, kDim, kDx, kDy, x, y);

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::PhaseCorrelation;
    const auto rep = MultimodalMatcher::matchImages(src.data(), kDim, kDim, dst.data(), kDim, kDim,
                                                    opt);
    REQUIRE(rep.status == RegistrationStatus::Success);
    REQUIRE(rep.inlierCount >= opt.minMatches);
    const auto [ox, oy] = observedShift(rep);
    REQUIRE(ox == Catch::Approx(kDx).margin(0.5));
    REQUIRE(oy == Catch::Approx(kDy).margin(0.5));
}

TEST_CASE("multimodal: subpixel translation within tolerance", "[f13][matcher]")
{
    const auto src = makeScene(kDim);
    std::vector<float> dst(static_cast<std::size_t>(kDim) * kDim);
    constexpr double kDx = 2.5, kDy = -1.5;
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x)
            dst[static_cast<std::size_t>(y) * kDim + x] = sampleShifted(src, kDim, kDx, kDy, x, y);

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::PhaseCorrelation;
    const auto rep = MultimodalMatcher::matchImages(src.data(), kDim, kDim, dst.data(), kDim, kDim,
                                                    opt);
    REQUIRE(rep.status == RegistrationStatus::Success);
    const auto [ox, oy] = observedShift(rep);
    REQUIRE(ox == Catch::Approx(kDx).margin(0.4));
    REQUIRE(oy == Catch::Approx(kDy).margin(0.4));
}

TEST_CASE("multimodal: pyramid handles offsets beyond the fine search radius", "[f13][matcher]")
{
    const auto src = makeScene(kDim);
    std::vector<float> dst(static_cast<std::size_t>(kDim) * kDim);
    constexpr double kDx = 21.0, kDy = 13.0; // >> searchRadius (6)
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x)
            dst[static_cast<std::size_t>(y) * kDim + x] = sampleShifted(src, kDim, kDx, kDy, x, y);

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::PhaseCorrelation;
    const auto rep = MultimodalMatcher::matchImages(src.data(), kDim, kDim, dst.data(), kDim, kDim,
                                                    opt);
    REQUIRE(rep.status == RegistrationStatus::Success);
    REQUIRE(rep.stages.size() >= 2); // pyramid actually engaged
    const auto [ox, oy] = observedShift(rep);
    REQUIRE(ox == Catch::Approx(kDx).margin(0.75));
    REQUIRE(oy == Catch::Approx(kDy).margin(0.75));
}

TEST_CASE("multimodal: MI locks through a monotone radiometric remap (optical-SAR seam)",
          "[f13][matcher]")
{
    const auto src = makeScene(kDim);
    std::vector<float> dst(static_cast<std::size_t>(kDim) * kDim);
    constexpr double kDx = 4.0, kDy = 6.0;
    // Cross-sensor proxy: log compression + affine rescale (monotone).
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x) {
            const double v = std::log1p(std::max(0.0, static_cast<double>(
                                     sampleShifted(src, kDim, kDx, kDy, x, y)))) * 30.0;
            dst[static_cast<std::size_t>(y) * kDim + x] = static_cast<float>(v);
        }

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::MutualInformation;
    opt.searchRadius = 8;
    const auto rep = MultimodalMatcher::matchImages(src.data(), kDim, kDim, dst.data(), kDim, kDim,
                                                    opt);
    REQUIRE(rep.status == RegistrationStatus::Success);
    const auto [ox, oy] = observedShift(rep);
    REQUIRE(ox == Catch::Approx(kDx).margin(0.75));
    REQUIRE(oy == Catch::Approx(kDy).margin(0.75));
}

TEST_CASE("multimodal: SAR-like multiplicative speckle still locks", "[f13][matcher]")
{
    const auto src = makeScene(kDim);
    std::vector<float> dst(static_cast<std::size_t>(kDim) * kDim);
    constexpr double kDx = 5.0, kDy = 9.0;
    // Speckle: multiplicative (1 + 0.4 * grain) — mean 1, deterministic.
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x) {
            const double s = static_cast<double>(sampleShifted(src, kDim, kDx, kDy, x, y));
            const double speckle = 1.0 + 0.4 * static_cast<double>(grain(x + 31, y - 17));
            dst[static_cast<std::size_t>(y) * kDim + x] = static_cast<float>(s * speckle);
        }

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::Auto;
    opt.searchRadius = 8;
    const auto rep = MultimodalMatcher::matchImages(src.data(), kDim, kDim, dst.data(), kDim, kDim,
                                                    opt);
    REQUIRE(rep.status == RegistrationStatus::Success);
    const auto [ox, oy] = observedShift(rep);
    REQUIRE(ox == Catch::Approx(kDx).margin(1.0));
    REQUIRE(oy == Catch::Approx(kDy).margin(1.0));
}

TEST_CASE("multimodal: flat scenes refuse instead of hallucinating a match",
          "[f13][matcher][negative]")
{
    std::vector<float> flat(static_cast<std::size_t>(kDim) * kDim, 42.0f);
    std::vector<float> flat2(static_cast<std::size_t>(kDim) * kDim, 17.0f);

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::PhaseCorrelation;
    const auto rep =
        MultimodalMatcher::matchImages(flat.data(), kDim, kDim, flat2.data(), kDim, kDim, opt);
    REQUIRE(rep.status == RegistrationStatus::Refused);
    REQUIRE(rep.reason == QStringLiteral("flat_region"));
    REQUIRE(rep.points.empty());
}

TEST_CASE("multimodal: structure-free cross-modal pair returns refusal, not a guess",
          "[f13][matcher][negative]")
{
    // Pure independent noise on both sides — no shared structure, so any
    // "match" would be a hallucination.
    std::vector<float> a(static_cast<std::size_t>(kDim) * kDim);
    std::vector<float> b(static_cast<std::size_t>(kDim) * kDim);
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x) {
            a[static_cast<std::size_t>(y) * kDim + x] = 50.f + 20.f * grain(x, y);
            b[static_cast<std::size_t>(y) * kDim + x] = 50.f + 20.f * grain(x + 911, y + 337);
        }
    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::Auto;
    // Independent white noise can produce a spurious phase peak — the trust
    // gate that must hold is: NOT a confident Success.
    const auto rep =
        MultimodalMatcher::matchImages(a.data(), kDim, kDim, b.data(), kDim, kDim, opt);
    REQUIRE(rep.status != RegistrationStatus::Success);
}

TEST_CASE("multimodal: clustered matches are flagged insufficient_coverage (Oracle)",
          "[f13][matcher][negative]")
{
    // Texture confined to a small patch: matches can only form there, so a
    // full-extent coverage grid must rate them as clustered.
    std::vector<float> src(static_cast<std::size_t>(kDim) * kDim, 10.0f);
    std::vector<float> dst(static_cast<std::size_t>(kDim) * kDim, 10.0f);
    for (int y = 40; y < 70; ++y)
        for (int x = 40; x < 70; ++x) {
            src[static_cast<std::size_t>(y) * kDim + x] = texture(x, y);
            dst[static_cast<std::size_t>(y + 3) * kDim + (x + 2)] = texture(x, y);
        }

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::PhaseCorrelation;
    opt.searchRadius = 6;
    const auto rep =
        MultimodalMatcher::matchImages(src.data(), kDim, kDim, dst.data(), kDim, kDim, opt);
    REQUIRE(rep.status == RegistrationStatus::LowConfidence);
    REQUIRE(rep.reason == QStringLiteral("insufficient_coverage"));
    // Evidence is still returned for the reviewer — flagged, not discarded.
    REQUIRE(rep.inlierCount >= opt.minMatches);
    REQUIRE(rep.coverageRatio < opt.minCoverageRatio);
}

TEST_CASE("multimodal: NoData windows are skipped, valid ones carry the match",
          "[f13][matcher]")
{
    const auto src = makeScene(kDim);
    std::vector<float> dst(static_cast<std::size_t>(kDim) * kDim);
    constexpr double kDx = 3.0, kDy = 4.0;
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x)
            dst[static_cast<std::size_t>(y) * kDim + x] = sampleShifted(src, kDim, kDx, kDy, x, y);
    // A large NoData hole in dst (NaN = NoData). With full-extent coverage
    // the valid matches necessarily cluster in the remaining corner, so the
    // caller lowers the coverage gate to accept a partial-overlap scene —
    // coverage is still reported honestly in the result.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 90; ++x)
            dst[static_cast<std::size_t>(y) * kDim + x] = nan;

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::PhaseCorrelation;
    opt.minCoverageRatio = 0.2;
    const auto rep =
        MultimodalMatcher::matchImages(src.data(), kDim, kDim, dst.data(), kDim, kDim, opt);
    REQUIRE(rep.status == RegistrationStatus::Success);
    const auto [ox, oy] = observedShift(rep);
    REQUIRE(ox == Catch::Approx(kDx).margin(0.5));
    REQUIRE(oy == Catch::Approx(kDy).margin(0.5));
}

TEST_CASE("multimodal: cancellation is observed and reported", "[f13][matcher][negative]")
{
    const auto src = makeScene(kDim);
    std::atomic_bool cancel{true};
    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::PhaseCorrelation;
    const auto rep = MultimodalMatcher::matchImages(src.data(), kDim, kDim, src.data(), kDim, kDim,
                                                    opt, &cancel);
    REQUIRE(rep.status == RegistrationStatus::Refused);
    REQUIRE(rep.reason == QStringLiteral("cancelled"));
}

TEST_CASE("multimodal: scratch estimate is a deterministic closed form", "[f13][matcher]")
{
    MultimodalMatchOptions opt;
    // 512² pair, window 64: fft = 3·64²·16 B; pyramid ≤ 2×(512²+512²)·4 B.
    const double miB = MultimodalMatcher::estimateScratchMiB(512, 512, 512, 512, opt);
    REQUIRE(miB == Catch::Approx(3.0 * 64 * 64 * 16.0 / (1024 * 1024)
                                 + (512.0 * 512 + 512.0 * 512) * 4.0 * 2.0 / (1024 * 1024))
                         .epsilon(1e-9));
}

TEST_CASE("multimodal: invalid buffers throw, tiny images refuse", "[f13][matcher][negative]")
{
    MultimodalMatchOptions opt;
    REQUIRE_THROWS_AS(MultimodalMatcher::matchImages(nullptr, 0, 0, nullptr, 0, 0, opt),
                      std::invalid_argument);

    // 16x16 image: too small for any window -> structural refusal.
    std::vector<float> tiny(16 * 16, 1.0f);
    std::vector<float> tiny2(16 * 16, 2.0f);
    const auto rep =
        MultimodalMatcher::matchImages(tiny.data(), 16, 16, tiny2.data(), 16, 16, opt);
    REQUIRE(rep.status == RegistrationStatus::Refused);
}
