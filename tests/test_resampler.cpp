// test_resampler.cpp — D14 Package E: kernel math + interpolation + warp.
// Every expectation is analytic: closed-form kernel values, the partition of
// unity axiom, exact evaluation of an analytic plane f(x,y) = 2x + 3y + 10
// (f(1.4, 2.6) = 20.6 by hand), and geotransform identities.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/resampler.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::algorithms;

namespace {

constexpr double kNoData = -9999.0;

/// Build a 10x10 sample grid of the analytic plane f(x, y) = 2x + 3y + 10.
std::vector<float> analyticPlaneGrid(int size = 10)
{
    std::vector<float> grid(static_cast<size_t>(size) * size, 0.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            grid[static_cast<size_t>(y) * size + x] = static_cast<float>(2.0 * x + 3.0 * y + 10.0);
    return grid;
}

} // namespace

TEST_CASE("test_resampler - Cubic kernel matches closed-form values and partitions unity", "[resampler][d14]")
{
    REQUIRE_THAT(Resampler::cubicKernel(0.0), WithinAbs(1.0, 1e-15));
    REQUIRE_THAT(Resampler::cubicKernel(1.0), WithinAbs(0.0, 1e-15));
    REQUIRE_THAT(Resampler::cubicKernel(-1.0), WithinAbs(0.0, 1e-15));
    REQUIRE_THAT(Resampler::cubicKernel(2.0), WithinAbs(0.0, 1e-15));
    // a = -0.5: W(0.5) = 1.5·0.125 - 2.5·0.25 + 1 = 0.5625.
    REQUIRE_THAT(Resampler::cubicKernel(0.5), WithinAbs(0.5625, 1e-15));
    // W(1.5) = -0.5·3.375 + 2.5·2.25 - 4·1.5 + 2 = -0.0625 (negative lobe).
    REQUIRE_THAT(Resampler::cubicKernel(1.5), WithinAbs(-0.0625, 1e-15));
    REQUIRE_THAT(Resampler::cubicKernel(3.5), WithinAbs(0.0, 1e-15));

    // Partition of unity: sum_{k=-1..2} W(k - delta) == 1 for any delta.
    for (double delta = 0.0; delta < 1.0; delta += 0.05) {
        const double sum = Resampler::cubicKernel(-1 - delta) + Resampler::cubicKernel(-delta) +
                           Resampler::cubicKernel(1 - delta) + Resampler::cubicKernel(2 - delta);
        REQUIRE_THAT(sum, WithinAbs(1.0, 1e-12));
    }
}

TEST_CASE("test_resampler - Lanczos-3 kernel matches closed-form values", "[resampler][d14]")
{
    REQUIRE_THAT(Resampler::lanczos3Kernel(0.0), WithinAbs(1.0, 1e-15));
    // Integer taps other than zero are exact zeros.
    for (int k = -3; k <= 3; ++k) {
        if (k != 0)
            REQUIRE_THAT(Resampler::lanczos3Kernel(k), WithinAbs(0.0, 1e-15));
    }
    // L(1.5) = sinc(1.5)·sinc(0.5) = -4 / (3 pi^2).
    const double pi = 3.14159265358979323846;
    REQUIRE_THAT(Resampler::lanczos3Kernel(1.5), WithinAbs(-4.0 / (3.0 * pi * pi), 1e-12));
    REQUIRE_THAT(Resampler::lanczos3Kernel(3.5), WithinAbs(0.0, 1e-15));
}

TEST_CASE("test_resampler - Analytic plane is reproduced exactly by bilinear and cubic", "[resampler][d14]")
{
    const auto grid = analyticPlaneGrid();

    const double expected = 2.0 * 1.4 + 3.0 * 2.6 + 10.0; // 20.6 by hand
    const double bilinear = Resampler::interpolate(grid.data(), 10, 10, 1.4, 2.6,
                                                   ResampleMethod::Bilinear, kNoData);
    REQUIRE_THAT(bilinear, WithinAbs(expected, 1e-6));
    const double cubic = Resampler::interpolate(grid.data(), 10, 10, 1.4, 2.6,
                                                ResampleMethod::CubicConvolution, kNoData);
    REQUIRE_THAT(cubic, WithinAbs(expected, 1e-6));
    // Lanczos-3 is a windowed sinc: it only *approximates* linear
    // reproduction (the window breaks the first-moment identity), so a
    // 0.5% band replaces the exactness demand held by bilinear/cubic.
    const double lanczos = Resampler::interpolate(grid.data(), 10, 10, 1.4, 2.6,
                                                  ResampleMethod::Lanczos, kNoData);
    REQUIRE_THAT(lanczos, WithinAbs(expected, 0.1));

    // A second sample point on a different cell for good measure: (7.3, 4.8).
    const double expected2 = 2.0 * 7.3 + 3.0 * 4.8 + 10.0; // 39.0
    REQUIRE_THAT(Resampler::interpolate(grid.data(), 10, 10, 7.3, 4.8,
                                        ResampleMethod::CubicConvolution, kNoData),
                 WithinAbs(expected2, 1e-6));
}

TEST_CASE("test_resampler - Nearest neighbour picks the rounded cell", "[resampler][d14]")
{
    const auto grid = analyticPlaneGrid();
    // round(1.4) = 1, round(2.6) = 3 -> f(1,3) = 2 + 9 + 10 = 21.
    const double value = Resampler::interpolate(grid.data(), 10, 10, 1.4, 2.6,
                                                ResampleMethod::NearestNeighbor, kNoData);
    REQUIRE_THAT(value, WithinAbs(21.0, 1e-12));
}

TEST_CASE("test_resampler - Out-of-range sampling yields the NoData sentinel", "[resampler][d14]")
{
    const auto grid = analyticPlaneGrid();
    for (const auto method : {ResampleMethod::NearestNeighbor, ResampleMethod::Bilinear,
                              ResampleMethod::CubicConvolution, ResampleMethod::Lanczos}) {
        REQUIRE(Resampler::interpolate(grid.data(), 10, 10, -0.25, 5.0, method, kNoData) == kNoData);
        REQUIRE(Resampler::interpolate(grid.data(), 10, 10, 5.0, 10.5, method, kNoData) == kNoData);
    }
}

TEST_CASE("test_resampler - NoData neighbourhood renormalizes over valid weights", "[resampler][d14]")
{
    auto grid = analyticPlaneGrid();
    // Poke one hole at (2, 2). Sampling (1.5, 2.5) bilinear uses the 2x2
    // neighbourhood {(1,2),(2,2),(1,3),(2,3)} with weights 0.25 each; the
    // valid share is 0.75 >= 0.5, so the result is the mean of the three
    // valid plane values: (18 + 21 + 23)/3 = 62/3.
    grid[static_cast<size_t>(2) * 10 + 2] = static_cast<float>(kNoData);
    const double mixed = Resampler::interpolate(grid.data(), 10, 10, 1.5, 2.5,
                                                ResampleMethod::Bilinear, kNoData);
    REQUIRE_THAT(mixed, WithinAbs(62.0 / 3.0, 1e-6));
    REQUIRE(mixed != kNoData);

    // Three holes leave only 0.25 of the weight -> forced NoData output
    // (a 50% valid share is not "below 50%", so two holes still renormalize).
    grid[static_cast<size_t>(3) * 10 + 1] = static_cast<float>(kNoData);
    grid[static_cast<size_t>(3) * 10 + 2] = static_cast<float>(kNoData);
    const double starved = Resampler::interpolate(grid.data(), 10, 10, 1.5, 2.5,
                                                  ResampleMethod::Bilinear, kNoData);
    REQUIRE(starved == kNoData);
}

TEST_CASE("test_resampler - Identity warp reproduces the source raster", "[resampler][d14]")
{
    const auto src = analyticPlaneGrid(8);
    std::vector<float> dst(static_cast<size_t>(8) * 8, kNoData);
    const double srcGt[6] = {0.0, 10.0, 0.0, 0.0, 0.0, -10.0};
    const double dstGt[6] = {0.0, 10.0, 0.0, 0.0, 0.0, -10.0};

    WarpOptions options;
    options.method = ResampleMethod::Bilinear;
    options.noDataValue = kNoData;
    options.clampRange = false;

    REQUIRE(Resampler::warpRaster(src.data(), 8, 8, srcGt, dst.data(), 8, 8, dstGt,
                                  [](double x, double y) { return std::make_pair(x, y); },
                                  options));
    for (size_t i = 0; i < dst.size(); ++i)
        REQUIRE_THAT(static_cast<double>(dst[i]), WithinAbs(static_cast<double>(src[i]), 1e-6));
}

TEST_CASE("test_resampler - Translation warp shifts by one pixel and fills the border with NoData", "[resampler][d14]")
{
    const auto src = analyticPlaneGrid(8);
    std::vector<float> dst(static_cast<size_t>(8) * 8, kNoData);
    const double srcGt[6] = {0.0, 10.0, 0.0, 0.0, 0.0, 10.0};
    const double dstGt[6] = {0.0, 10.0, 0.0, 0.0, 0.0, 10.0};

    WarpOptions options;
    options.method = ResampleMethod::NearestNeighbor;
    options.noDataValue = kNoData;
    options.clampRange = false;

    // Inverse map: destination world x maps back to source world x - 10,
    // i.e. dst column i samples src column i-1 (image appears shifted right).
    REQUIRE(Resampler::warpRaster(src.data(), 8, 8, srcGt, dst.data(), 8, 8, dstGt,
                                  [](double x, double y) { return std::make_pair(x - 10.0, y); },
                                  options));
    for (int j = 0; j < 8; ++j) {
        REQUIRE(dst[static_cast<size_t>(j) * 8 + 0] == static_cast<float>(kNoData));
        for (int i = 1; i < 8; ++i)
            REQUIRE(dst[static_cast<size_t>(j) * 8 + i] == src[static_cast<size_t>(j) * 8 + i - 1]);
    }
}

TEST_CASE("test_resampler - clampRange suppresses cubic overshoot at sharp edges", "[resampler][d14]")
{
    // Sharp vertical edge: left half 0, right half 1. Cubic convolution
    // overshoots beyond the data range near the edge.
    const int size = 8;
    std::vector<float> src(static_cast<size_t>(size) * size, 0.0f);
    for (int j = 0; j < size; ++j)
        for (int i = 4; i < size; ++i)
            src[static_cast<size_t>(j) * size + i] = 1.0f;

    // Offset only the OUTPUT grid by half a pixel so cubic sampling straddles
    // the edge (with identical grids every sample sits exactly on a cell
    // centre and the kernel degenerates to a plain copy, overshoot-free).
    const double srcGt[6] = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    const double gt[6] = {0.5, 1.0, 0.0, 0.5, 0.0, 1.0};
    const auto runWarp = [&](bool clamp) {
        std::vector<float> dst(static_cast<size_t>(size) * size, kNoData);
        WarpOptions options;
        options.method = ResampleMethod::CubicConvolution;
        options.noDataValue = kNoData;
        options.clampRange = clamp;
        options.minValue = 0.0;
        options.maxValue = 1.0;
        REQUIRE(Resampler::warpRaster(src.data(), size, size, srcGt, dst.data(), size, size, gt,
                                      [](double x, double y) { return std::make_pair(x, y); },
                                      options));
        return dst;
    };

    const auto clamped = runWarp(true);
    for (const float value : clamped) {
        if (value == static_cast<float>(kNoData))
            continue; // outside the source footprint at the shifted border
        REQUIRE(value >= 0.0f);
        REQUIRE(value <= 1.0f);
    }
    const auto unclamped = runWarp(false);
    bool overshot = false;
    for (const float value : unclamped) {
        if (value == static_cast<float>(kNoData))
            continue;
        if (value > 1.0f || value < 0.0f)
            overshot = true;
    }
    REQUIRE(overshot); // the overshoot exists and clamping was doing real work
}
