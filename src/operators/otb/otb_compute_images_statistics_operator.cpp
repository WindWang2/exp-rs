/***************************************************************************
 * otb_compute_images_statistics_operator.cpp
 ***************************************************************************/
#include "otb_compute_images_statistics_operator.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <QFile>
#include <QString>

namespace sicnu::operators::otb {

Json::Value OtbComputeImagesStatisticsOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);

    Json::Value inputItems(Json::objectValue);
    inputItems["type"] = "string";
    inputItems["format"] = "raster";
    Json::Value inputParam = makeRasterParam("input", "Input raster file path");
    inputParam["description"] = "Input raster file path (or use 'inputs' for multiple rasters)";
    inputParam["required"] = false;
    props["input"] = inputParam;

    Json::Value inputsParam(Json::objectValue);
    inputsParam["name"] = "inputs";
    inputsParam["type"] = "array";
    inputsParam["description"] = "List of input raster file paths";
    inputsParam["items"] = inputItems;
    inputsParam["required"] = false;
    props["inputs"] = inputsParam;

    props["ram"] = makeBooleanParam("ram", "Enable RAM estimation mode", false);
    props["output"] = makeOutputParam("output", "Output statistics XML file", "xml");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeStringParam("output", "Output statistics XML file path");

    return buildSchema(displayName(), description(), props, outputs,
                       {}); // input or inputs required at runtime
}

Json::Value OtbComputeImagesStatisticsOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("statistics");
    meta["tags"].append("normalization");
    meta["tags"].append("otb");
    meta["purpose"] = "Compute image mean and standard deviation for classifier training.";
    meta["prerequisites"].append("At least one input raster must be provided via 'input' or 'inputs'.");
    meta["workflowHints"].append("Pass the resulting XML to otb:svm_classification via 'stats'.");
    meta["limitations"].append("All input rasters should have the same number of bands.");
    return meta;
}

QStringList OtbComputeImagesStatisticsOperator::buildOtbArgs(const Json::Value& params,
                                                             RSOperatorContext& context) const {
    Q_UNUSED(context);

    std::vector<std::string> inputPaths = getStringArray(params, "inputs");
    if (inputPaths.empty() && params.isMember("input") && params["input"].isString()) {
        inputPaths.push_back(params["input"].asString());
    }

    if (inputPaths.empty()) {
        throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                              "At least one input raster is required (parameter 'input' or 'inputs')");
    }

    for (const auto& path : inputPaths) {
        if (!QFile::exists(QString::fromStdString(path))) {
            throw RSOperatorError(ErrorCode::FileNotFound,
                                  "Input raster not found: " + path);
        }
    }

    const bool ram = getBool(params, "ram", false);
    const std::string outputPath = requireString(params, "output");

    QStringList args;
    for (const auto& path : inputPaths) {
        args << "-il" << QString::fromStdString(path);
    }

    if (ram) {
        args << "-ram" << QStringLiteral("256");
    }

    args << "-out" << QString::fromStdString(outputPath);
    return args;
}

} // namespace sicnu::operators::otb
