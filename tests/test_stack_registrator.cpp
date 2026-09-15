// test_stack_registrator.cpp — F13 Package E: global translation adjustment.
// Ground truth: hand-built observation graphs with documented offsets and a
// triangle with an inconsistency — the loop-closure error must show up in
// the drift metrics and be distributed across the triangle edges. Nothing is
// derived from the code under test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/registration/stack_registrator.h"

#include <atomic>
#include <stdexcept>
#include <vector>

using namespace sicnu::registration;

namespace {
StackPairObservation obs(const QString& from, const QString& to, double tx = 0.0,
                         double ty = 0.0, double confidence = 1.0, int inliers = 10)
{
    StackPairObservation o;
    o.fromId = from;
    o.toId = to;
    o.tx = tx;
    o.ty = ty;
    o.confidence = confidence;
    o.inlierCount = inliers;
    return o;
}
} // namespace

TEST_CASE("stack: exact cycle recovers truth with zero drift", "[f13][stack]")
{
    // Triangle A-B-C with consistent observations (a rigid field):
    // B = A + (10, 0); C = A + (0, 5); and C - B = (-10, 5) — consistent.
    const std::vector<QString> ids = {"A", "B", "C"};
    const std::vector<StackPairObservation> edges = {
        obs("A", "B", 10.0, 0.0),
        obs("A", "C", 0.0, 5.0),
        obs("B", "C", -10.0, 5.0),
    };
    const auto sol = StackRegistrator::solveTranslations(ids, edges);
    REQUIRE(sol.status == RegistrationStatus::Success);
    REQUIRE(sol.referenceId == "A"); // ties broken by input order
    const auto at = [&sol](const QString& id) {
        for (const auto& s : sol.scenes)
            if (s.sceneId == id)
                return s;
        FAIL("missing scene");
        return StackSceneSolution{};
    };
    REQUIRE(at("A").offsetX == Catch::Approx(0.0));
    REQUIRE(at("A").offsetY == Catch::Approx(0.0));
    REQUIRE(at("B").offsetX == Catch::Approx(10.0).margin(1e-9));
    REQUIRE(at("B").offsetY == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(at("C").offsetX == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(at("C").offsetY == Catch::Approx(5.0).margin(1e-9));
    REQUIRE(sol.maxEdgeResidual < 1e-9);
    REQUIRE(at("A").hopCount == 0);
    REQUIRE(at("B").hopCount == 1);
    REQUIRE(at("C").hopCount == 1);
}

TEST_CASE("stack: inconsistent triangle distributes closure error into drift metrics",
          "[f13][stack]")
{
    // Same triangle but the C-B observation says (-8, 5): 2 px of closure
    // error around A→B→C→A. The global solve must leave every edge residual
    // strictly below the raw 2 px (error distributed, not hidden) and the
    // max drift must be nonzero and equal across the three edges (symmetric
    // weights).
    const std::vector<QString> ids = {"A", "B", "C"};
    const std::vector<StackPairObservation> edges = {
        obs("A", "B", 10.0, 0.0),
        obs("A", "C", 0.0, 5.0),
        obs("B", "C", -8.0, 5.0),
    };
    const auto sol = StackRegistrator::solveTranslations(ids, edges);
    REQUIRE(sol.status == RegistrationStatus::Success);
    REQUIRE(sol.maxEdgeResidual > 1e-6);
    REQUIRE(sol.maxEdgeResidual < 2.0 - 1e-6); // strictly distributed
    REQUIRE(sol.rmsEdgeResidual == Catch::Approx(sol.maxEdgeResidual).epsilon(1e-6));
    for (const auto& s : sol.scenes)
        REQUIRE(s.maxEdgeResidual == Catch::Approx(sol.maxEdgeResidual).epsilon(1e-6));
}

TEST_CASE("stack: weighted averaging favors high-confidence chains", "[f13][stack]")
{
    // Line graph R-S-T where S-T was measured with near-zero confidence —
    // the solved T offset must follow the (dominant) high-confidence path
    // R-S + S-T-strong: we give two observations of S-T, one strong.
    const std::vector<QString> ids = {"R", "S", "T"};
    const std::vector<StackPairObservation> edges = {
        obs("R", "S", 4.0, 0.0, 1.0, 20),
        obs("S", "T", 2.0, 0.0, 1.0, 20),     // strong: total 6
        obs("S", "T", 100.0, 0.0, 0.001, 1),  // weak outlier: weight ~0.001
    };
    StackOptions opt;
    opt.referenceId = QStringLiteral("R"); // pin the reference: S is the hub
    const auto sol = StackRegistrator::solveTranslations(ids, edges, opt);
    REQUIRE(sol.status == RegistrationStatus::Success);
    const StackSceneSolution* t = nullptr;
    for (const auto& s : sol.scenes)
        if (s.sceneId == "T")
            t = &s;
    REQUIRE(t != nullptr);
    // Weighted mean is dominated by the strong observation.
    REQUIRE(t->offsetX == Catch::Approx(6.0).margin(0.05));
}

TEST_CASE("stack: disconnected scenes are reported, solved subgraph still returns",
          "[f13][stack]")
{
    const std::vector<QString> ids = {"A", "B", "island"};
    const std::vector<StackPairObservation> edges = {obs("A", "B", 3.0, -1.0)};
    const auto sol = StackRegistrator::solveTranslations(ids, edges);
    REQUIRE(sol.status == RegistrationStatus::Success);
    REQUIRE(sol.disconnectedScenes == 1);
    for (const auto& s : sol.scenes) {
        if (s.sceneId == "island") {
            REQUIRE_FALSE(s.connected);
            REQUIRE(s.hopCount == -1);
        } else {
            REQUIRE(s.connected);
        }
    }
}

TEST_CASE("stack: no edges refuses; reference suggestion picks the hub",
          "[f13][stack][negative]")
{
    const std::vector<QString> ids = {"A", "B"};
    const auto sol = StackRegistrator::solveTranslations(ids, {});
    REQUIRE(sol.status == RegistrationStatus::Refused);
    REQUIRE(sol.reason == QStringLiteral("too_few_matches"));

    // Hub: B participates in two edges, A in one.
    const std::vector<StackPairObservation> edges = {obs("A", "B"), obs("C", "B")};
    REQUIRE(StackRegistrator::suggestReference({"A", "B", "C"}, edges) == "B");
}

TEST_CASE("stack: unknown ids throw; scene cap refuses; cancellation reports",
          "[f13][stack][negative]")
{
    const std::vector<QString> ids = {"A", "B"};
    REQUIRE_THROWS_AS(StackRegistrator::solveTranslations(ids, {obs("A", "Z")}),
                      std::invalid_argument);

    StackOptions opt;
    opt.maxScenes = 2;
    const std::vector<QString> three = {"A", "B", "C"};
    const auto capped = StackRegistrator::solveTranslations(three, {obs("A", "B")}, opt);
    REQUIRE(capped.status == RegistrationStatus::Refused);
    REQUIRE(capped.reason == QStringLiteral("cap_exhausted"));

    std::atomic_bool cancel{false};
    const auto sol =
        StackRegistrator::solveTranslations({"A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
                                             "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"},
                                            {obs("A", "B")}, {}, &cancel);
    REQUIRE(sol.status == RegistrationStatus::Success); // small system, cancel not hit
}
