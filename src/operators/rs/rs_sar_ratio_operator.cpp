/***************************************************************************
 * rs_sar_ratio_operator.cpp — SAR ratio / log-ratio pair metric (Platform 3.0)
 ***************************************************************************/
#include "rs_sar_ratio_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_ratio.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_output_types = { "ratio", "log_ratio", "log_difference" };

const std::vector<std::string> s_domains = { "linear_power", "db" };

Json::Value makeSarInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "sar";
    return c;
}

} // anonymous namespace

Json::Value RsSarRatioOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["inputA"] = makeRasterParam("inputA", "First SAR scene (co-registered)");
    props["inputA"]["x-rs-contract"] = makeSarInputContract();
    props["inputB"] = makeRasterParam("inputB", "Second SAR scene (co-registered)");
    props["inputB"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output pair-metric raster (Float32)", "tif");
    props["bandA"] = makeIntegerParam("bandA", "1-based band on inputA", 1);
    props["bandB"] = makeIntegerParam("bandB", "1-based band on inputB", 1);
    props["outputType"] = makeEnumParam("outputType",
                                        "Pair metric: ratio (linear power quotient A/B), "
                                        "log_ratio (10·log10(A/B), dB), log_difference "
                                        "(|ΔdB| change magnitude)",
                                        s_output_types, "log_ratio");
    props["inputDomain"] = makeEnumParam("inputDomain", "Numeric domain of both inputs", s_domains, "linear_power");
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Pair-metric raster path");
    outputs["outputType"] = makeStringParam("outputType", "Pair metric written to the output");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"inputA", "inputB", "output"});
    return root;
}

Json::Value RsSarRatioOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("change");
    meta["tags"].append("ratio");
    meta["purpose"] = "SAR incoherent change pair metric: ratio, log-ratio or "
                      "absolute log-difference magnitude between two co-registered "
                      "scenes.";
    meta["workflowHints"].append("Calibrate both scenes first: rs:sar_calibrate -> rs:sar_ratio.");
    meta["workflowHints"].append("Feed a log_difference output to rs:threshold_raster "
                                 "for a binary change mask.");
    Json::Value units(Json::objectValue);
    units["ratio"] = "Linear power quotient A/B (dimensionless).";
    units["log_ratio"] = "dB, 10·log10(A/B).";
    units["log_difference"] = "|ΔdB| (absolute dB change magnitude).";
    meta["units"] = units;
    meta["limitations"].append("Scenes must be co-registered; no hidden resampling is applied.");
    meta["limitations"].append("Nonpositive power becomes NoData (NaN) for the "
                               "log-domain outputs; B == 0 is NoData for ratio.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarRatioOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 4ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarRatioOperator::run(const Json::Value& params,
                                    RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string pathA = requireString(params, "inputA");
    const std::string pathB = requireString(params, "inputB");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(pathA)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + pathA);
    }
    if (!fileExists(pathB)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + pathB);
    }

    const int bandA = getInt(params, "bandA", 1);
    const int bandB = getInt(params, "bandB", 1);
    const std::string outputTypeStr = getEnum(params, "outputType", s_output_types, "log_ratio");
    const std::string inputDomainStr = getEnum(params, "inputDomain", s_domains, "linear_power");
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );

    sicnu::sar::RatioParams ratioParams;
    ratioParams.output = sicnu::sar::RatioOutput::LogRatio;
    if (outputTypeStr == "ratio") {
        ratioParams.output = sicnu::sar::RatioOutput::Ratio;
    } else if (outputTypeStr == "log_difference") {
        ratioParams.output = sicnu::sar::RatioOutput::LogDifference;
    }
    ratioParams.inputIsDb = inputDomainStr == "db";

    GdalDatasetWrapper srcA;
    if (!srcA.open(QString::fromStdString(pathA))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + pathA);
    }
    GdalDatasetWrapper srcB;
    if (!srcB.open(QString::fromStdString(pathB))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + pathB);
    }

    if (bandA < 1 || bandA > srcA.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "bandA out of range: " + std::to_string(bandA));
    }
    if (bandB < 1 || bandB > srcB.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "bandB out of range: " + std::to_string(bandB));
    }
    if (srcA.width() != srcB.width() || srcA.height() != srcB.height()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Input dimensions do not match: inputA is " +
                                  std::to_string(srcA.width()) + "x" +
                                  std::to_string(srcA.height()) + ", inputB is " +
                                  std::to_string(srcB.width()) + "x" +
                                  std::to_string(srcB.height()) +
                                  " (co-registered scenes required)");
    }

    // Declared sentinels on the analysis bands (NaN when undeclared).
    bool hasNodataA = false;
    const double nodataRawA = srcA.bandNoDataValue(bandA, &hasNodataA);
    const float nodataA = hasNodataA ? static_cast<float>(nodataRawA)
                                     : std::numeric_limits<float>::quiet_NaN();
    bool hasNodataB = false;
    const double nodataRawB = srcB.bandNoDataValue(bandB, &hasNodataB);
    const float nodataB = hasNodataB ? static_cast<float>(nodataRawB)
                                     : std::numeric_limits<float>::quiet_NaN();

    context.reportProgress(0.05, "Computing SAR pair metric");
    GdalStreamingOutput dst(QString::fromStdString(outputPath), srcA.width(), srcA.height(),
                            1, GDT_Float32, srcA.geoTransform(), srcA.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setNoDataValue(std::numeric_limits<float>::quiet_NaN());

    context.throwIfCancelled();
    const bool ok = sicnu::sar::ratioRaster(srcA, bandA, srcB, bandB, ratioParams,
                                            nodataA, nodataB, dst, 256,
                                            polarizations, sensor);
    if (!ok) {
        dst.abandon();
        throw RSOperatorError(ErrorCode::GdalError, "SAR ratio failed while streaming");
    }

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["outputType"] = outputTypeStr;
    result["bands"] = 1;
    context.reportProgress(1.0, "SAR pair metric complete");
    return result;
}

} // namespace sicnu::operators::rs
