/***************************************************************************
 * rs_mnf_operator.cpp  —  Minimum Noise Fraction RSOperator
 ***************************************************************************/
#include "rs_mnf_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsMnfOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output MNF components raster", "tif");
    props["numComponents"] = makeIntegerParam("numComponents",
                                              "Number of components (0 = all bands)",
                                              0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "MNF GeoTIFF", "tif");
    outputs["numComponents"] = makeIntegerParam("numComponents", "Components written", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsMnfOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("mnf");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("dimensionality-reduction");
    meta["purpose"] = "Reduce hyperspectral dimensionality by signal-to-noise "
                      "ordered components (PCA of noise-whitened data).";
    meta["prerequisites"].append("Input should be calibrated reflectance/radiance; "
                                 "wavelength metadata is not required for MNF itself.");
    return meta;
}

Json::Value RsMnfOperator::run(const Json::Value& params, RSOperatorContext& context) {
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
                              "MNF requires at least 2 bands, got " + std::to_string(bandCount));
    }

    if (numComponents <= 0 || numComponents > bandCount) {
        numComponents = bandCount;
    }

    context.reportProgress(0.1, "Running MNF (" + std::to_string(numComponents) + " components)");
    context.throwIfCancelled();

    QString error;
    const bool ok = ImageEnhancement::processMnfFile(
        QString::fromStdString(inputPath),
        QString::fromStdString(outputPath),
        numComponents,
        &error);

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              error.isEmpty() ? "MNF failed" : error.toStdString());
    }

    context.reportProgress(1.0, "MNF complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["numComponents"] = numComponents;
    result["width"] = src.width();
    result["height"] = src.height();
    return result;
}

} // namespace sicnu::operators::rs
