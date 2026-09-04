/***************************************************************************
 * rs_obia_hierarchy_operator.cpp  — Dual-write U4: operators → analysis hierarchy
 *
 * Issue #663 — the hierarchy gains a rehydrate mode (labelsFine/labelsCoarse/
 * parents inputs → RsObjectHierarchy::setLevels) so the OBIA GUI's classify
 * iterations reuse a previously built hierarchy instead of re-running OTB,
 * and OTB-less environments can classify an existing one. The classify leg
 * additionally accepts interactive segmentClasses labels, classifier
 * hyperparameters, a classColors palette, an entropy sidecar and reports
 * training accuracy — parity with rs:obia_classify.
 ***************************************************************************/
#include "rs_obia_hierarchy_operator.h"

#include "rs_obia_common.h"

#include "analysis/segmentation/rs_class_raster.h"
#include "analysis/segmentation/rs_hierarchy_features.h"
#include "analysis/segmentation/rs_object_classify.h"
#include "analysis/segmentation/rs_object_hierarchy.h"
#include "analysis/segmentation/rs_otb_segmenter.h"
#include "analysis/segmentation/rs_parent_link.h"
#include "analysis/segmentation/rs_roi_labeler.h"
#include "analysis/segmentation/rs_segmenter_port.h"
#include "analysis/classification/rs_classifier_backend_factory.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <QFile>
#include <QSet>
#include <QTextStream>

#include <gdal.h>
#include <ogr_api.h>

#include <memory>
#include <set>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using obia::hasObjectParam;
using obia::objectParam;
using obia::intKey;
using obia::parseClassColors;
using obia::accuracyToJson;
using obia::trainingAccuracy;
using obia::writeUncertaintyCsv;

namespace {

const std::vector<std::string> s_methods = {"svm", "normal_bayes", "random_forest", "kmeans", "mlp"};

void writeParentCsv(const QString& path, const QMap<quint32, quint32>& table) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        throw RSOperatorError(ErrorCode::FileNotWritable, "Cannot write " + path.toStdString());
    QTextStream out(&f);
    out << "fine_id,parent_id\n";
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
        out << it.key() << ',' << it.value() << '\n';
}

RsParentTable readParentCsv(const QString& path) {
    RsParentTable table;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Cannot read parents CSV: " + path.toStdString());
    QTextStream in(&f);
    int lineNo = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty() || line.startsWith(QStringLiteral("fine_id")))
            continue; // header
        const QStringList parts = line.split(',');
        if (parts.size() != 2) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Malformed parents CSV line " + std::to_string(lineNo) +
                                  ": " + line.toStdString());
        }
        bool ok1 = false, ok2 = false;
        const quint32 fine = parts[0].toUInt(&ok1);
        const quint32 parent = parts[1].toUInt(&ok2);
        if (!ok1 || !ok2)
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Non-integer ids in parents CSV line " +
                                      std::to_string(lineNo) + ": " + line.toStdString());
        table.fineToParent.insert(fine, parent);
    }
    return table;
}

/// Rehydrate a hierarchy from label GeoTIFFs (+ optional parents CSV) via the
/// fixture installer — the same validation path buildLevels populates.
RsObjectHierarchy rehydrateHierarchy(const std::string& labelsFine,
                                     const std::string& labelsCoarse,
                                     const std::string& parents,
                                     RSOperatorContext& context) {
    context.reportProgress(0.1, "Loading hierarchy levels");
    RsSegmentMap fine = RsSegmentMap::fromGeoTIFF(QString::fromStdString(labelsFine));
    if (fine.isEmpty())
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "labelsFine is empty or unreadable as a UInt32 label map");

    QVector<RsSegmentMap> levels;
    levels.reserve(2);
    levels.append(fine);
    if (!labelsCoarse.empty()) {
        RsSegmentMap coarse = RsSegmentMap::fromGeoTIFF(QString::fromStdString(labelsCoarse));
        if (coarse.isEmpty())
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "labelsCoarse is empty or unreadable as a UInt32 label map");
        levels.append(coarse);
    }

    QVector<RsParentTable> parentsTables;
    if (levels.size() == 2) {
        // Missing parents CSV → all-orphan hierarchy (F2a zeroed inter-level
        // fields); the file when present must parse cleanly.
        parentsTables.append(parents.empty() ? RsParentTable{}
                                             : readParentCsv(QString::fromStdString(parents)));
    } else if (!parents.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "`parents` requires `labelsCoarse` (two levels to link)");
    }

    RsObjectHierarchy hierarchy;
    QString err;
    if (!hierarchy.setLevels(std::move(levels), std::move(parentsTables), &err))
        throw RSOperatorError(ErrorCode::InvalidInputData, err.toStdString());
    return hierarchy;
}

} // namespace

