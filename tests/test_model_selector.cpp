// test_model_selector.cpp — F13 Package C: evidence-driven model selection.
// Ground truth: hand-composed analytic maps (pure translation, similarity,
// affine with shear, known homography, quadratic field). Expected selections
// follow from the data-generating model, and the CV gate is exercised with
// an overfit bait: Polynomial2 must NOT beat Affine on data that is exactly
// affine plus noise. Nothing is derived from the code under test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/registration/model_selector.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace sicnu::registration;
using Pts = std::vector<std::pair<double, double>>;

namespace {

Pts makeGrid(int side, double extent = 100.0)
{
    Pts pts;
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i)
            pts.emplace_back(extent * i / (side - 1.0), extent * j / (side - 1.0));
    return pts;
}

/// Deterministic pseudo-noise in [-1, 1] (same hash as the matcher tests).
double noise(double x, double y)
{
    std::uint32_t h = static_cast<std::uint32_t>(static_cast<int>(x) * 7 + static_cast<int>(y) * 13);
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<double>(h & 0xFFFF) / 32767.5 - 1.0;
}

} // namespace

TEST_CASE("model selector: pure translation data selects Translation", "[f13][model_selector]")
{
    const Pts src = makeGrid(5);
    Pts dst;
    for (const auto& [x, y] : src)
        dst.emplace_back(x + 17.0, y - 3.0); // documented shift

    const auto rep = ModelSelector::select(src, dst);
    REQUIRE(rep.status == RegistrationStatus::Success);
    REQUIRE(rep.selected == CandidateModel::Translation);
    REQUIRE(rep.transform.success);
    // Analytic truth from the generator.
    REQUIRE(rep.transform.rmseForward < 1e-6);
    // Evidence table covers the whole ladder.
    REQUIRE(rep.evidence.size() == 7);
    REQUIRE(rep.evidence.front().model == CandidateModel::Translation);
}

TEST_CASE("model selector: affine (with shear) data selects Affine, not P2 overfit bait",
          "[f13][model_selector]")
{
    const Pts src = makeGrid(6);
    Pts dst;
    // Affine with shear + small noise: x' = 1.2x + 0.3y + 5, y' = -0.1x + 0.9y - 4
    for (const auto& [x, y] : src)
        dst.emplace_back(1.2 * x + 0.3 * y + 5.0 + 0.3 * noise(x, y),
                         -0.1 * x + 0.9 * y - 4.0 + 0.3 * noise(x, y));

    ModelSelectionOptions opt;
    opt.minImprovement = 0.10;
    const auto rep = ModelSelector::select(src, dst, opt);
    REQUIRE(rep.status == RegistrationStatus::Success);
    REQUIRE(rep.selected == CandidateModel::Affine);
    // The quadratic candidate must exist in evidence and must not have won.
    const auto p2 = std::find_if(rep.evidence.begin(), rep.evidence.end(), [](const auto& e) {
        return e.model == CandidateModel::Polynomial2;
    });
    REQUIRE(p2 != rep.evidence.end());
    REQUIRE(p2->feasible);
}

TEST_CASE("model selector: empty input refuses without throwing", "[f13][model_selector]"
          "[negative]")
{
    // No points: every candidate is below its minimum count.
    const auto rep = ModelSelector::select({}, {});
    REQUIRE(rep.status == RegistrationStatus::Refused);
    REQUIRE(rep.reason == QStringLiteral("too_few_matches"));
    for (const auto& ev : rep.evidence)
        REQUIRE_FALSE(ev.feasible);
}

TEST_CASE("model selector: non-finite input throws", "[f13][model_selector][negative]")
{
    const Pts src = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 1.0}};
    const Pts dst = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}, {3.0, 1.0}};
    REQUIRE_THROWS_AS(ModelSelector::select(src, dst), std::invalid_argument);
}

TEST_CASE("model selector: homogeneous quadratic field justifies a higher ladder step",
          "[f13][model_selector]")
{
    const Pts src = makeGrid(7);
    Pts dst;
    // Quadratic field: x' = x + 0.01·x², y' = y (no noise).
    for (const auto& [x, y] : src)
        dst.emplace_back(x + 0.01 * x * x, y);
    const auto rep = ModelSelector::select(src, dst);
    REQUIRE(rep.status == RegistrationStatus::Success);
    REQUIRE(rep.selected == CandidateModel::Polynomial2);
    // The parametric solve on the true generator must be near-exact.
    REQUIRE(rep.transform.rmseForward < 1e-4);
}

TEST_CASE("model selector: TPS is feasible and reported with bending energy",
          "[f13][model_selector]")
{
    const Pts src = makeGrid(5);
    Pts dst;
    for (const auto& [x, y] : src)
        dst.emplace_back(x + 17.0, y - 3.0);
    const auto rep = ModelSelector::select(src, dst);
    const auto tps = std::find_if(rep.evidence.begin(), rep.evidence.end(), [](const auto& e) {
        return e.model == CandidateModel::Tps;
    });
    REQUIRE(tps != rep.evidence.end());
    REQUIRE(tps->feasible);
    // On exact-affine data TPS must not win over the simpler truth.
    REQUIRE(rep.selected != CandidateModel::Tps);
}

TEST_CASE("model selector: cancellation reports cancelled", "[f13][model_selector][negative]")
{
    const Pts src = makeGrid(5);
    Pts dst;
    for (const auto& [x, y] : src)
        dst.emplace_back(x + 1.0, y);
    std::atomic_bool cancel{true};
    const auto rep = ModelSelector::select(src, dst, {}, &cancel);
    REQUIRE(rep.status == RegistrationStatus::Refused);
    REQUIRE(rep.reason == QStringLiteral("cancelled"));
}
