// test_tps_interpolator.cpp — D14 Package C: thin plate spline tests.
// Truths are mathematical properties of the TPS itself: exact interpolation
// at the knots, exact affine reproduction, positive bending energy under
// non-rigid deformation, and the closed-form basis values — none of them are
// computed by the implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/tps_interpolator.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::algorithms;

namespace {

std::vector<std::pair<double, double>> squareWithCenterSource()
{
    return {{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}, {50.0, 50.0}};
}

/// Corners fixed; the center knot is displaced to (55, 52) — non-rigid.
std::vector<std::pair<double, double>> centerDisplacedTarget()
{
    return {{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}, {55.0, 52.0}};
}

} // namespace

TEST_CASE("test_tps_interpolator - Radial basis matches r^2 ln r with a safe origin", "[tps][d14]")
{
    REQUIRE(TpsInterpolator::radialBasis(0.0) == 0.0);
    // r²·ln r at r = 1 is 0; at r = sqrt(e) it is e·(1/2); at r = 0.5 it is
    // 0.25·ln(0.5) (negative — the basis dips below zero inside the unit disc).
    REQUIRE(TpsInterpolator::radialBasis(1.0) == 0.0);
    REQUIRE_THAT(TpsInterpolator::radialBasis(std::sqrt(std::exp(1.0))), WithinAbs(std::exp(1.0) / 2.0, 1e-12));
    REQUIRE_THAT(TpsInterpolator::radialBasis(0.5), WithinAbs(0.25 * std::log(0.5), 1e-12));
    // Sub-tolerance radii short-circuit instead of producing NaN from ln(0).
    REQUIRE(TpsInterpolator::radialBasis(1e-300) == 0.0);
    REQUIRE(std::isfinite(TpsInterpolator::radialBasis(1e-13)));
}

TEST_CASE("test_tps_interpolator - Exact interpolation at all five knots with lambda = 0", "[tps][d14]")
{
    const auto src = squareWithCenterSource();
    const auto dst = centerDisplacedTarget();

    TpsInterpolator tps;
    REQUIRE(tps.fit(src, dst, TpsConfig{.regularizationLambda = 0.0}));
    REQUIRE(tps.isFitted());
    REQUIRE(tps.knotCount() == 5);

    for (size_t i = 0; i < src.size(); ++i) {
        const auto [x, y] = tps.transform(src[i].first, src[i].second);
        REQUIRE_THAT(x, WithinAbs(dst[i].first, 1e-10));
        REQUIRE_THAT(y, WithinAbs(dst[i].second, 1e-10));
    }
    REQUIRE(tps.computeBendingEnergy() > 0.0);
}

TEST_CASE("test_tps_interpolator - The spline reproduces affine maps exactly with zero bending energy", "[tps][d14]")
{
    // Targets are a similarity transform of the sources: the polynomial part
    // must absorb the whole map, leaving weights (and hence energy) at zero.
    const auto src = squareWithCenterSource();
    const double c = std::cos(0.7), s = std::sin(0.7);
    std::vector<std::pair<double, double>> dst;
    for (auto [u, v] : src)
        dst.emplace_back(3.0 + 2.0 * (c * u - s * v), -4.0 + 2.0 * (s * u + c * v));

    TpsInterpolator tps;
    REQUIRE(tps.fit(src, dst, TpsConfig{.regularizationLambda = 0.0}));
    for (double u = -25.0; u <= 125.0; u += 12.5) {
        for (double v = -25.0; v <= 125.0; v += 12.5) {
            const auto [x, y] = tps.transform(u, v);
            const double ex = 3.0 + 2.0 * (c * u - s * v);
            const double ey = -4.0 + 2.0 * (s * u + c * v);
            REQUIRE(std::abs(x - ex) < 1e-8);
            REQUIRE(std::abs(y - ey) < 1e-8);
        }
    }
    REQUIRE(tps.computeBendingEnergy() < 1e-12);
}

TEST_CASE("test_tps_interpolator - Regularization trades knot residual for bending energy", "[tps][d14]")
{
    const auto src = squareWithCenterSource();
    const auto dst = centerDisplacedTarget();

    TpsInterpolator exact;
    REQUIRE(exact.fit(src, dst, TpsConfig{.regularizationLambda = 0.0}));
    const double exactEnergy = exact.computeBendingEnergy();

    TpsInterpolator smooth;
    REQUIRE(smooth.fit(src, dst, TpsConfig{.regularizationLambda = 100.0}));
    REQUIRE(smooth.computeBendingEnergy() < exactEnergy);

    // The smoothing spline no longer passes exactly through the moved knot.
    const auto [x, y] = smooth.transform(50.0, 50.0);
    REQUIRE(std::hypot(x - 55.0, y - 52.0) > 1e-6);
    // ...but stays bounded: it cannot wander arbitrarily far.
    REQUIRE(std::hypot(x - 55.0, y - 52.0) < 5.0);
}

TEST_CASE("test_tps_interpolator - Batch transform equals pointwise evaluation", "[tps][d14]")
{
    const auto src = squareWithCenterSource();
    TpsInterpolator tps;
    REQUIRE(tps.fit(src, centerDisplacedTarget()));

    std::vector<double> us{0.0, 17.25, 50.0, 100.0, 83.5}, vs{0.0, 64.0, 50.0, 12.75, 100.0};
    std::vector<double> xs(us.size(), 0.0), ys(us.size(), 0.0);
    tps.transformBatch(us, vs, xs, ys);
    for (size_t i = 0; i < us.size(); ++i) {
        const auto [x, y] = tps.transform(us[i], vs[i]);
        REQUIRE_THAT(xs[i], WithinAbs(x, 1e-12));
        REQUIRE_THAT(ys[i], WithinAbs(y, 1e-12));
    }
}

TEST_CASE("test_tps_interpolator - Unfitted splines map identically", "[tps][d14]")
{
    TpsInterpolator tps;
    REQUIRE_FALSE(tps.isFitted());
    const auto [x, y] = tps.transform(31.4, -15.9);
    REQUIRE_THAT(x, WithinAbs(31.4, 0.0));
    REQUIRE_THAT(y, WithinAbs(-15.9, 0.0));
    REQUIRE(tps.computeBendingEnergy() == 0.0);
}

TEST_CASE("test_tps_interpolator - fit rejects malformed inputs and deduplicates knots", "[tps][d14]")
{
    TpsInterpolator tps;
    const auto src = squareWithCenterSource();
    const auto dst = centerDisplacedTarget();

    // Too few points.
    REQUIRE_FALSE(tps.fit({src[0], src[1]}, {dst[0], dst[1]}));
    // Size mismatch.
    REQUIRE_FALSE(tps.fit(src, {dst[0], dst[1], dst[2], dst[3]}));
    // Non-finite coordinates.
    std::vector<std::pair<double, double>> nanSrc = src;
    nanSrc[2].first = std::nan("");
    REQUIRE_FALSE(tps.fit(nanSrc, dst));

    // Duplicate knot is deduplicated, not rejected.
    auto dupSrc = src;
    dupSrc.push_back({50.0 + 1e-12, 50.0});
    auto dupDst = dst;
    dupDst.push_back({55.0, 52.0});
    REQUIRE(tps.fit(dupSrc, dupDst));
    REQUIRE(tps.knotCount() == 5);
    const auto [x, y] = tps.transform(50.0, 50.0);
    REQUIRE_THAT(x, WithinAbs(55.0, 1e-10));
}
