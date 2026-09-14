// geometric_tool.cpp — D14 Package H implementation (ADR 0159).
#include "agent/tools/geometric_tool.h"

#include "processing/algorithms/feature_matcher.h"

#include <gdal_priv.h>

#include <QFile>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace rs::agent {

namespace {

QJsonObject envelope(bool success, const QString& action, const QString& message,
                     const QJsonObject& data = {})
{
    QJsonObject obj;
    obj.insert("success", success);
    obj.insert("action", action);
    obj.insert("data", data);
    obj.insert("diagnostic_message", message);
    return obj;
}

} // namespace

QString GeometricTool::toolDescription() const
{
    return QStringLiteral(
        "Spatial geometric registration: audits GCP residuals (3-sigma gross "
        "blunders), recommends the optimal transform model from point count / "
        "terrain roughness / spatial coverage, and pre-checks misalignment "
        "between a source and a reference raster via robust feature matching.");
}

QJsonObject GeometricTool::parameterSchema() const
{
    QJsonObject schema;
    schema.insert("$schema", QStringLiteral("http://json-schema.org/draft-07/schema#"));
    schema.insert("title", toolName());
    schema.insert("type", QStringLiteral("object"));
    schema.insert("additionalProperties", true);

    QJsonObject action;
    action.insert("type", QStringLiteral("string"));
    action.insert("description", QStringLiteral("Operation to run."));
    action.insert("enum", QJsonArray{QStringLiteral("audit_residuals"),
                                     QStringLiteral("recommend_model"),
                                     QStringLiteral("inspect_misalignment")});

    QJsonObject gcps;
    gcps.insert("type", QStringLiteral("array"));
    gcps.insert("description", QStringLiteral("GCP objects with id, residual_x, residual_y."));
    QJsonObject gcpItem;
    gcpItem.insert("type", QStringLiteral("object"));
    QJsonObject gcpProps;
    QJsonObject idProp;
    idProp.insert("type", QStringLiteral("string"));
    gcpProps.insert("id", idProp);
    QJsonObject rxProp;
    rxProp.insert("type", QStringLiteral("number"));
    gcpProps.insert("residual_x", rxProp);
    QJsonObject ryProp;
    ryProp.insert("type", QStringLiteral("number"));
    gcpProps.insert("residual_y", ryProp);
    gcpItem.insert("properties", gcpProps);
    gcps.insert("items", gcpItem);

    QJsonObject threshold;
    threshold.insert("type", QStringLiteral("number"));
    threshold.insert("description", QStringLiteral("RMSE threshold in pixels (rule: r > 3 * threshold)."));
    threshold.insert("minimum", 0.0);

    QJsonObject gcpCount;
    gcpCount.insert("type", QStringLiteral("integer"));
    gcpCount.insert("minimum", 0.0);
    QJsonObject roughness;
    roughness.insert("type", QStringLiteral("number"));
    roughness.insert("minimum", 0.0);
    roughness.insert("maximum", 1.0);
    QJsonObject coverage;
    coverage.insert("type", QStringLiteral("number"));
    coverage.insert("minimum", 0.0);
    coverage.insert("maximum", 1.0);
    QJsonObject sourcePath;
    sourcePath.insert("type", QStringLiteral("string"));
    QJsonObject refPath;
    refPath.insert("type", QStringLiteral("string"));

    QJsonObject properties;
    properties.insert("action", action);
    properties.insert("gcps", gcps);
    properties.insert("rmse_threshold", threshold);
    properties.insert("active_gcp_count", gcpCount);
    properties.insert("terrain_roughness", roughness);
    properties.insert("coverage_ratio", coverage);
    properties.insert("source_image_path", sourcePath);
    properties.insert("reference_image_path", refPath);
    schema.insert("properties", properties);

    QJsonArray required{QStringLiteral("action")};
    schema.insert("required", required);
    return schema;
}

