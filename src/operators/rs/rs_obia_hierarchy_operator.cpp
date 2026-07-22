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
#include "analysis/classification/rs_classifier_svm.h"
#include "analysis/classification/rs_classifier_normalbayes.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <QFile>
#include <QTextStream>

#include <gdal.h>
#include <cpl_string.h>
#include <ogr_api.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

void writeLabelGeoTiff(const QString& path, const RsSegmentMap& map,
                       const QString& refPath) {
    GDALDriverH drv = GDALGetDriverByName("GTiff");
    if (!drv)
        throw RSOperatorError(ErrorCode::GdalError, "GTiff driver missing");

    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH ds = GDALCreate(drv, path.toUtf8().constData(),
                                 map.width(), map.height(), 1, GDT_UInt32, opts);
    CSLDestroy(opts);
    if (!ds)
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create " + path.toStdString());

    GDALDatasetH ref = GDALOpen(refPath.toUtf8().constData(), GA_ReadOnly);
    if (ref) {
        double gt[6];
        if (GDALGetGeoTransform(ref, gt) == CE_None)
            GDALSetGeoTransform(ds, gt);
        const char* proj = GDALGetProjectionRef(ref);
        if (proj && proj[0])
            GDALSetProjection(ds, proj);
        GDALClose(ref);
    }

    QVector<quint32> buf = map.labels();
    // GDAL UInt32 write
    if (GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Write, 0, 0, map.width(), map.height(),
                     buf.data(), map.width(), map.height(), GDT_UInt32, 0, 0) != CE_None) {
        GDALClose(ds);
        throw RSOperatorError(ErrorCode::GdalError, "Failed writing labels " + path.toStdString());
    }
    GDALClose(ds);
}

void writeParentCsv(const QString& path, const QMap<quint32, quint32>& table) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        throw RSOperatorError(ErrorCode::FileNotFound, "Cannot write " + path.toStdString());
    QTextStream out(&f);
    out << "fine_id,parent_id\n";
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
        out << it.key() << ',' << it.value() << '\n';
}

