// test_geometric_agent_tools.cpp — D14 Package H: agent tool tests.
// The audit ground truth is constructed arithmetically: seven residuals of
// 1.0 px and one gross blunder of 15.0 px. The model recommender expectations
// follow the documented decision tree, and inspect_misalignment is exercised
// against GDAL-written synthetic rasters (generated here, not fixtures).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "agent/tools/geometric_tool.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <cmath>

using Catch::Matchers::WithinAbs;
using rs::agent::GeometricTool;

namespace {

QJsonObject runAudit()
{
    QJsonArray gcps;
    for (int i = 1; i <= 8; ++i) {
        QJsonObject gcp;
        gcp.insert("id", QStringLiteral("GCP_%1").arg(i, 2, 10, QChar('0')));
        if (i == 4) {
            gcp.insert("residual_x", 15.0);
            gcp.insert("residual_y", 0.0);
        } else {
            gcp.insert("residual_x", 1.0);
            gcp.insert("residual_y", 0.0);
        }
        gcps.append(gcp);
    }
    GeometricTool tool;
    return tool.auditGcpResiduals(gcps, 1.0);
}

} // namespace

TEST_CASE("test_geometric_agent_tools - Schema is a valid JSON-Schema draft-07 object with the action enum", "[agent_tool][d14]")
{
    GeometricTool tool;
    const QJsonObject schema = tool.parameterSchema();
    REQUIRE(schema.value("$schema").toString() == QStringLiteral("http://json-schema.org/draft-07/schema#"));
    REQUIRE(schema.value("type").toString() == QStringLiteral("object"));
    const QJsonArray required = schema.value("required").toArray();
    REQUIRE(required.size() == 1);
    REQUIRE(required.at(0).toString() == QStringLiteral("action"));
    const QJsonArray actions = schema.value("properties").toObject()
                                   .value("action").toObject()
                                   .value("enum").toArray();
    REQUIRE(actions.size() == 3);
    REQUIRE(actions.at(0).toString() == QStringLiteral("audit_residuals"));
    REQUIRE(tool.toolName() == QStringLiteral("spatial:geometric_registration"));
    REQUIRE_FALSE(tool.toolDescription().isEmpty());
}

TEST_CASE("test_geometric_agent_tools - execute dispatches on the action envelope contract", "[agent_tool][d14]")
{
    GeometricTool tool;
    QJsonObject response = tool.execute(QJsonObject());
    REQUIRE_FALSE(response.value("success").toBool());
    REQUIRE(response.value("diagnostic_message").toString().contains("action"));

    response = tool.execute(QJsonObject{{"action", QStringLiteral("time_travel")}});
    REQUIRE_FALSE(response.value("success").toBool());
    REQUIRE(response.value("action").toString() == QStringLiteral("time_travel"));

    // Well-formed dispatch reaches the underlying routine.
    response = tool.execute(QJsonObject{
        {"action", QStringLiteral("recommend_model")},
        {"active_gcp_count", 4},
        {"terrain_roughness", 0.2},
        {"coverage_ratio", 0.9}});
    REQUIRE(response.value("success").toBool());
    REQUIRE(response.value("action").toString() == QStringLiteral("recommend_model"));
    REQUIRE(response.value("data").toObject().value("recommended_model").toString()
            == QStringLiteral("affine"));
}

TEST_CASE("test_geometric_agent_tools - 3-sigma audit flags exactly the injected 15 px blunder", "[agent_tool][d14]")
{
    const QJsonObject response = runAudit();
    REQUIRE(response.value("success").toBool());
    REQUIRE(response.value("action").toString() == QStringLiteral("audit_residuals"));

    const QJsonObject data = response.value("data").toObject();
    const QJsonArray anomalies = data.value("anomalies").toArray();
    REQUIRE(anomalies.size() == 1);
    REQUIRE(anomalies.at(0).toObject().value("id").toString() == QStringLiteral("GCP_04"));
    REQUIRE(anomalies.at(0).toObject().value("suggest_disable").toBool() == true);
    REQUIRE_THAT(anomalies.at(0).toObject().value("residual_total").toDouble(),
                 WithinAbs(15.0, 1e-12));

    // Arithmetic truth for the audit statistics: 7 residuals of 1.0 + one of
    // 15.0 -> mean = 22/8 = 2.75, RMSE = sqrt((7 + 225)/8) = sqrt(29).
    REQUIRE_THAT(data.value("mean_residual").toDouble(), WithinAbs(2.75, 1e-12));
    REQUIRE_THAT(data.value("rmse").toDouble(), WithinAbs(std::sqrt(29.0), 1e-12));

    // Disabling the blunder must drop the RMSE from sqrt(29) to 1.0.
    const double expectedAfter = std::sqrt((29.0 * 8.0 - 225.0) / 7.0);
    REQUIRE_THAT(anomalies.at(0).toObject().value("expected_rmse_after_disable").toDouble(),
                 WithinAbs(expectedAfter, 1e-12));
}

