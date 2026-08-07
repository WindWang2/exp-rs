/***************************************************************************
 * rs_rx_anomaly_operator.cpp  —  Reed-Xiaoli anomaly detection RSOperator
 ***************************************************************************/
#include "rs_rx_anomaly_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_anomaly.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <algorithm>
#include <numeric>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsRxAnomalyOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output RX score raster", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "RX score GeoTIFF", "tif");
    outputs["mean"] = makeNumberParam("mean", "Mean RX score", 0.0);
    outputs["max"] = makeNumberParam("max", "Maximum RX score", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsRxAnomalyOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("rx");
    meta["tags"].append("anomaly");
    meta["tags"].append("hyperspectral");
    meta["purpose"] = "Detect pixels that deviate from the scene background "
                     "by Mahalanobis distance (unsupervised).";
    meta["workflowHints"].append("Input should be calibrated reflectance/radiance; "
                                 "run before target detection / screening.");
    meta["limitations"].append("Global RX uses the whole scene as background; "
                               "for local background use a windowed variant.");
    return meta;
}

Json::Value RsRxAnomalyOperator::run(const Json::Value& params,
                                     RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (bandCount < 2)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "RX detection requires at least 2 bands, got "
                                  + std::to_string(bandCount));

    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> pixels(pixelCount * static_cast<size_t>(bandCount), 0.0f);
    context.reportProgress(0.15, "Reading bands");
    for (int b = 1; b <= bandCount; ++b)
    {
        std::vector<float> bandData(pixelCount);
        if (!ds.readBandData(b, bandData.data(), width, height))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(b));
        for (size_t p = 0; p < pixelCount; ++p)
            pixels[p * static_cast<size_t>(bandCount) + (b - 1)] = bandData[p];
    }

    context.reportProgress(0.45, "Computing background statistics");
    context.throwIfCancelled();

    std::vector<float> rxScores;
    QString error;
    if (!SpectralAnomaly::rxDetector(pixels.data(), pixelCount, bandCount,
                                     &rxScores, &error))
        throw RSOperatorError(ErrorCode::ComputationError,
                              error.isEmpty() ? "RX detection failed" : error.toStdString());

    context.reportProgress(0.75, "Writing RX score raster");

    QString writeError;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, {rxScores},
                         ds.geoTransform(), ds.projection(), &writeError))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write RX raster: " + writeError.toStdString());

    const double mean = rxScores.empty()
        ? 0.0
        : std::accumulate(rxScores.begin(), rxScores.end(), 0.0)
              / static_cast<double>(rxScores.size());
    const double maxScore = rxScores.empty()
        ? 0.0
        : static_cast<double>(*std::max_element(rxScores.begin(), rxScores.end()));

    ds.close();
    context.reportProgress(1.0, "RX anomaly detection complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["mean"] = mean;
    result["max"] = maxScore;
    return result;
}

} // namespace sicnu::operators::rs
