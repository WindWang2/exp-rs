// test_geometric_transform.cpp — D14 Package B: closed-form transform tests.
// Expected values are analytic: a hand-composed rotation of 30 degrees
// (cos 30 = sqrt(3)/2, sin 30 = 1/2) with scale 1.5 and translation
// (25, -10); explicit polynomial coefficients evaluated by hand; a documented
// projective matrix. Nothing is derived from the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/geometric_transform.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::algorithms;

namespace {

constexpr double kSqrt3_2 = 0.8660254037844386;
constexpr double kCos30 = kSqrt3_2;
constexpr double kSin30 = 0.5;

/// Similarity truth: x = 25 + 1.5(cos30·u − sin30·v), y = −10 + 1.5(sin30·u + cos30·v)
std::pair<double, double> analyticSimilarity(double u, double v)
{
    return {25.0 + 1.5 * (kCos30 * u - kSin30 * v), -10.0 + 1.5 * (kSin30 * u + kCos30 * v)};
}

/// 4x4 grid on [0, 100]^2.
std::vector<std::pair<double, double>> makeGrid(int side, double extent = 100.0)
{
    std::vector<std::pair<double, double>> pts;
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i)
            pts.emplace_back(extent * i / (side - 1.0), extent * j / (side - 1.0));
    return pts;
}

} // namespace

TEST_CASE("test_geometric_transform - Minimum point requirements follow the model table", "[transform][d14]")
{
    REQUIRE(GeometricTransform::minPointsRequired(TransformModel::Translation) == 1);
    REQUIRE(GeometricTransform::minPointsRequired(TransformModel::Rigid) == 2);
    REQUIRE(GeometricTransform::minPointsRequired(TransformModel::Similarity) == 2);
    REQUIRE(GeometricTransform::minPointsRequired(TransformModel::Affine) == 3);
    REQUIRE(GeometricTransform::minPointsRequired(TransformModel::Polynomial2) == 6);
    REQUIRE(GeometricTransform::minPointsRequired(TransformModel::Polynomial3) == 10);
    REQUIRE(GeometricTransform::minPointsRequired(TransformModel::Projective) == 4);
}

TEST_CASE("test_geometric_transform - Insufficient or invalid input points throw", "[transform][d14]")
{
    const std::vector<std::pair<double, double>> one{{1.0, 1.0}};
    const std::vector<std::pair<double, double>> two{{1.0, 1.0}, {2.0, 2.0}};
    REQUIRE_THROWS_AS(GeometricTransform::solve(TransformModel::Affine, one, one),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(GeometricTransform::solve(TransformModel::Projective, two, two),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(GeometricTransform::solve(TransformModel::Translation, one, two),
                      std::invalid_argument);

    const std::vector<std::pair<double, double>> bad{{1.0, std::nan("")}};
    REQUIRE_THROWS_AS(GeometricTransform::solve(TransformModel::Translation, bad, bad),
                      std::invalid_argument);
}

TEST_CASE("test_geometric_transform - Translation recovers the exact shift", "[transform][d14]")
{
    const auto src = makeGrid(4);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src)
        dst.emplace_back(u + 25.0, v - 10.0);

    const auto res = GeometricTransform::solve(TransformModel::Translation, src, dst);
    REQUIRE(res.success);
    REQUIRE_THAT(res.forwardCoeffs[0], WithinAbs(25.0, 1e-9));
    REQUIRE_THAT(res.forwardCoeffs[1], WithinAbs(-10.0, 1e-9));
    REQUIRE_THAT(res.rmseForward, WithinAbs(0.0, 1e-9));

    const auto [x, y] = GeometricTransform::applyForward(res, 7.0, 3.0);
    REQUIRE_THAT(x, WithinAbs(32.0, 1e-12));
    REQUIRE_THAT(y, WithinAbs(-7.0, 1e-12));
    const auto [u, v] = GeometricTransform::applyBackward(res, 32.0, -7.0);
    REQUIRE_THAT(u, WithinAbs(7.0, 1e-12));
    REQUIRE_THAT(v, WithinAbs(3.0, 1e-12));
}

TEST_CASE("test_geometric_transform - Rigid recovers pure 30 degree rotation without scale", "[transform][d14]")
{
    const auto src = makeGrid(4);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src) {
        // Rotation by 30 degrees about the origin, no scale.
        dst.emplace_back(kCos30 * u - kSin30 * v, kSin30 * u + kCos30 * v);
    }

    const auto res = GeometricTransform::solve(TransformModel::Rigid, src, dst);
    REQUIRE(res.success);
    REQUIRE_THAT(res.forwardCoeffs[0], WithinAbs(std::numbers::pi / 6.0, 1e-9));
    REQUIRE_THAT(res.rmseForward, WithinAbs(0.0, 1e-6));
    // Round-trip closure at an arbitrary point.
    const auto [x, y] = GeometricTransform::applyForward(res, 31.4, -15.9);
    const auto [u, v] = GeometricTransform::applyBackward(res, x, y);
    REQUIRE_THAT(u, WithinAbs(31.4, 1e-6));
    REQUIRE_THAT(v, WithinAbs(-15.9, 1e-6));
}

