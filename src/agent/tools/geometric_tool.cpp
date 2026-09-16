// geometric_tool.cpp — D14 Package H implementation (ADR 0159).
#include "agent/tools/geometric_tool.h"

#include "processing/algorithms/feature_matcher.h"
#include "processing/algorithms/registration/multimodal_matcher.h"
#include "processing/algorithms/registration/model_selector.h"
#include "processing/algorithms/registration/registration_quality.h"
#include "processing/algorithms/registration/stack_registrator.h"

#include <gdal_priv.h>

#include <QFile>

#include <algorithm>
#include <cmath>
#include <memory>
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

namespace {

// Shared bounded raster reader (F13): decimates to at most 512 px on the
// long side via GDAL read-time resampling so agent-supplied rasters cannot
// blow the memory budget. Returns false when the band cannot be read.
bool readBand1Bounded(GDALDataset* dataset, int& width, int& height, std::vector<float>& buffer)
{
    const int rawW = dataset->GetRasterXSize();
    const int rawH = dataset->GetRasterYSize();
    const int longest = std::max(rawW, rawH);
    width = longest > 512 ? rawW * 512 / longest : rawW;
    height = longest > 512 ? rawH * 512 / longest : rawH;
    width = std::clamp(width, 1, rawW);
    height = std::clamp(height, 1, rawH);
    buffer.assign(static_cast<size_t>(width) * height, 0.0f);
    return dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, rawW, rawH, buffer.data(), width,
                                               height, GDT_Float32, 0, 0)
           == CE_None;
}

} // namespace

QString GeometricTool::toolDescription() const
{
    return QStringLiteral(
        "Spatial geometric registration: audits GCP residuals (3-sigma gross "
        "blunders), recommends the optimal transform model from point count / "
        "terrain roughness / spatial coverage, pre-checks misalignment "
        "between a source and a reference raster via robust feature matching, "
        "runs cross-modal (optical-SAR) tie-point matching with explicit "
        "refusal semantics and CE90/residual-field quality, performs "
        "evidence-driven transform model selection (k-fold held-out gate), "
        "and solves multi-scene stack registration with loop-closure drift "
        "metrics.");
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
                                     QStringLiteral("inspect_misalignment"),
                                     QStringLiteral("multimodal_register"),
                                     QStringLiteral("select_model"),
                                     QStringLiteral("stack_register")});

    QJsonObject gcps;
    gcps.insert("type", QStringLiteral("array"));
    gcps.insert("description", QStringLiteral(
        "audit_residuals: {id, residual_x, residual_y[, residual_total]}. "
        "select_model: {source_x, source_y, target_x, target_y}."));
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

    QJsonObject metricProp;
    metricProp.insert("type", QStringLiteral("string"));
    metricProp.insert("description",
                      QStringLiteral("multimodal_register similarity metric."));
    metricProp.insert("enum", QJsonArray{QStringLiteral("auto"),
                                         QStringLiteral("phase_correlation"),
                                         QStringLiteral("mutual_information"),
                                         QStringLiteral("ncc")});
    QJsonObject windowProp;
    windowProp.insert("type", QStringLiteral("integer"));
    windowProp.insert("description", QStringLiteral("Window side in px (16..256)."));
    QJsonObject radiusProp;
    radiusProp.insert("type", QStringLiteral("integer"));
    radiusProp.insert("description", QStringLiteral("Fine-level search radius in px."));
    QJsonObject foldsProp;
    foldsProp.insert("type", QStringLiteral("integer"));
    foldsProp.insert("description", QStringLiteral("select_model k-fold count (>= 2)."));
    foldsProp.insert("minimum", 2.0);
    QJsonObject improvementProp;
    improvementProp.insert("type", QStringLiteral("number"));
    improvementProp.insert("description",
                           QStringLiteral("select_model relative held-out improvement gate."));
    improvementProp.insert("minimum", 0.0);
    QJsonObject sceneIdsProp;
    sceneIdsProp.insert("type", QStringLiteral("array"));
    sceneIdsProp.insert("description", QStringLiteral("stack_register scene id array."));
    sceneIdsProp.insert("items", [] {
        QJsonObject s;
        s.insert("type", QStringLiteral("string"));
        return s;
    }());
    QJsonObject observationsProp;
    observationsProp.insert("type", QStringLiteral("array"));
    observationsProp.insert(
        "description",
        QStringLiteral("stack_register pairwise translations {from_id, to_id, tx, ty, "
                       "confidence, inlier_count}."));
    observationsProp.insert("items", [] {
        QJsonObject s;
        s.insert("type", QStringLiteral("object"));
        return s;
    }());
    QJsonObject referenceProp;
    referenceProp.insert("type", QStringLiteral("string"));
    referenceProp.insert("description",
                         QStringLiteral("stack_register reference scene id (optional)."));

    QJsonObject properties;
    properties.insert("action", action);
    properties.insert("gcps", gcps);
    properties.insert("rmse_threshold", threshold);
    properties.insert("active_gcp_count", gcpCount);
    properties.insert("terrain_roughness", roughness);
    properties.insert("coverage_ratio", coverage);
    properties.insert("source_image_path", sourcePath);
    properties.insert("reference_image_path", refPath);
    properties.insert("metric", metricProp);
    properties.insert("window_size", windowProp);
    properties.insert("search_radius", radiusProp);
    properties.insert("folds", foldsProp);
    properties.insert("min_improvement", improvementProp);
    properties.insert("scene_ids", sceneIdsProp);
    properties.insert("observations", observationsProp);
    properties.insert("reference_id", referenceProp);
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
    struct DatasetCloser {
        void operator()(GDALDataset* dataset) const { if (dataset) GDALClose(dataset); }
    };
    std::unique_ptr<GDALDataset, DatasetCloser> src(
        static_cast<GDALDataset*>(GDALOpen(sourceImagePath.toUtf8().constData(), GA_ReadOnly)));
    std::unique_ptr<GDALDataset, DatasetCloser> ref(
        static_cast<GDALDataset*>(GDALOpen(refImagePath.toUtf8().constData(), GA_ReadOnly)));
    if (!src || !ref) {
        return envelope(false, QStringLiteral("inspect_misalignment"),
                        QStringLiteral("Failed to open one of the rasters with GDAL."));
    }

    // Agent-supplied rasters can be arbitrarily large: read through the
    // shared bounded reader (512 px long-side cap).
    int srcW = 0, srcH = 0, refW = 0, refH = 0;
    std::vector<float> srcData, refData;
    if (!readBand1Bounded(src.get(), srcW, srcH, srcData)
        || !readBand1Bounded(ref.get(), refW, refH, refData)) {
        return envelope(false, QStringLiteral("inspect_misalignment"),
                        QStringLiteral("Failed to read band 1 of one of the rasters."));
    }
    src.reset();
    ref.reset();

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