QJsonObject GeometricTool::auditGcpResiduals(const QJsonArray& gcpArray, double rmseThreshold)
{
    struct Residual {
        QString id;
        double total{0.0};
    };
    std::vector<Residual> residuals;
    residuals.reserve(gcpArray.size());
    for (const auto& item : gcpArray) {
        if (!item.isObject())
            continue;
        const QJsonObject obj = item.toObject();
        Residual r;
        r.id = obj.value("id").toString();
        double total = 0.0;
        if (obj.contains("residual_total")) {
            total = obj.value("residual_total").toDouble();
        } else {
            total = std::hypot(obj.value("residual_x").toDouble(),
                               obj.value("residual_y").toDouble());
        }
        if (r.id.isEmpty() || !std::isfinite(total))
            continue;
        r.total = total;
        residuals.push_back(r);
    }
    if (residuals.size() < 2) {
        return envelope(false, QStringLiteral("audit_residuals"),
                        QStringLiteral("Need at least two valid GCPs for a statistical audit."));
    }

    // Welford mean/std over the residual magnitudes.
    double mean = 0.0;
    double m2 = 0.0;
    int n = 0;
    for (const auto& r : residuals) {
        ++n;
        const double delta = r.total - mean;
        mean += delta / n;
        m2 += delta * (r.total - mean);
    }
    const double stdDev = std::sqrt(m2 / (n - 1));
    const double rmse = std::sqrt(std::accumulate(
        residuals.begin(), residuals.end(), 0.0,
        [](double sum, const Residual& r) { return sum + r.total * r.total; }) / n);

    QJsonArray anomalies;
    double sumSqAfterAll = rmse * rmse * n;
    for (const auto& r : residuals) {
        const double threeSigma = mean + 3.0 * stdDev;
        const bool statistical = r.total > threeSigma;
        const bool thresholded = r.total > 3.0 * rmseThreshold;
        if (!statistical && !thresholded)
            continue;
        QJsonObject anomaly;
        anomaly.insert("id", r.id);
        anomaly.insert("residual_total", r.total);
        anomaly.insert("z_score", stdDev > 1e-12 ? (r.total - mean) / stdDev : 0.0);
        anomaly.insert("suggest_disable", true);
        // Expected global RMSE with this point removed ( unbiased N-1 form).
        const double sumSqAfter = sumSqAfterAll - r.total * r.total;
        const double rmseAfter = residuals.size() > 1
                                     ? std::sqrt(sumSqAfter / (residuals.size() - 1))
                                     : 0.0;
        anomaly.insert("expected_rmse_after_disable", rmseAfter);
        anomalies.append(anomaly);
    }

    QJsonObject data;
    data.insert("gcp_count", static_cast<qint64>(residuals.size()));
    data.insert("mean_residual", mean);
    data.insert("std_residual", stdDev);
    data.insert("rmse", rmse);
    data.insert("rmse_threshold", rmseThreshold);
    data.insert("anomalies", anomalies);
    const QString message = anomalies.isEmpty()
                                ? QStringLiteral("No gross blunders detected.")
                                : QStringLiteral("%1 suspect GCP(s) flagged; consider disabling them.").arg(anomalies.size());
    return envelope(true, QStringLiteral("audit_residuals"), message, data);
}

QJsonObject GeometricTool::recommendOptimalModel(int activeGcpCount, double terrainRoughness,
                                                 double coverageRatio)
{
    QJsonObject data;
    data.insert("active_gcp_count", activeGcpCount);
    data.insert("terrain_roughness", terrainRoughness);
    data.insert("coverage_ratio", coverageRatio);

    if (activeGcpCount < 3) {
        return envelope(false, QStringLiteral("recommend_model"),
                        QStringLiteral("Insufficient control points: at least 3 are required."), data);
    }

    QString recommended;
    QString rationale;
    if (activeGcpCount < 6) {
        recommended = QStringLiteral("affine");
        rationale = QStringLiteral(
            "3-5 points only support the 6-parameter affine model; higher-order "
            "deformations cannot be fitted reliably.");
    } else if (activeGcpCount < 10) {
        if (coverageRatio > 0.7 && terrainRoughness < 0.5) {
            recommended = QStringLiteral("polynomial2");
            rationale = QStringLiteral(
                "6-9 well-spread points over smooth terrain support a 2nd-order polynomial.");
        } else {
            recommended = QStringLiteral("affine");
            rationale = QStringLiteral(
                "6-9 points with clumped coverage or rough terrain: stay conservative with affine.");
        }
    } else {
        if (terrainRoughness > 0.6) {
            recommended = QStringLiteral("thin_plate_spline");
            rationale = QStringLiteral(
                "10+ points over rough terrain: the TPS absorbs local non-rigid distortion.");
        } else {
            recommended = QStringLiteral("polynomial3");
            rationale = QStringLiteral(
                "10+ points over smooth terrain: a 3rd-order polynomial is sufficient and stable.");
        }
    }
    data.insert("recommended_model", recommended);
    data.insert("rationale", rationale);
    return envelope(true, QStringLiteral("recommend_model"),
                    QStringLiteral("Recommended model: %1").arg(recommended), data);
}

