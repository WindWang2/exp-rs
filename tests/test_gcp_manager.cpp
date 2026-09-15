// test_gcp_manager.cpp — D14 Package A: GCP manager and spatial distribution
// analytics. All expected values come from hand-derived analytic geometry
// (shoelace formula, Pythagoras, closed-form Clark-Evans / aspect-ratio
// expressions), never from the implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/gcp_manager.h"

#include <QTemporaryDir>

#include <cmath>
#include <limits>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::core;

namespace {

GcpPoint makePoint(const QString& id, double sx, double sy, double tx, double ty)
{
    GcpPoint pt;
    pt.id = id;
    pt.sourceX = sx;
    pt.sourceY = sy;
    pt.targetX = tx;
    pt.targetY = ty;
    return pt;
}

/// 4 corners of a 100x100 square + its center, with identity target coords.
std::vector<GcpPoint> squareWithCenter()
{
    return {
        makePoint("P1", 0.0, 0.0, 0.0, 0.0),
        makePoint("P2", 100.0, 0.0, 100.0, 0.0),
        makePoint("P3", 100.0, 100.0, 100.0, 100.0),
        makePoint("P4", 0.0, 100.0, 0.0, 100.0),
        makePoint("P5", 50.0, 50.0, 50.0, 50.0),
    };
}

} // namespace

TEST_CASE("test_gcp_manager - GCP CRUD validates ids and coordinates", "[gcp][d14]")
{
    GcpManager mgr;

    REQUIRE(mgr.addPoint(makePoint("P1", 1.0, 2.0, 3.0, 4.0)));
    REQUIRE(mgr.size() == 1);

    // Duplicate id rejected.
    REQUIRE_FALSE(mgr.addPoint(makePoint("P1", 9.0, 9.0, 9.0, 9.0)));
    // Empty id rejected.
    REQUIRE_FALSE(mgr.addPoint(makePoint("", 1.0, 1.0, 1.0, 1.0)));
    // Non-finite coordinates rejected.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    REQUIRE_FALSE(mgr.addPoint(makePoint("BAD1", nan, 0.0, 0.0, 0.0)));
    REQUIRE_FALSE(mgr.addPoint(makePoint("BAD2", 0.0, inf, 0.0, 0.0)));
    REQUIRE(mgr.size() == 1);

    // Update keeps identity and validates coordinates.
    auto updated = makePoint("P1", 5.0, 6.0, 7.0, 8.0);
    REQUIRE(mgr.updatePoint(updated));
    REQUIRE_FALSE(mgr.updatePoint(makePoint("P1", nan, 0.0, 0.0, 0.0)));
    REQUIRE_FALSE(mgr.updatePoint(makePoint("MISSING", 1.0, 1.0, 1.0, 1.0)));
    const auto found = mgr.findPoint("P1");
    REQUIRE(found.has_value());
    REQUIRE_THAT(found->sourceX, WithinAbs(5.0, 1e-12));
    REQUIRE_THAT(found->targetY, WithinAbs(8.0, 1e-12));

    REQUIRE(mgr.removePoint("P1"));
    REQUIRE_FALSE(mgr.removePoint("P1"));
    REQUIRE(mgr.size() == 0);
}

TEST_CASE("test_gcp_manager - Convex hull and coverage on 100x100 grid", "[gcp][d14]")
{
    GcpManager mgr;
    for (const auto& pt : squareWithCenter())
        REQUIRE(mgr.addPoint(pt));

    const auto metrics = mgr.evaluateDistribution(100.0, 100.0);
    REQUIRE(metrics.activeCount == 5);
    // Shoelace on the unit square scaled to 100: area = 100*100 = 10000.
    REQUIRE_THAT(metrics.convexHullArea, WithinAbs(10000.0, 1e-6));
    REQUIRE_THAT(metrics.coverageRatio, WithinAbs(1.0, 1e-6));

    // The hull itself is exactly the four corners.
    const auto hull = mgr.computeConvexHull();
    REQUIRE(hull.size() == 4);
}

TEST_CASE("test_gcp_manager - Collinear points yield exactly zero hull area", "[gcp][d14]")
{
    GcpManager mgr;
    REQUIRE(mgr.addPoint(makePoint("A", 0.0, 0.0, 0.0, 0.0)));
    REQUIRE(mgr.addPoint(makePoint("B", 50.0, 50.0, 50.0, 50.0)));
    REQUIRE(mgr.addPoint(makePoint("C", 100.0, 100.0, 100.0, 100.0)));

    const auto metrics = mgr.evaluateDistribution(100.0, 100.0);
    REQUIRE(metrics.activeCount == 3);
    REQUIRE(metrics.convexHullArea == 0.0);
    REQUIRE(metrics.coverageRatio == 0.0);
}

