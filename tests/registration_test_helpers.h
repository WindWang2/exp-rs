// registration_test_helpers.h — F13 Package H: synthetic warp fixtures.
//
// Header-only, deterministic (closed-form texture + hash grain). These
// helpers generate image-level warped pairs with KNOWN ground truth — the
// analytic map is applied by the test, never by production code.
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace f13 {

/// Deterministic grain in [-1, 1] (splitmix-style hash).
inline float grain(int x, int y)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                      + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
}

/// Smooth multi-frequency scene with light grain (phase- and MI-friendly).
inline float texture(int x, int y)
{
    const double fx = static_cast<double>(x);
    const double fy = static_cast<double>(y);
    const double v = 50.0 + 20.0 * std::sin(fx / 9.3) * std::sin(fy / 7.7)
                     + 10.0 * std::sin((fx + fy) / 5.1) + 8.0 * std::sin(fx / 3.7)
                     + 3.0 * grain(x, y);
    return static_cast<float>(v);
}

/// Bilinear inverse-map warp: out(x, y) = src(inverseMap(x, y)), NaN outside.
/// `pixelTransform` receives (sampled value, x, y) for position-dependent
/// radiometric post-transforms (e.g. multiplicative speckle).
inline std::vector<float> warpImage(
    const std::vector<float>& src, int width, int height,
    const std::function<std::pair<double, double>(double, double)>& inverseMap,
    const std::function<float(float, int, int)>& pixelTransform = {})
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> out(static_cast<std::size_t>(width) * height, nan);
    auto sample = [&](double sx, double sy) {
        const int x0 = static_cast<int>(std::floor(sx));
        const int y0 = static_cast<int>(std::floor(sy));
        if (x0 < 0 || y0 < 0 || x0 + 1 >= width || y0 + 1 >= height)
            return nan;
        const double fx = sx - x0;
        const double fy = sy - y0;
        const auto at = [&](int xx, int yy) {
            return static_cast<double>(src[static_cast<std::size_t>(yy) * width + xx]);
        };
        return static_cast<float>((1 - fx) * (1 - fy) * at(x0, y0)
                                  + fx * (1 - fy) * at(x0 + 1, y0)
                                  + (1 - fx) * fy * at(x0, y0 + 1)
                                  + fx * fy * at(x0 + 1, y0 + 1));
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto [sx, sy] = inverseMap(static_cast<double>(x), static_cast<double>(y));
            float v = sample(sx, sy);
            if (!std::isnan(v) && pixelTransform)
                v = pixelTransform(v, x, y);
            out[static_cast<std::size_t>(y) * width + x] = v;
        }
    }
    return out;
}

/// Mean absolute difference and valid-pixel fraction between two images
/// (NaN pixels excluded from both numerator and denominator).
inline void compareImages(const std::vector<float>& a, const std::vector<float>& b,
                          double& meanAbsDiff, double& validFraction)
{
    double acc = 0.0;
    std::size_t valid = 0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(a[i]) || std::isnan(b[i]))
            continue;
        acc += std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        ++valid;
    }
    meanAbsDiff = valid > 0 ? acc / static_cast<double>(valid)
                            : std::numeric_limits<double>::infinity();
    validFraction = n > 0 ? static_cast<double>(valid) / static_cast<double>(n) : 0.0;
}

} // namespace f13
