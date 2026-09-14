// tps_interpolator.cpp — D14 Package C implementation (ADR 0159).
//
// The augmented system [[K+λI, P], [Pᵀ, 0]] is small and dense; partial-pivot
// LU factorizes it once per fit and both right-hand sides reuse the factors.
#include "processing/algorithms/tps_interpolator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace rs::algorithms {

namespace {

constexpr double kCoincidentTolerance = 1e-9;

/// Solves A·x = b with partial-pivot LU; A is modified in place.
/// Returns false for a singular system.
bool solveLinearSystem(std::vector<double>& a, int n, std::vector<double>& b)
{
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        double best = std::abs(a[static_cast<size_t>(col) * n + col]);
        for (int row = col + 1; row < n; ++row) {
            const double candidate = std::abs(a[static_cast<size_t>(row) * n + col]);
            if (candidate > best) {
                best = candidate;
                pivot = row;
            }
        }
        if (best < 1e-300)
            return false;
        if (pivot != col) {
            for (int k = 0; k < n; ++k)
                std::swap(a[static_cast<size_t>(col) * n + k], a[static_cast<size_t>(pivot) * n + k]);
            std::swap(b[col], b[pivot]);
        }
        for (int row = col + 1; row < n; ++row) {
            const double factor = a[static_cast<size_t>(row) * n + col] / a[static_cast<size_t>(col) * n + col];
            if (factor == 0.0)
                continue;
            a[static_cast<size_t>(row) * n + col] = 0.0;
            for (int k = col + 1; k < n; ++k)
                a[static_cast<size_t>(row) * n + k] -= factor * a[static_cast<size_t>(col) * n + k];
            b[row] -= factor * b[col];
        }
    }
    for (int row = n - 1; row >= 0; --row) {
        double sum = b[row];
        for (int k = row + 1; k < n; ++k)
            sum -= a[static_cast<size_t>(row) * n + k] * b[k];
        b[row] = sum / a[static_cast<size_t>(row) * n + row];
    }
    return true;
}

} // namespace

double TpsInterpolator::radialBasis(double r) noexcept
{
    if (!(r > 1e-12))
        return 0.0; // also guards the ln(0) singularity
    const double r2 = r * r;
    return r2 * std::log(r);
}

bool TpsInterpolator::fit(const std::vector<std::pair<double, double>>& sourcePts,
                          const std::vector<std::pair<double, double>>& targetPts,
                          const TpsConfig& config)
{
    mFitted = false;
    mKnots.clear();
    mWeightsX.clear();
    mWeightsY.clear();
    mKernel.clear();

    if (sourcePts.size() != targetPts.size() || sourcePts.size() < 3)
        return false;
    for (const auto& p : sourcePts)
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            return false;
    for (const auto& p : targetPts)
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            return false;

    // Deduplicate coincident source knots (first occurrence wins).
    for (const auto& [u, v] : sourcePts) {
        bool dup = false;
        for (const auto& k : mKnots) {
            if (std::hypot(u - k.first, v - k.second) < kCoincidentTolerance) {
                dup = true;
                break;
            }
        }
        if (!dup)
            mKnots.emplace_back(u, v);
    }
    if (mKnots.size() < 3)
        return false;

    const size_t n = mKnots.size();
    mKernel.assign(n * n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const double value = radialBasis(std::hypot(mKnots[i].first - mKnots[j].first,
                                                        mKnots[i].second - mKnots[j].second));
            mKernel[i * n + j] = value;
            mKernel[j * n + i] = value; // symmetric lower/upper fill
        }
    }

    // Augmented system: [[K+λI, P], [Pᵀ, 0]] with P = [1, u, v].
    const size_t size = n + 3;
    std::vector<double> aug(static_cast<size_t>(size) * size, 0.0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j)
            aug[i * size + j] = mKernel[i * n + j];
        aug[i * size + i] += config.regularizationLambda;
        aug[i * size + n] = 1.0;
        aug[i * size + n + 1] = mKnots[i].first;
        aug[i * size + n + 2] = mKnots[i].second;
        aug[n * size + i] = 1.0;
        aug[(n + 1) * size + i] = mKnots[i].first;
        aug[(n + 2) * size + i] = mKnots[i].second;
    }

    std::vector<double> rhsX(size, 0.0);
    std::vector<double> rhsY(size, 0.0);
    {
        // Map deduplicated knots back to their target counterparts.
        size_t knotIndex = 0;
        for (size_t i = 0; i < sourcePts.size() && knotIndex < n; ++i) {
            const auto& [u, v] = sourcePts[i];
            bool dup = false;
            for (size_t k = 0; k < knotIndex; ++k) {
                if (std::hypot(u - mKnots[k].first, v - mKnots[k].second) < kCoincidentTolerance) {
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;
            rhsX[knotIndex] = targetPts[i].first;
            rhsY[knotIndex] = targetPts[i].second;
            ++knotIndex;
        }
    }

    std::vector<double> augCopy = aug;
    if (!solveLinearSystem(augCopy, static_cast<int>(size), rhsX))
        return false;
    if (!solveLinearSystem(aug, static_cast<int>(size), rhsY))
        return false;

    mWeightsX.assign(rhsX.begin(), rhsX.begin() + static_cast<long>(n));
    mWeightsY.assign(rhsY.begin(), rhsY.begin() + static_cast<long>(n));
    mAffineX = {rhsX[n], rhsX[n + 1], rhsX[n + 2]};
    mAffineY = {rhsY[n], rhsY[n + 1], rhsY[n + 2]};
    mFitted = true;
    return true;
}

std::pair<double, double> TpsInterpolator::transform(double u, double v) const
{
    if (!mFitted)
        return {u, v};
    double x = mAffineX[0] + mAffineX[1] * u + mAffineX[2] * v;
    double y = mAffineY[0] + mAffineY[1] * u + mAffineY[2] * v;
    const size_t n = mKnots.size();
    for (size_t i = 0; i < n; ++i) {
        const double basis = radialBasis(std::hypot(u - mKnots[i].first, v - mKnots[i].second));
        x += mWeightsX[i] * basis;
        y += mWeightsY[i] * basis;
    }
    return {x, y};
}

void TpsInterpolator::transformBatch(std::span<const double> srcU,
                                     std::span<const double> srcV,
                                     std::span<double> dstX,
                                     std::span<double> dstY) const
{
    if (srcU.size() != srcV.size() || srcU.size() != dstX.size() || srcU.size() != dstY.size())
        return;
    for (size_t i = 0; i < srcU.size(); ++i) {
        const auto [x, y] = transform(srcU[i], srcV[i]);
        dstX[i] = x;
        dstY[i] = y;
    }
}

double TpsInterpolator::computeBendingEnergy() const
{
    if (!mFitted)
        return 0.0;
    const size_t n = mKnots.size();
    double energyX = 0.0;
    double energyY = 0.0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            energyX += mWeightsX[i] * mKernel[i * n + j] * mWeightsX[j];
            energyY += mWeightsY[i] * mKernel[i * n + j] * mWeightsY[j];
        }
    }
    return (energyX + energyY) / (16.0 * std::numbers::pi);
}

size_t TpsInterpolator::knotCount() const noexcept
{
    return mKnots.size();
}

bool TpsInterpolator::isFitted() const noexcept
{
    return mFitted;
}

} // namespace rs::algorithms