TEST_CASE("test_gcp_manager - Fewer than three active points have no hull", "[gcp][d14]")
{
    GcpManager mgr;
    REQUIRE(mgr.addPoint(makePoint("A", 0.0, 0.0, 0.0, 0.0)));
    REQUIRE(mgr.addPoint(makePoint("B", 100.0, 100.0, 100.0, 100.0)));

    const auto metrics = mgr.evaluateDistribution(100.0, 100.0);
    REQUIRE(metrics.convexHullArea == 0.0);
    REQUIRE(metrics.coverageRatio == 0.0);
}

TEST_CASE("test_gcp_manager - Clark-Evans index matches closed-form values", "[gcp][d14]")
{
    // Four corners of 100x100: every nearest neighbour is exactly 100 away,
    // so r̄_A = 100. Density ρ = 4/10000, r̄_E = 1/(2√ρ) = 25. R = 4 exactly.
    GcpManager corners;
    REQUIRE(corners.addPoint(makePoint("A", 0.0, 0.0, 0.0, 0.0)));
    REQUIRE(corners.addPoint(makePoint("B", 100.0, 0.0, 100.0, 0.0)));
    REQUIRE(corners.addPoint(makePoint("C", 100.0, 100.0, 100.0, 100.0)));
    REQUIRE(corners.addPoint(makePoint("D", 0.0, 100.0, 0.0, 100.0)));

    const auto m4 = corners.evaluateDistribution(100.0, 100.0);
    REQUIRE_THAT(m4.meanNearestNeighborDist, WithinAbs(100.0, 1e-9));
    REQUIRE_THAT(m4.expectedNearestNeighborDist, WithinAbs(25.0, 1e-9));
    REQUIRE_THAT(m4.clarkEvansIndex, WithinAbs(4.0, 1e-9));

    // Square + center: nearest neighbour for every point is 50√2 (corner to
    // center). ρ = 5/10000, r̄_E = 1/(2√ρ) = 10√5, so R = 50√2 / (10√5) = √10.
    GcpManager withCenter;
    for (const auto& pt : squareWithCenter())
        REQUIRE(withCenter.addPoint(pt));
    const auto m5 = withCenter.evaluateDistribution(100.0, 100.0);
    REQUIRE_THAT(m5.clarkEvansIndex, WithinAbs(std::sqrt(10.0), 1e-9));
}

TEST_CASE("test_gcp_manager - Delaunay triangulation of square+center makes four triangles", "[gcp][d14]")
{
    GcpManager mgr;
    for (const auto& pt : squareWithCenter())
        REQUIRE(mgr.addPoint(pt));

    const auto triangles = mgr.computeDelaunayTriangles();
    // The center connects to all four corners → exactly 4 triangles.
    REQUIRE(triangles.size() == 4);

    // Every triangle here is a right isosceles triangle with legs 50√2 and
    // hypotenuse 100. For a right triangle R_circum = c/2 and
    // r_in = (a+b-c)/2, so the aspect ratio is
    //   (c/2) / (2·r_in) = 50 / (100√2 − 100) = (√2+1)/2 ≈ 1.2071.
    const double expectedAspect = (std::sqrt(2.0) + 1.0) / 2.0;
    const auto metrics = mgr.evaluateDistribution(100.0, 100.0);
    REQUIRE_THAT(metrics.maxDelaunayAspectRatio, WithinAbs(expectedAspect, 1e-9));
}

TEST_CASE("test_gcp_manager - Residual update and global RMSE follow the RMS definition", "[gcp][d14]")
{
    GcpManager mgr;
    for (const auto& pt : squareWithCenter())
        REQUIRE(mgr.addPoint(pt));

    // Δx = 0.3, Δy = 0.4 for every point → r_i = 0.5, RMSE = 0.5.
    std::vector<std::pair<double, double>> transformed;
    for (const auto& pt : mgr.activePoints())
        transformed.emplace_back(pt.targetX + 0.3, pt.targetY + 0.4);

    mgr.updateResiduals(transformed);
    for (const auto& pt : mgr.activePoints()) {
        REQUIRE_THAT(pt.residualTotal, WithinAbs(0.5, 1e-12));
    }
    REQUIRE_THAT(mgr.computeGlobalRmse(), WithinAbs(0.5, 1e-12));

    const auto metrics = mgr.evaluateDistribution(100.0, 100.0);
    REQUIRE_THAT(metrics.globalRmse, WithinAbs(0.5, 1e-12));

    // Size mismatch with the active count must be a no-op.
    mgr.updateResiduals({{0.0, 0.0}});
    REQUIRE_THAT(mgr.computeGlobalRmse(), WithinAbs(0.5, 1e-12));
}

