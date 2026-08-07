/***************************************************************************
 * rs_image_fusion_operator.cpp  —  Image fusion / pan-sharpening RSOperator
 ***************************************************************************/
#include "rs_image_fusion_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/image_fusion.h"

#include <QString>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {
    "linear", "brovey", "pca", "ihs", "gram_schmidt"
};

} // anonymous namespace

Json::Value RsImageFusionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["pan"] = makeRasterParam("pan", "High-resolution panchromatic raster");
    props["ms"] = makeRasterParam("ms", "Low-resolution multispectral raster");
    props["output"] = makeOutputParam("output", "Output fused raster", "tif");
    props["method"] = makeEnumParam("method", "Fusion method", s_methods, "linear");
    props["panWeight"] = makeNumberParam("panWeight", "Panchromatic weight for linear fusion", 0.5);
    props["redIdx"] = makeIntegerParam("redIdx", "0-based MS band index for IHS R", 0);
    props["greenIdx"] = makeIntegerParam("greenIdx", "0-based MS band index for IHS G", 1);
    props["blueIdx"] = makeIntegerParam("blueIdx", "0-based MS band index for IHS B", 2);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method", "");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"pan", "ms", "output"});
    return root;
}

Json::Value RsImageFusionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("fusion");
    meta["tags"].append("pan-sharpening");
    meta["tags"].append("enhancement");
    meta["purpose"] = "Combine spatial detail of panchromatic data with spectral richness of multispectral data.";
    meta["prerequisites"].append("Pan and MS rasters must be co-registered.");
    meta["workflowHints"].append("Use IHS for RGB composites; use PCA/Brovey for general multi-band fusion.");
    meta["workflowHints"].append("Use Gram-Schmidt (gram_schmidt) for general multi-band sharpening with minimal spectral distortion.");
    meta["limitations"].append("IHS requires exactly 3 MS bands mapped to R/G/B.");
    return meta;
}

Json::Value RsImageFusionOperator::run(const Json::Value& params,
                                       RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string panPath = requireString(params, "pan");
    const std::string msPath = requireString(params, "ms");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(panPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Panchromatic raster not found: " + panPath);
    }
    if (!fileExists(msPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Multispectral raster not found: " + msPath);
    }

    const std::string method = getEnum(params, "method", s_methods, "linear");
    const float panWeight = static_cast<float>(getDouble(params, "panWeight", 0.5));

    context.logInfo("Running " + method + " fusion: " + panPath + " + " + msPath);
    context.reportProgress(0.2, "Running image fusion");

    ImageFusion::NativeFusionParams fusionParams;
    fusionParams.method = QString::fromStdString(method);
    fusionParams.panWeight = panWeight;
    fusionParams.redIdx = getInt(params, "redIdx", 0);
    fusionParams.greenIdx = getInt(params, "greenIdx", 1);
    fusionParams.blueIdx = getInt(params, "blueIdx", 2);
    if (params.isMember("msWeights") && params["msWeights"].isArray()) {
        for (Json::ArrayIndex i = 0; i < params["msWeights"].size(); ++i) {
            if (params["msWeights"][i].isNumeric()) {
                fusionParams.msWeights.append(
                    static_cast<float>(params["msWeights"][i].asDouble()));
            }
        }
    }

    QString errorMessage;
    if (!ImageFusion::processNativeFusion(QString::fromStdString(panPath),
                                          QString::fromStdString(msPath),
                                          QString::fromStdString(outputPath),
                                          fusionParams,
                                          &errorMessage)) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Image fusion failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Image fusion complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    return result;
}

} // namespace sicnu::operators::rs
