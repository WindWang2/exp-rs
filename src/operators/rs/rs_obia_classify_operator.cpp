/***************************************************************************
 * rs_obia_classify_operator.cpp  —  Object-based classification pipeline
 *
 * ADR 0019 slice S4 + ADR 0060/0061 — segmentation delegates to
 * RsSimpleSegmenter (single teaching segmenter), ROI labeling to RsRoiLabeler
 * (single ROI-majority owner), backend construction to
 * RsClassifierBackendFactory (one construction path).
 *
 * Issue #663 — the classification core is unified on RsObjectClassify (the
 * same kernel the OBIA GUI used in-app): Z-score scaling, train on labeled
 * rows, predict all rows, entropy uncertainties. New surface area: a
 * `labels` raster input (skip segmentation), a `segmentClasses` interactive
 * training source, the full RsSegmentFeatures feature model with a
 * selection mask, classifier hyperparameters, `classColors` palette output
 * and an optional per-segment entropy CSV. The class-map write delegates to
 * RsClassRaster::paint (palette + dtype escalation + NoData=0 + atomic
 * rename — ADR 0054/0055), replacing the bare saveLabelRaster call so GUI
 * and operator outputs are byte-compatible.
 ***************************************************************************/
#include "rs_obia_classify_operator.h"
#include "rs_obia_common.h"
#include "rs_segmentation_utils.h"

#include "analysis/classification/rs_accuracy_assessment.h"
#include "analysis/classification/rs_classifier_backend_factory.h"
#include "analysis/segmentation/rs_class_raster.h"
#include "analysis/segmentation/rs_object_classify.h"
#include "analysis/segmentation/rs_roi_labeler.h"
#include "analysis/segmentation/rs_segment_features.h"
#include "analysis/segmentation/rs_segment_map.h"
#include "analysis/segmentation/rs_simple_segmenter.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QColor>
#include <QFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <opencv2/core.hpp>

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using namespace segutil;
using obia::objectParam;
using obia::hasObjectParam;
using obia::intKey;
using obia::parseClassColors;
using obia::accuracyToJson;
using obia::trainingAccuracy;
using obia::writeUncertaintyCsv;

namespace {

const std::vector<std::string> s_methods = {"svm", "normal_bayes", "random_forest", "kmeans", "mlp"};
const std::vector<std::string> s_featureModels = {"mean", "full"};

/// segutil::segmentGrid (CV_32S labels 1..N) → analysis RsSegmentMap. The
/// grid superpixel fallback is the classify-specific complement to the
/// quantize path (ADR 0060); grid cells are always contiguous 1..N.
RsSegmentMap gridSegmentMap(int width, int height, int cellSize,
                            RSOperatorContext& context) {
    const cv::Mat grid = segmentGrid(width, height, cellSize, context);
    QVector<quint32> labels(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const int* row = grid.ptr<int>(y);
        for (int x = 0; x < width; ++x)
            labels[static_cast<size_t>(y) * width + x] = static_cast<quint32>(row[x]);
    }
    return RsSegmentMap(std::move(labels), width, height);
}

RsFeatureSelection parseFeatureSelection(const Json::Value& selection) {
    RsFeatureSelection sel; // struct defaults: every family enabled
    sel.useMean = getBool(selection, "mean", sel.useMean);
    sel.useStdDev = getBool(selection, "stddev", sel.useStdDev);
    sel.useMin = getBool(selection, "min", sel.useMin);
    sel.useMax = getBool(selection, "max", sel.useMax);
    sel.useGlcmContrast = getBool(selection, "glcmContrast", sel.useGlcmContrast);
    sel.useGlcmCorrelation = getBool(selection, "glcmCorrelation", sel.useGlcmCorrelation);
    sel.useGlcmEnergy = getBool(selection, "glcmEnergy", sel.useGlcmEnergy);
    sel.useGlcmHomogeneity = getBool(selection, "glcmHomogeneity", sel.useGlcmHomogeneity);
    sel.useArea = getBool(selection, "area", sel.useArea);
    sel.usePerimeter = getBool(selection, "perimeter", sel.usePerimeter);
    sel.useShapeIndex = getBool(selection, "shapeIndex", sel.useShapeIndex);
    sel.useCompactness = getBool(selection, "compactness", sel.useCompactness);
    sel.useRectangularity = getBool(selection, "rectangularity", sel.useRectangularity);
    sel.useAspectRatio = getBool(selection, "aspectRatio", sel.useAspectRatio);
    return sel;
}

} // namespace

