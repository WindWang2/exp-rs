/***************************************************************************
 * rs_obia_hierarchy_operator.cpp  — Dual-write U4: operators → analysis hierarchy
 ***************************************************************************/
#include "rs_obia_hierarchy_operator.h"

#include "analysis/segmentation/rs_object_hierarchy.h"
#include "analysis/segmentation/rs_otb_segmenter.h"
#include "analysis/segmentation/rs_parent_link.h"
#include "analysis/segmentation/rs_hierarchy_features.h"
#include "analysis/segmentation/rs_object_classify.h"
#include "analysis/segmentation/rs_class_raster.h"
#include "analysis/segmentation/rs_segmenter_port.h"
#include "analysis/segmentation/rs_roi_labeler.h"
#include "analysis/classification/rs_classifier_backend_factory.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <QFile>
#include <QTextStream>

#include <gdal.h>
#include <ogr_api.h>

#include <memory>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

void writeParentCsv(const QString& path, const QMap<quint32, quint32>& table) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        throw RSOperatorError(ErrorCode::FileNotFound, "Cannot write " + path.toStdString());
    QTextStream out(&f);
    out << "fine_id,parent_id\n";
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
        out << it.key() << ',' << it.value() << '\n';
}

} // namespace

Json::Value RsObiaHierarchyOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Source multi-band raster");
    props["outputFine"] = makeOutputParam("outputFine", "Fine-level label raster (UInt32)", "tif");
    props["outputCoarse"] = makeOutputParam("outputCoarse", "Optional coarse-level label raster", "tif");
    props["outputCoarse"]["required"] = false;
    props["outputParents"] = makeOutputParam("outputParents", "Optional fine→parent CSV", "csv");
    props["outputParents"]["required"] = false;
    props["outputClass"] = makeOutputParam("outputClass", "Optional class raster (requires training)", "tif");
    props["outputClass"]["required"] = false;
    props["training"] = makeVectorParam("training", "Optional training polygons");
    props["training"]["required"] = false;
    props["classField"] = makeStringParam("classField", "Integer class field", "class_id");
    props["method"] = makeEnumParam("method", "Classifier when training set",
                                    {"svm", "normal_bayes"}, "svm");
    props["classifyLevel"] = makeIntegerParam("classifyLevel", "Hierarchy level to classify (0=finest)", 0);
    props["minLabelPixels"] = makeIntegerParam("minLabelPixels", "Min ROI pixels to label a segment", 3);
    props["spatialRadius"] = makeIntegerParam("spatialRadius", "MeanShift spatial radius (fine)", 5);
    props["rangeRadius"] = makeNumberParam("rangeRadius", "MeanShift range radius (fine)", 15.0);
    props["minRegionSize"] = makeIntegerParam("minRegionSize", "MeanShift min region size (fine)", 100);
    props["watershedThreshold"] = makeNumberParam("watershedThreshold", "Watershed threshold (coarse)", 0.01);

    Json::Value outputs(Json::objectValue);
    outputs["outputFine"] = makeStringParam("outputFine", "Fine labels path");
    outputs["fineSegments"] = makeIntegerParam("fineSegments", "Fine segment count", 0);
    outputs["coarseSegments"] = makeIntegerParam("coarseSegments", "Coarse segment count", 0);
    outputs["labeledSegments"] = makeIntegerParam("labeledSegments", "ROI-labeled segments", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "outputFine"});
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
    meta["purpose"] = "Hierarchical OBIA V1 dual-write path (analysis library)";
    meta["prerequisites"].append("OTB Segmentation CLI must be installed (SICNU_OTB_PATH)");
    meta["limitations"] = "Primary segmenters require OTB; no silent teaching fallback";
    meta["workflowHints"] = Json::Value(Json::arrayValue);
    meta["workflowHints"].append("Segment only: set input + outputFine (+ optional coarse/parents)");
    meta["workflowHints"].append("Full path: add training + outputClass for K1 classify on classifyLevel");
    return meta;
}

