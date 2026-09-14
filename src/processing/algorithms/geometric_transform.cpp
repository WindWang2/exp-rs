// geometric_transform.cpp — D14 Package B implementation (ADR 0159).
//
// Dense linear algebra lives in detail/linalg.hpp (one-sided Jacobi SVD).
// Model-specific work:
//   - Translation: closed-form mean shift.
//   - Rigid / Similarity: closed-form Procrustes (rotation/scale from the
//     cross-covariance of the centered correspondences).
//   - Affine / Polynomial2 / Polynomial3: Hartley-normalized design matrix,
//     SVD pseudo-inverse per output dimension, coefficients expanded back to
//     real image coordinates through the monomial substitution identity.
//   - Projective: normalized DLT (detail::dltHomography), backward solved by
//     swapping the correspondence roles.
#include "processing/algorithms/geometric_transform.h"

#include "processing/algorithms/detail/linalg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace rs::algorithms {

namespace {

using detail::dltHomography;
using detail::HartleyNormalizer;
using detail::Matrix;

constexpr double kMaxConditionNumber = 1e14;

// ---- Polynomial basis plumbing -------------------------------------------

/// Monomial (i, j) = u^i v^j for total degree <= degree, ordered by
/// (total degree, i).
std::vector<std::pair<int, int>> monomialBasis(int degree)
{
    std::vector<std::pair<int, int>> basis;
    for (int d = 0; d <= degree; ++d)
        for (int i = d; i >= 0; --i)
            basis.emplace_back(i, d - i);
    return basis;
}

double monomialValue(const std::pair<int, int>& ij, double u, double v)
{
    return std::pow(u, ij.first) * std::pow(v, ij.second);
}

double binomial(int n, int k)
{
    double result = 1.0;
    for (int i = 1; i <= k; ++i)
        result = result * static_cast<double>(n - i + 1) / static_cast<double>(i);
    return result;
}

/// Expand a polynomial given in normalized coordinates
///   f_n = sum_k c_k * u_n^i * v_n^j,  u_n = s(u - cu), v_n = s(v - cv)
/// into real-coordinate coefficients, then apply the output-side affine
/// g = outScale * f_n + outShift.
std::vector<double> expandNormalizedPolynomial(const std::vector<double>& coeffs,
                                               const std::vector<std::pair<int, int>>& basis,
                                               double s, double cu, double cv,
                                               double outScale, double outShift)
{
    std::vector<double> out(basis.size(), 0.0);
    for (size_t k = 0; k < basis.size(); ++k) {
        const auto [i, j] = basis[k];
        const double c = coeffs[k];
        if (c == 0.0)
            continue;
        const double sPow = std::pow(s, i + j);
        for (int p = 0; p <= i; ++p) {
            for (int q = 0; q <= j; ++q) {
                const double term = sPow * binomial(i, p) * std::pow(-cu, i - p) *
                                    binomial(j, q) * std::pow(-cv, j - q);
                const auto it = std::ranges::find(basis, std::make_pair(p, q));
                out[static_cast<size_t>(it - basis.begin())] += c * term;
            }
        }
    }
    for (double& coefficient : out)
        coefficient *= outScale;
    out[0] += outShift;
    return out;
}

// Hartley-normalized polynomial solve shared by Affine/P2/P3.
bool solvePolynomial(const std::vector<std::pair<double, double>>& sourcePts,
                     const std::vector<std::pair<double, double>>& targetPts,
                     int degree, TransformResult& result)
{
    const auto basis = monomialBasis(degree);
    const auto n = basis.size();

    const HartleyNormalizer srcNorm = HartleyNormalizer::fit(sourcePts);
    const HartleyNormalizer dstNorm = HartleyNormalizer::fit(targetPts);

    Matrix design;
    design.rows = static_cast<int>(sourcePts.size());
    design.cols = static_cast<int>(n);
    design.a.resize(static_cast<size_t>(design.rows) * n);
    std::vector<double> bx(sourcePts.size(), 0.0);
    std::vector<double> by(sourcePts.size(), 0.0);
    for (size_t r = 0; r < sourcePts.size(); ++r) {
        const auto [un, vn] = srcNorm.apply(sourcePts[r].first, sourcePts[r].second);
        for (size_t k = 0; k < n; ++k)
            design.at(static_cast<int>(r), static_cast<int>(k)) = monomialValue(basis[k], un, vn);
        const auto [xn, yn] = dstNorm.apply(targetPts[r].first, targetPts[r].second);
        bx[r] = xn;
        by[r] = yn;
    }

    std::vector<double> cx, cy;
    double kappa = 0.0;
    if (!detail::solveLeastSquares(design, bx, cx, kappa))
        return false;
    double kappaY = 0.0;
    if (!detail::solveLeastSquares(design, by, cy, kappaY))
        return false;
    result.conditionNumber = std::max(kappa, kappaY);
    if (result.conditionNumber > kMaxConditionNumber)
        return false;

    // x = x_n / dstNorm.scale + dstNorm.cx — expand the composition.
    const std::vector<double> realX = expandNormalizedPolynomial(cx, basis, srcNorm.scale,
                                                                 srcNorm.cx, srcNorm.cy,
                                                                 1.0 / dstNorm.scale, dstNorm.cx);
    const std::vector<double> realY = expandNormalizedPolynomial(cy, basis, srcNorm.scale,
                                                                 srcNorm.cx, srcNorm.cy,
                                                                 1.0 / dstNorm.scale, dstNorm.cy);
    result.forwardCoeffs.assign(realX.begin(), realX.end());
    result.forwardCoeffs.insert(result.forwardCoeffs.end(), realY.begin(), realY.end());
    return true;
}

// ---- Closed-form rigid / similarity (Procrustes) --------------------------

TransformResult solveRigidOrSimilarity(const std::vector<std::pair<double, double>>& sourcePts,
                                       const std::vector<std::pair<double, double>>& targetPts,
                                       bool withScale)
{
    TransformResult result;
    const double count = static_cast<double>(sourcePts.size());
    double muU = 0.0, muV = 0.0, muX = 0.0, muY = 0.0;
    for (const auto& [u, v] : sourcePts) {
        muU += u / count;
        muV += v / count;
    }
    for (const auto& [x, y] : targetPts) {
        muX += x / count;
        muY += y / count;
    }
    double z = 0.0; // sum of (x-x̄)(u-ū) + (y-ȳ)(v-v̄)
    double w = 0.0; // sum of (y-ȳ)(u-ū) - (x-x̄)(v-v̄)
    double t = 0.0; // sum of (u-ū)² + (v-v̄)²
    for (size_t i = 0; i < sourcePts.size(); ++i) {
        const auto [u, v] = sourcePts[i];
        const auto [x, y] = targetPts[i];
        const double du = u - muU;
        const double dv = v - muV;
        const double dx = x - muX;
        const double dy = y - muY;
        z += dx * du + dy * dv;
        w += dy * du - dx * dv;
        t += du * du + dv * dv;
    }
    double a = 1.0, b = 0.0;
    if (t <= 1e-300)
        return result; // coincident sources: no rotation is defined
    if (withScale) {
        a = z / t;
        b = w / t;
    } else {
        const double angle = std::atan2(w, z);
        a = std::cos(angle);
        b = std::sin(angle);
    }
    const double scale = std::hypot(a, b);
    if (!(scale > 1e-300))
        return result;
    const double theta = std::atan2(b, a);
    const double tx = muX - (a * muU - b * muV);
    const double ty = muY - (b * muU + a * muV);

    if (withScale)
        result.forwardCoeffs = {scale, theta, tx, ty};
    else
        result.forwardCoeffs = {theta, tx, ty};

    // Inverse: u = (c(x-tx) + s(y-ty))/scale, v = (-s(x-tx) + c(y-ty))/scale.
    const double c = a / scale;
    const double s = b / scale;
    if (withScale)
        result.backwardCoeffs = {1.0 / scale, -theta, -(c * tx + s * ty) / scale,
                                 (s * tx - c * ty) / scale};
    else
        result.backwardCoeffs = {-theta, -(c * tx + s * ty), (s * tx - c * ty)};
    result.conditionNumber = 1.0;
    result.success = true;
    return result;
}

std::vector<double> solveTranslation(const std::vector<std::pair<double, double>>& sourcePts,
                                     const std::vector<std::pair<double, double>>& targetPts)
{
    const double count = static_cast<double>(sourcePts.size());
    double tx = 0.0, ty = 0.0;
    for (size_t i = 0; i < sourcePts.size(); ++i) {
        tx += (targetPts[i].first - sourcePts[i].first) / count;
        ty += (targetPts[i].second - sourcePts[i].second) / count;
    }
    return {tx, ty};
}

void computeRMSE(TransformResult& result,
                 const std::vector<std::pair<double, double>>& sourcePts,
                 const std::vector<std::pair<double, double>>& targetPts)
{
    double sumSqF = 0.0;
    for (size_t i = 0; i < sourcePts.size(); ++i) {
        const auto [x, y] = GeometricTransform::applyForward(result, sourcePts[i].first, sourcePts[i].second);
        sumSqF += std::pow(x - targetPts[i].first, 2) + std::pow(y - targetPts[i].second, 2);
    }
    result.rmseForward = std::sqrt(sumSqF / static_cast<double>(sourcePts.size()));

    double sumSqB = 0.0;
    for (size_t i = 0; i < sourcePts.size(); ++i) {
        const auto [u, v] = GeometricTransform::applyBackward(result, targetPts[i].first, targetPts[i].second);
        sumSqB += std::pow(u - sourcePts[i].first, 2) + std::pow(v - sourcePts[i].second, 2);
    }
    result.rmseBackward = std::sqrt(sumSqB / static_cast<double>(sourcePts.size()));
}

} // namespace

