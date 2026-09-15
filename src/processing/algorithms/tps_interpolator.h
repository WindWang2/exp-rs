// tps_interpolator.h — D14 Package C: thin plate spline non-rigid correction
// (ADR 0159).
//
// x(u,v) = a0 + a1·u + a2·v + Σ w_i·U(||(u,v)-(u_i,v_i)||)
// U(r)   = r²·ln r (0 at r = 0), computed per output dimension with its own
// weights and affine part. The augmented (N+3) system carries the side
// constraint Pᵀ·W = 0 so the spline reproduces affine maps exactly.
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace rs::algorithms {

struct TpsConfig {
    double regularizationLambda{0.0}; // 0: exact interpolation; >0: smoothing spline
};

class TpsInterpolator {
  public:
    TpsInterpolator() = default;
    ~TpsInterpolator() = default;

    /// Fit the mapping source -> target. Returns false on size mismatch,
    /// fewer than 3 distinct knots, or non-finite coordinates. Source knots
    /// closer than 1e-9 are deduplicated (first occurrence wins).
    bool fit(const std::vector<std::pair<double, double>>& sourcePts,
             const std::vector<std::pair<double, double>>& targetPts,
             const TpsConfig& config = {});

    /// Map a point; an unfitted interpolator maps identically.
    [[nodiscard]] std::pair<double, double> transform(double u, double v) const;

    /// Batch mapping; all spans must have equal length (no-op otherwise).
    void transformBatch(std::span<const double> srcU,
                        std::span<const double> srcV,
                        std::span<double> dstX,
                        std::span<double> dstY) const;

    /// Bending energy I = (WxᵀKWx + WyᵀKWy) / (16π); 0 for a pure affine fit.
    [[nodiscard]] double computeBendingEnergy() const;
    [[nodiscard]] size_t knotCount() const noexcept;
    [[nodiscard]] bool isFitted() const noexcept;

    /// Bi-harmonic radial basis U(r) = r²·ln r, with U(0) = 0 (short-circuit).
    [[nodiscard]] static double radialBasis(double r) noexcept;

  private:
    std::vector<std::pair<double, double>> mKnots; // source knots
    std::vector<double> mWeightsX;
    std::vector<double> mWeightsY;
    std::array<double, 3> mAffineX{0.0, 1.0, 0.0}; // a0, a1, a2
    std::array<double, 3> mAffineY{0.0, 0.0, 1.0}; // b0, b1, b2
    std::vector<double> mKernel;                   // K (row-major, knotCount²)
    bool mFitted{false};
};

} // namespace rs::algorithms