TEST_CASE("test_geometric_transform - Similarity recovers scale 1.5, rotation 30 deg, translation (25,-10)", "[transform][d14]")
{
    const auto src = makeGrid(4);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src)
        dst.push_back(analyticSimilarity(u, v));

    const auto res = GeometricTransform::solve(TransformModel::Similarity, src, dst);
    REQUIRE(res.success);
    REQUIRE(res.forwardCoeffs.size() == 4);
    REQUIRE_THAT(res.forwardCoeffs[0], WithinAbs(1.5, 1e-9));                    // scale
    REQUIRE_THAT(res.forwardCoeffs[1], WithinAbs(std::numbers::pi / 6.0, 1e-9)); // theta
    REQUIRE_THAT(res.forwardCoeffs[2], WithinAbs(25.0, 1e-9));                   // tx
    REQUIRE_THAT(res.forwardCoeffs[3], WithinAbs(-10.0, 1e-9));                  // ty
    REQUIRE_THAT(res.rmseForward, WithinAbs(0.0, 1e-6));
}

TEST_CASE("test_geometric_transform - Affine coefficients match the analytic rotation-scale-translation", "[transform][d14]")
{
    const auto src = makeGrid(4);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src)
        dst.push_back(analyticSimilarity(u, v));

    const auto res = GeometricTransform::solve(TransformModel::Affine, src, dst);
    REQUIRE(res.success);
    REQUIRE(res.forwardCoeffs.size() == 6);
    REQUIRE_THAT(res.forwardCoeffs[0], WithinAbs(25.0, 1e-6));
    REQUIRE_THAT(res.forwardCoeffs[1], WithinAbs(1.5 * kCos30, 1e-6)); // 1.299038105676658
    REQUIRE_THAT(res.forwardCoeffs[2], WithinAbs(-1.5 * kSin30, 1e-6)); // -0.75
    REQUIRE_THAT(res.forwardCoeffs[3], WithinAbs(-10.0, 1e-6));
    REQUIRE_THAT(res.forwardCoeffs[4], WithinAbs(1.5 * kSin30, 1e-6)); // 0.75
    REQUIRE_THAT(res.forwardCoeffs[5], WithinAbs(1.5 * kCos30, 1e-6));
    REQUIRE_THAT(res.rmseForward, WithinAbs(0.0, 1e-6));
    // The Hartley-normalized design of a well-spread grid is well-conditioned.
    REQUIRE(res.conditionNumber < 100.0);
}