int GeometricTransform::minPointsRequired(TransformModel model) noexcept
{
    switch (model) {
    case TransformModel::Translation: return 1;
    case TransformModel::Rigid: return 2;
    case TransformModel::Similarity: return 2;
    case TransformModel::Affine: return 3;
    case TransformModel::Polynomial2: return 6;
    case TransformModel::Polynomial3: return 10;
    case TransformModel::Projective: return 4;
    }
    return 3;
}

TransformResult GeometricTransform::solve(TransformModel model,
                                          const std::vector<std::pair<double, double>>& sourcePts,
                                          const std::vector<std::pair<double, double>>& targetPts)
{
    if (sourcePts.size() != targetPts.size())
        throw std::invalid_argument("source/target point count mismatch");
    if (sourcePts.size() < static_cast<size_t>(minPointsRequired(model)))
        throw std::invalid_argument("insufficient points for the requested transform model");
    for (const auto& p : sourcePts)
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            throw std::invalid_argument("non-finite source coordinate");
    for (const auto& p : targetPts)
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            throw std::invalid_argument("non-finite target coordinate");

    TransformResult result;
    result.model = model;

    switch (model) {
    case TransformModel::Translation:
        result.forwardCoeffs = solveTranslation(sourcePts, targetPts);
        result.backwardCoeffs = {-result.forwardCoeffs[0], -result.forwardCoeffs[1]};
        result.conditionNumber = 1.0;
        result.success = true;
        break;
    case TransformModel::Rigid:
        result = solveRigidOrSimilarity(sourcePts, targetPts, false);
        result.model = model;
        break;
    case TransformModel::Similarity:
        result = solveRigidOrSimilarity(sourcePts, targetPts, true);
        result.model = model;
        break;
    case TransformModel::Affine:
    case TransformModel::Polynomial2:
    case TransformModel::Polynomial3: {
        // The polynomial path solves one direction at a time; the inverse
        // coefficients come from an independent swapped solve so the backward
        // map is a genuine least-squares fit of the reversed correspondences.
        const int degree = (model == TransformModel::Affine) ? 1
                           : (model == TransformModel::Polynomial2) ? 2 : 3;
        TransformResult forward;
        TransformResult backward;
        if (solvePolynomial(sourcePts, targetPts, degree, forward) &&
            solvePolynomial(targetPts, sourcePts, degree, backward)) {
            result.forwardCoeffs = std::move(forward.forwardCoeffs);
            result.backwardCoeffs = std::move(backward.forwardCoeffs);
            result.conditionNumber = std::max(forward.conditionNumber, backward.conditionNumber);
            result.success = result.conditionNumber <= kMaxConditionNumber;
        }
        result.model = model;
        break;
    }
    case TransformModel::Projective: {
        std::array<double, 9> forward{};
        std::array<double, 9> backward{};
        double kappaF = 0.0;
        double kappaB = 0.0;
        if (dltHomography(sourcePts, targetPts, forward, kappaF) &&
            dltHomography(targetPts, sourcePts, backward, kappaB)) {
            result.forwardCoeffs.assign(forward.begin(), forward.end());
            result.backwardCoeffs.assign(backward.begin(), backward.end());
            result.conditionNumber = std::max(kappaF, kappaB);
            result.success = result.conditionNumber <= kMaxConditionNumber;
        }
        result.model = model;
        break;
    }
    }

    if (result.success)
        computeRMSE(result, sourcePts, targetPts);
    return result;
}