TEST_CASE("test_gcp_manager - Disabled points are excluded from metrics", "[gcp][d14]")
{
    GcpManager mgr;
    for (const auto& pt : squareWithCenter())
        REQUIRE(mgr.addPoint(pt));

    // Four clean points (r = 0.5) and one gross blunder (3-4-5 → r = 5.0).
    std::vector<std::pair<double, double>> transformed;
    int index = 0;
    for (const auto& pt : mgr.activePoints()) {
        if (index == 4)
            transformed.emplace_back(pt.targetX + 3.0, pt.targetY + 4.0);
        else
            transformed.emplace_back(pt.targetX + 0.3, pt.targetY + 0.4);
        ++index;
    }
    mgr.updateResiduals(transformed);
    // RMSE = sqrt((4·0.5² + 5²) / 5) = sqrt(5.2).
    REQUIRE_THAT(mgr.computeGlobalRmse(), WithinAbs(std::sqrt(5.2), 1e-12));

    mgr.setPointEnabled("P5", false);
    REQUIRE(mgr.activeCount() == 4);
    // Without the blunder: RMSE = sqrt(0.5²) = 0.5.
    REQUIRE_THAT(mgr.computeGlobalRmse(), WithinAbs(0.5, 1e-12));

    const auto metrics = mgr.evaluateDistribution(100.0, 100.0);
    REQUIRE(metrics.activeCount == 4);
    REQUIRE_THAT(metrics.globalRmse, WithinAbs(0.5, 1e-12));

    mgr.setPointEnabled("P5", true);
    REQUIRE(mgr.activeCount() == 5);
    REQUIRE_THAT(mgr.computeGlobalRmse(), WithinAbs(std::sqrt(5.2), 1e-12));
}

TEST_CASE("test_gcp_manager - CSV round trip preserves every field", "[gcp][d14]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("gcps.csv");

    GcpManager mgr;
    for (const auto& pt : squareWithCenter())
        REQUIRE(mgr.addPoint(pt));
    mgr.setPointEnabled("P5", false);
    std::vector<std::pair<double, double>> transformed;
    for (const auto& pt : mgr.activePoints())
        transformed.emplace_back(pt.targetX + 0.3, pt.targetY + 0.4);
    mgr.updateResiduals(transformed);

    REQUIRE(mgr.saveToCsv(path));

    GcpManager loaded;
    REQUIRE(loaded.loadFromCsv(path));
    REQUIRE(loaded.size() == 5);
    REQUIRE(loaded.activeCount() == 4);

    const auto original = mgr.allPoints();
    const auto restored = loaded.allPoints();
    for (size_t i = 0; i < original.size(); ++i) {
        REQUIRE(restored[i].id == original[i].id);
        REQUIRE_THAT(restored[i].sourceX, WithinAbs(original[i].sourceX, 1e-12));
        REQUIRE_THAT(restored[i].sourceY, WithinAbs(original[i].sourceY, 1e-12));
        REQUIRE_THAT(restored[i].targetX, WithinAbs(original[i].targetX, 1e-12));
        REQUIRE_THAT(restored[i].targetY, WithinAbs(original[i].targetY, 1e-12));
        REQUIRE_THAT(restored[i].residualX, WithinAbs(original[i].residualX, 1e-12));
        REQUIRE_THAT(restored[i].residualY, WithinAbs(original[i].residualY, 1e-12));
        REQUIRE(restored[i].enabled == original[i].enabled);
    }
    // Malformed rows are skipped instead of aborting the load.
    REQUIRE_FALSE(loaded.loadFromCsv(path + ".does-not-exist"));
}

TEST_CASE("test_gcp_manager - JSON round trip preserves every field and rejects garbage", "[gcp][d14]")
{
    GcpManager mgr;
    for (const auto& pt : squareWithCenter())
        REQUIRE(mgr.addPoint(pt));

    const QString json = mgr.toJson();
    GcpManager loaded;
    REQUIRE(loaded.fromJson(json));
    REQUIRE(loaded.size() == 5);
    const auto original = mgr.allPoints();
    const auto restored = loaded.allPoints();
    for (size_t i = 0; i < original.size(); ++i) {
        REQUIRE(restored[i].id == original[i].id);
        REQUIRE_THAT(restored[i].sourceY, WithinAbs(original[i].sourceY, 1e-12));
        REQUIRE_THAT(restored[i].targetX, WithinAbs(original[i].targetX, 1e-12));
        REQUIRE(restored[i].enabled == original[i].enabled);
    }

    REQUIRE_FALSE(loaded.fromJson("this is not json"));
    REQUIRE_FALSE(loaded.fromJson("{\"gcps\": 42}"));
}
