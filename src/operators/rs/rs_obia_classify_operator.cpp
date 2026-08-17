/***************************************************************************
 * rs_obia_classify_operator.cpp  —  Object-based classification pipeline
 *
 * ADR 0019 slice S4 — the segmentation front-end (grid superpixels via
 * segutil, or the analysis-layer RsSimpleSegmenter quantize path), per-segment
 * mean spectra, ROI majority labeling, segment-paint output) stays here: it is
 * genuinely segment-level and does not fit the pixel pipeline. Everything
 * downstream of labeling is re-based on the analysis layer exactly like the
 * supervised adapter (S3): backends are constructed via the analysis-layer
 * RsClassifierBackendFactory (one construction path, no hyperparameter
 * drift — ADR 0061), the class-field fallback comes from
 * RsTrainingDataExtraction::classFieldIndex,
 * and the class-map write delegates to RsPostProcess::saveLabelRaster, which
 * owns the dtype escalation + NoData policy (class ids come from a user vector
 * field and can exceed 255 — no silent clamp; ADR 0055).
 *
 * ADR 0060 — segmentation delegates to RsSimpleSegmenter (the single teaching
 * segmenter) and ROI labeling to RsRoiLabeler (the single ROI-majority owner);
 * the segutil quantize stack is retired.
 ***************************************************************************/
#include "rs_obia_classify_operator.h"
#include "rs_segmentation_utils.h"

#include "analysis/segmentation/rs_roi_labeler.h"
#include "analysis/segmentation/rs_segment_map.h"
#include "analysis/segmentation/rs_simple_segmenter.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include "rs_classifier_backend_factory.h"
#include "rs_post_process.h"

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

#include <opencv2/core.hpp>

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using namespace segutil;

namespace {

const std::vector<std::string> s_methods = {"svm", "normal_bayes"};

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

} // namespace

Json::Value RsObiaClassifyOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band raster");
    props["training"] = makeVectorParam("training", "Training polygons with class ids");
    props["output"] = makeOutputParam("output", "Object-based class map (Byte)", "tif");
    props["method"] = makeEnumParam("method", "Classifier", s_methods, "svm");
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

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "training", "output"});
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
    meta["purpose"] = "Object-based image analysis classification for teaching labs";
    meta["workflowHints"] = Json::Value(Json::arrayValue);
    meta["workflowHints"].append("Use rs:obia_segment first to inspect objects, then this operator");
    meta["limitations"] = "Teaching segmenter; not OTB MeanShift quality";
    return meta;
}

Json::Value RsObiaClassifyOperator::executionEstimate() const
{
    // FullRaster (default policy): input bands, the UInt32 segment label map,
    // per-segment mean-feature statistics and the Int32 class map are resident,
    // plus the segment-feature/classifier matrices (small for typical inputs).
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 67108864; // bands + labels + features + class map (64 MiB)
    return est;
}