TEST_CASE("test_geometric_transform - Affine inverse closes the loop below 1e-5 pixel", "[transform][d14]")
{
    const auto src = makeGrid(4);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src)
        dst.push_back(analyticSimilarity(u, v));
    const auto res = GeometricTransform::solve(TransformModel::Affine, src, dst);
    REQUIRE(res.success);

    for (double u = -20.0; u <= 120.0; u += 17.3) {
        for (double v = -20.0; v <= 120.0; v += 19.7) {
            const auto [x, y] = GeometricTransform::applyForward(res, u, v);
            const auto [ub, vb] = GeometricTransform::applyBackward(res, x, y);
            REQUIRE(std::abs(ub - u) < 1e-5);
            REQUIRE(std::abs(vb - v) < 1e-5);
        }
    }
    // The analytic inverse matrix entries (from inverting the 2x2 rotation
    // block by hand): u = (c(x-tx) + s(y-ty))/s, so bwd[1] = c/s, bwd[2] = s/s.
    REQUIRE_THAT(res.backwardCoeffs[1], WithinAbs(kCos30 / 1.5, 1e-6));
    REQUIRE_THAT(res.backwardCoeffs[2], WithinAbs(kSin30 / 1.5, 1e-6));
}

TEST_CASE("test_geometric_transform - Polynomial2 recovers hand-chosen real coefficients exactly", "[transform][d14]")
{
    // Truth polynomial chosen by hand:
    //   x = 1 + 2u - v + 0.5u² - 0.3uv + 0.1v²
    //   y = -2 + 0.1u + 3v - 0.2u² + 0.05uv + 0.4v²
    const std::vector<double> truthX{1.0, 2.0, -1.0, 0.5, -0.3, 0.1};
    const std::vector<double> truthY{-2.0, 0.1, 3.0, -0.2, 0.05, 0.4};
    const auto basisEval = [](double u, double v) {
        return std::vector<double>{1.0, u, v, u * u, u * v, v * v};
    };

    const auto src = makeGrid(4);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src) {
        const auto bx = basisEval(u, v);
        double x = 0.0, y = 0.0;
        for (size_t k = 0; k < bx.size(); ++k) {
            x += truthX[k] * bx[k];
            y += truthY[k] * bx[k];
        }
        dst.emplace_back(x, y);
    }

    const auto res = GeometricTransform::solve(TransformModel::Polynomial2, src, dst);
    REQUIRE(res.success);
    REQUIRE(res.forwardCoeffs.size() == 12);
    for (size_t k = 0; k < truthX.size(); ++k) {
        REQUIRE_THAT(res.forwardCoeffs[k], WithinAbs(truthX[k], 1e-6));
        REQUIRE_THAT(res.forwardCoeffs[6 + k], WithinAbs(truthY[k], 1e-6));
    }
    REQUIRE_THAT(res.rmseForward, WithinAbs(0.0, 1e-6));
}

TEST_CASE("test_geometric_transform - Polynomial3 recovers hand-chosen real coefficients exactly", "[transform][d14]")
{
    const std::vector<double> truthX{1.0, 2.0, -1.0, 0.5, -0.3, 0.1, 0.02, -0.01, 0.03, -0.02};
    const std::vector<double> truthY{-2.0, 0.1, 3.0, -0.2, 0.05, 0.4, -0.01, 0.02, 0.005, -0.015};
    const auto basisEval = [](double u, double v) {
        return std::vector<double>{1.0, u, v, u * u, u * v, v * v,
                                   u * u * u, u * u * v, u * v * v, v * v * v};
    };

    const auto src = makeGrid(5); // 25 points >= 10 required
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src) {
        const auto bx = basisEval(u, v);
        double x = 0.0, y = 0.0;
        for (size_t k = 0; k < bx.size(); ++k) {
            x += truthX[k] * bx[k];
            y += truthY[k] * bx[k];
        }
        dst.emplace_back(x, y);
    }

    const auto res = GeometricTransform::solve(TransformModel::Polynomial3, src, dst);
    REQUIRE(res.success);
    REQUIRE(res.forwardCoeffs.size() == 20);
    for (size_t k = 0; k < truthX.size(); ++k) {
        REQUIRE_THAT(res.forwardCoeffs[k], WithinAbs(truthX[k], 1e-5));
        REQUIRE_THAT(res.forwardCoeffs[10 + k], WithinAbs(truthY[k], 1e-5));
    }
    REQUIRE_THAT(res.rmseForward, WithinAbs(0.0, 1e-6));
}