QJsonObject GeometricTool::multimodalRegister(const QString& sourceImagePath,
                                              const QString& refImagePath, const QString& metric,
                                              int windowSize, int searchRadius)
{
    const QString action = QStringLiteral("multimodal_register");
    if (sourceImagePath.isEmpty() || refImagePath.isEmpty())
        return envelope(false, action,
                        QStringLiteral("Both source and reference image paths are required."));
    if (!QFile::exists(sourceImagePath) || !QFile::exists(refImagePath))
        return envelope(false, action,
                        QStringLiteral("Source or reference raster does not exist."));

    GDALAllRegister();
    struct DatasetCloser {
        void operator()(GDALDataset* dataset) const
        {
            if (dataset)
                GDALClose(dataset);
        }
    };
    std::unique_ptr<GDALDataset, DatasetCloser> src(
        static_cast<GDALDataset*>(GDALOpen(sourceImagePath.toUtf8().constData(), GA_ReadOnly)));
    std::unique_ptr<GDALDataset, DatasetCloser> ref(
        static_cast<GDALDataset*>(GDALOpen(refImagePath.toUtf8().constData(), GA_ReadOnly)));
    if (!src || !ref)
        return envelope(false, action,
                        QStringLiteral("Failed to open one of the rasters with GDAL."));
    int srcW = 0, srcH = 0, refW = 0, refH = 0;
    std::vector<float> srcData, refData;
    if (!readBand1Bounded(src.get(), srcW, srcH, srcData)
        || !readBand1Bounded(ref.get(), refW, refH, refData))
        return envelope(false, action,
                        QStringLiteral("Failed to read band 1 of one of the rasters."));
    src.reset();
    ref.reset();

    sicnu::registration::MultimodalMatchOptions options;
    if (metric == QLatin1String("phase_correlation"))
        options.metric = sicnu::registration::MatchMetric::PhaseCorrelation;
    else if (metric == QLatin1String("ncc"))
        options.metric = sicnu::registration::MatchMetric::NormalizedCrossCorrelation;
    else if (metric == QLatin1String("mutual_information"))
        options.metric = sicnu::registration::MatchMetric::MutualInformation;
    // "auto" (default) stays Auto.
    if (windowSize > 0)
        options.windowSize = windowSize;
    if (searchRadius > 0)
        options.searchRadius = searchRadius;

    const auto report = sicnu::registration::MultimodalMatcher::matchImages(
        srcData.data(), srcW, srcH, refData.data(), refW, refH, options);

    QJsonObject data;
    data.insert("status", sicnu::registration::statusToString(report.status));
    data.insert("reason", report.reason);
    data.insert("inlier_count", report.inlierCount);
    data.insert("inlier_ratio", report.inlierRatio);
    data.insert("inlier_rmse_px", report.inlierRmse);
    data.insert("coverage_ratio", report.coverageRatio);
    data.insert("rejected_flat_windows", report.rejectedFlatWindows);
    data.insert("rejected_low_snr_windows", report.rejectedLowSnrWindows);
    QJsonArray homography;
    for (const double value : report.consensusHomography)
        homography.append(value);
    data.insert("consensus_homography", homography);

    // Stage evidence: the coarse-to-fine trace (explain surface).
    QJsonArray stages;
    for (const auto& stage : report.stages) {
        QJsonObject s;
        s.insert("level", stage.level);
        s.insert("source_width", stage.sourceWidth);
        s.insert("source_height", stage.sourceHeight);
        s.insert("accepted", stage.accepted);
        s.insert("median_score", stage.medianScore);
        stages.append(s);
    }
    data.insert("pyramid_stages", stages);

    // Up to 12 tie points inline (bounded payload; the caller gets the
    // summary plus a reviewer-visible sample, not the full set).
    QJsonArray ties;
    int shown = 0;
    for (const auto& p : report.points) {
        if (!p.inlier || shown >= 12)
            continue;
        QJsonObject t;
        t.insert("src_x", p.srcX);
        t.insert("src_y", p.srcY);
        t.insert("dst_x", p.dstX);
        t.insert("dst_y", p.dstY);
        t.insert("score", p.score);
        t.insert("residual_px", p.residual);
        ties.append(t);
        ++shown;
    }
    data.insert("tie_points_sample", ties);

    // Quality block (CE90 / residual field / confidence) over inliers.
    const double imgW = srcW;
    const double imgH = srcH;
    const auto quality = sicnu::registration::RegistrationQuality::evaluate(
        report.points, imgW, imgH, report.coverageRatio);
    const auto field = sicnu::registration::RegistrationQuality::residualField(
        report.points, imgW, imgH, 8);
    data.insert("quality", sicnu::registration::RegistrationQuality::toJson(
                               quality, field, sicnu::registration::statusToString(report.status),
                               report.reason));

    const bool trustworthy = report.status == sicnu::registration::RegistrationStatus::Success;
    const QString message =
        trustworthy
            ? QStringLiteral("Cross-modal registration succeeded: %1 inliers, RMSE %2 px.")
                  .arg(report.inlierCount)
                  .arg(report.inlierRmse)
            : QStringLiteral("Registration not trustworthy (status %1, reason %2). Treat the "
                             "geometry as unverified.")
                  .arg(sicnu::registration::statusToString(report.status), report.reason);
    return envelope(true, action, message, data);
}