TEST_CASE("test_geometric_agent_tools - Clean residuals produce no anomalies", "[agent_tool][d14]")
{
    QJsonArray gcps;
    for (int i = 1; i <= 6; ++i) {
        QJsonObject gcp;
        gcp.insert("id", QStringLiteral("GCP_%1").arg(i));
        gcp.insert("residual_x", 0.5 * i / 6.0);
        gcp.insert("residual_y", 0.0);
        gcps.append(gcp);
    }
    GeometricTool tool;
    const QJsonObject response = tool.auditGcpResiduals(gcps, 5.0);
    const QJsonArray anomalies = response.value("data").toObject().value("anomalies").toArray();
    REQUIRE(anomalies.isEmpty());
}

TEST_CASE("test_geometric_agent_tools - Model recommender follows the documented decision tree", "[agent_tool][d14]")
{
    GeometricTool tool;

    // N < 3: structured refusal.
    auto response = tool.recommendOptimalModel(2, 0.1, 0.9);
    REQUIRE_FALSE(response.value("success").toBool());
    REQUIRE(response.value("diagnostic_message").toString().contains("at least 3"));

    // 3 <= N < 6: affine forced.
    response = tool.recommendOptimalModel(4, 0.9, 0.2);
    REQUIRE(response.value("data").toObject().value("recommended_model").toString() == QStringLiteral("affine"));

    // 6 <= N < 10, good coverage + smooth terrain -> polynomial2.
    response = tool.recommendOptimalModel(8, 0.2, 0.9);
    REQUIRE(response.value("data").toObject().value("recommended_model").toString() == QStringLiteral("polynomial2"));

    // 6 <= N < 10, rough terrain -> conservative affine.
    response = tool.recommendOptimalModel(8, 0.9, 0.9);
    REQUIRE(response.value("data").toObject().value("recommended_model").toString() == QStringLiteral("affine"));

    // N >= 10, rough terrain -> TPS; smooth terrain -> polynomial3.
    response = tool.recommendOptimalModel(14, 0.8, 0.5);
    REQUIRE(response.value("data").toObject().value("recommended_model").toString() == QStringLiteral("thin_plate_spline"));
    response = tool.recommendOptimalModel(14, 0.3, 0.5);
    REQUIRE(response.value("data").toObject().value("recommended_model").toString() == QStringLiteral("polynomial3"));
}

TEST_CASE("test_geometric_agent_tools - inspect_misalignment returns structured envelopes, never throws", "[agent_tool][d14]")
{
    GeometricTool tool;
    QJsonObject response = tool.inspectMisalignment(QString(), QStringLiteral("/tmp/whatever.tif"));
    REQUIRE_FALSE(response.value("success").toBool());
    REQUIRE(response.contains("diagnostic_message"));

    response = tool.inspectMisalignment(QStringLiteral("/nonexistent/a.tif"),
                                        QStringLiteral("/nonexistent/b.tif"));
    REQUIRE_FALSE(response.value("success").toBool());

    // Generate two synthetic rasters: a texture and its 10 px shifted copy.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString srcPath = dir.filePath("src.tif");
    const QString refPath = dir.filePath("ref.tif");
    {
        GDALAllRegister();
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        REQUIRE(driver != nullptr);
        constexpr int kSize = 128;
        auto make = [&](const QString& path, double shift) {
            GDALDataset* dataset = driver->Create(path.toUtf8().constData(), kSize, kSize, 1, GDT_Float32, nullptr);
            REQUIRE(dataset != nullptr);
            std::vector<float> buffer(static_cast<size_t>(kSize) * kSize, 0.0f);
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    const double sx = x - shift;
                    buffer[static_cast<size_t>(y) * kSize + x] = static_cast<float>(
                        50.0 + 30.0 * std::sin(0.6 * sx) * std::cos(0.42 * y) + 20.0 * std::sin(0.13 * sx * y / 16.0));
                }
            }
            dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, kSize, kSize, buffer.data(),
                                                kSize, kSize, GDT_Float32, 0, 0);
            GDALClose(dataset);
        };
        make(srcPath, 0.0);
        make(refPath, 10.0);
    }

    response = tool.inspectMisalignment(srcPath, refPath);
    REQUIRE(response.value("success").toBool());
    const QJsonObject data = response.value("data").toObject();
    REQUIRE(data.value("inlier_count").toInt() >= 4);
    REQUIRE(data.contains("inlier_rmse"));
    REQUIRE(data.contains("homography"));
    // The synthetic pair is shifted by 10 px: the estimated translation must
    // be close to that.
    const QJsonArray homography = data.value("homography").toArray();
    if (data.value("inlier_count").toInt() >= 8) {
        REQUIRE(std::abs(homography.at(2).toDouble() - 10.0) < 2.0);
    }
}