TEST_CASE("test_geometric_transform - Projective DLT recovers the documented homography", "[transform][d14]")
{
    // Same truth matrix as the Package D interop case.
    const std::vector<double> H{1.05, -0.02, 10.0,
                                0.02, 1.03, -5.0,
                                0.0001, 0.0001, 1.0};
    const auto src = makeGrid(5);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src) {
        const double w = H[6] * u + H[7] * v + H[8];
        dst.emplace_back((H[0] * u + H[1] * v + H[2]) / w, (H[3] * u + H[4] * v + H[5]) / w);
    }

    const auto res = GeometricTransform::solve(TransformModel::Projective, src, dst);
    REQUIRE(res.success);
    REQUIRE(res.forwardCoeffs.size() == 9);
    // h33 is normalized to 1; the truth matrix already has h33 = 1.
    for (int k = 0; k < 9; ++k)
        REQUIRE_THAT(res.forwardCoeffs[static_cast<size_t>(k)], WithinAbs(H[static_cast<size_t>(k)], 1e-6));
    REQUIRE_THAT(res.rmseForward, WithinAbs(0.0, 1e-6));

    // Closure well away from the vanishing line.
    const auto [x, y] = GeometricTransform::applyForward(res, 42.0, 17.0);
    const auto [u, v] = GeometricTransform::applyBackward(res, x, y);
    REQUIRE(std::abs(u - 42.0) < 1e-5);
    REQUIRE(std::abs(v - 17.0) < 1e-5);
}

TEST_CASE("test_geometric_transform - Degenerate geometry reports failure instead of garbage", "[transform][d14]")
{
    // Exactly collinear affine: design rank drops to 2.
    const std::vector<std::pair<double, double>> src{{0.0, 0.0}, {50.0, 50.0}, {100.0, 100.0}};
    std::vector<std::pair<double, double>> dst{{0.0, 1.0}, {50.0, 51.0}, {100.0, 101.0}};
    const auto res = GeometricTransform::solve(TransformModel::Affine, src, dst);
    REQUIRE_FALSE(res.success);
    REQUIRE(res.forwardCoeffs.empty());

    // Nearly collinear: solvable but visibly degraded conditioning (the
    // Hartley-normalized design still spans a thin triangle).
    const std::vector<std::pair<double, double>> src2{{0.0, 0.0}, {50.0, 50.0}, {100.0, 100.5}};
    const auto res2 = GeometricTransform::solve(TransformModel::Affine, src2, dst);
    if (res2.success)
        REQUIRE(res2.conditionNumber > 100.0);
}

TEST_CASE("test_geometric_transform - Batch mapping agrees with pointwise evaluation", "[transform][d14]")
{
    const auto src = makeGrid(4);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src)
        dst.push_back(analyticSimilarity(u, v));
    const auto res = GeometricTransform::solve(TransformModel::Similarity, src, dst);
    REQUIRE(res.success);

    std::vector<double> us{0.0, 13.5, 100.0, -7.25}, vs{0.0, 41.0, -3.0, 88.75};
    std::vector<double> xs(us.size(), 0.0), ys(us.size(), 0.0);
    GeometricTransform::applyForwardBatch(res, us, vs, xs, ys);
    for (size_t i = 0; i < us.size(); ++i) {
        const auto [x, y] = GeometricTransform::applyForward(res, us[i], vs[i]);
        REQUIRE_THAT(xs[i], WithinAbs(x, 1e-12));
        REQUIRE_THAT(ys[i], WithinAbs(y, 1e-12));
    }
}