Json::Value RsObiaClassifyOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string trainingPath = requireString(params, "training");
    const std::string outputPath = requireString(params, "output");
    const std::string method = getEnum(params, "method", s_methods, "svm");
    const std::string classField = getString(params, "classField", "class_id");
    const std::string segmentMethod =
        getEnum(params, "segmentMethod", {"grid", "quantize"}, "grid");
    const int cellSize = getInt(params, "cellSize", 16);
    const int smoothKernel = getInt(params, "smoothKernel", 3);
    const int quantizeBins = getInt(params, "quantizeBins", 64);
    const int minRegionSize = getInt(params, "minRegionSize", 20);
    const int minLabelPixels = getInt(params, "minLabelPixels", 3);

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
    if (!fileExists(trainingPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Training not found: " + trainingPath);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input");

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    const auto gtArr = ds.geoTransform();
    double gt[6] = {gtArr[0], gtArr[1], gtArr[2], gtArr[3], gtArr[4], gtArr[5]};
    const std::vector<int> bands = parseBands(params, bandCount);
    const int nFeat = static_cast<int>(bands.size());
    const size_t nPix = static_cast<size_t>(width) * height;

    context.reportProgress(0.05, "Reading bands");
    std::vector<std::vector<float>> bandData(static_cast<size_t>(nFeat));
    for (int i = 0; i < nFeat; ++i) {
        bandData[static_cast<size_t>(i)].resize(nPix);
        if (!ds.readBandData(bands[static_cast<size_t>(i)],
                             bandData[static_cast<size_t>(i)].data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bands[static_cast<size_t>(i)]));
        }
        context.throwIfCancelled();
    }

    // --- Segment: analysis RsSimpleSegmenter (quantize) or grid fallback ---
    // ADR 0060: the segutil quantize stack is retired; the grid superpixel
    // path stays as the classify-specific fallback (cell labels 1..N).
    RsSegmentMap segMap;
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
            [&context](float f) { context.reportProgress(0.15 + 0.15 * f, "Segmenting"); });
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

    // Segment ids are 1-based with 0 = nodata; after merges they may carry
    // gaps, so per-segment arrays are indexed by the max label id.
    const auto &segLabels = segMap.labels();
    quint32 maxLabel = 0;
    for (quint32 sid : segLabels)
        maxLabel = (std::max)(maxLabel, sid);
    const int nSeg = static_cast<int>(maxLabel);

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
    std::vector<std::vector<int>> validCounts(static_cast<size_t>(nSeg + 1),
                                              std::vector<int>(static_cast<size_t>(nFeat), 0));
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

    std::vector<std::vector<float>> feats(static_cast<size_t>(nSeg + 1),
                                          std::vector<float>(static_cast<size_t>(nFeat), 0.0f));
    for (int s = 1; s <= nSeg; ++s) {
        for (int f = 0; f < nFeat; ++f) {
            const int cnt = validCounts[static_cast<size_t>(s)][static_cast<size_t>(f)];
            if (cnt > 0) {
                feats[static_cast<size_t>(s)][static_cast<size_t>(f)] =
                    static_cast<float>(sum[static_cast<size_t>(s)][static_cast<size_t>(f)] / cnt);
            }
        }
    }

    // --- Label segments by ROI majority (analysis canonical, ADR 0060) ---
    context.reportProgress(0.5, "Labeling segments from training polygons");
    QString roiErr;
    const QMap<quint32, int> segLabelMap = RsRoiLabeler::labelByMajority(
        segMap, QString::fromStdString(inputPath), QString::fromStdString(trainingPath),
        QString::fromStdString(classField), minLabelPixels, &roiErr,
        [&context]() { return context.isCancelled(); });
    context.throwIfCancelled();
    if (segLabelMap.isEmpty() && !roiErr.isEmpty())
        throw RSOperatorError(ErrorCode::GdalError, roiErr.toStdString());

    std::vector<int> segLabel(static_cast<size_t>(nSeg + 1), 0);
    int labeledSegments = 0;
    for (auto it = segLabelMap.constBegin(); it != segLabelMap.constEnd(); ++it) {
        if (it.key() > static_cast<quint32>(nSeg))
            continue;
        segLabel[static_cast<size_t>(it.key())] = it.value();
        ++labeledSegments;
    }

    if (labeledSegments < 2)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Fewer than 2 segments labeled by training polygons "
                              "(check CRS overlap / minLabelPixels)");

    // Build training matrix
    cv::Mat trainX(labeledSegments, nFeat, CV_32F);
    cv::Mat trainY(labeledSegments, 1, CV_32S);
    int row = 0;
    for (int s = 1; s <= nSeg; ++s) {
        if (segLabel[static_cast<size_t>(s)] <= 0)
            continue;
        for (int f = 0; f < nFeat; ++f)
            trainX.at<float>(row, f) = feats[static_cast<size_t>(s)][static_cast<size_t>(f)];
        trainY.at<int>(row, 0) = segLabel[static_cast<size_t>(s)];
        ++row;
    }

    context.reportProgress(0.65, "Training " + method + " on " +
                                     std::to_string(labeledSegments) + " labeled objects");
    std::unique_ptr<RsClassifierBackend> backend =
        RsClassifierBackendFactory::create(QString::fromStdString(method));
    if (!backend->fit(trainX, trainY)) {
        throw RSOperatorError(ErrorCode::OpenCvError,
                              method == "normal_bayes" ? "NormalBayes training failed"
                                                       : "SVM training failed");
    }

    // Predict all segments
    context.reportProgress(0.75, "Predicting all segments");
    cv::Mat allX(nSeg, nFeat, CV_32F);
    for (int s = 1; s <= nSeg; ++s) {
        for (int f = 0; f < nFeat; ++f)
            allX.at<float>(s - 1, f) = feats[static_cast<size_t>(s)][static_cast<size_t>(f)];
    }
    const cv::Mat pred = backend->predict(allX);
    if (pred.empty() || pred.rows < nSeg) {
        throw RSOperatorError(ErrorCode::OpenCvError, "predict failed");
    }

    std::vector<int32_t> classOfSeg(static_cast<size_t>(nSeg + 1), 0);
    for (int s = 1; s <= nSeg; ++s) {
        // Backend predictions are integral class ids already; negative is
        // defensive only (SVM/Bayes predict within the trained label set).
        classOfSeg[static_cast<size_t>(s)] = (std::max)(0, pred.at<int>(s - 1, 0));
    }

    // Paint class map. RsPostProcess::saveLabelRaster owns the dtype policy
    // (ADR 0019 S4: Byte <= 255, UInt16 <= 65535, Int32 beyond — never a
    // silent clamp) plus the NoData marker; segutil::writeClassGeoTiff was
    // deleted in ADR 0060 (no callers since ADR 0055).
    context.reportProgress(0.9, "Writing class map");
    std::vector<int32_t> classMap(nPix, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pix = static_cast<size_t>(y) * width + x;
            const quint32 sid = segLabels[pix];
            if (sid > 0)
                classMap[pix] = classOfSeg[static_cast<size_t>(sid)];
        }
    }
    // Zero-copy CV_32S header over the flat class map (continuous buffer).
    const cv::Mat classMapMat(height, width, CV_32S, classMap.data());
    QString err;
    if (!RsPostProcess::saveLabelRaster(
            QString::fromStdString(outputPath), classMapMat, gt,
            ds.projection(), QVector<QRgb>(),
            QStringList{QStringLiteral("COMPRESS=LZW")},
            0.0 /* NoData: unclassified */, &err))
        throw RSOperatorError(ErrorCode::GdalError, err.toStdString());

    context.reportProgress(1.0, "OBIA classification complete");

    // Count unique predicted classes
    std::map<int, int> classHist;
    for (int s = 1; s <= nSeg; ++s) {
        if (classOfSeg[static_cast<size_t>(s)] > 0)
            classHist[classOfSeg[static_cast<size_t>(s)]]++;
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    // True object count (merge can leave id gaps, so maxLabel may exceed it;
    // the old segutil remapped ids contiguously — same value in grid mode).
    result["segments"] = segMap.segmentCount();
    result["labeledSegments"] = labeledSegments;
    result["trainSamples"] = labeledSegments;
    result["classes"] = static_cast<int>(classHist.size());
    result["features"] = nFeat;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::rs