QJsonObject GeometricTool::inspectMisalignment(const QString& sourceImagePath,
                                               const QString& refImagePath)
{
    if (sourceImagePath.isEmpty() || refImagePath.isEmpty()) {
        return envelope(false, QStringLiteral("inspect_misalignment"),
                        QStringLiteral("Both source and reference image paths are required."));
    }
    if (!QFile::exists(sourceImagePath) || !QFile::exists(refImagePath)) {
        return envelope(false, QStringLiteral("inspect_misalignment"),
                        QStringLiteral("Source or reference raster does not exist."), {});
    }

    GDALAllRegister();
    GDALDataset* src = static_cast<GDALDataset*>(GDALOpen(sourceImagePath.toUtf8().constData(), GA_ReadOnly));
    GDALDataset* ref = static_cast<GDALDataset*>(GDALOpen(refImagePath.toUtf8().constData(), GA_ReadOnly));
    if (!src || !ref) {
        if (src)
            GDALClose(src);
        if (ref)
            GDALClose(ref);
        return envelope(false, QStringLiteral("inspect_misalignment"),
                        QStringLiteral("Failed to open one of the rasters with GDAL."));
    }

    auto readBand1 = [](GDALDataset* dataset, int& width, int& height) -> std::vector<float> {
        width = dataset->GetRasterXSize();
        height = dataset->GetRasterYSize();
        std::vector<float> buffer(static_cast<size_t>(width) * height, 0.0f);
        dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, width, height, buffer.data(),
                                            width, height, GDT_Float32, 0, 0);
        return buffer;
    };
    int srcW = 0, srcH = 0, refW = 0, refH = 0;
    const std::vector<float> srcData = readBand1(src, srcW, srcH);
    const std::vector<float> refData = readBand1(ref, refW, refH);
    GDALClose(src);
    GDALClose(ref);

    rs::algorithms::FeatureMatchOptions options;
    rs::algorithms::FeatureMatcher matcher;
    const auto report = matcher.matchImages(srcData.data(), srcW, srcH,
                                            refData.data(), refW, refH, options);

    QJsonObject data;
    data.insert("total_candidates", report.totalCandidates);
    data.insert("inlier_count", report.inlierCount);
    data.insert("inlier_ratio", report.inlierRatio);
    data.insert("inlier_rmse", report.inlierRmse);
    QJsonArray homography;
    for (const double value : report.homographyMatrix)
        homography.append(value);
    data.insert("homography", homography);

    const bool aligned = report.inlierCount >= 8 && report.inlierRmse <= options.ransacReprojThreshold;
    data.insert("aligned", aligned);
    data.insert("recommended_action", aligned ? QStringLiteral("none")
                                              : QStringLiteral("collect_gcps_and_rectify"));
    const QString message = aligned
                                ? QStringLiteral("Rasters appear co-registered (inlier RMSE %1 px).").arg(report.inlierRmse)
                                : QStringLiteral("Misalignment detected: %1 inliers, RMSE %2 px.")
                                      .arg(report.inlierCount)
                                      .arg(report.inlierRmse);
    return envelope(true, QStringLiteral("inspect_misalignment"), message, data);
}

QJsonObject GeometricTool::execute(const QJsonObject& params)
{
    const QString action = params.value("action").toString();
    if (action == QLatin1String("audit_residuals")) {
        return auditGcpResiduals(params.value("gcps").toArray(),
                                 params.value("rmse_threshold").toDouble(1.0));
    }
    if (action == QLatin1String("recommend_model")) {
        return recommendOptimalModel(params.value("active_gcp_count").toInt(0),
                                     params.value("terrain_roughness").toDouble(0.0),
                                     params.value("coverage_ratio").toDouble(0.0));
    }
    if (action == QLatin1String("inspect_misalignment")) {
        return inspectMisalignment(params.value("source_image_path").toString(),
                                   params.value("reference_image_path").toString());
    }
    if (action.isEmpty()) {
        return envelope(false, QString(),
                        QStringLiteral("Missing required parameter: action."));
    }
    return envelope(false, action,
                    QStringLiteral("Unknown action: supported actions are audit_residuals, "
                                   "recommend_model, inspect_misalignment."));
}

} // namespace rs::agent
