// test_pansharpening.cpp — D14 Package F: fusion + Wald metrics tests.
// Ground truths: analytic constant-scene identities (Brovey scale invariance,
// GS/IHS/HPF exact reconstruction when the pan equals the simulated
// intensity), the D14 physics gates (ERGAS <= 2.5, CC >= 0.94 on a synthetic
// reflectivity scene), and the identity metrics of evaluateQuality.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/pansharpening.h"

#include <cmath>
#include <numeric>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::algorithms;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// 4-band 64x64 synthetic reflectivity scene: smooth low-frequency texture,
/// strictly positive.
std::vector<float> makeMsBand(int size, double base, double amp, double fx, double fy)
{
    std::vector<float> band(static_cast<size_t>(size) * size, 0.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            band[static_cast<size_t>(y) * size + x] = static_cast<float>(
                base + amp * std::sin(2 * kPi * fx * x / size) * std::cos(2 * kPi * fy * y / size));
    return band;
}

/// Constant image of given value.
std::vector<float> constantImage(int width, int height, double value)
{
    return std::vector<float>(static_cast<size_t>(width) * height, static_cast<float>(value));
}

/// 4x4x4 block-average degradation (256 -> 64). Zero-mean, period-1 detail
/// injected at the fine scale cancels exactly inside each block.
std::vector<float> degrade4(const std::vector<float>& fine, int size)
{
    const int coarse = size / 4;
    std::vector<float> out(static_cast<size_t>(coarse) * coarse, 0.0f);
    for (int cy = 0; cy < coarse; ++cy) {
        for (int cx = 0; cx < coarse; ++cx) {
            double sum = 0.0;
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 4; ++dx)
                    sum += fine[static_cast<size_t>(cy * 4 + dy) * size + (cx * 4 + dx)];
            out[static_cast<size_t>(cy) * coarse + cx] = static_cast<float>(sum / 16.0);
        }
    }
    return out;
}

/// A 256x256 pan built from the MS scene (nearest sampling) plus zero-mean
/// high-frequency detail that averages out in every 4x4 block.
std::vector<float> makePan(int fineSize, const std::vector<std::vector<float>>& ms,
                           const std::vector<double>& weights)
{
    std::vector<float> pan(static_cast<size_t>(fineSize) * fineSize, 0.0f);
    const int coarse = fineSize / 4;
    for (int y = 0; y < fineSize; ++y) {
        for (int x = 0; x < fineSize; ++x) {
            double value = 0.0;
            for (size_t k = 0; k < ms.size(); ++k)
                value += weights[k] * ms[k][static_cast<size_t>(y / 4) * coarse + (x / 4)];
            value += 10.0 * std::sin(2 * kPi * x) * std::cos(3 * kPi * y + 0.7);
            pan[static_cast<size_t>(y) * fineSize + x] = static_cast<float>(value);
        }
    }
    return pan;
}

} // namespace

TEST_CASE("test_pansharpening - sharpen validates its inputs", "[pansharpen][d14]")
{
    const auto a = constantImage(4, 4, 10.0);
    const auto b = constantImage(4, 4, 20.0);
    const auto pan = constantImage(8, 8, 20.0);
    std::vector<float> out0(64, 0.0f);
    std::vector<float> out1(64, 0.0f);
    const std::vector<const float*> twoBands{a.data(), b.data()};

    std::vector<float*> out2{out0.data(), out1.data()};
    // Fewer than three bands.
    REQUIRE_FALSE(PanSharpening::sharpen(PanSharpenMethod::Brovey, twoBands, 4, 4,
                                         pan.data(), 8, 8, out2));
    // Non-integer resolution ratio.
    const auto c = constantImage(4, 4, 30.0);
    const std::vector<const float*> threeBands{a.data(), b.data(), c.data()};
    std::vector<float> out3(64, 0.0f);
    std::vector<float*> out3p{out0.data(), out1.data(), out3.data()};
    REQUIRE_FALSE(PanSharpening::sharpen(PanSharpenMethod::Brovey, threeBands, 4, 4,
                                         pan.data(), 6, 8, out3p));
    // Mismatched output allocation.
    std::vector<float*> twoOutputs{out0.data(), out1.data()};
    REQUIRE_FALSE(PanSharpening::sharpen(PanSharpenMethod::Brovey, threeBands, 4, 4,
                                         pan.data(), 8, 8, twoOutputs));
    // Wrong bandWeights size.
    REQUIRE_FALSE(PanSharpening::sharpen(PanSharpenMethod::GramSchmidt, threeBands, 4, 4,
                                         pan.data(), 8, 8, out3p, {0.5, 0.5}));
}