Json::Value RsObiaHierarchyOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Source multi-band raster");
    props["outputFine"] = makeOutputParam("outputFine", "Fine-level label raster (UInt32)", "tif");
    props["outputFine"]["required"] = false;
    props["outputCoarse"] = makeOutputParam("outputCoarse", "Optional coarse-level label raster", "tif");
    props["outputCoarse"]["required"] = false;
    props["outputParents"] = makeOutputParam("outputParents", "Optional fine→parent CSV", "csv");
    props["outputParents"]["required"] = false;
    props["labelsFine"] = makeRasterParam(
        "labelsFine", "Rehydrate mode: existing fine label raster (skips OTB; outputs* ignored)");
    props["labelsFine"]["required"] = false;
    props["labelsCoarse"] = makeRasterParam(
        "labelsCoarse", "Rehydrate mode: existing coarse label raster");
    props["labelsCoarse"]["required"] = false;
    props["parents"] = makeStringParam(
        "parents", "Rehydrate mode: fine→parent CSV (as written by outputParents)", "");
    props["parents"]["required"] = false;
    props["outputClass"] = makeOutputParam("outputClass", "Optional class raster (requires training)", "tif");
    props["outputClass"]["required"] = false;
    props["training"] = makeVectorParam("training", "Optional training polygons");
    props["training"]["required"] = false;
    props["segmentClasses"] = makeStringParam(
        "segmentClasses",
        "Interactive training: JSON {segmentId: classId} on classifyLevel "
        "(exactly one of training / segmentClasses when classifying)",
        "");
    props["segmentClasses"]["required"] = false;
    props["classField"] = makeStringParam("classField", "Integer class field", "class_id");
    props["method"] = makeEnumParam("method", "Classifier when classifying", s_methods, "svm");
    props["classifyLevel"] = makeIntegerParam("classifyLevel", "Hierarchy level to classify (0=finest)", 0);
    props["minLabelPixels"] = makeIntegerParam("minLabelPixels", "Min ROI pixels to label a segment", 3);
    props["spatialRadius"] = makeIntegerParam("spatialRadius", "MeanShift spatial radius (fine)", 5);
    props["rangeRadius"] = makeNumberParam("rangeRadius", "MeanShift range radius (fine)", 15.0);
    props["minRegionSize"] = makeIntegerParam("minRegionSize", "MeanShift min region size (fine)", 100);
    props["watershedThreshold"] = makeNumberParam("watershedThreshold", "Watershed threshold (coarse)", 0.01);
    props["maxIterations"] = makeIntegerParam("maxIterations", "MeanShift iteration cap (fine)", 100);
    props["threshold"] = makeNumberParam("threshold", "MeanShift convergence threshold (fine)", 0.1);

    // Classifier hyperparameters (defaults = RsClassifierBackendParams).
    props["rfNumTrees"] = makeIntegerParam("rfNumTrees", "RandomForest tree count", 100);
    props["rfMaxDepth"] = makeIntegerParam("rfMaxDepth", "RandomForest max tree depth", 10);
    props["rfMinSampleCount"] = makeIntegerParam("rfMinSampleCount", "RandomForest min samples per node", 5);
    props["mlpHiddenLayerSize"] = makeIntegerParam("mlpHiddenLayerSize", "MLP hidden-layer neurons", 16);
    props["mlpMaxIter"] = makeIntegerParam("mlpMaxIter", "MLP max iterations", 500);

    props["classColors"] = makeStringParam(
        "classColors", "Optional palette JSON {classId: \"#rrggbb\"} embedded in outputClass", "");
    props["classColors"]["required"] = false;
    Json::Value outputUncertainty = makeOutputParam(
        "outputUncertainty", "Optional per-segment CSV (segment_id, entropy, class_id)", "csv");
    outputUncertainty["required"] = false;
    props["outputUncertainty"] = outputUncertainty;

    Json::Value outputs(Json::objectValue);
    outputs["fineSegments"] = makeIntegerParam("fineSegments", "Fine segment count", 0);
    outputs["coarseSegments"] = makeIntegerParam("coarseSegments", "Coarse segment count", 0);
    outputs["labeledSegments"] = makeIntegerParam("labeledSegments", "ROI-labeled segments", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input"});
    return root;
}

