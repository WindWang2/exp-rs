/***************************************************************************
 * rs_atmospheric_correction_operator.cpp  —  Atmospheric correction RSOperator
 ***************************************************************************/
#include "rs_atmospheric_correction_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/atmospheric_correction.h"

#include <QString>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {
    "dn_to_radiance", "dos1", "dos2", "quac"
};

} // anonymous namespace

Json::Value RsAtmosphericCorrectionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input optical raster");
    props["output"] = makeOutputParam("output", "Output corrected raster", "tif");
    props["band"] = makeIntegerParam("band", "1-based band number", 1);
    props["method"] = makeEnumParam("method", "Correction method", s_methods, "dos1");
    props["gain"] = makeNumberParam("gain", "Radiance gain", 1.0);
    props["bias"] = makeNumberParam("bias", "Radiance bias", 0.0);
    props["airmass"] = makeNumberParam("airmass", "Relative airmass for DOS2", 1.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method", "");
    outputs["band"] = makeIntegerParam("band", "Processed band", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsAtmosphericCorrectionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("atmospheric");
    meta["tags"].append("dos");
    meta["tags"].append("radiometric");
    meta["purpose"] = "Convert DN to radiance or surface reflectance before spectral analysis.";
    meta["workflowHints"].append("Apply before computing spectral indices.");
    meta["limitations"].append("Gain/bias must be obtained from image metadata.");
    return meta;
}

Json::Value RsAtmosphericCorrectionOperator::run(const Json::Value& params,
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
    const std::string method = getEnum(params, "method", s_methods, "dos1");
    const float gain = static_cast<float>(getDouble(params, "gain", 1.0));
    const float bias = static_cast<float>(getDouble(params, "bias", 0.0));
    const float airmass = static_cast<float>(getDouble(params, "airmass", 1.0));

    int methodCode = 1; // DOS1
    if (method == "dn_to_radiance") methodCode = 0;
    else if (method == "dos2") methodCode = 2;
    else if (method == "quac") methodCode = 3;

    context.logInfo("Applying " + method + " to " + inputPath);
    context.reportProgress(0.2, "Running atmospheric correction");

    QString errorMessage;
    bool ok = false;
    if (methodCode == 3) {
        // QUAC processes all bands jointly (image-statistics based).
        ok = AtmosphericCorrection::processFileMultiBand(
            QString::fromStdString(inputPath),
            QString::fromStdString(outputPath),
            methodCode, &errorMessage,
            [&](double frac, const QString &msg) {
                context.reportProgress(0.2 + 0.75 * frac, msg.toStdString());
                context.throwIfCancelled();
            });
    } else {
        ok = AtmosphericCorrection::processFile(QString::fromStdString(inputPath),
                                                QString::fromStdString(outputPath),
                                                band, methodCode, gain, bias, airmass,
                                                &errorMessage);
    }
    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Atmospheric correction failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Atmospheric correction complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["band"] = band;
    return result;
}

} // namespace sicnu::operators::rs