namespace {

// Coefficients are stored as [x-terms..., y-terms...] over the given basis;
// backward coefficients reuse the same evaluator on their own coordinates.
bool applyPolynomial(const std::vector<double>& coeffs, size_t half, int degree,
                     double inU, double inV, double& outX, double& outY)
{
    if (coeffs.size() != 2 * half)
        return false;
    const auto basis = monomialBasis(degree);
    if (basis.size() != half)
        return false;
    const double* cx = coeffs.data();
    const double* cy = coeffs.data() + half;
    double sumX = 0.0, sumY = 0.0;
    for (size_t k = 0; k < basis.size(); ++k) {
        const double m = monomialValue(basis[k], inU, inV);
        sumX += cx[k] * m;
        sumY += cy[k] * m;
    }
    outX = sumX;
    outY = sumY;
    return true;
}

} // namespace

std::pair<double, double> GeometricTransform::applyForward(const TransformResult& res, double u, double v)
{
    const auto& c = res.forwardCoeffs;
    if (c.empty())
        return {u, v}; // failed/empty solve maps identically
    switch (res.model) {
    case TransformModel::Translation:
        return {u + c[0], v + c[1]};
    case TransformModel::Rigid: {
        const double theta = c[0];
        return {c[1] + std::cos(theta) * u - std::sin(theta) * v,
                c[2] + std::sin(theta) * u + std::cos(theta) * v};
    }
    case TransformModel::Similarity: {
        const double s = c[0], theta = c[1];
        return {c[2] + s * (std::cos(theta) * u - std::sin(theta) * v),
                c[3] + s * (std::sin(theta) * u + std::cos(theta) * v)};
    }
    case TransformModel::Affine: {
        double x = 0.0, y = 0.0;
        applyPolynomial(c, 3, 1, u, v, x, y);
        return {x, y};
    }
    case TransformModel::Polynomial2: {
        double x = 0.0, y = 0.0;
        applyPolynomial(c, 6, 2, u, v, x, y);
        return {x, y};
    }
    case TransformModel::Polynomial3: {
        double x = 0.0, y = 0.0;
        applyPolynomial(c, 10, 3, u, v, x, y);
        return {x, y};
    }
    case TransformModel::Projective: {
        if (c.size() < 9)
            return {u, v};
        std::array<double, 9> h{};
        std::copy(c.begin(), c.begin() + 9, h.begin());
        const double w = h[6] * u + h[7] * v + h[8];
        if (std::abs(w) < 1e-12)
            return {std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity()};
        return {(h[0] * u + h[1] * v + h[2]) / w, (h[3] * u + h[4] * v + h[5]) / w};
    }
    }
    return {u, v};
}

