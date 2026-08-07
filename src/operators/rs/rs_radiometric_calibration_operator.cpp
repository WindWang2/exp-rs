/***************************************************************************
 * rs_radiometric_calibration_operator.cpp  —  Radiometric calibration RSOperator
 ***************************************************************************/
#include "rs_radiometric_calibration_operator.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/radiometric_calibration.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>
#include <QList>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {
const std::vector<std::string> s_units = {
    "radiance", "toa_reflectance", "brightness_temperature"
};
} // anonymous namespace

Json::Value RsRadiometricCalibrationOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster (DN values)");
    props["output"] = makeOutputParam("output", "Output calibrated raster", "tif");
    props["metadata_path"] = makeStringParam("metadata_path",
        "Path to Landsat *_MTL.txt or Sentinel-2 MTD_*.xml (optional; falls back to embedded GDAL metadata)", "");
    props["unit"] = makeEnumParam("unit", "Output physical unit", s_units, "radiance");

    Json::Value bands = makeIntegerParam("bands", "Optional 1-based band indices (default: all bands)", 0);
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Calibrated raster path", "tif");
    outputs["unit"] = makeStringParam("unit", "Applied output unit", "");
    outputs["bandCount"] = makeIntegerParam("bandCount", "Number of processed bands", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsRadiometricCalibrationOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("radiometric");
    meta["tags"].append("calibration");
    meta["tags"].append("preprocessing");
    meta["purpose"] = "Convert DN to radiance / TOA reflectance / brightness temperature "
                      "before quantitative analysis or multi-temporal comparison.";
    meta["workflowHints"].append("Run after rs:landsat_import / rs:sentinel2_import, "
                                 "before rs:atmospheric_correction or rs:spectral_index.");
    meta["limitations"].append("Coefficients are read from MTL/MTD metadata; "
                               "provide metadata_path for stacked rasters without embedded coefficients.");
    meta["limitations"].append("Brightness temperature requires thermal-band K1/K2 constants.");
    return meta;
}

Json::Value RsRadiometricCalibrationOperator::run(const Json::Value &params,
                                                  RSOperatorContext &context)
{
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

    const std::string metadataPathParam = getString(params, "metadata_path", "");
    const std::string unit = getEnum(params, "unit", s_units, "radiance");

    // Auto-detect the sensor metadata file (MTL/MTD) next to the input when the
    // caller did not supply one — "metadata automatically detected" workflow.
    QString metadataPath = QString::fromStdString(metadataPathParam);
    if (metadataPath.isEmpty()) {
        metadataPath = RadiometricCalibration::autoDetectMetadataFile(
            QString::fromStdString(inputPath));
        if (!metadataPath.isEmpty())
            context.logInfo("Auto-detected calibration metadata: " + metadataPath.toStdString());
    }

    int unitCode = 0; // Radiance
    if (unit == "toa_reflectance") unitCode = 1;
    else if (unit == "brightness_temperature") unitCode = 2;

    // Optional band selection (1-based indices). Empty -> all bands.
    QList<int> bandIndices;
    if (params.isMember("bands") && params["bands"].isArray() && !params["bands"].empty()) {
        for (Json::ArrayIndex i = 0; i < params["bands"].size(); ++i) {
            if (params["bands"][i].isIntegral())
                bandIndices.append(params["bands"][i].asInt());
        }
    }

    context.logInfo("Radiometric calibration (" + unit + ") on " + inputPath);
    context.reportProgress(0.1, "Loading calibration metadata");

    QString errorMessage;
    if (!RadiometricCalibration::processFile(
            QString::fromStdString(inputPath),
            QString::fromStdString(outputPath),
            metadataPath,
            unitCode, bandIndices, &errorMessage,
            [&](double frac, const QString &msg) {
                context.reportProgress(0.1 + 0.85 * frac, msg.toStdString());
                context.throwIfCancelled();
            })) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Radiometric calibration failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Radiometric calibration complete");

    // Report the actual number of processed bands (empty bandIndices = all bands).
    int processedBandCount = static_cast<int>(bandIndices.size());
    if (processedBandCount == 0) {
        GdalDatasetWrapper src;
        if (src.open(QString::fromStdString(inputPath)))
            processedBandCount = src.bandCount();
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["unit"] = unit;
    result["bandCount"] = processedBandCount;
    return result;
}

} // namespace sicnu::operators::rs
