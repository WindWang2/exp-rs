/***************************************************************************
 * rs_kmeans_operator.cpp  —  Unsupervised K-Means classification
 *
 * ADR 0019 slice S4 — thin JSON adapter over the analysis-layer
 * classification pipeline:
 *
 *   params → full-band read + deterministic std::mt19937(42) subsample
 *            (the operator's long-standing sampling policy, unchanged) →
 *            RsClassificationPipeline::run with an RsClassifierKMeans
 *            backend.
 *
 * Tiled prediction, GTiff creation options, NoData/ignore policy and the
 * Byte color table all come from the pipeline (parity with the GUI path by
 * construction). The backend is the analysis-layer RsClassifierKMeans, so
 * K-Means init / attempts / termination criteria cannot drift from the GUI
 * again.
 ***************************************************************************/
#include "rs_kmeans_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include "rs_classification_pipeline.h"
#include "rs_classifier_kmeans.h"

#include <QColor>
#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// Deterministic per-cluster colors, same synthesis as the supervised
/// adapter (headless runs have no ROI class defs; the palette also feeds
/// the pipeline's max-class-id dtype check).
QColor classColor(int classId) {
    return QColor::fromHsv((classId * 47) % 360, 200, 230);
}

/// Map a failed pipeline run back onto the operator's stable error codes.
[[noreturn]] void throwPipelineError(const RsClassificationPipelineResult& res) {
    using E = RsClassificationPipelineResult::Error;
    const std::string msg = res.errorMessage.toStdString();
    switch (res.error) {
        case E::Cancelled:
            throw RSOperatorError(ErrorCode::Cancelled, "Cancelled");
        case E::TrainingFailed:
            // RsClassifierKMeans swallows the cv::kmeans exception text and
            // returns false; the old hand-rolled fit surfaced e.what().
            throw RSOperatorError(ErrorCode::OpenCvError, "K-Means training failed");
        case E::NotFittedNoTrainingData:
            throw RSOperatorError(ErrorCode::InvalidInputData, msg);
        case E::InvalidBand:
            throw RSOperatorError(ErrorCode::OutOfRange, msg);
        case E::OutputDriverUnavailable:
            throw RSOperatorError(ErrorCode::GdalError, "GTiff driver not available");
        case E::OutputCreateFailed:
            throw RSOperatorError(ErrorCode::GdalError, "Failed to create output");
        case E::PredictionFailed:
        case E::PredictionSizeMismatch:
            throw RSOperatorError(ErrorCode::OpenCvError, msg);
        case E::RasterOpenFailed:
        case E::RasterReadFailed:
            throw RSOperatorError(ErrorCode::GdalError, msg);
        default:
            throw RSOperatorError(ErrorCode::ComputationError, msg);
    }
}

} // namespace

