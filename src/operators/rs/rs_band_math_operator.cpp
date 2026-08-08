/***************************************************************************
 * rs_band_math_operator.cpp  —  Band math RSOperator
 ***************************************************************************/
#include "rs_band_math_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/band_math.h"

#include <QString>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsBandMathOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output single-band raster", "tif");
    props["expression"] = makeStringParam("expression",
                                          "Band math expression (e.g. (b1-b2)/(b1+b2))",
                                          "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["expression"] = makeStringParam("expression", "Evaluated expression", "");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "expression"});
    return root;
}

Json::Value RsBandMathOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("band-math");
    meta["tags"].append("spectral");
    meta["tags"].append("calculator");
    meta["purpose"] = "Apply custom per-pixel arithmetic across raster bands.";
    meta["workflowHints"].append("Use NDVI-like formulas for vegetation or water indices.");
    meta["limitations"].append("Expression uses 1-based band references (b1, b2, ...).");
    return meta;
}

Json::Value RsBandMathOperator::executionEstimate() const {
    // FullRaster (base default): no preferred tile. BandMath::processFile loads
    // every input band plus the output buffer; typical input 1024x1024x4 float32
    // (~4 MiB/band) -> 4 input bands + 1 output = ~20 MiB.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 20971520;
    return est;
}

Json::Value RsBandMathOperator::run(const Json::Value& params,
                                    RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string expression = requireString(params, "expression");

    if (expression.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Expression parameter cannot be empty");
    }

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    context.throwIfCancelled();
    context.logInfo("Evaluating expression: " + expression);
    context.reportProgress(0.2, "Running band math");

    QString errorMessage;
    if (!BandMath::processFile(QString::fromStdString(inputPath),
                               QString::fromStdString(outputPath),
                               QString::fromStdString(expression),
                               &errorMessage)) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Band math evaluation failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Band math complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["expression"] = expression;
    return result;
}

} // namespace sicnu::operators::rs