Json::Value RsObiaHierarchyOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("obia");
    meta["tags"].append("hierarchy");
    meta["tags"].append("otb");
    meta["purpose"] = "Hierarchical OBIA: build (OTB) or reuse (labelsFine) a two-level object hierarchy, optionally classify a level";
    meta["prerequisites"] = "Build mode: OTB Segmentation CLI (SICNU_OTB_PATH). Rehydrate mode: outputs of a previous build";
    meta["limitations"] =
        "Build mode has no silent teaching fallback (fail-closed without OTB); use "
        "labelsFine/labelsCoarse/parents to reuse an existing hierarchy without OTB";
    Json::Value hints(Json::arrayValue);
    hints.append("Build: input + outputFine (+ outputCoarse / outputParents)");
    hints.append("Reuse: labelsFine (+ labelsCoarse + parents) — classify without re-segmenting");
    hints.append("Classify: add training XOR segmentClasses + outputClass (+ classifyLevel)");
    meta["workflowHints"] = hints;
    return meta;
}

Json::Value RsObiaHierarchyOperator::executionEstimate() const
{
    // FullRaster (default policy). Primary segmentation runs as external OTB
    // CLIs (their RAM is managed outside this process); the in-process footprint
    // is the label rasters, parent table and per-segment feature matrix. OTB
    // writes its intermediate label rasters into a temporary directory.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 33554432; // 2 label rasters + feature matrix + fixed
    est["temporaryDiskBytes"] = 8388608; // OTB temp label rasters (2 x 1024x1024 UInt32)
    return est;
}