Json::Value RsKmeansOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output class map (Byte GeoTIFF)", "tif");
    props["k"] = makeIntegerParam("k", "Number of clusters", 3);
    props["maxSamples"] = makeIntegerParam("maxSamples",
                                           "Max samples for centroid fitting (0 = all pixels)",
                                           100000);
    Json::Value bands = makeStringParam("bands", "Optional 1-based band index array", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Class map", "tif");
    outputs["k"] = makeIntegerParam("k", "Clusters", 0);
    outputs["samplesUsed"] = makeIntegerParam("samplesUsed", "Training sample count", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsKmeansOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("kmeans");
    meta["tags"].append("unsupervised");
    meta["tags"].append("classification");
    meta["purpose"] = "Cluster multi-band pixels into k spectral classes";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["useCases"].append("Exploratory land-cover stratification without training samples");
    meta["limitations"] = "Writes class IDs 1..k; large rasters are subsampled for centroid fit";
    return meta;
}

Json::Value RsKmeansOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const int k = getInt(params, "k", 3);
    const int maxSamples = getInt(params, "maxSamples", 100000);

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    }
    if (k < 2 || k > 255) {
        throw RSOperatorError(ErrorCode::InvalidParameter, "k must be in [2, 255]");
    }

    ensureGdalInit();
    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open: " + inputPath);
    }

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (width <= 0 || height <= 0 || bandCount <= 0) {
        throw RSOperatorError(ErrorCode::InvalidInputData, "Invalid raster dimensions");
    }

    const std::vector<int> bands = parseBands(params, bandCount);
    const int nFeat = static_cast<int>(bands.size());
    const size_t nPix = static_cast<size_t>(width) * static_cast<size_t>(height);

    context.reportProgress(0.05, "Reading bands");
    context.throwIfCancelled();

    std::vector<std::vector<float>> bandData(static_cast<size_t>(nFeat));
    for (int i = 0; i < nFeat; ++i) {
        bandData[static_cast<size_t>(i)].resize(nPix);
        if (!ds.readBandData(bands[static_cast<size_t>(i)],
                             bandData[static_cast<size_t>(i)].data(),
                             width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bands[static_cast<size_t>(i)]));
        }
        context.reportProgress(0.05 + 0.25 * (i + 1) / nFeat, "Read band");
        context.throwIfCancelled();
    }

    // Subsample for centroid fitting when raster is large (unchanged
    // operator policy: deterministic std::mt19937(42) shuffle).
    std::vector<size_t> sampleIdx;
    sampleIdx.reserve(nPix);
    for (size_t i = 0; i < nPix; ++i)
        sampleIdx.push_back(i);

    if (maxSamples > 0 && static_cast<int>(nPix) > maxSamples) {
        std::mt19937 rng(42);
        std::shuffle(sampleIdx.begin(), sampleIdx.end(), rng);
        sampleIdx.resize(static_cast<size_t>(maxSamples));
    }

    const int nSamples = static_cast<int>(sampleIdx.size());
    if (nSamples < k) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Not enough samples (" + std::to_string(nSamples) +
                                  ") for k=" + std::to_string(k));
    }

    context.reportProgress(0.35, "Building feature matrix (" + std::to_string(nSamples) + " samples)");

    cv::Mat trainX(nSamples, nFeat, CV_32F);
    for (int r = 0; r < nSamples; ++r) {
        const size_t pix = sampleIdx[static_cast<size_t>(r)];
        for (int c = 0; c < nFeat; ++c) {
            trainX.at<float>(r, c) = bandData[static_cast<size_t>(c)][pix];
        }
    }

    RsClassificationPipeline::Config cfg;
    cfg.sourceRaster = QString::fromStdString(inputPath);
    cfg.outputRaster = QString::fromStdString(outputPath);
    cfg.bandIndices = QVector<int>(bands.begin(), bands.end());
    cfg.trainX = trainX;
    // KMeans fit() ignores y, but the pipeline requires a non-empty trainY
    // when the backend is not fitted. Dummy zeros satisfy the check; the
    // methodName deliberately stays lowercase so the pipeline's "KMeans"
    // Hungarian cluster→class remap (supervised accuracy only) is skipped —
    // there are no true class ids here and the operator's 1-based cluster
    // ids must survive verbatim.
    cfg.trainY = cv::Mat::zeros(nSamples, 1, CV_32S);
    cfg.methodName = QStringLiteral("kmeans");
    for (int id = 1; id <= k; ++id)
        cfg.classColors[id] = classColor(id);
    cfg.backend = std::make_unique<RsClassifierKMeans>(k);

    // Bridge: pipeline fraction [0,1] → operator progress 0.40–0.98; cancel
    // via the sink's return value so the pipeline removes a partially-written
    // output before reporting Error::Cancelled.
    const RsClassificationPipelineResult res = RsClassificationPipeline::run(
        std::move(cfg),
        [&context](double fraction, const QString& message) {
            if (context.isCancelled())
                return false;
            // Preserve the operator's progress message style.
            const std::string msg = message == QStringLiteral("Predicting tiles")
                                        ? "Classifying"
                                        : message.toStdString();
            context.reportProgress(0.40 + 0.58 * fraction, msg);
            return true;
        });

    if (!res.ok) {
        throwPipelineError(res);
    }

    context.reportProgress(1.0, "K-Means complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["k"] = k;
    result["width"] = width;
    result["height"] = height;
    result["samplesUsed"] = nSamples;
    result["features"] = nFeat;
    return result;
}

} // namespace sicnu::operators::rs
