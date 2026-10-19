// resampler.cpp — D14 Package E implementation (ADR 0159).
#include "processing/algorithms/resampler.h"

#include "processing/algorithms/nodata_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace rs::algorithms {

namespace {

constexpr double kHalfValidWeight = 0.5;

bool isNoData(double value, double noDataValue)
{
    // Delegates to the module-wide predicate owner (nodata_utils.h, #A1):
    // exact sentinel equality plus non-finite = invalid. This file used to
    // carry its own copy that accepted ±Inf, which drifted from every other
    // kernel's invalid-value test.
    return sicnu::rs::isNoDataValue(value, noDataValue);
}

double sampleClamped(const float* buffer, int width, int height, int x, int y)
{
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return buffer[static_cast<size_t>(y) * width + x];
}

double sampleNearest(const float* buffer, int width, int height, double u, double v, double noData)
{
    if (u < 0.0 || v < 0.0 || u > width - 1 || v > height - 1)
        return noData;
    const int iu = std::clamp(static_cast<int>(std::floor(u + 0.5)), 0, width - 1);
    const int iv = std::clamp(static_cast<int>(std::floor(v + 0.5)), 0, height - 1);
    return buffer[static_cast<size_t>(iv) * width + iu];
}

/// Weighted kernel sample with NoData renormalization. `taps` is the kernel
/// half-width (1: bilinear, 2: cubic, 3: lanczos); weights come from the
/// separable 1D kernels evaluated at the fractional offsets.
double sampleWeighted(const float* buffer, int width, int height,
                      double u, double v, double noData,
                      int taps, const std::function<double(double)>& kernel1D)
{
    const int iu = static_cast<int>(std::floor(u));
    const int iv = static_cast<int>(std::floor(v));
    const double du = u - iu;
    const double dv = v - iv;

    // Fixed-capacity stack weights (max kernel half-width 3 → 6 taps) keep
    // the per-pixel warp loop allocation-free.
    std::array<double, 6> wx{};
    std::array<double, 6> wy{};
    for (int m = 0; m < 2 * taps; ++m) {
        wx[static_cast<size_t>(m)] = kernel1D(static_cast<double>(iu + m - (taps - 1)) - u);
        wy[static_cast<size_t>(m)] = kernel1D(static_cast<double>(iv + m - (taps - 1)) - v);
    }

    double total = 0.0;
    double validWeight = 0.0;
    double sum = 0.0;
    for (int m = 0; m < 2 * taps; ++m) {
        for (int n = 0; n < 2 * taps; ++n) {
            const double w = wy[static_cast<size_t>(m)] * wx[static_cast<size_t>(n)];
            if (w == 0.0)
                continue;
            total += w;
            const double value = sampleClamped(buffer, width, height,
                                               iu + n - (taps - 1), iv + m - (taps - 1));
            if (isNoData(value, noData))
                continue;
            validWeight += w;
            sum += w * value;
        }
    }
    if (total <= 0.0 || validWeight < kHalfValidWeight * total)
        return noData;
    return sum / validWeight;
}

} // namespace

double Resampler::cubicKernel(double x, double a) noexcept
{
    const double absx = std::abs(x);
    if (absx < 1.0) {
        return (a + 2.0) * absx * absx * absx - (a + 3.0) * absx * absx + 1.0;
    }
    if (absx < 2.0) {
        return a * absx * absx * absx - 5.0 * a * absx * absx + 8.0 * a * absx - 4.0 * a;
    }
    return 0.0;
}

double Resampler::lanczos3Kernel(double x) noexcept
{
    if (x == 0.0)
        return 1.0;
    const double absx = std::abs(x);
    if (absx >= 3.0)
        return 0.0;
    const double pi = std::numbers::pi;
    const double sincX = std::sin(pi * x) / (pi * x);
    const double sincX3 = std::sin(pi * x / 3.0) / (pi * x / 3.0);
    return sincX * sincX3;
}

double Resampler::interpolate(const float* buffer, int width, int height,
                              double u, double v,
                              ResampleMethod method,
                              double noDataValue)
{
    if (!buffer || width <= 0 || height <= 0)
        return noDataValue;
    if (!std::isfinite(u) || !std::isfinite(v))
        return noDataValue;
    if (u < 0.0 || v < 0.0 || u > width - 1 || v > height - 1)
        return noDataValue;

    switch (method) {
    case ResampleMethod::NearestNeighbor:
        return sampleNearest(buffer, width, height, u, v, noDataValue);
    case ResampleMethod::Bilinear:
        return sampleWeighted(buffer, width, height, u, v, noDataValue, 1,
                              [](double x) { return std::max(0.0, 1.0 - std::abs(x)); });
    case ResampleMethod::CubicConvolution:
        return sampleWeighted(buffer, width, height, u, v, noDataValue, 2,
                              [](double x) { return cubicKernel(x); });
    case ResampleMethod::Lanczos:
        return sampleWeighted(buffer, width, height, u, v, noDataValue, 3,
                              [](double x) { return lanczos3Kernel(x); });
    }
    return noDataValue;
}

bool Resampler::warpRaster(const float* srcBuffer, int srcWidth, int srcHeight,
                           const double srcGeoTransform[6],
                           float* dstBuffer, int dstWidth, int dstHeight,
                           const double dstGeoTransform[6],
                           const std::function<std::pair<double, double>(double, double)>& inverseCoordMap,
                           const WarpOptions& options)
{
    if (!srcBuffer || !dstBuffer || !srcGeoTransform || !dstGeoTransform)
        return false;
    if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0)
        return false;
    if (!inverseCoordMap)
        return false;
    const double det = srcGeoTransform[1] * srcGeoTransform[5] - srcGeoTransform[2] * srcGeoTransform[4];
    if (std::abs(det) < 1e-15)
        return false;

    for (int j = 0; j < dstHeight; ++j) {
        for (int i = 0; i < dstWidth; ++i) {
            // Destination pixel center -> world -> user inverse map -> source world.
            const double worldX = dstGeoTransform[0] + dstGeoTransform[1] * (i + 0.5) + dstGeoTransform[2] * (j + 0.5);
            const double worldY = dstGeoTransform[3] + dstGeoTransform[4] * (i + 0.5) + dstGeoTransform[5] * (j + 0.5);
            const auto [srcWorldX, srcWorldY] = inverseCoordMap(worldX, worldY);
            // Source world -> source pixel (invert the affine geotransform, then center-align).
            const double dx = srcWorldX - srcGeoTransform[0];
            const double dy = srcWorldY - srcGeoTransform[3];
            const double u = (dx * srcGeoTransform[5] - dy * srcGeoTransform[2]) / det - 0.5;
            const double v = (-dx * srcGeoTransform[4] + dy * srcGeoTransform[1]) / det - 0.5;

            double value = interpolate(srcBuffer, srcWidth, srcHeight, u, v,
                                       options.method, options.noDataValue);
            if (options.clampRange && !isNoData(value, options.noDataValue)) {
                value = std::clamp(value, options.minValue, options.maxValue);
            }
            dstBuffer[static_cast<size_t>(j) * dstWidth + i] = static_cast<float>(value);
        }
    }
    return true;
}

} // namespace rs::algorithms