Json::Value RsObiaClassifyOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band raster");
    props["training"] = makeVectorParam("training", "Training polygons with class ids");
    props["training"]["required"] = false;
    props["segmentClasses"] = makeStringParam(
        "segmentClasses",
        "Interactive training: JSON {segmentId: classId} over the `labels` raster "
        "(exactly one of training / segmentClasses)",
        "");
    props["segmentClasses"]["required"] = false;
    props["labels"] = makeRasterParam(
        "labels",
        "Existing segment label raster (UInt32) — skip internal segmentation; "
        "required with segmentClasses (e.g. rs:obia_segment output)");
    props["labels"]["required"] = false;
    props["output"] = makeOutputParam("output", "Object-based class map (Byte/UInt16)", "tif");
    props["method"] = makeEnumParam("method", "Classifier", s_methods, "svm");
    props["scale"] = makeBooleanParam(
        "scale",
        "Fit a Z-score feature scaler on the training segments and transform prediction "
        "features — the canonical object-classification path standardizes, and the default "
        "SVM gamma is tuned for z-scored inputs. Disable only to classify on raw features.",
        true);
    props["classField"] = makeStringParam("classField", "Integer class field", "class_id");
    props["segmentMethod"] = makeEnumParam("segmentMethod",
                                           "grid (default, reliable) or quantize (CC-based)",
                                           {"grid", "quantize"}, "grid");
    props["cellSize"] = makeIntegerParam("cellSize", "Grid superpixel cell size (grid mode)", 16);
    props["smoothKernel"] = makeIntegerParam("smoothKernel", "Gaussian kernel (quantize mode)", 3);
    props["quantizeBins"] = makeIntegerParam("quantizeBins", "Quantization levels (quantize mode)", 64);
    props["minRegionSize"] = makeIntegerParam("minRegionSize", "Min region size (quantize mode)", 20);
    props["minLabelPixels"] = makeIntegerParam(
        "minLabelPixels", "Min training pixels to label a segment", 3);
    props["features"] = makeEnumParam(
        "features", "Feature model: mean (per-band means, default) or full (spectral+GLCM+shape)",
        s_featureModels, "mean");
    props["featureSelection"] = makeStringParam(
        "featureSelection",
        "Enabled feature families, JSON {family: bool} (features=full only; default all true)",
        "");
    props["featureSelection"]["required"] = false;

    // Classifier hyperparameters (defaults = RsClassifierBackendParams, the
    // single source of truth shared with the factory and the GUI Params dialog).
    props["rfNumTrees"] = makeIntegerParam("rfNumTrees", "RandomForest tree count", 100);
    props["rfMaxDepth"] = makeIntegerParam("rfMaxDepth", "RandomForest max tree depth", 10);
    props["rfMinSampleCount"] = makeIntegerParam("rfMinSampleCount", "RandomForest min samples per node", 5);
    props["mlpHiddenLayerSize"] = makeIntegerParam("mlpHiddenLayerSize", "MLP hidden-layer neurons", 16);
    props["mlpMaxIter"] = makeIntegerParam("mlpMaxIter", "MLP max iterations", 500);

    props["classColors"] = makeStringParam(
        "classColors", "Optional palette JSON {classId: \"#rrggbb\"} embedded in the output", "");
    props["classColors"]["required"] = false;

    Json::Value outputUncertainty = makeOutputParam(
        "outputUncertainty", "Optional per-segment CSV (segment_id, entropy, class_id)", "csv");
    outputUncertainty["required"] = false;
    props["outputUncertainty"] = outputUncertainty;

    Json::Value bands = makeStringParam("bands", "Optional 1-based band indices", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Class map", "tif");
    outputs["segments"] = makeIntegerParam("segments", "Segment count", 0);
    outputs["labeledSegments"] = makeIntegerParam("labeledSegments", "Segments with training", 0);
    outputs["classes"] = makeIntegerParam("classes", "Distinct predicted classes", 0);
    outputs["accuracy"] = makeStringParam("accuracy", "Training-set accuracy (OA/kappa/confusion)", "");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsObiaClassifyOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("obia");
    meta["tags"].append("object-based");
    meta["tags"].append("classification");
    meta["purpose"] = "Object-based classification: polygon training or interactive segment labels";
    Json::Value useCases(Json::arrayValue);
    useCases.append("End-to-end teaching lab (segment internally from training polygons)");
    useCases.append("Interactive OBIA session (labels + segmentClasses, features=full)");
    useCases.append("Agent chain: rs:obia_segment → rs:obia_label → rs:obia_classify");
    meta["useCases"] = useCases;
    Json::Value hints(Json::arrayValue);
    hints.append("Segment first with rs:obia_segment and pass its output as `labels` to skip re-segmentation");
    hints.append("features=full + featureSelection mirrors the GUI feature tree (GLCM/shape included)");
    meta["workflowHints"] = hints;
    meta["limitations"] =
        "Internal segmentation (no `labels`) is the teaching stack (grid/quantize), not OTB MeanShift — "
        "pass rs:obia_segment(engine=otb|auto) output as `labels` for OTB-quality objects";
    return meta;
}