Json::Value RsObiaHierarchyOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputFine = requireString(params, "outputFine");
    const std::string outputCoarse = getString(params, "outputCoarse", "");
    const std::string outputParents = getString(params, "outputParents", "");
    const std::string outputClass = getString(params, "outputClass", "");
    const std::string training = getString(params, "training", "");
    const std::string classField = getString(params, "classField", "class_id");
    const std::string method = getEnum(params, "method", {"svm", "normal_bayes"}, "svm");
    const int classifyLevel = getInt(params, "classifyLevel", 0);
    const int minLabelPixels = getInt(params, "minLabelPixels", 3);

    if (!QFile::exists(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);

    if (!RsOtbSegmenter::isAvailable()) {
        throw RSOperatorError(
            ErrorCode::OtbError,
            "OTB Segmentation CLI not found — set SICNU_OTB_PATH or install OTB. "
            "Hierarchical OBIA primary segmenters require OTB (no silent teaching fallback).");
    }

    GDALAllRegister();
    OGRRegisterAll();

    RsLevelSpec fine;
    fine.filter = RsLevelSpec::Filter::MeanShift;
    fine.name = QStringLiteral("fine");
    fine.spatialRadius = getInt(params, "spatialRadius", 5);
    fine.rangeRadius = getDouble(params, "rangeRadius", 15.0);
    fine.minRegionSize = getInt(params, "minRegionSize", 100);

    RsLevelSpec coarse;
    coarse.filter = RsLevelSpec::Filter::Watershed;
    coarse.name = QStringLiteral("coarse");
    coarse.watershedThreshold = getDouble(params, "watershedThreshold", 0.01);

    context.reportProgress(0.05, "buildLevels (MeanShift + Watershed + link)");
    RsObjectHierarchy hierarchy;
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

    int labeledSegments = 0;
    if (!training.empty() && !outputClass.empty()) {
        if (classifyLevel < 0 || classifyLevel >= hierarchy.levelCount())
            throw RSOperatorError(ErrorCode::InvalidParameter, "classifyLevel out of range");

        context.reportProgress(0.65, "ROI majority labels");
        QString roiErr;
        // ADR 0060: canonical ROI-majority labeling (was the local
        // labelFromRoi helper — point-in-polygon — in this file).
        QMap<quint32, int> trainLabels = RsRoiLabeler::labelByMajority(
            hierarchy.level(classifyLevel), rasterQ,
            QString::fromStdString(training), QString::fromStdString(classField),
            minLabelPixels, &roiErr,
            [&context]() { return context.isCancelled(); });
        context.throwIfCancelled();
        if (trainLabels.isEmpty() && !roiErr.isEmpty())
            throw RSOperatorError(ErrorCode::GdalError, roiErr.toStdString());
        labeledSegments = trainLabels.size();
        if (labeledSegments < 2)
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Fewer than 2 segments labeled by training polygons");

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
            hierarchy, rasterQ, classifyLevel, bands);
        if (!feat.ok)
            throw RSOperatorError(ErrorCode::ComputationError, feat.errorMessage.toStdString());

        // ADR 0061 — backend construction through the single factory (was
        // an inline normal_bayes/SVM branch here).
        std::unique_ptr<RsClassifierBackend> backend =
            RsClassifierBackendFactory::create(QString::fromStdString(method));

        auto cls = RsObjectClassify::classify(
            feat.X, feat.meta.segmentIds, trainLabels, *backend);
        if (!cls.ok)
            throw RSOperatorError(ErrorCode::ComputationError, cls.errorMessage.toStdString());

        context.reportProgress(0.9, "Paint class raster");
        auto paint = RsClassRaster::paint(
            hierarchy.level(classifyLevel), cls.segmentClasses, rasterQ,
            QString::fromStdString(outputClass));
        if (!paint.ok)
            throw RSOperatorError(ErrorCode::GdalError, paint.errorMessage.toStdString());
    }

    context.reportProgress(1.0, "obia_hierarchy complete");
    Json::Value result(Json::objectValue);
    result["outputFine"] = outputFine;
    result["fineSegments"] = hierarchy.level(0).segmentCount();
    result["coarseSegments"] = hierarchy.levelCount() > 1 ? hierarchy.level(1).segmentCount() : 0;
    result["labeledSegments"] = labeledSegments;
    if (!outputCoarse.empty())
        result["outputCoarse"] = outputCoarse;
    if (!outputParents.empty())
        result["outputParents"] = outputParents;
    if (!outputClass.empty())
        result["outputClass"] = outputClass;
    result["levels"] = hierarchy.levelCount();
    return result;
}

} // namespace sicnu::operators::rs