QJsonObject GeometricTool::selectModel(const QJsonArray& gcpArray, int folds,
                                       double minImprovement)
{
    const QString action = QStringLiteral("select_model");
    std::vector<std::pair<double, double>> srcPts, dstPts;
    for (const auto& item : gcpArray) {
        if (!item.isObject())
            continue;
        const QJsonObject obj = item.toObject();
        const double sx = obj.value("source_x").toDouble();
        const double sy = obj.value("source_y").toDouble();
        const double tx = obj.value("target_x").toDouble();
        const double ty = obj.value("target_y").toDouble();
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(tx) || !std::isfinite(ty))
            continue;
        srcPts.emplace_back(sx, sy);
        dstPts.emplace_back(tx, ty);
    }
    if (srcPts.size() < 2)
        return envelope(false, action,
                        QStringLiteral(
                            "Need at least two valid correspondences (source_x/source_y/"
                            "target_x/target_y per point)."));

    sicnu::registration::ModelSelectionOptions options;
    if (folds >= 2)
        options.folds = folds;
    if (minImprovement >= 0.0)
        options.minImprovement = minImprovement;
    const auto report = sicnu::registration::ModelSelector::select(srcPts, dstPts, options);

    QJsonObject data;
    data.insert("status", sicnu::registration::statusToString(report.status));
    data.insert("reason", report.reason);
    data.insert("selected_model", sicnu::registration::candidateModelName(report.selected));
    QJsonArray evidence;
    for (const auto& ev : report.evidence) {
        QJsonObject e;
        e.insert("model", sicnu::registration::candidateModelName(ev.model));
        e.insert("feasible", ev.feasible);
        e.insert("fit_rmse_px", ev.fitRmse);
        e.insert("heldout_rmse_px", ev.cvRmse);
        e.insert("condition_number", ev.conditionNumber);
        e.insert("bending_energy", ev.bendingEnergy);
        e.insert("rejected_reason", ev.rejectedReason);
        evidence.append(e);
    }
    data.insert("candidate_evidence", evidence);
    if (report.transform.success) {
        QJsonArray coeffs;
        for (const double c : report.transform.forwardCoeffs)
            coeffs.append(c);
        data.insert("forward_coeffs", coeffs);
        data.insert("rmse_forward_px", report.transform.rmseForward);
        data.insert("condition_number", report.transform.conditionNumber);
    }
    if (report.tpsFit.isFitted())
        data.insert("tps_bending_energy", report.tpsFit.computeBendingEnergy());

    const QString message =
        report.status == sicnu::registration::RegistrationStatus::Success
            ? QStringLiteral("Selected model: %1 (held-out evidence gate).")
                  .arg(sicnu::registration::candidateModelName(report.selected))
            : QStringLiteral("Model selection refused: %1.").arg(report.reason);
    return envelope(report.status == sicnu::registration::RegistrationStatus::Success, action,
                    message, data);
}

