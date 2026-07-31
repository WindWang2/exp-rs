/***************************************************************************
 * rs_obia_classify_operator.cpp  —  Object-based classification pipeline
 *
 * ADR 0019 slice S4 — the segmentation front-end (grid/quantize via
 * segutil, per-segment mean spectra, ROI majority labeling, segment-paint
 * output) stays here: it is genuinely segment-level and does not fit the
 * pixel pipeline. Everything downstream of labeling is re-based on the
 * analysis layer exactly like the supervised adapter (S3): backends are
 * constructed via RsClassifierSvm / RsClassifierNormalBayes (one
 * construction path, no hyperparameter drift), the class-field fallback
 * comes from RsTrainingDataExtraction::classFieldIndex, and the class-map
 * write follows the pipeline's dtype escalation + NoData policy (class ids
 * come from a user vector field and can exceed 255 — no silent clamp).
 ***************************************************************************/
#include "rs_obia_classify_operator.h"
#include "rs_segmentation_utils.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"
#include "rs_training_data_extraction.h"

#include <QString>

#include <gdal.h>
#include <ogr_api.h>

#include <opencv2/core.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using namespace segutil;

namespace {

const std::vector<std::string> s_methods = {"svm", "normal_bayes"};

/// Single backend construction path shared with the GUI and the supervised
/// adapter (ADR 0019 S3/S4 — hyperparameters live in the analysis-layer
/// classes only).
std::unique_ptr<RsClassifierBackend> makeBackend(const std::string& method) {
    if (method == "normal_bayes")
        return std::make_unique<RsClassifierNormalBayes>();
    return std::make_unique<RsClassifierSvm>();
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

    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    if (!fileExists(trainingPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Training not found: " + trainingPath);

    ensureGdalInit();
    GDALAllRegister();
    OGRRegisterAll();

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

    // --- Segment (shared segutil) ---
    cv::Mat labels;
    if (segmentMethod == "quantize") {
        labels = segmentQuantize(bandData, width, height, smoothKernel, quantizeBins,
                                 minRegionSize, context);
        int maxCheck = 0;
        for (int y = 0; y < height; ++y) {
            const int* row = labels.ptr<int>(y);
            for (int x = 0; x < width; ++x)
                maxCheck = std::max(maxCheck, row[x]);
        }
        if (maxCheck < 8) {
            context.logWarning("Quantize segmentation yielded few objects; falling back to grid");
            labels = segmentGrid(width, height, cellSize, context);
        }
    } else {
        labels = segmentGrid(width, height, cellSize, context);
    }
    int maxSeg = 0;
    for (int y = 0; y < height; ++y) {
        const int* row = labels.ptr<int>(y);
        for (int x = 0; x < width; ++x)
            maxSeg = std::max(maxSeg, row[x]);
    }
    const int nSeg = maxSeg;
    if (nSeg < 2)
        throw RSOperatorError(ErrorCode::ComputationError, "Segmentation produced too few objects");

    context.reportProgress(0.35, "Extracting segment mean features (" + std::to_string(nSeg) + ")");

    // Per-segment mean features + pixel counts
    std::vector<std::vector<double>> sum(static_cast<size_t>(nSeg + 1),
                                         std::vector<double>(static_cast<size_t>(nFeat), 0.0));
    std::vector<int> counts(static_cast<size_t>(nSeg + 1), 0);
    for (int y = 0; y < height; ++y) {
        const int* row = labels.ptr<int>(y);
        for (int x = 0; x < width; ++x) {
            const int sid = row[x];
            if (sid <= 0)
                continue;
            const size_t pix = static_cast<size_t>(y) * width + x;
            for (int f = 0; f < nFeat; ++f)
                sum[static_cast<size_t>(sid)][static_cast<size_t>(f)] +=
                    bandData[static_cast<size_t>(f)][pix];
            counts[static_cast<size_t>(sid)]++;
        }
    }

    std::vector<std::vector<float>> feats(static_cast<size_t>(nSeg + 1),
                                          std::vector<float>(static_cast<size_t>(nFeat), 0.0f));
    for (int s = 1; s <= nSeg; ++s) {
        if (counts[static_cast<size_t>(s)] <= 0)
            continue;
        const double inv = 1.0 / counts[static_cast<size_t>(s)];
        for (int f = 0; f < nFeat; ++f)
            feats[static_cast<size_t>(s)][static_cast<size_t>(f)] =
                static_cast<float>(sum[static_cast<size_t>(s)][static_cast<size_t>(f)] * inv);
    }

    // --- Label segments by ROI majority ---
    context.reportProgress(0.5, "Labeling segments from training polygons");
    GDALDatasetH vecDs = GDALOpenEx(trainingPath.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (!vecDs)
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open training vector");

    OGRLayerH layer = GDALDatasetGetLayer(vecDs, 0);
    if (!layer) {
        GDALClose(vecDs);
        throw RSOperatorError(ErrorCode::InvalidInputData, "Training has no layers");
    }
    OGRFeatureDefnH defn = OGR_L_GetLayerDefn(layer);
    // Shared fallback chain (classField → "class" → "id") from the analysis
    // layer — same resolution the pixel-level extraction module applies.
    const int fieldIdx = RsTrainingDataExtraction::classFieldIndex(
        defn, QString::fromStdString(classField));
    if (fieldIdx < 0) {
        GDALClose(vecDs);
        throw RSOperatorError(ErrorCode::InvalidParameter, "classField not found: " + classField);
    }

    // votes[segId][classId] = count
    std::vector<std::map<int, int>> votes(static_cast<size_t>(nSeg + 1));
    OGR_L_ResetReading(layer);
    OGRFeatureH feat = nullptr;
    while ((feat = OGR_L_GetNextFeature(layer)) != nullptr) {
        context.throwIfCancelled();
        const int classId = OGR_F_GetFieldAsInteger(feat, fieldIdx);
        if (classId <= 0) {
            OGR_F_Destroy(feat);
            continue;
        }
        OGRGeometryH geom = OGR_F_GetGeometryRef(feat);
        if (!geom) {
            OGR_F_Destroy(feat);
            continue;
        }
        std::vector<uint8_t> mask = rasterizeGeometry(geom, width, height, gt);
        for (size_t i = 0; i < mask.size(); ++i) {
            if (!mask[i])
                continue;
            const int y = static_cast<int>(i / static_cast<size_t>(width));
            const int x = static_cast<int>(i % static_cast<size_t>(width));
            const int sid = labels.at<int>(y, x);
            if (sid > 0)
                votes[static_cast<size_t>(sid)][classId]++;
        }
        OGR_F_Destroy(feat);
    }
    GDALClose(vecDs);

    std::vector<int> segLabel(static_cast<size_t>(nSeg + 1), 0);
    int labeledSegments = 0;
    for (int s = 1; s <= nSeg; ++s) {
        int bestClass = 0;
        int bestCount = 0;
        int total = 0;
        for (const auto& [cid, c] : votes[static_cast<size_t>(s)]) {
            total += c;
            if (c > bestCount) {
                bestCount = c;
                bestClass = cid;
            }
        }
        if (total >= minLabelPixels && bestClass > 0) {
            segLabel[static_cast<size_t>(s)] = bestClass;
            ++labeledSegments;
        }
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
    std::unique_ptr<RsClassifierBackend> backend = makeBackend(method);
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
    int maxClassId = 0;
    for (int s = 1; s <= nSeg; ++s) {
        // Backend predictions are integral class ids already; negative is
        // defensive only (SVM/Bayes predict within the trained label set).
        const int label = std::max(0, pred.at<int>(s - 1, 0));
        classOfSeg[static_cast<size_t>(s)] = label;
        maxClassId = std::max(maxClassId, label);
    }

    // Pipeline dtype policy (ADR 0019 S4): class ids come from a user vector
    // field and can exceed 255 — escalate the output dtype instead of the
    // old silent 0-255 clamp.
    GDALDataType outType = GDT_Byte;
    if (maxClassId > 65535)
        outType = GDT_Int32;
    else if (maxClassId > 255)
        outType = GDT_UInt16;

    // Paint class map
    context.reportProgress(0.9, "Writing class map");
    std::vector<int32_t> classMap(nPix, 0);
    for (int y = 0; y < height; ++y) {
        const int* rowL = labels.ptr<int>(y);
        for (int x = 0; x < width; ++x) {
            const int sid = rowL[x];
            if (sid > 0)
                classMap[static_cast<size_t>(y) * width + x] =
                    classOfSeg[static_cast<size_t>(sid)];
        }
    }
    writeClassGeoTiff(outputPath, classMap, width, height, gt,
                      ds.projection().toStdString(), outType);

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
    result["segments"] = nSeg;
    result["labeledSegments"] = labeledSegments;
    result["trainSamples"] = labeledSegments;
    result["classes"] = static_cast<int>(classHist.size());
    result["features"] = nFeat;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::rs