Json::Value RsObiaClassifyOperator::executionEstimate() const
{
    // FullRaster (default policy): input bands, the UInt32 segment label map,
    // per-segment feature statistics and the class map are resident, plus the
    // segment-feature/classifier matrices (small for typical inputs).
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 67108864; // bands + labels + features + class map (64 MiB)
    return est;
}

Json::Value RsObiaClassifyOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string trainingPath = getString(params, "training", "");
    const std::string labelsPath = getString(params, "labels", "");
    const std::string method = getEnum(params, "method", s_methods, "svm");
    const std::string classField = getString(params, "classField", "class_id");
    const std::string segmentMethod =
        getEnum(params, "segmentMethod", {"grid", "quantize"}, "grid");
    const int cellSize = getInt(params, "cellSize", 16);
    const int smoothKernel = getInt(params, "smoothKernel", 3);
    const int quantizeBins = getInt(params, "quantizeBins", 64);
    const int minRegionSize = getInt(params, "minRegionSize", 20);
    const int minLabelPixels = getInt(params, "minLabelPixels", 3);
    const std::string featureModel = getEnum(params, "features", s_featureModels, "mean");
    const std::string outputUncertainty = getString(params, "outputUncertainty", "");
    const bool scale = getBool(params, "scale", true);

    // Hyperparameters default to the factory struct (single source of truth).
    const RsClassifierBackendParams hyperparams = obia::classifierHyperParams(params);

    // --- Training source: exactly one of training (polygons) / segmentClasses.
    const bool hasTraining = !trainingPath.empty();
    const bool hasSegmentClasses = hasObjectParam(params, "segmentClasses");
    if (hasTraining == hasSegmentClasses)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Provide exactly one training source: `training` (polygons) "
                              "or `segmentClasses` (interactive labels)");
    if (hasSegmentClasses && labelsPath.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "`segmentClasses` requires `labels` (the label raster the ids refer to)");
    if (hasObjectParam(params, "featureSelection") && featureModel != "full")
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "`featureSelection` applies to features=full only");

    if (cellSize <= 0)
        throw RSOperatorError(ErrorCode::InvalidParameter, "cellSize must be > 0");
    if (smoothKernel <= 0 || smoothKernel % 2 == 0)
        throw RSOperatorError(ErrorCode::InvalidParameter, "smoothKernel must be an odd positive integer");
    if (quantizeBins <= 0 || quantizeBins > 256)
        throw RSOperatorError(ErrorCode::InvalidParameter, "quantizeBins must be between 1 and 256");
    if (minRegionSize < 0)
        throw RSOperatorError(ErrorCode::InvalidParameter, "minRegionSize must be >= 0");
    if (minLabelPixels <= 0)
        throw RSOperatorError(ErrorCode::InvalidParameter, "minLabelPixels must be > 0");

    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    if (hasTraining && !fileExists(trainingPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Training not found: " + trainingPath);
    if (!labelsPath.empty() && !fileExists(labelsPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Labels not found: " + labelsPath);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input");

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    const std::vector<int> bands = parseBands(params, bandCount);
    const int nFeat = static_cast<int>(bands.size());
    const size_t nPix = static_cast<size_t>(width) * height;

    // Band reads are shared between the quantize segmenter and the mean
    // feature model — read once, only when one of them needs the pixels.
    const bool needsBandData = featureModel == "mean"
                               || (labelsPath.empty() && segmentMethod == "quantize");
    std::vector<std::vector<float>> bandData;
    if (needsBandData) {
        context.reportProgress(0.05, "Reading bands");
        bandData.resize(static_cast<size_t>(nFeat));
        for (int i = 0; i < nFeat; ++i) {
            bandData[static_cast<size_t>(i)].resize(nPix);
            if (!ds.readBandData(bands[static_cast<size_t>(i)],
                                 bandData[static_cast<size_t>(i)].data(), width, height)) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read band " + std::to_string(bands[static_cast<size_t>(i)]));
            }
            context.throwIfCancelled();
        }
    }

    // --- Segment geometry: provided label raster or internal segmentation ---
    RsSegmentMap segMap;
    if (!labelsPath.empty()) {
        context.reportProgress(0.1, "Loading segment labels");
        segMap = RsSegmentMap::fromGeoTIFF(QString::fromStdString(labelsPath));
        if (segMap.isEmpty())
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Labels raster is empty or unreadable as a UInt32 label map");
        if (segMap.width() != width || segMap.height() != height)
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Labels grid (" + std::to_string(segMap.width()) + "x" +
                                      std::to_string(segMap.height()) +
                                  ") does not match the source raster (" +
                                  std::to_string(width) + "x" + std::to_string(height) + ")");
        if (segMap.segmentCount() < 1)
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Labels raster contains no segments (all pixels are 0)");
    } else {
        // --- Segment: analysis RsSimpleSegmenter (quantize) or grid fallback ---
        // ADR 0060: the segutil quantize stack is retired; the grid superpixel
        // path stays as the classify-specific fallback (cell labels 1..N).
        if (segmentMethod == "quantize") {
            bool hasNodata = false;
            const double nodataValue = ds.bandNoDataValue(1, &hasNodata);
            const float nodata = hasNodata ? static_cast<float>(nodataValue)
                                           : std::numeric_limits<float>::quiet_NaN();
            std::vector<const float*> bandPtrs(static_cast<size_t>(nFeat));
            for (int i = 0; i < nFeat; ++i)
                bandPtrs[static_cast<size_t>(i)] = bandData[static_cast<size_t>(i)].data();
            RsSimpleSegmenter::Params segParams;
            segParams.smoothKernel = smoothKernel;
            segParams.quantizeBins = quantizeBins;
            segParams.minRegionSize = minRegionSize;
            segMap = RsSimpleSegmenter::segmentMultiBand(
                bandPtrs.data(), nFeat, width, height, nodata, segParams,
                [&context]() { return context.isCancelled(); },
                [&context](float f) { context.reportProgress(0.1 + 0.15 * f, "Segmenting"); });
            context.throwIfCancelled();
            if (segMap.segmentCount() < 8) {
                context.logWarning("Quantize segmentation yielded few objects; falling back to grid");
                segMap = gridSegmentMap(width, height, cellSize, context);
            }
        } else {
            segMap = gridSegmentMap(width, height, cellSize, context);
        }
        if (segMap.isEmpty())
            throw RSOperatorError(ErrorCode::ComputationError, "Segmentation produced an empty label map");
        if (segMap.segmentCount() < 2)
            throw RSOperatorError(ErrorCode::ComputationError, "Segmentation produced too few objects");
    }

    // Segment ids are 1-based with 0 = nodata; after merges they may carry
    // gaps, so per-segment arrays are indexed by the max label id.
    const auto &segLabels = segMap.labels();
    quint32 maxLabel = 0;
    for (quint32 sid : segLabels)
        maxLabel = (std::max)(maxLabel, sid);
    const int nSeg = static_cast<int>(maxLabel);
    QSet<quint32> knownSegIds;
    for (quint32 sid : segLabels)
        if (sid != 0)
            knownSegIds.insert(sid);

    // --- Feature matrix ---
    cv::Mat allFeatures;
    QVector<quint32> segmentIds;
    // Mean model: segments whose every pixel in a band was NoData have no
    // information in that band — they are excluded from training AND
    // prediction (painted unclassified, #682). Full model: the GUI-parity
    // RsSegmentFeatures path (valid-pixel statistics, no exclusions).
    if (featureModel == "full") {
        const RsFeatureSelection selection = parseFeatureSelection(objectParam(params, "featureSelection"));
        if (selection.activeFeatureCount(nFeat) <= 0)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "featureSelection must enable at least one feature family");
        context.reportProgress(0.35, "Extracting full segment features");
        QVector<int> bandIndices;
        bandIndices.reserve(nFeat);
        for (int b : bands)
            bandIndices.append(b);
        const QMap<quint32, RsSegmentFeatures::SegmentStat> stats = RsSegmentFeatures::extract(
            QString::fromStdString(inputPath), segMap, bandIndices,
            [&context]() { return context.isCancelled(); },
            [&context](float f) { context.reportProgress(0.35 + 0.25 * f, "Extracting features"); });
        context.throwIfCancelled();
        if (stats.isEmpty())
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "Feature extraction produced no segment statistics");
        allFeatures = RsSegmentFeatures::toFeatureMatrix(stats, segmentIds, selection);
    } else {
        context.reportProgress(0.35, "Extracting segment mean features (" + std::to_string(nSeg) + ")");

        // Per-segment mean features + pixel counts (valid pixels only per band)
        std::vector<bool> bandHasNodata(static_cast<size_t>(nFeat), false);
        std::vector<float> bandNodataVal(static_cast<size_t>(nFeat), 0.0f);
        for (int f = 0; f < nFeat; ++f) {
            bool hasNd = false;
            double ndVal = ds.bandNoDataValue(bands[static_cast<size_t>(f)], &hasNd);
            bandHasNodata[static_cast<size_t>(f)] = hasNd;
            bandNodataVal[static_cast<size_t>(f)] = hasNd ? static_cast<float>(ndVal) : 0.0f;
        }

        std::vector<std::vector<double>> sum(static_cast<size_t>(nSeg + 1),
                                             std::vector<double>(static_cast<size_t>(nFeat), 0.0));
        std::vector<std::vector<int64_t>> validCounts(static_cast<size_t>(nSeg + 1),
                                                      std::vector<int64_t>(static_cast<size_t>(nFeat), 0));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t pix = static_cast<size_t>(y) * width + x;
                const quint32 sid = segLabels[pix];
                if (sid == 0)
                    continue;
                for (int f = 0; f < nFeat; ++f) {
                    const float val = bandData[static_cast<size_t>(f)][pix];
                    if (!std::isfinite(val))
                        continue;
                    if (bandHasNodata[static_cast<size_t>(f)] && val == bandNodataVal[static_cast<size_t>(f)])
                        continue;
                    sum[static_cast<size_t>(sid)][static_cast<size_t>(f)] += val;
                    validCounts[static_cast<size_t>(sid)][static_cast<size_t>(f)]++;
                }
            }
        }

        // Reuse the canonical matrix builder with a mean-only selection: the
        // map only carries segments that survive the #682 completeness rule.
        RsFeatureSelection meanOnly;
        meanOnly.useMean = true;
        meanOnly.useStdDev = meanOnly.useMin = meanOnly.useMax = false;
        meanOnly.useGlcmContrast = meanOnly.useGlcmCorrelation = false;
        meanOnly.useGlcmEnergy = meanOnly.useGlcmHomogeneity = false;
        meanOnly.useArea = meanOnly.usePerimeter = meanOnly.useShapeIndex = false;
        meanOnly.useCompactness = meanOnly.useRectangularity = meanOnly.useAspectRatio = false;
        QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
        for (int s = 1; s <= nSeg; ++s) {
            bool complete = true;
            int64_t totalValid = 0;
            RsSegmentFeatures::SegmentStat stat;
            stat.mean.resize(nFeat);
            for (int f = 0; f < nFeat; ++f) {
                const int64_t cnt = validCounts[static_cast<size_t>(s)][static_cast<size_t>(f)];
                totalValid += cnt;
                if (cnt > 0) {
                    stat.mean[f] = static_cast<double>(
                        sum[static_cast<size_t>(s)][static_cast<size_t>(f)] / cnt);
                } else {
                    complete = false; // at least one band had no valid sample in this segment
                }
            }
            if (!complete || totalValid <= 0)
                continue;
            stats.insert(static_cast<quint32>(s), stat);
        }
        allFeatures = RsSegmentFeatures::toFeatureMatrix(stats, segmentIds, meanOnly);
    }

    if (allFeatures.empty() || segmentIds.isEmpty())
        throw RSOperatorError(ErrorCode::ComputationError,
                              "No classifiable segments with a complete spectrum");

    // --- Training labels ---
    context.reportProgress(0.6, hasSegmentClasses ? "Loading interactive labels"
                                                  : "Labeling segments from training polygons");
    QMap<quint32, int> trainLabels;
    if (hasSegmentClasses) {
        const Json::Value segmentClasses = objectParam(params, "segmentClasses");
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
                                  " segment ids not present in the labels raster");
    } else {
        QString roiErr;
        const QMap<quint32, int> segLabelMap = RsRoiLabeler::labelByMajority(
            segMap, QString::fromStdString(inputPath), QString::fromStdString(trainingPath),
            QString::fromStdString(classField), minLabelPixels, &roiErr,
            [&context]() { return context.isCancelled(); });
        context.throwIfCancelled();
        if (segLabelMap.isEmpty() && !roiErr.isEmpty())
            throw RSOperatorError(ErrorCode::GdalError, roiErr.toStdString());
        // Mean-model completeness: labeled segments missing from the feature
        // matrix (deficient spectrum) are dropped from training — the rows
        // must match the prediction set (#682).
        QSet<quint32> featured;
        featured.reserve(segmentIds.size());
        for (quint32 sid : segmentIds)
            featured.insert(sid);
        for (auto it = segLabelMap.constBegin(); it != segLabelMap.constEnd(); ++it) {
            if (it.key() == 0 || it.value() <= 0)
                continue;
            if (featured.contains(it.key()))
                trainLabels.insert(it.key(), it.value());
        }
    }

    if (trainLabels.size() < 2)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Fewer than 2 segments labeled by training "
                              "(check CRS overlap / minLabelPixels / segment ids)");

    std::set<int> uniqueClasses;
    for (auto it = trainLabels.constBegin(); it != trainLabels.constEnd(); ++it)
        uniqueClasses.insert(it.value());
    if (uniqueClasses.size() < 2)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Training must contain at least 2 distinct classes (found " +
                                  std::to_string(uniqueClasses.size()) + ")");

    // --- Train + predict via the canonical object-classification kernel ---
    context.reportProgress(0.7, "Training " + method + " on " +
                                    std::to_string(trainLabels.size()) + " labeled objects");
    std::unique_ptr<RsClassifierBackend> backend =
        RsClassifierBackendFactory::create(QString::fromStdString(method), hyperparams);
    if (!backend)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Failed to create classifier backend: " + method);

    const RsObjectClassifyResult cls = RsObjectClassify::classify(
        allFeatures, segmentIds, trainLabels, *backend, /*enableScaling=*/scale);
    if (!cls.ok)
        throw RSOperatorError(ErrorCode::OpenCvError, cls.errorMessage.toStdString());
    context.throwIfCancelled();
    const QMap<quint32, int> segmentClasses = cls.segmentClasses;

    // Training-set accuracy (true labels vs predicted class of labeled segments).
    const RsAccuracyAssessment::Result accuracy = trainingAccuracy(trainLabels, segmentClasses);
    const bool hasAccuracy = !accuracy.classIds.isEmpty();

    // --- Optional entropy sidecar (active-learning workflow) ---
    if (!outputUncertainty.empty()) {
        context.reportProgress(0.85, "Writing uncertainty CSV");
        writeUncertaintyCsv(outputUncertainty, cls.segmentUncertainties, segmentClasses);
    }

    // --- Paint class map. RsClassRaster::paint owns the dtype policy
    // (Byte <= 255, UInt16 <= 65535, > 65535 errors — no silent clamp), the
    // optional class palette, NoData=0 and the atomic .tmp~ rename
    // (ADR 0054/0055). Segments absent from segmentClasses paint as 0.
    context.reportProgress(0.9, "Writing class map");
    const QHash<int, QColor> classColors =
        hasObjectParam(params, "classColors")
            ? parseClassColors(objectParam(params, "classColors"))
            : QHash<int, QColor>();
    const RsClassRasterResult paint = RsClassRaster::paint(
        segMap, segmentClasses, QString::fromStdString(inputPath),
        QString::fromStdString(outputPath), classColors);
    if (!paint.ok)
        throw RSOperatorError(ErrorCode::GdalError, paint.errorMessage.toStdString());

    context.reportProgress(1.0, "OBIA classification complete");

    // Count unique predicted classes
    std::map<int, int> classHist;
    for (auto it = segmentClasses.constBegin(); it != segmentClasses.constEnd(); ++it) {
        if (it.value() > 0)
            classHist[it.value()]++;
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    // True object count (merge can leave id gaps, so maxLabel may exceed it;
    // the old segutil remapped ids contiguously — same value in grid mode).
    result["segments"] = segMap.segmentCount();
    result["labeledSegments"] = static_cast<int>(trainLabels.size());
    result["trainSamples"] = static_cast<int>(trainLabels.size());
    result["classes"] = static_cast<int>(classHist.size());
    result["features"] = allFeatures.cols;
    result["width"] = width;
    result["height"] = height;
    if (!labelsPath.empty())
        result["labels"] = labelsPath;
    if (hasAccuracy) {
        result["accuracy"] = accuracyToJson(accuracy);
        result["labeledAccuracy"] = accuracy.overallAccuracy;
    }
    if (!outputUncertainty.empty())
        result["uncertaintyOutput"] = outputUncertainty;
    return result;
}

} // namespace sicnu::operators::rs