QJsonObject GeometricTool::stackRegister(const QJsonArray& sceneIds,
                                         const QJsonArray& observations,
                                         const QString& referenceId)
{
    const QString action = QStringLiteral("stack_register");
    std::vector<QString> ids;
    for (const auto& item : sceneIds) {
        const QString id = item.toString();
        if (!id.isEmpty())
            ids.push_back(id);
    }
    std::vector<sicnu::registration::StackPairObservation> obs;
    for (const auto& item : observations) {
        if (!item.isObject())
            continue;
        const QJsonObject obj = item.toObject();
        sicnu::registration::StackPairObservation o;
        o.fromId = obj.value("from_id").toString();
        o.toId = obj.value("to_id").toString();
        o.tx = obj.value("tx").toDouble();
        o.ty = obj.value("ty").toDouble();
        o.confidence = obj.value("confidence").toDouble(1.0);
        o.inlierCount = obj.value("inlier_count").toInt(1);
        if (o.fromId.isEmpty() || o.toId.isEmpty())
            continue;
        obs.push_back(o);
    }
    if (ids.empty())
        return envelope(false, action, QStringLiteral("scene_ids must be a non-empty array."));

    try {
        sicnu::registration::StackOptions options;
        if (!referenceId.isEmpty())
            options.referenceId = referenceId;
        const auto sol = sicnu::registration::StackRegistrator::solveTranslations(ids, obs, options);

        QJsonObject data;
        data.insert("status", sicnu::registration::statusToString(sol.status));
        data.insert("reason", sol.reason);
        data.insert("reference_id", sol.referenceId);
        data.insert("max_edge_residual_px", sol.maxEdgeResidual);
        data.insert("rms_edge_residual_px", sol.rmsEdgeResidual);
        data.insert("disconnected_scenes", sol.disconnectedScenes);
        QJsonArray scenes;
        for (const auto& s : sol.scenes) {
            QJsonObject sc;
            sc.insert("scene_id", s.sceneId);
            sc.insert("offset_x", s.offsetX);
            sc.insert("offset_y", s.offsetY);
            sc.insert("hop_count", s.hopCount);
            sc.insert("max_edge_residual_px", s.maxEdgeResidual);
            sc.insert("connected", s.connected);
            scenes.append(sc);
        }
        data.insert("scenes", scenes);
        const QString message =
            sol.status == sicnu::registration::RegistrationStatus::Success
                ? QStringLiteral(
                      "Stack adjustment solved on reference %1 (max edge residual %2 px).")
                      .arg(sol.referenceId)
                      .arg(sol.maxEdgeResidual)
                : QStringLiteral("Stack adjustment refused: %1.").arg(sol.reason);
        return envelope(sol.status == sicnu::registration::RegistrationStatus::Success, action,
                        message, data);
    } catch (const std::exception& e) {
        return envelope(false, action,
                        QStringLiteral("Stack adjustment failed: %1").arg(e.what()));
    }
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
    if (action == QLatin1String("multimodal_register")) {
        return multimodalRegister(params.value("source_image_path").toString(),
                                  params.value("reference_image_path").toString(),
                                  params.value("metric").toString(QStringLiteral("auto")),
                                  params.value("window_size").toInt(0),
                                  params.value("search_radius").toInt(0));
    }
    if (action == QLatin1String("select_model")) {
        return selectModel(params.value("gcps").toArray(),
                           params.value("folds").toInt(0),
                           params.value("min_improvement").toDouble(-1.0));
    }
    if (action == QLatin1String("stack_register")) {
        return stackRegister(params.value("scene_ids").toArray(),
                             params.value("observations").toArray(),
                             params.value("reference_id").toString());
    }
    if (action.isEmpty()) {
        return envelope(false, QString(),
                        QStringLiteral("Missing required parameter: action."));
    }
    return envelope(false, action,
                    QStringLiteral("Unknown action: supported actions are audit_residuals, "
                                   "recommend_model, inspect_misalignment, multimodal_register, "
                                   "select_model, stack_register."));
}

} // namespace rs::agent
