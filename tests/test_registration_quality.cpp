// test_registration_quality.cpp — F13 Package F: quality products.
// Ground truth: hand-placed residual sets with known CE90 (exact percentile
// arithmetic on documented residuals), a clustered configuration whose
// per-point confidence must stay low (Oracle #2), a closed-form residual
// field, and JSON schema presence. Nothing is derived from the code under
// test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/registration/registration_quality.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cmath>
#include <vector>

using namespace sicnu::registration;

namespace {

RegistrationPoint pt(double sx, double sy, double dx, double dy, double residual, bool inlier,
                     double score = 1.0)
{
    RegistrationPoint p;
    p.srcX = sx;
    p.srcY = sy;
    p.dstX = dx;
    p.dstY = dy;
    p.residual = residual;
    p.inlier = inlier;
    p.score = score;
    return p;
}

} // namespace

TEST_CASE("quality: CE90 is the exact empirical percentile on a documented residual set",
          "[f13][quality]")
{
    // 100 points at the 10 identically-y offsets? Use exact radial values:
    // 90 points with residual exactly 1.0, 10 points with residual exactly
    // 3.0 — the 90th percentile of the sorted set is exactly 1.0.
    std::vector<RegistrationPoint> pts;
    for (int i = 0; i < 90; ++i)
        pts.push_back(pt(10.0 * (i % 10), 10.0 * (i / 10), 11.0 * (i % 10), 10.0 * (i / 10), 1.0,
                         true));
    for (int i = 0; i < 10; ++i)
        pts.push_back(pt(100.0 + i, 100.0, 103.0 + i, 100.0, 3.0, true));

    const auto rep = RegistrationQuality::evaluate(pts, 200.0, 200.0, 1.0);
    REQUIRE(rep.inlierCount == 100);
    // RMSE = sqrt((90·1 + 10·9)/100) = sqrt(1.8)
    REQUIRE(rep.rmse == Catch::Approx(std::sqrt(1.8)).margin(1e-12));
    REQUIRE_FALSE(rep.ce90Degraded);
    REQUIRE(rep.ce90 == Catch::Approx(1.0).margin(1e-12));
}

TEST_CASE("quality: small samples degrade CE90 honestly", "[f13][quality]")
{
    std::vector<RegistrationPoint> pts;
    for (int i = 0; i < 9; ++i)
        pts.push_back(pt(10.0 * i, 0.0, 10.0 * i + 1.0, 0.0, 1.0, true));
    pts.push_back(pt(100.0, 0.0, 105.0, 0.0, 5.0, true)); // worst error 5
    const auto rep = RegistrationQuality::evaluate(pts, 200.0, 200.0, 1.0);
    REQUIRE(rep.ce90Degraded);
    // Degraded reporting falls back to the worst observed error, flagged.
    REQUIRE(rep.ce90 == Catch::Approx(5.0).margin(1e-12));
    // Rayleigh reference still reported: 2.146·σ, σ = RMSE/√2.
    const double sigma = std::sqrt(rep.rmse * rep.rmse / 2.0);
    REQUIRE(rep.ce90NormalReference == Catch::Approx(2.1460183666010975 * sigma).margin(1e-9));
}

TEST_CASE("quality: clustered configuration cannot yield high per-point confidence (Oracle)",
          "[f13][quality]")
{
    // All points clustered in one small corner: full-extent coverage ~ 1/16.
    std::vector<RegistrationPoint> pts;
    for (int i = 0; i < 30; ++i)
        pts.push_back(pt(180.0 + (i % 6), 180.0 + (i / 6), 181.0 + (i % 6), 180.0 + (i / 6), 1.0,
                         true));
    const double clusteredCoverage = 4.0 / 64.0;
    const auto rep =
        RegistrationQuality::evaluate(pts, 256.0, 256.0, clusteredCoverage);
    REQUIRE(rep.coverageRatio == Catch::Approx(clusteredCoverage).margin(1e-12));
    // Every per-point confidence is capped by the coverage factor.
    REQUIRE(rep.medianConfidence <= clusteredCoverage + 1e-9);

    // The same residuals well spread across the image score clearly higher.
    std::vector<RegistrationPoint> spread;
    for (int i = 0; i < 30; ++i)
        spread.push_back(pt(20.0 + 70.0 * (i % 5), 20.0 + 70.0 * (i / 5),
                            21.0 + 70.0 * (i % 5), 20.0 + 70.0 * (i / 5), 1.0, true));
    const auto rep2 = RegistrationQuality::evaluate(spread, 256.0, 256.0, 1.0);
    REQUIRE(rep2.medianConfidence > rep.medianConfidence);
}

TEST_CASE("quality: residual vector field carries signed per-cell means", "[f13][quality]")
{
    // Offsets (2,0), (1,0), (0,0) across three cells: the robust median shift
    // is 1, so the field reports the deviation FROM the median: +1, 0, -1.
    std::vector<RegistrationPoint> pts;
    pts.push_back(pt(10.0, 10.0, 12.0, 10.0, 1.0, true));   // lower-left: dx=+2
    pts.push_back(pt(70.0, 70.0, 71.0, 70.0, 1.0, true));   // center: dx=+1
    pts.push_back(pt(150.0, 150.0, 150.0, 150.0, 0.0, true)); // upper right: dx=0
    const auto field = RegistrationQuality::residualField(pts, 160.0, 160.0, 4);
    REQUIRE(field.grid == 4);
    REQUIRE(field.cells.size() == 16);
    const auto& cell00 = field.cells[0];
    REQUIRE(cell00.count == 1);
    REQUIRE(cell00.meanDx == Catch::Approx(1.0).margin(1e-12));
    REQUIRE(cell00.meanDy == Catch::Approx(0.0).margin(1e-12));
    const auto& cell33 = field.cells[3 * 4 + 3];
    REQUIRE(cell33.count == 1);
    REQUIRE(cell33.meanDx == Catch::Approx(-1.0).margin(1e-12));
    REQUIRE(field.cells[12].count == 0); // empty cell (0,3)
}

TEST_CASE("quality: JSON schema + atomic sidecar write", "[f13][quality]")
{
    std::vector<RegistrationPoint> pts;
    for (int i = 0; i < 25; ++i)
        pts.push_back(pt(10.0 * (i % 5), 10.0 * (i / 5), 10.0 * (i % 5) + 1.0, 10.0 * (i / 5),
                         1.0, true));
    const auto rep = RegistrationQuality::evaluate(pts, 50.0, 50.0, 1.0);
    const auto field = RegistrationQuality::residualField(pts, 50.0, 50.0, 4);
    const auto doc = RegistrationQuality::toJson(rep, field, QStringLiteral("success"),
                                                 QString());
    REQUIRE(doc[QStringLiteral("schema")] == QStringLiteral("exp_rs_registration_quality/1"));
    REQUIRE(doc[QStringLiteral("accuracy")][QStringLiteral("inlierCount")] == 25);
    REQUIRE(doc[QStringLiteral("residualField")][QStringLiteral("grid")] == 4);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("quality_report.json"));
    REQUIRE(RegistrationQuality::writeReportAtomic(path, doc));
    QFile out(path);
    REQUIRE(out.open(QIODevice::ReadOnly));
    const auto parsed = QJsonDocument::fromJson(out.readAll()).object();
    REQUIRE(parsed[QStringLiteral("schema")] == QStringLiteral("exp_rs_registration_quality/1"));

    // Unwritable path fails cleanly (false, no throw).
    REQUIRE_FALSE(RegistrationQuality::writeReportAtomic(
        QStringLiteral("/proc/nonexistent-dir/f13/quality.json"), doc));
}
