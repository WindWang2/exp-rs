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

#include <algorithm>
#include <cmath>
#include <cstdint>

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
    REQUIRE(actions.size() == 6); // D14 trio + F13 multimodal/select/stack
    REQUIRE(actions.contains(QStringLiteral("multimodal_register")));
    REQUIRE(actions.contains(QStringLiteral("select_model")));
    REQUIRE(actions.contains(QStringLiteral("stack_register")));
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
    // The synthetic pair is shifted by 10 px; the 8 px keypoint grid
    // quantizes the consensus, so allow half a cell of slack.
    const QJsonArray homography = data.value("homography").toArray();
    if (data.value("inlier_count").toInt() >= 8) {
        REQUIRE(std::abs(homography.at(2).toDouble() - 10.0) < 4.0);
    }
}

// ---- F13: multimodal / model-selection / stack actions --------------------

namespace {

/// Deterministic grain in [-1, 1] (same hash family as the matcher tests).
float f13Grain(int x, int y)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                      + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
}

float f13Texture(int x, int y)
{
    const double fx = x, fy = y;
    return static_cast<float>(50.0 + 20.0 * std::sin(fx / 9.3) * std::sin(fy / 7.7)
                              + 10.0 * std::sin((fx + fy) / 5.1) + 3.0 * f13Grain(x, y));
}

} // namespace

TEST_CASE("test_geometric_agent_tools - multimodal_register reports status and quality for a shifted pair",
          "[agent_tool][f13]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString srcPath = dir.filePath("f13_src.tif");
    const QString refPath = dir.filePath("f13_ref.tif");

    const int kSize = 256;
    const double kDx = 4.0, kDy = 6.0;
    GDALAllRegister();
    auto make = [&](const QString& path, double dx, double dy) {
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        GDALDataset* dataset = driver->Create(path.toUtf8().constData(), kSize, kSize, 1,
                                              GDT_Float32, nullptr);
        REQUIRE(dataset != nullptr);
        std::vector<float> buffer(static_cast<size_t>(kSize) * kSize, 0.0f);
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x) {
                const int sx = static_cast<int>(x - dx);
                const int sy = static_cast<int>(y - dy);
                buffer[static_cast<size_t>(y) * kSize + x] =
                    f13Texture(std::clamp(sx, 0, kSize - 1), std::clamp(sy, 0, kSize - 1));
            }
        dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, kSize, kSize, buffer.data(), kSize,
                                            kSize, GDT_Float32, 0, 0);
        GDALClose(dataset);
    };
    make(srcPath, 0.0, 0.0);
    make(refPath, kDx, kDy);

    GeometricTool tool;
    QJsonObject params;
    params.insert("action", QStringLiteral("multimodal_register"));
    params.insert("source_image_path", srcPath);
    params.insert("reference_image_path", refPath);
    params.insert("metric", QStringLiteral("phase_correlation"));
    const QJsonObject response = tool.execute(params);
    REQUIRE(response.value("success").toBool());
    const QJsonObject data = response.value("data").toObject();
    // The envelope succeeds; the registration status must be trustworthy.
    REQUIRE(data.value("status").toString() == QStringLiteral("success"));
    REQUIRE(data.value("inlier_count").toInt() >= 8);
    REQUIRE(data.contains("consensus_homography"));
    REQUIRE(data.contains("quality"));
    REQUIRE(data.contains("pyramid_stages"));
}

TEST_CASE("test_geometric_agent_tools - multimodal_register refuses a flat pair instead of guessing",
          "[agent_tool][f13][negative]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString srcPath = dir.filePath("flat_src.tif");
    const QString refPath = dir.filePath("flat_ref.tif");
    GDALAllRegister();
    auto makeFlat = [&](const QString& path, float value) {
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        GDALDataset* dataset = driver->Create(path.toUtf8().constData(), 128, 128, 1,
                                              GDT_Float32, nullptr);
        REQUIRE(dataset != nullptr);
        std::vector<float> buffer(static_cast<size_t>(128) * 128, value);
        dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 128, 128, buffer.data(), 128, 128,
                                            GDT_Float32, 0, 0);
        GDALClose(dataset);
    };
    makeFlat(srcPath, 10.0f);
    makeFlat(refPath, 20.0f);

    GeometricTool tool;
    QJsonObject params;
    params.insert("action", QStringLiteral("multimodal_register"));
    params.insert("source_image_path", srcPath);
    params.insert("reference_image_path", refPath);
    const QJsonObject response = tool.execute(params);
    // The tool call succeeds (envelope contract) but the registration must
    // carry refusal semantics.
    REQUIRE(response.value("success").toBool());
    const QJsonObject data = response.value("data").toObject();
    REQUIRE(data.value("status").toString() == QStringLiteral("refused"));
    REQUIRE_FALSE(data.value("reason").toString().isEmpty());
}

TEST_CASE("test_geometric_agent_tools - select_model returns the evidence table for affine data",
          "[agent_tool][f13]")
{
    QJsonArray gcps;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const double x = 10.0 * i, y = 10.0 * j;
            QJsonObject g;
            g.insert("source_x", x);
            g.insert("source_y", y);
            // Affine truth with shear: x' = 1.2x + 0.3y + 5 ; y' = -0.1x + 0.9y - 4
            g.insert("target_x", 1.2 * x + 0.3 * y + 5.0);
            g.insert("target_y", -0.1 * x + 0.9 * y - 4.0);
            gcps.append(g);
        }
    GeometricTool tool;
    QJsonObject params;
    params.insert("action", QStringLiteral("select_model"));
    params.insert("gcps", gcps);
    const QJsonObject response = tool.execute(params);
    REQUIRE(response.value("success").toBool());
    const QJsonObject data = response.value("data").toObject();
    REQUIRE(data.value("selected_model").toString() == QStringLiteral("affine"));
    const QJsonArray evidence = data.value("candidate_evidence").toArray();
    REQUIRE(evidence.size() == 7); // full ladder, nothing hidden
}

TEST_CASE("test_geometric_agent_tools - stack_register solves a consistent triangle with zero drift",
          "[agent_tool][f13]")
{
    GeometricTool tool;
    QJsonObject params;
    params.insert("action", QStringLiteral("stack_register"));
    QJsonArray ids{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    params.insert("scene_ids", ids);
    auto edge = [](const QString& from, const QString& to, double tx, double ty) {
        QJsonObject o;
        o.insert("from_id", from);
        o.insert("to_id", to);
        o.insert("tx", tx);
        o.insert("ty", ty);
        return o;
    };
    QJsonArray obs;
    obs.append(edge("A", "B", 10.0, 0.0));
    obs.append(edge("A", "C", 0.0, 5.0));
    obs.append(edge("B", "C", -10.0, 5.0));
    params.insert("observations", obs);
    const QJsonObject response = tool.execute(params);
    REQUIRE(response.value("success").toBool());
    const QJsonObject data = response.value("data").toObject();
    REQUIRE(data.value("reference_id").toString() == QStringLiteral("A"));
    REQUIRE(data.value("max_edge_residual_px").toDouble() < 1e-6);
}
