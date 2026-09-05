/***************************************************************************
 * rs_sar_texture_operator.cpp — SAR GLCM texture measures (Platform 3.0)
 ***************************************************************************/
#include "rs_sar_texture_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_texture.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_directions = { "0", "45", "90", "135" };

// Canonical measure order the kernel expands to when no tokens are given;
// also the output band order for that default case.
const std::vector<sicnu::sar::GlcmMeasure> s_allMeasures = {
    sicnu::sar::GlcmMeasure::Contrast,    sicnu::sar::GlcmMeasure::Dissimilarity,
    sicnu::sar::GlcmMeasure::Homogeneity, sicnu::sar::GlcmMeasure::Energy,
    sicnu::sar::GlcmMeasure::Entropy,     sicnu::sar::GlcmMeasure::Mean,
    sicnu::sar::GlcmMeasure::StdDev,      sicnu::sar::GlcmMeasure::Correlation,
};

Json::Value makeSarInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "sar";
    return c;
}

} // anonymous namespace

Json::Value RsSarTextureOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input SAR intensity raster (calibrated sigma0 recommended)");
    props["input"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output texture raster, one Float32 band per measure", "tif");
    props["band"] = makeIntegerParam("band", "1-based input band to analyze", 1);
    Json::Value windowSize = makeIntegerParam("windowSize", "Odd GLCM window size", 5);
    setRange(windowSize, 3, 15);
    props["windowSize"] = windowSize;
    Json::Value quantLevels = makeIntegerParam("quantLevels", "Gray levels per window (equal-width quantization)", 16);
    setRange(quantLevels, 2, 64);
    props["quantLevels"] = quantLevels;
    props["directionDeg"] = makeEnumParam("directionDeg", "Co-occurrence direction (degrees)", s_directions, "0");
    props["displacement"] = makeIntegerParam("displacement", "Co-occurrence offset distance in pixels", 1);
    Json::Value measures = makeStringParam("measures",
                                           "Measure tokens (contrast, dissimilarity, homogeneity, "
                                           "energy, asm, entropy, mean, stddev, correlation); "
                                           "empty = all 8 distinct measures",
                                           "");
    measures["type"] = "array";
    measures["items"] = Json::Value(Json::objectValue);
    measures["items"]["type"] = "string";
    props["measures"] = measures;
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Texture raster path");
    outputs["measures"] = makeStringParam("measures", "Resolved measure tokens in output band order");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands (one per measure)");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSarTextureOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("texture");
    meta["tags"].append("glcm");
    meta["purpose"] = "Compute per-pixel GLCM (Haralick) texture measures over a "
                      "sliding window of a SAR intensity raster, one Float32 band "
                      "per requested measure.";
    meta["prerequisites"].append("Calibrated SAR intensity raster (rs:sar_calibrate first) "
                                 "so window statistics operate on physically scaled data.");
    meta["prerequisites"].append("Each window is quantized to quantLevels equal-width bins "
                                 "spanning the window's [min, max].");
    meta["workflowHints"].append("Texture feature bands feed classification and feature "
                                 "stacks (e.g. rs:kmeans_classification, "
                                 "rs:supervised_classification).");
    meta["workflowHints"].append("rs:sar_calibrate -> rs:sar_texture.");
    meta["limitations"].append("Windows containing NoData produce NaN in every measure.");
    meta["limitations"].append("Quantization is equal-width per window, so measures are "
                               "not directly comparable across windows with different "
                               "dynamic ranges.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarTextureOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    // halo copy + window buffer + per-measure output planes
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarTextureOperator::run(const Json::Value& params,
                                        RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    const int band = getInt(params, "band", 1);
    const int windowSize = getInt(params, "windowSize", 5);
    if (windowSize < 3 || windowSize > 15 || windowSize % 2 == 0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "windowSize must be an odd integer in [3, 15]");
    }
    const int quantLevels = getInt(params, "quantLevels", 16);
    if (quantLevels < 2 || quantLevels > 64) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "quantLevels must be in [2, 64]");
    }
    const std::string directionStr = getEnum(params, "directionDeg", s_directions, "0");
    const int directionDeg = std::stoi(directionStr);
    const int displacement = getInt(params, "displacement", 1);
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );

    // Resolve + dedupe measure tokens, keeping first-occurrence order. "asm"
    // and "energy" map to the same measure; the first token supplies the
    // result label.
    const std::vector<std::string> tokens = getStringArray(params, "measures");
    std::vector<sicnu::sar::GlcmMeasure> measureList;
    std::vector<std::string> measureLabels;
    if (tokens.empty()) {
        measureList = s_allMeasures;
        measureLabels.reserve(measureList.size());
        for (const auto measure : measureList) {
            measureLabels.push_back(sicnu::sar::glcmMeasureToString(measure).toStdString());
        }
    } else {
        for (const auto& token : tokens) {
            bool ok = false;
            const sicnu::sar::GlcmMeasure measure =
                sicnu::sar::glcmMeasureFromString(QString::fromStdString(token), &ok);
            if (!ok) {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "Unknown texture measure: '" + token + "'");
            }
            if (std::find(measureList.begin(), measureList.end(), measure) != measureList.end()) {
                continue;
            }
            measureList.push_back(measure);
            measureLabels.push_back(token);
        }
    }

    sicnu::sar::TextureParams textureParams;
    textureParams.windowSize = windowSize;
    textureParams.quantLevels = quantLevels;
    textureParams.directionDeg = directionDeg;
    textureParams.displacement = displacement;
    for (const auto measure : measureList) {
        textureParams.measures << sicnu::sar::glcmMeasureToString(measure);
    }

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }
    if (band < 1 || band > src.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "band out of range: " + std::to_string(band));
    }

    // Sentinel declared on the analysis band (NaN when undeclared).
    bool hasNodata = false;
    const double nodataRaw = src.bandNoDataValue(band, &hasNodata);
    const float nodata = hasNodata ? static_cast<float>(nodataRaw)
                                   : std::numeric_limits<float>::quiet_NaN();

    context.throwIfCancelled();
    context.reportProgress(0.05, "Computing GLCM texture measures");
    GdalStreamingOutput dst(QString::fromStdString(outputPath), src.width(), src.height(),
                            static_cast<int>(measureList.size()), GDT_Float32,
                            src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setNoDataValue(std::numeric_limits<float>::quiet_NaN());

    // The kernel writes one Float32 band per measure in measureList order and
    // records SICNU_SAR_TEXTURE_MEASURES on the dataset.
    if (!sicnu::sar::textureRaster(src, band, textureParams, nodata, dst, 256,
                                   polarizations, sensor)) {
        dst.abandon();
        throw RSOperatorError(ErrorCode::GdalError,
                              "SAR texture computation failed while streaming");
    }

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    Json::Value measuresOut(Json::arrayValue);
    for (const auto& label : measureLabels) {
        measuresOut.append(label);
    }
    result["measures"] = measuresOut;
    result["bands"] = static_cast<int>(measureList.size());
    context.reportProgress(1.0, "SAR texture computation complete");
    return result;
}

} // namespace sicnu::operators::rs