Json::Value RsObiaHierarchyOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputFine = getString(params, "outputFine", "");
    const std::string outputCoarse = getString(params, "outputCoarse", "");
    const std::string outputParents = getString(params, "outputParents", "");
    const std::string labelsFine = getString(params, "labelsFine", "");
    const std::string labelsCoarse = getString(params, "labelsCoarse", "");
    const std::string parents = getString(params, "parents", "");
    const std::string outputClass = getString(params, "outputClass", "");
    const std::string training = getString(params, "training", "");
    const std::string classField = getString(params, "classField", "class_id");
    const std::string method = getEnum(params, "method", s_methods, "svm");
    const int classifyLevel = getInt(params, "classifyLevel", 0);
    const int minLabelPixels = getInt(params, "minLabelPixels", 3);
    const std::string outputUncertainty = getString(params, "outputUncertainty", "");

    if (!QFile::exists(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);

    const bool rehydrate = !labelsFine.empty();
    const bool classify = hasObjectParam(params, "segmentClasses") || !training.empty();
    const bool hasSegmentClasses = hasObjectParam(params, "segmentClasses");
    const bool hasTraining = !training.empty();

    if (hasSegmentClasses && hasTraining)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Provide exactly one training source: `training` (polygons) "
                              "or `segmentClasses` (interactive labels)");
    if (classify && outputClass.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Classifying requires `outputClass`");
    if (!classify && !outputClass.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "`outputClass` requires a training source (training / segmentClasses)");
    if (!rehydrate && outputFine.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Build mode requires `outputFine` (or pass `labelsFine` to reuse a hierarchy)");
    if (minLabelPixels <= 0)
        throw RSOperatorError(ErrorCode::InvalidParameter, "minLabelPixels must be > 0");
    // Hyperparameters default to the factory struct (single source of truth).
    const RsClassifierBackendParams hyperparams = obia::classifierHyperParams(params);

    GDALAllRegister();
    OGRRegisterAll();

    RsObjectHierarchy hierarchy;
    if (rehydrate) {
        if (!QFile::exists(QString::fromStdString(labelsFine)))
            throw RSOperatorError(ErrorCode::FileNotFound, "labelsFine not found: " + labelsFine);
        if (!labelsCoarse.empty() && !QFile::exists(QString::fromStdString(labelsCoarse)))
            throw RSOperatorError(ErrorCode::FileNotFound, "labelsCoarse not found: " + labelsCoarse);
        if (!parents.empty() && !QFile::exists(QString::fromStdString(parents)))
            throw RSOperatorError(ErrorCode::FileNotFound, "parents not found: " + parents);
        hierarchy = rehydrateHierarchy(labelsFine, labelsCoarse, parents, context);
        context.throwIfCancelled();
    } else {
        if (!RsOtbSegmenter::isAvailable()) {
            throw RSOperatorError(
                ErrorCode::OtbError,
                "OTB Segmentation CLI not found — set SICNU_OTB_PATH or install OTB. "
                "Hierarchical OBIA primary segmenters require OTB (no silent teaching fallback; "
                "pass labelsFine/labelsCoarse/parents to reuse an existing hierarchy).");
        }

        RsLevelSpec fine;
        fine.filter = RsLevelSpec::Filter::MeanShift;
        fine.name = QStringLiteral("fine");
        fine.spatialRadius = getInt(params, "spatialRadius", 5);
        fine.rangeRadius = getDouble(params, "rangeRadius", 15.0);
        fine.minRegionSize = getInt(params, "minRegionSize", 100);
        fine.maxIterations = getInt(params, "maxIterations", 100);
        fine.threshold = getDouble(params, "threshold", 0.1);

        RsLevelSpec coarse;
        coarse.filter = RsLevelSpec::Filter::Watershed;
        coarse.name = QStringLiteral("coarse");
        coarse.minRegionSize = fine.minRegionSize;
        coarse.watershedThreshold = getDouble(params, "watershedThreshold", 0.01);

        context.reportProgress(0.05, "buildLevels (MeanShift + Watershed + link)");
        RsOtbSegmenter segmenter;
        RsPixelMajorityParentLink linker;
        QString err;
        const QString rasterQ = QString::fromStdString(inputPath);
        const bool ok = hierarchy.buildLevels(
            rasterQ, {fine, coarse}, segmenter, linker, &err,
            [&context]() { return context.isCancelled(); });
        if (!ok)
            throw RSOperatorError(ErrorCode::ComputationError, err.toStdString());

        context.throwIfCancelled();
        context.reportProgress(0.55, "Writing fine labels");
        {
            QString err;
            if (!hierarchy.level(0).toGeoTIFF(QString::fromStdString(outputFine), rasterQ, &err))
                throw RSOperatorError(ErrorCode::GdalError, err.toStdString());
        }

        if (!outputCoarse.empty() && hierarchy.levelCount() > 1) {
            QString err;
            if (!hierarchy.level(1).toGeoTIFF(QString::fromStdString(outputCoarse), rasterQ, &err))
                throw RSOperatorError(ErrorCode::GdalError, err.toStdString());
        }

        if (!outputParents.empty() && hierarchy.levelCount() > 1)
            writeParentCsv(QString::fromStdString(outputParents), hierarchy.parentTable(0));
    }

    int labeledSegments = 0;
    if (classify) {
        if (classifyLevel < 0 || classifyLevel >= hierarchy.levelCount())
            throw RSOperatorError(ErrorCode::InvalidParameter, "classifyLevel out of range");

        context.reportProgress(0.65, "Training labels");
        QMap<quint32, int> trainLabels;
        if (hasSegmentClasses) {
            const Json::Value segmentClasses = objectParam(params, "segmentClasses");
            QSet<quint32> knownSegIds = hierarchy.level(classifyLevel).uniqueLabels();
            int unknownIds = 0;
            for (auto it = segmentClasses.begin(); it != segmentClasses.end(); ++it) {
                const quint32 segId = static_cast<quint32>(
                    intKey(it.key().asString(), "segmentClasses"));
                const int classId = it->asInt();
                if (classId <= 0)
                    continue;
                if (!knownSegIds.contains(segId)) {
                    ++unknownIds;
                    continue;
                }
                trainLabels.insert(segId, classId);
            }
            if (unknownIds > 0)
                throw RSOperatorError(ErrorCode::InvalidInputData,
                                      "segmentClasses references " + std::to_string(unknownIds) +
                                      " segment ids not present at classifyLevel");
        } else {
            QString roiErr;
            // ADR 0060: canonical ROI-majority labeling (was the local
            // labelFromRoi helper — point-in-polygon — in this file).
            trainLabels = RsRoiLabeler::labelByMajority(
                hierarchy.level(classifyLevel), QString::fromStdString(inputPath),
                QString::fromStdString(training), QString::fromStdString(classField),
                minLabelPixels, &roiErr,
                [&context]() { return context.isCancelled(); });
            context.throwIfCancelled();
            if (trainLabels.isEmpty() && !roiErr.isEmpty())
                throw RSOperatorError(ErrorCode::GdalError, roiErr.toStdString());
        }
        labeledSegments = trainLabels.size();
        if (labeledSegments < 2)
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Fewer than 2 segments labeled by training");

        std::set<int> uniqueClasses;
        for (auto it = trainLabels.constBegin(); it != trainLabels.constEnd(); ++it)
            uniqueClasses.insert(it.value());
        if (uniqueClasses.size() < 2)
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Training must contain at least 2 distinct classes (found " +
                                      std::to_string(uniqueClasses.size()) + ")");

        context.reportProgress(0.75, "F2a features + classify");
        QVector<int> bands;
        {
            GDALDatasetH ds = GDALOpen(inputPath.c_str(), GA_ReadOnly);
            if (!ds)
                throw RSOperatorError(ErrorCode::GdalError, "Cannot reopen input for bands");
            const int bc = GDALGetRasterCount(ds);
            GDALClose(ds);
            for (int b = 1; b <= bc; ++b)
                bands.append(b);
        }

        auto feat = RsHierarchyFeatures::buildFeatureMatrix(
            hierarchy, QString::fromStdString(inputPath), classifyLevel, bands);
        if (!feat.ok)
            throw RSOperatorError(ErrorCode::ComputationError, feat.errorMessage.toStdString());

        // ADR 0061 — backend construction through the single factory (was
        // an inline normal_bayes/SVM branch here).
        std::unique_ptr<RsClassifierBackend> backend = RsClassifierBackendFactory::create(
            QString::fromStdString(method), hyperparams);
        if (!backend)
            throw RSOperatorError(ErrorCode::InvalidParameter, "Failed to create classifier backend: " + method);

        auto cls = RsObjectClassify::classify(
            feat.X, feat.meta.segmentIds, trainLabels, *backend);
        if (!cls.ok)
            throw RSOperatorError(ErrorCode::ComputationError, cls.errorMessage.toStdString());
        context.throwIfCancelled();

        if (!outputUncertainty.empty())
            writeUncertaintyCsv(outputUncertainty, cls.segmentUncertainties, cls.segmentClasses);

        context.reportProgress(0.9, "Paint class raster");
        const QHash<int, QColor> classColors =
            hasObjectParam(params, "classColors")
                ? parseClassColors(objectParam(params, "classColors"))
                : QHash<int, QColor>();
        auto paint = RsClassRaster::paint(
            hierarchy.level(classifyLevel), cls.segmentClasses,
            QString::fromStdString(inputPath),
            QString::fromStdString(outputClass), classColors);
        if (!paint.ok)
            throw RSOperatorError(ErrorCode::GdalError, paint.errorMessage.toStdString());

        context.reportProgress(1.0, "obia_hierarchy complete");
        Json::Value result(Json::objectValue);
        if (!rehydrate)
            result["outputFine"] = outputFine;
        result["fineSegments"] = hierarchy.level(0).segmentCount();
        result["coarseSegments"] = hierarchy.levelCount() > 1 ? hierarchy.level(1).segmentCount() : 0;
        result["labeledSegments"] = labeledSegments;
        result["outputClass"] = outputClass;
        result["classifyLevel"] = classifyLevel;
        result["method"] = method;
        if (!outputCoarse.empty() && !rehydrate)
            result["outputCoarse"] = outputCoarse;
        if (!outputParents.empty() && !rehydrate)
            result["outputParents"] = outputParents;
        if (!outputUncertainty.empty())
            result["uncertaintyOutput"] = outputUncertainty;
        result["levels"] = hierarchy.levelCount();

        const RsAccuracyAssessment::Result accuracy = trainingAccuracy(trainLabels, cls.segmentClasses);
        if (!accuracy.classIds.isEmpty()) {
            result["accuracy"] = accuracyToJson(accuracy);
            result["labeledAccuracy"] = accuracy.overallAccuracy;
        }
        return result;
    }

    context.reportProgress(1.0, "obia_hierarchy complete");
    Json::Value result(Json::objectValue);
    if (!rehydrate)
        result["outputFine"] = outputFine;
    result["fineSegments"] = hierarchy.level(0).segmentCount();
    result["coarseSegments"] = hierarchy.levelCount() > 1 ? hierarchy.level(1).segmentCount() : 0;
    result["labeledSegments"] = labeledSegments;
    if (!outputCoarse.empty() && !rehydrate)
        result["outputCoarse"] = outputCoarse;
    if (!outputParents.empty() && !rehydrate)
        result["outputParents"] = outputParents;
    result["levels"] = hierarchy.levelCount();
    return result;
}

} // namespace sicnu::operators::rs
