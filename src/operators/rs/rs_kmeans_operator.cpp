/***************************************************************************
 * rs_kmeans_operator.cpp  —  Unsupervised K-Means classification
 *
 * ADR 0019 slice S4 — thin JSON adapter over the analysis-layer
 * classification pipeline:
 *
 *   params → band sampling (full read when the raster is small, deterministic
 *            std::mt19937(42) reservoir sampling when maxSamples is set and
 *            the raster is large — bounded memory O(maxSamples*nFeat)) →
 *            RsClassificationPipeline::run with an RsClassifierKMeans
 *            backend.
 *
 * Tiled prediction, GTiff creation options, NoData/ignore policy and the
 * Byte color table all come from the pipeline (parity with the GUI path by
 * construction). The backend is built by RsClassifierBackendFactory
 * (createKMeans — one construction path, no drift), so K-Means init /
 * attempts / termination criteria cannot drift from the GUI again.
 ***************************************************************************/
#include "rs_kmeans_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include "rs_classification_pipeline.h"
#include "rs_classifier_backend_factory.h"
#include "rs_classification_utils.h"

#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

#include <algorithm>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

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

Json::Value RsKmeansOperator::executionEstimate() const
{
    // FullRaster (default policy): all selected bands, the pixel index and the
    // Float32 training matrix are resident (prediction itself is tile-streamed
    // by the pipeline at 256x256). Typical: 4 x 1024x1024 bands + index + trainX.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 33554432; // 16 MiB bands + 8 MiB index + trainX + fixed
    return est;
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
    if (maxSamples < 0) {
        throw RSOperatorError(ErrorCode::InvalidParameter, "maxSamples must be non-negative");
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

    // Collect per-band NoData values to filter invalid/sentinel pixels during training
    std::vector<float> noDataPerBand( static_cast<size_t>( nFeat ), -9999.0f );
    std::vector<bool> hasNoDataPerBand( static_cast<size_t>( nFeat ), false );
    for ( size_t i = 0; i < static_cast<size_t>( nFeat ); ++i )
    {
        bool hasNd = false;
        double ndVal = ds.bandNoDataValue( bands[i], &hasNd );
        if ( hasNd && std::isfinite( ndVal ) )
        {
            noDataPerBand[i] = static_cast<float>( ndVal );
            hasNoDataPerBand[i] = true;
        }
    }

    auto isPixelValid = [&]( const float *pixelFeatures ) -> bool {
        for ( size_t i = 0; i < static_cast<size_t>( nFeat ); ++i )
        {
            const float v = pixelFeatures[i];
            if ( !std::isfinite( v ) )
                return false;
            if ( hasNoDataPerBand[i] && std::abs( v - noDataPerBand[i] ) < 1e-4f )
                return false;
        }
        return true;
    };

    // Subsample for centroid fitting when the raster is large. Instead of
    // materializing every band fully plus a full-size index array (8 B/px),
    // a deterministic reservoir sample (std::mt19937(42), ADR 0061 policy)
    // keeps only the sampled pixels' features in memory — O(maxSamples*nFeat)
    // regardless of raster dimensions.
    std::vector<size_t> sampleIdx;
    std::vector<std::vector<float>> bandData;
    const bool useReservoir = (maxSamples > 0 && nPix > static_cast<size_t>(maxSamples));

    if (useReservoir)
    {
        const size_t k = static_cast<size_t>(maxSamples);
        std::vector<std::vector<float>> reservoir(static_cast<size_t>(nFeat),
                                                  std::vector<float>(k, 0.0f));
        sampleIdx.resize(k);
        std::mt19937 rng(42);
        std::uniform_int_distribution<size_t> dist(0, std::numeric_limits<size_t>::max());

        constexpr int kTile = 256;
        std::vector<float> tileBuf(static_cast<size_t>(kTile) * kTile * static_cast<size_t>(nFeat));
        std::vector<float> bandScratch(static_cast<size_t>(kTile) * kTile);
        size_t seen = 0;
        for (int y = 0; y < height; y += kTile)
        {
            const int h = std::min(kTile, height - y);
            for (int x = 0; x < width; x += kTile)
            {
                const int w = std::min(kTile, width - x);
                const size_t n = static_cast<size_t>(w) * h;
                context.throwIfCancelled();
                for (int i = 0; i < nFeat; ++i)
                {
                    if (!ds.readBandWindow(bands[static_cast<size_t>(i)], x, y, w, h,
                                           bandScratch.data()))
                        throw RSOperatorError(ErrorCode::GdalError,
                                              "Failed to read band " +
                                                  std::to_string(bands[static_cast<size_t>(i)]));
                    for (size_t p = 0; p < n; ++p)
                        tileBuf[p * static_cast<size_t>(nFeat) + static_cast<size_t>(i)] =
                            bandScratch[p];
                }
                for (size_t p = 0; p < n; ++p)
                {
                    const float *pFeat = tileBuf.data() + p * static_cast<size_t>(nFeat);
                    if (!isPixelValid(pFeat))
                        continue;

                    if (seen < k)
                    {
                        sampleIdx[seen] = seen;
                        for (int i = 0; i < nFeat; ++i)
                            reservoir[static_cast<size_t>(i)][seen] = pFeat[i];
                    }
                    else
                    {
                        // Reservoir replace: j = uniform(0, seen); if j < k replace.
                        const size_t j = static_cast<size_t>(
                            dist(rng) % (seen + 1));
                        if (j < k)
                        {
                            sampleIdx[j] = seen;
                            for (int i = 0; i < nFeat; ++i)
                                reservoir[static_cast<size_t>(i)][j] = pFeat[i];
                        }
                    }
                    ++seen;
                }
            }
        }
        if (seen < k)
        {
            sampleIdx.resize(seen);
            for (int i = 0; i < nFeat; ++i)
                reservoir[static_cast<size_t>(i)].resize(seen);
        }
        bandData = std::move(reservoir);
    }
    else
    {
        std::vector<std::vector<float>> fullData(static_cast<size_t>(nFeat));
        for (int i = 0; i < nFeat; ++i) {
            fullData[static_cast<size_t>(i)].resize(nPix);
            if (!ds.readBandData(bands[static_cast<size_t>(i)],
                                 fullData[static_cast<size_t>(i)].data(),
                                 width, height)) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read band " + std::to_string(bands[static_cast<size_t>(i)]));
            }
            context.reportProgress(0.05 + 0.25 * (i + 1) / nFeat, "Read band");
            context.throwIfCancelled();
        }
        bandData = std::move(fullData);
        sampleIdx.reserve(nPix);
        std::vector<float> pFeat(static_cast<size_t>(nFeat));
        for (size_t i = 0; i < nPix; ++i) {
            for (int c = 0; c < nFeat; ++c)
                pFeat[static_cast<size_t>(c)] = bandData[static_cast<size_t>(c)][i];
            if (isPixelValid(pFeat.data()))
                sampleIdx.push_back(i);
        }
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
        // Reservoir path: bandData is already the sample matrix (position r);
        // full path: bandData is indexed by the original pixel index.
        const size_t pix = useReservoir ? static_cast<size_t>(r)
                                        : sampleIdx[static_cast<size_t>(r)];
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
    // when the backend is not fitted. Dummy zeros satisfy the check.
    // methodName is metadata only (log + sidecar) — the cluster→class remap
    // is gated on RsClassifierBackend::needsLabelRemap() (ADR 0061), so no
    // lowercase spelling workaround is needed and 1-based cluster ids
    // survive verbatim.
    cfg.trainY = cv::Mat::zeros(nSamples, 1, CV_32S);
    cfg.methodName = QStringLiteral("kmeans");
    for (int id = 1; id <= k; ++id)
        cfg.classColors[id] = rsSynthesizedClassColor(id);
    cfg.backend = RsClassifierBackendFactory::createKMeans(k);

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
