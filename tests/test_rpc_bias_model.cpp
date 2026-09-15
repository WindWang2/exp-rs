// test_rpc_bias_model.cpp — F13 Package D: RPC bias refinement math.
// Ground truth: hand-built bias fields — a constant field (must stay
// Constant), a rotated/linear field (must promote to Affine and recover the
// coefficients), an outlier-resistant constant case, and a closed-form
// height-sensitivity reprojector. Nothing is derived from the code under
// test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/registration/rpc_bias_model.h"

#include <cmath>
#include <vector>

using namespace sicnu::registration;

TEST_CASE("rpc bias: constant field stays Constant with median robustness",
          "[f13][rpc_bias]")
{
    // 8 samples of a constant bias (5, -2) plus ONE outlier (60, 40).
    std::vector<RpcBiasSample> samples;
    for (int i = 0; i < 8; ++i)
        samples.push_back({10.0 + i, 20.0 + 2.0 * i, 5.0, -2.0});
    samples.push_back({50.0, 20.0, 60.0, 40.0}); // gross outlier
    const auto fit = RpcBiasModel::fit(samples);
    REQUIRE(fit.applied);
    REQUIRE(fit.kind == RpcBiasModelKind::Constant);
    // Median kills the single outlier exactly.
    REQUIRE(fit.constX == Catch::Approx(5.0).margin(1e-9));
    REQUIRE(fit.constY == Catch::Approx(-2.0).margin(1e-9));
    REQUIRE(fit.rmseAfter < fit.rmseBefore);
}

TEST_CASE("rpc bias: linear field promotes to Affine and recovers coefficients",
          "[f13][rpc_bias]")
{
    // True bias field: dx = 2 + 0.01x - 0.005y, dy = -1 + 0.002x + 0.004y
    std::vector<RpcBiasSample> samples;
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 5; ++i) {
            const double x = 1000.0 + 100.0 * i;
            const double y = 2000.0 + 80.0 * j;
            samples.push_back({x, y, 2.0 + 0.01 * x - 0.005 * y, -1.0 + 0.002 * x + 0.004 * y});
        }
    const auto fit = RpcBiasModel::fit(samples);
    REQUIRE(fit.applied);
    REQUIRE(fit.kind == RpcBiasModelKind::Affine);
    // Recovered coefficients must match the generator (no noise).
    REQUIRE(fit.affine[0] == Catch::Approx(2.0).margin(1e-6));
    REQUIRE(fit.affine[1] == Catch::Approx(0.01).margin(1e-9));
    REQUIRE(fit.affine[2] == Catch::Approx(-0.005).margin(1e-9));
    REQUIRE(fit.affine[3] == Catch::Approx(-1.0).margin(1e-6));
    REQUIRE(fit.affine[4] == Catch::Approx(0.002).margin(1e-9));
    REQUIRE(fit.affine[5] == Catch::Approx(0.004).margin(1e-9));
    REQUIRE(fit.rmseAfter < 1e-6);
}

TEST_CASE("rpc bias: pure noise field refuses to apply a model", "[f13][rpc_bias][negative]")
{
    // Bias samples that are pure centered noise: no model can improve RMSE.
    std::vector<RpcBiasSample> samples;
    for (int i = 0; i < 12; ++i) {
        const double v = (i % 2 == 0) ? 1.0 : -1.0;
        samples.push_back({10.0 + i, 10.0 + i, v, -v});
    }
    const auto fit = RpcBiasModel::fit(samples);
    // With median 0 both axes and no structure, correction must be refused
    // or not improve: applied == false either way here.
    REQUIRE_FALSE(fit.applied);
    REQUIRE_FALSE(fit.refusalReason.isEmpty());
}

TEST_CASE("rpc bias: too few samples is a refusal", "[f13][rpc_bias][negative]")
{
    std::vector<RpcBiasSample> samples{{0, 0, 1, 1}, {1, 0, 1, 1}};
    const auto fit = RpcBiasModel::fit(samples);
    REQUIRE_FALSE(fit.applied);
    REQUIRE(fit.refusalReason == QStringLiteral("too_few_matches"));
    // Apply on an unapplied fit is the identity.
    const auto [ax, ay] = RpcBiasModel::apply(fit, 42.0, 17.0);
    REQUIRE(ax == Catch::Approx(42.0));
    REQUIRE(ay == Catch::Approx(17.0));
}

TEST_CASE("rpc bias: height sensitivity through a known reprojector",
          "[f13][rpc_bias]")
{
    // Synthetic reprojector: ground shift is (0.3, -0.2) m per meter of
    // height (plus a height-independent constant), so the finite difference
    // must recover exactly those derivatives.
    const auto reproject = [](double x, double y, double h) {
        return std::make_pair(x + 0.3 * h + 11.0, y - 0.2 * h - 7.0);
    };
    const std::vector<std::pair<double, double>> pts{{100.0, 200.0}, {300.0, 400.0}};
    const auto rep = RpcBiasModel::heightSensitivity(pts, 500.0, reproject, 10.0);
    REQUIRE(rep.samples == 2);
    REQUIRE(rep.medianDxPerM == Catch::Approx(0.3).margin(1e-9));
    REQUIRE(rep.medianDyPerM == Catch::Approx(-0.2).margin(1e-9));
    REQUIRE(rep.maxMagnitudePerM == Catch::Approx(std::hypot(0.3, -0.2)).margin(1e-9));
}