TEST_CASE("test_pansharpening - Brovey reduces to the input bands for a self-consistent pan", "[pansharpen][d14]")
{
    // Constants (10, 20, 30) with pan = their sum (60):
    // fused_k = MS_k / sum(MS) * pan = MS_k exactly.
    const auto a = constantImage(8, 8, 10.0);
    const auto b = constantImage(8, 8, 20.0);
    const auto c = constantImage(8, 8, 30.0);
    const auto pan = constantImage(32, 32, 60.0);
    std::vector<float> oa(1024), ob(1024), oc(1024);
    std::vector<float*> out{oa.data(), ob.data(), oc.data()};
    const std::vector<const float*> ms{a.data(), b.data(), c.data()};

    REQUIRE(PanSharpening::sharpen(PanSharpenMethod::Brovey, ms, 8, 8, pan.data(), 32, 32, out));
    for (size_t i = 0; i < oa.size(); ++i) {
        REQUIRE_THAT(static_cast<double>(oa[i]), WithinAbs(10.0, 1e-4));
        REQUIRE_THAT(static_cast<double>(ob[i]), WithinAbs(20.0, 1e-4));
        REQUIRE_THAT(static_cast<double>(oc[i]), WithinAbs(30.0, 1e-4));
    }
}

TEST_CASE("test_pansharpening - Gram-Schmidt reconstructs the input when pan equals the simulated intensity", "[pansharpen][d14]")
{
    // Spatially constant bands make the upsample exact, so the simulated
    // low-res pan is (10+20+30)/3 = 20; a constant 20 pan matches its mean
    // and sigma, PAN_norm == GS1, and every fused band collapses to its input.
    const auto a = constantImage(8, 8, 10.0);
    const auto b = constantImage(8, 8, 20.0);
    const auto c = constantImage(8, 8, 30.0);
    const auto pan = constantImage(32, 32, 20.0);
    std::vector<float> oa(1024), ob(1024), oc(1024);
    std::vector<float*> out{oa.data(), ob.data(), oc.data()};
    const std::vector<const float*> ms{a.data(), b.data(), c.data()};

    REQUIRE(PanSharpening::sharpen(PanSharpenMethod::GramSchmidt, ms, 8, 8, pan.data(), 32, 32, out));
    for (size_t i = 0; i < oa.size(); ++i) {
        REQUIRE_THAT(static_cast<double>(oa[i]), WithinAbs(10.0, 1e-5));
        REQUIRE_THAT(static_cast<double>(ob[i]), WithinAbs(20.0, 1e-5));
        REQUIRE_THAT(static_cast<double>(oc[i]), WithinAbs(30.0, 1e-5));
    }
}

TEST_CASE("test_pansharpening - IHS reconstruction is exact when the pan carries the true intensity", "[pansharpen][d14]")
{
    // Constant (5, 50, 100): I = 155/3; a constant pan at that intensity makes
    // the substituted intensity identical to the original one.
    const auto a = constantImage(8, 8, 5.0);
    const auto b = constantImage(8, 8, 50.0);
    const auto c = constantImage(8, 8, 100.0);
    const double intensity = (5.0 + 50.0 + 100.0) / 3.0;
    const auto pan = constantImage(32, 32, intensity);
    std::vector<float> oa(1024), ob(1024), oc(1024);
    std::vector<float*> out{oa.data(), ob.data(), oc.data()};
    const std::vector<const float*> ms{a.data(), b.data(), c.data()};

    REQUIRE(PanSharpening::sharpen(PanSharpenMethod::Ihs, ms, 8, 8, pan.data(), 32, 32, out));
    for (size_t i = 0; i < oa.size(); ++i) {
        REQUIRE_THAT(static_cast<double>(oa[i]), WithinAbs(5.0, 1e-4));
        REQUIRE_THAT(static_cast<double>(ob[i]), WithinAbs(50.0, 1e-4));
        REQUIRE_THAT(static_cast<double>(oc[i]), WithinAbs(100.0, 1e-4));
    }
}