QMap<quint32, int> labelFromRoi(const RsSegmentMap& segMap,
                                const QString& rasterPath,
                                const std::string& trainingPath,
                                const std::string& classField,
                                int minLabelPixels,
                                RSOperatorContext& context) {
    GDALDatasetH vecDs = GDALOpenEx(trainingPath.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (!vecDs)
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open training vector");

    OGRLayerH layer = GDALDatasetGetLayer(vecDs, 0);
    if (!layer) {
        GDALClose(vecDs);
        throw RSOperatorError(ErrorCode::InvalidInputData, "Training has no layers");
    }

    OGRFeatureDefnH defn = OGR_L_GetLayerDefn(layer);
    int fieldIdx = OGR_FD_GetFieldIndex(defn, classField.c_str());
    if (fieldIdx < 0)
        fieldIdx = OGR_FD_GetFieldIndex(defn, "class");
    if (fieldIdx < 0)
        fieldIdx = OGR_FD_GetFieldIndex(defn, "id");
    if (fieldIdx < 0) {
        GDALClose(vecDs);
        throw RSOperatorError(ErrorCode::InvalidParameter, "classField not found: " + classField);
    }

    double gt[6] = {0, 1, 0, 0, 0, 1};
    GDALDatasetH rds = GDALOpen(rasterPath.toUtf8().constData(), GA_ReadOnly);
    if (rds) {
        GDALGetGeoTransform(rds, gt);
        GDALClose(rds);
    }

    const int w = segMap.width();
    const int h = segMap.height();
    const auto& labels = segMap.labels();
    QHash<quint32, QHash<int, int>> votes;

    OGR_L_ResetReading(layer);
    OGRFeatureH feat = nullptr;
    while ((feat = OGR_L_GetNextFeature(layer)) != nullptr) {
        context.throwIfCancelled();
        const int classId = OGR_F_GetFieldAsInteger(feat, fieldIdx);
        OGRGeometryH geom = OGR_F_GetGeometryRef(feat);
        if (classId <= 0 || !geom) {
            OGR_F_Destroy(feat);
            continue;
        }
        if (std::abs(gt[1]) < 1e-12 || std::abs(gt[5]) < 1e-12) {
            OGR_F_Destroy(feat);
            continue;
        }

        OGREnvelope env;
        OGR_G_GetEnvelope(geom, &env);
        // Normalize for north-up or south-up geotransforms.
        int cA = static_cast<int>(std::floor((env.MinX - gt[0]) / gt[1]));
        int cB = static_cast<int>(std::ceil((env.MaxX - gt[0]) / gt[1]));
        int rA = static_cast<int>(std::floor((env.MinY - gt[3]) / gt[5]));
        int rB = static_cast<int>(std::ceil((env.MaxY - gt[3]) / gt[5]));
        const int c0 = std::max(0, std::min(cA, cB));
        const int c1 = std::min(w - 1, std::max(cA, cB));
        const int r0 = std::max(0, std::min(rA, rB));
        const int r1 = std::min(h - 1, std::max(rA, rB));
        if (c0 > c1 || r0 > r1) {
            OGR_F_Destroy(feat);
            continue;
        }

        OGRGeometryH pt = OGR_G_CreateGeometry(wkbPoint);
        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                const double x = gt[0] + (c + 0.5) * gt[1] + (r + 0.5) * gt[2];
                const double y = gt[3] + (c + 0.5) * gt[4] + (r + 0.5) * gt[5];
                OGR_G_SetPoint_2D(pt, 0, x, y);
                if (!OGR_G_Contains(geom, pt) && !OGR_G_Intersects(geom, pt))
                    continue;
                const quint32 sid = labels[r * w + c];
                if (sid != 0)
                    ++votes[sid][classId];
            }
        }
        OGR_G_DestroyGeometry(pt);
        OGR_F_Destroy(feat);
    }
    GDALClose(vecDs);

    QMap<quint32, int> result;
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
        int bestClass = 0;
        int bestCount = 0;
        int total = 0;
        for (auto cit = it.value().constBegin(); cit != it.value().constEnd(); ++cit) {
            total += cit.value();
            // Majority; ties → smaller classId (deterministic, mirrors parent-link P1).
            if (cit.value() > bestCount
                || (cit.value() == bestCount && (bestClass == 0 || cit.key() < bestClass))) {
                bestCount = cit.value();
                bestClass = cit.key();
            }
        }
        if (total >= minLabelPixels && bestClass > 0)
            result[it.key()] = bestClass;
    }
    return result;
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
    writeLabelGeoTiff(QString::fromStdString(outputFine), hierarchy.level(0), rasterQ);

    if (!outputCoarse.empty() && hierarchy.levelCount() > 1)
        writeLabelGeoTiff(QString::fromStdString(outputCoarse), hierarchy.level(1), rasterQ);

    if (!outputParents.empty() && hierarchy.levelCount() > 1)
        writeParentCsv(QString::fromStdString(outputParents), hierarchy.parentTable(0));

    int labeledSegments = 0;
    if (!training.empty() && !outputClass.empty()) {
        if (classifyLevel < 0 || classifyLevel >= hierarchy.levelCount())
            throw RSOperatorError(ErrorCode::InvalidParameter, "classifyLevel out of range");

        context.reportProgress(0.65, "ROI majority labels");
        QMap<quint32, int> trainLabels = labelFromRoi(
            hierarchy.level(classifyLevel), rasterQ, training, classField, minLabelPixels, context);
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

        std::unique_ptr<RsClassifierBackend> backend;
        if (method == "normal_bayes")
            backend = std::make_unique<RsClassifierNormalBayes>();
        else
            backend = std::make_unique<RsClassifierSvm>();

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
