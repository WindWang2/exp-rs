// resampler.h — D14 Package E: multi-method resampling and warp pipeline
// (ADR 0159).
//
// The warp is strictly reverse-mapped (target pixel -> world -> user inverse
// map -> source pixel), so the output can never contain holes from forward
// scattering. Kernel taps that reach past the raster border are clamped to
// the edge (edge replication); NoData values in the neighbourhood are
// excluded from the weighted sum and trigger a NoData output when the valid
// weight share drops below 50% — the sentinel never leaks into arithmetic.
#pragma once

#include <functional>
#include <utility>

namespace rs::algorithms {

enum class ResampleMethod {
    NearestNeighbor,
    Bilinear,
    CubicConvolution, // Keys cubic convolution (a = -0.5)
    Lanczos           // Lanczos-3 windowed sinc
};

struct WarpOptions {
    ResampleMethod method{ResampleMethod::Bilinear};
    double noDataValue{-9999.0};
    bool clampRange{false};
    double minValue{0.0};
    double maxValue{1.0};
};

class Resampler {
  public:
    /// Keys cubic convolution weight (parameter a, default -0.5).
    [[nodiscard]] static double cubicKernel(double x, double a = -0.5) noexcept;
    /// Lanczos-3 windowed sinc weight.
    [[nodiscard]] static double lanczos3Kernel(double x) noexcept;

    /// 2D sub-pixel sample at pixel coordinate (u, v). Coordinates outside
    /// the raster-edge footprint [-0.5, width-0.5] x [-0.5, height-0.5]
    /// yield noDataValue; inside it, taps are edge-clamped (#1686 — the
    /// outer half-pixel ring between the first/last pixel centre and the
    /// raster edge is valid coverage, GDAL semantics).
    static double interpolate(const float* buffer, int width, int height,
                              double u, double v,
                              ResampleMethod method,
                              double noDataValue);

    /// Full raster warp. Pixel centres are placed with GDAL geotransform
    /// convention: world = (gt[0] + gt[1]*(i + 0.5), gt[3] + gt[5]*(j + 0.5)).
    /// `inverseCoordMap` receives the DESTINATION world coordinate and must
    /// return the SOURCE world coordinate (the georeferenced inverse of the
    /// correction). Returns false on invalid buffers/sizes.
    static bool warpRaster(const float* srcBuffer, int srcWidth, int srcHeight,
                           const double srcGeoTransform[6],
                           float* dstBuffer, int dstWidth, int dstHeight,
                           const double dstGeoTransform[6],
                           const std::function<std::pair<double, double>(double, double)>& inverseCoordMap,
                           const WarpOptions& options);
};

} // namespace rs::algorithms