TEST_CASE("test_pansharpening - High-pass fusion with a constant pan reproduces the upsampled bands", "[pansharpen][d14]")
{
    const auto a = constantImage(8, 8, 10.0);
    const auto b = constantImage(8, 8, 20.0);
    const auto c = constantImage(8, 8, 30.0);
    const auto pan = constantImage(32, 32, 25.0); // blur(const) = const -> no detail
    std::vector<float> oa(1024), ob(1024), oc(1024);
    std::vector<float*> out{oa.data(), ob.data(), oc.data()};
    const std::vector<const float*> ms{a.data(), b.data(), c.data()};

    REQUIRE(PanSharpening::sharpen(PanSharpenMethod::Hpf, ms, 8, 8, pan.data(), 32, 32, out));
    for (size_t i = 0; i < oa.size(); ++i) {
        REQUIRE_THAT(static_cast<double>(oa[i]), WithinAbs(10.0, 1e-5));
        REQUIRE_THAT(static_cast<double>(ob[i]), WithinAbs(20.0, 1e-5));
        REQUIRE_THAT(static_cast<double>(oc[i]), WithinAbs(30.0, 1e-5));
    }
}

TEST_CASE("test_pansharpening - Gram-Schmidt fusion meets the D14 physics gates (ERGAS<=2.5, CC>=0.94)",
          "[pansharpen][d14]")
{
    constexpr int kCoarse = 64;
    constexpr int kFine = 256;
    const std::vector<double> weights{0.25, 0.25, 0.25, 0.25};
    std::vector<std::vector<float>> ms;
    ms.push_back(makeMsBand(kCoarse, 60.0, 25.0, 3.0, 2.0));  // blue-ish
    ms.push_back(makeMsBand(kCoarse, 90.0, 30.0, 5.0, 3.0));  // green-ish
    ms.push_back(makeMsBand(kCoarse, 110.0, 35.0, 4.0, 6.0)); // red-ish
    ms.push_back(makeMsBand(kCoarse, 140.0, 20.0, 7.0, 4.0)); // nir
    const auto pan = makePan(kFine, ms, weights);

    std::vector<std::vector<float>> outs(ms.size(), std::vector<float>(static_cast<size_t>(kFine) * kFine));
    std::vector<const float*> msPtrs;
    std::vector<float*> outPtrs;
    for (size_t k = 0; k < ms.size(); ++k) {
        msPtrs.push_back(ms[k].data());
        outPtrs.push_back(outs[k].data());
    }

    REQUIRE(PanSharpening::sharpen(PanSharpenMethod::GramSchmidt, msPtrs, kCoarse, kCoarse,
                                   pan.data(), kFine, kFine, outPtrs, weights));

    // Wald protocol: degrade the fusion and compare with the original MS.
    std::vector<std::vector<float>> degraded;
    std::vector<const float*> degradedPtrs;
    for (const auto& fine : outs) {
        degraded.push_back(degrade4(fine, kFine));
        degradedPtrs.push_back(degraded.back().data());
    }

    const auto metrics = PanSharpening::evaluateQuality(degradedPtrs, msPtrs, kCoarse, kCoarse, 0.25);
    REQUIRE(metrics.ergas <= 2.5);
    REQUIRE(metrics.meanCc >= 0.94);

    // Physical conservation: mean drift < 1% and no negative reflectivity.
    for (size_t k = 0; k < ms.size(); ++k) {
        const double meanOriginal = std::accumulate(ms[k].begin(), ms[k].end(), 0.0) / ms[k].size();
        const double meanDegraded = std::accumulate(degraded[k].begin(), degraded[k].end(), 0.0) / degraded[k].size();
        REQUIRE(std::abs(meanDegraded - meanOriginal) / std::abs(meanOriginal) < 0.01);
        for (const float value : outs[k])
            REQUIRE(value >= 0.0f);
    }
}

TEST_CASE("test_pansharpening - evaluateQuality of identical rasters is the identity", "[pansharpen][d14]")
{
    const auto a = makeMsBand(16, 50.0, 10.0, 2.0, 3.0);
    const auto b = makeMsBand(16, 80.0, 15.0, 3.0, 1.0);
    const std::vector<const float*> fused{a.data(), b.data()};
    const std::vector<const float*> original{a.data(), b.data()};

    const auto metrics = PanSharpening::evaluateQuality(fused, original, 16, 16, 0.25);
    REQUIRE_THAT(metrics.rmse, WithinAbs(0.0, 1e-12));
    REQUIRE(metrics.ergas <= 1e-9);
    REQUIRE_THAT(metrics.meanCc, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(metrics.ssim, WithinAbs(1.0, 1e-9));
}
