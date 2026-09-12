/***************************************************************************
 * rs_sar_ratio_operator.cpp — SAR ratio / log-ratio pair metric (Platform 3.0)
 ***************************************************************************/
#include "rs_sar_ratio_operator.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/nodata_utils.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_ratio.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
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
    props["inputDomain"] = makeEnumParam("inputDomain",
                                        "Numeric domain of both inputs. Explicit inputDomain=db "
                                        "wins over declared SICNU_SAR_DOMAIN; a declared dB domain "
                                        "with the default linear_power refuses rather than nested-logging",
                                        s_domains, "linear_power");
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
    meta["limitations"].append("Scenes must share CRS, pixel size, origin and extent; "
                               "no hidden resampling is applied.");
    meta["limitations"].append("If either input declares SICNU_SAR_DOMAIN=db and "
                               "inputDomain is left at linear_power, the operator refuses "
                               "(pass inputDomain=db to convert, or convert first).");
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

    // Shared pixel-grid preflight (CRS, resolution, origin alignment, extent)
    // before any pixel comparison. Two unreferenced rasters are not spatially
    // comparable and pass as compatible; the dimension check below remains the
    // fallback for them.
    const sicnu::data::GridCompatReport gridReport =
        sicnu::data::compareGrids(sicnu::processing::gridFromDataset(srcA),
                                  sicnu::processing::gridFromDataset(srcB));
    for (const sicnu::data::GridCompatIssue& issue : gridReport.issues) {
        if (issue.blocking) {
            throw RSOperatorError(ErrorCode::InvalidInputData, issue.message.toStdString());
        }
        context.logWarning(issue.message.toStdString());
    }
    if (srcA.width() != srcB.width() || srcA.height() != srcB.height()) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Input dimensions do not match: inputA is " +
                                  std::to_string(srcA.width()) + "x" +
                                  std::to_string(srcA.height()) + ", inputB is " +
                                  std::to_string(srcB.width()) + "x" +
                                  std::to_string(srcB.height()) +
                                  " (co-registered scenes required)");
    }

    // Domain resolution: explicit inputDomain > declared SICNU_SAR_DOMAIN >
    // linear. A declared dB domain with the default linear_power inputDomain
    // is a typed refusal (do not silently nested-log).
    const QString declaredA = sicnu::sar::readDomain(srcA);
    const QString declaredB = sicnu::sar::readDomain(srcB);
    if (inputDomainStr == "db") {
        ratioParams.inputIsDb = true;
    } else if (declaredA == QLatin1String("db") || declaredB == QLatin1String("db")) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "input declares SICNU_SAR_DOMAIN=db; convert with "
                              "rs:sar_backscatter (or rs:sar_calibrate) first, or "
                              "pass inputDomain=db");
    } else {
        ratioParams.inputIsDb = false;
    }

    // Declared sentinels on the analysis bands (NaN when undeclared).
    const float nodataA = sicnu::rs::bandNoDataSentinel(srcA, bandA);
    const float nodataB = sicnu::rs::bandNoDataSentinel(srcB, bandB);

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
