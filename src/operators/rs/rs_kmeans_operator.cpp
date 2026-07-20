/***************************************************************************
 * rs_kmeans_operator.cpp  —  Unsupervised K-Means classification
 ***************************************************************************/
#include "rs_kmeans_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QString>

#include <gdal.h>
#include <cpl_string.h>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

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

    // Subsample for centroid fitting when raster is large.
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

    context.reportProgress(0.45, "Running K-Means (k=" + std::to_string(k) + ")");
    context.throwIfCancelled();

    cv::Mat labels;
    cv::Mat centers;
    const cv::TermCriteria term(cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, 100, 1.0);
    try {
        cv::kmeans(trainX, k, labels, term, 3, cv::KMEANS_PP_CENTERS, centers);
    } catch (const cv::Exception& e) {
        throw RSOperatorError(ErrorCode::OpenCvError,
                              std::string("cv::kmeans failed: ") + e.what());
    }

    if (centers.empty() || centers.rows != k || centers.cols != nFeat) {
        throw RSOperatorError(ErrorCode::ComputationError, "K-Means produced empty centers");
    }

    context.reportProgress(0.65, "Assigning all pixels to clusters");
    context.throwIfCancelled();

    // Predict every pixel: nearest centroid (Euclidean), 1-based labels.
    std::vector<uint8_t> classMap(nPix);
    for (size_t pix = 0; pix < nPix; ++pix) {
        int best = 0;
        float bestDist = std::numeric_limits<float>::max();
        for (int c = 0; c < k; ++c) {
            float dist = 0.0f;
            for (int f = 0; f < nFeat; ++f) {
                const float d = bandData[static_cast<size_t>(f)][pix] -
                                centers.at<float>(c, f);
                dist += d * d;
            }
            if (dist < bestDist) {
                bestDist = dist;
                best = c;
            }
        }
        classMap[pix] = static_cast<uint8_t>(best + 1); // 1-based
        if ((pix & 0xFFFF) == 0) {
            context.throwIfCancelled();
            context.reportProgress(0.65 + 0.25 * static_cast<double>(pix) / static_cast<double>(nPix),
                                   "Classifying");
        }
    }

    context.reportProgress(0.92, "Writing class map");

    // Write Byte GeoTIFF with geotransform from source.
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        throw RSOperatorError(ErrorCode::GdalError, "GTiff driver not available");
    }

    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH outDs = GDALCreate(driver, outputPath.c_str(), width, height, 1, GDT_Byte, opts);
    CSLDestroy(opts);
    if (!outDs) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create output: " + outputPath);
    }

    const auto gt = ds.geoTransform();
    double gtArr[6] = {gt[0], gt[1], gt[2], gt[3], gt[4], gt[5]};
    GDALSetGeoTransform(outDs, gtArr);
    const QString proj = ds.projection();
    if (!proj.isEmpty())
        GDALSetProjection(outDs, proj.toUtf8().constData());

    GDALRasterBandH outBand = GDALGetRasterBand(outDs, 1);
    if (!outBand) {
        GDALClose(outDs);
        QFile::remove(QString::fromStdString(outputPath));
        throw RSOperatorError(ErrorCode::GdalError, "Failed to get output band for class map");
    }

    const CPLErr err = GDALRasterIO(outBand, GF_Write, 0, 0, width, height,
                                    classMap.data(), width, height, GDT_Byte, 0, 0);
    if (err != CE_None) {
        GDALClose(outDs);
        QFile::remove(QString::fromStdString(outputPath));
        throw RSOperatorError(ErrorCode::GdalError, "Failed to write class map");
    }

    GDALSetRasterNoDataValue(outBand, 0);
    GDALClose(outDs);

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