std::pair<double, double> GeometricTransform::applyBackward(const TransformResult& res, double x, double y)
{
    const auto& c = res.backwardCoeffs;
    if (c.empty())
        return {x, y};
    switch (res.model) {
    case TransformModel::Translation:
        return {x + c[0], y + c[1]};
    case TransformModel::Rigid: {
        const double theta = c[0];
        return {c[1] + std::cos(theta) * x - std::sin(theta) * y,
                c[2] + std::sin(theta) * x + std::cos(theta) * y};
    }
    case TransformModel::Similarity: {
        const double s = c[0], theta = c[1];
        return {c[2] + s * (std::cos(theta) * x - std::sin(theta) * y),
                c[3] + s * (std::sin(theta) * x + std::cos(theta) * y)};
    }
    case TransformModel::Affine: {
        double u = 0.0, v = 0.0;
        applyPolynomial(c, 3, 1, x, y, u, v);
        return {u, v};
    }
    case TransformModel::Polynomial2: {
        double u = 0.0, v = 0.0;
        applyPolynomial(c, 6, 2, x, y, u, v);
        return {u, v};
    }
    case TransformModel::Polynomial3: {
        double u = 0.0, v = 0.0;
        applyPolynomial(c, 10, 3, x, y, u, v);
        return {u, v};
    }
    case TransformModel::Projective: {
        if (c.size() < 9)
            return {x, y};
        std::array<double, 9> h{};
        std::copy(c.begin(), c.begin() + 9, h.begin());
        const double w = h[6] * x + h[7] * y + h[8];
        if (std::abs(w) < 1e-12)
            return {std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity()};
        return {(h[0] * x + h[1] * y + h[2]) / w, (h[3] * x + h[4] * y + h[5]) / w};
    }
    }
    return {x, y};
}

void GeometricTransform::applyForwardBatch(const TransformResult& res,
                                           std::span<const double> srcU,
                                           std::span<const double> srcV,
                                           std::span<double> dstX,
                                           std::span<double> dstY)
{
    if (srcU.size() != srcV.size() || srcU.size() != dstX.size() || srcU.size() != dstY.size())
        return;
    for (size_t i = 0; i < srcU.size(); ++i) {
        const auto [x, y] = applyForward(res, srcU[i], srcV[i]);
        dstX[i] = x;
        dstY[i] = y;
    }
}

} // namespace rs::algorithms
