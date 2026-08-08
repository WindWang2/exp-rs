/***************************************************************************
 * rs_pca_operator.cpp  —  Principal Component Analysis RSOperator
 ***************************************************************************/
#include "rs_pca_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsPcaOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output PCA components raster", "tif");
    props["numComponents"] = makeIntegerParam("numComponents",
                                              "Number of principal components (0 = all bands)",
                                              0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "PCA GeoTIFF", "tif");
    outputs["numComponents"] = makeIntegerParam("numComponents", "Components written", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsPcaOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("pca");
    meta["tags"].append("enhancement");
    meta["tags"].append("dimensionality-reduction");
    meta["purpose"] = "Reduce multi-band correlation via PCA for visualization and analysis";
    return meta;
}

Json::Value RsPcaOperator::executionEstimate() const {
    // FullRaster (default policy): the whole raster is resident. For a typical
    // 1024x1024x4-band float32 input (16 MiB) peak RAM is ~3x the raster:
    // input bands + centered copy + output components buffer.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;          // full-raster: tiling not applicable
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 50331648; // ~48 MiB
    return est;
}

Json::Value RsPcaOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    int numComponents = getInt(params, "numComponents", 0);

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input raster not found: " + inputPath);
    }

    ensureGdalInit();

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input: " + inputPath);
    }

    const int bandCount = src.bandCount();
    if (bandCount < 2) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "PCA requires at least 2 bands, got " + std::to_string(bandCount));
    }

    if (numComponents <= 0 || numComponents > bandCount) {
        numComponents = bandCount;
    }

    context.reportProgress(0.1, "Running PCA (" + std::to_string(numComponents) + " components)");
    context.throwIfCancelled();

    QString error;
    const bool ok = ImageEnhancement::processPcaFile(
        QString::fromStdString(inputPath),
        QString::fromStdString(outputPath),
        numComponents,
        &error);

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              error.isEmpty() ? "PCA failed" : error.toStdString());
    }

    context.reportProgress(1.0, "PCA complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["numComponents"] = numComponents;
    result["width"] = src.width();
    result["height"] = src.height();
    return result;
}

} // namespace sicnu::operators::rs
