/***************************************************************************
 * rs_sar_change_operator.cpp — SAR change detection (log-ratio -> mask)
 ***************************************************************************/
#include "rs_sar_change_operator.h"

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
#include "rs_change_streaming.h"

#include <QString>

#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_threshold_methods = {
    "manual", "otsu", "percentile", "statistical"
};

const std::vector<std::string> s_cleanups = {
    "none", "erode", "dilate", "open", "close"
};

const std::vector<std::string> s_domains = { "linear_power", "db" };

Json::Value makeSarInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "sar";
    return c;
}

} // anonymous namespace

Json::Value RsSarChangeOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["inputA"] = makeRasterParam("inputA", "First SAR scene (before, co-registered)");
    props["inputA"]["x-rs-contract"] = makeSarInputContract();
    props["inputB"] = makeRasterParam("inputB", "Second SAR scene (after, co-registered)");
    props["inputB"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output binary change mask raster (UInt8)", "tif");
    props["bandA"] = makeIntegerParam("bandA", "1-based band on inputA", 1);
    props["bandB"] = makeIntegerParam("bandB", "1-based band on inputB", 1);
    props["inputDomain"] = makeEnumParam("inputDomain",
                                        "Numeric domain of both inputs. Explicit inputDomain=db "
                                        "wins over declared SICNU_SAR_DOMAIN; a declared dB domain "
                                        "with the default linear_power refuses rather than nested-logging",
                                        s_domains, "linear_power");
    props["thresholdMethod"] = makeEnumParam("thresholdMethod", "Threshold strategy", s_threshold_methods, "otsu");
    props["threshold"] = makeNumberParam("threshold", "Manual threshold in dB (thresholdMethod=manual): pixels with dB change >= threshold are changed", 0.0);
    props["percentile"] = makeNumberParam("percentile", "Percentile for thresholdMethod=percentile (0-100)", 90.0);
    props["statisticalK"] = makeNumberParam("statisticalK", "Stddev multiplier for thresholdMethod=statistical (mean + k*stddev)", 2.0);
    props["cleanup"] = makeEnumParam("cleanup", "Morphological mask cleanup", s_cleanups, "none");
    props["cleanupIterations"] = makeIntegerParam("cleanupIterations", "Cleanup iterations", 1);
    props["minAreaPixels"] = makeIntegerParam("minAreaPixels", "Minimum mapping unit: drop components smaller than this (pixels); 0 disables", 0);
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the outputs", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the outputs", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output change mask raster path");
    outputs["thresholdUsed"] = makeNumberParam("thresholdUsed", "Effective threshold (dB)", 0.0);
    outputs["changedPixels"] = makeIntegerParam("changedPixels", "Pixels at/above threshold", 0);
    outputs["evaluatedPixels"] = makeIntegerParam("evaluatedPixels", "Evaluated pixels", 0);
    outputs["changedPercent"] = makeNumberParam("changedPercent", "Changed pixel percentage", 0.0);
    outputs["magnitudeDomain"] = makeStringParam("magnitudeDomain", "Numeric domain of the magnitude raster (always dB)");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"inputA", "inputB", "output"});
    return root;
}

Json::Value RsSarChangeOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("change");
    meta["tags"].append("change-detection");
    meta["purpose"] = "SAR change detection: log-ratio magnitude (dB) between two "
                      "co-registered scenes thresholded into a binary change mask.";
    meta["workflowHints"].append("Calibrate both scenes first: rs:sar_calibrate -> rs:sar_change.");
    meta["workflowHints"].append("thresholdMethod=manual marks pixels with dB change >= "
                                 "threshold (default 0 dB); otsu / percentile / "
                                 "statistical adapt the threshold to the data.");
    meta["limitations"].append("Incoherent change only — no coherent/interferometric "
                               "phase analysis.");
    meta["limitations"].append("Scenes must share CRS, pixel size, origin and extent; "
                               "no hidden resampling is applied.");
    meta["limitations"].append("If either input declares SICNU_SAR_DOMAIN=db and "
                               "inputDomain is left at linear_power, the operator refuses "
                               "(pass inputDomain=db to convert, or convert first).");
    meta["facadeOf"] = "change_detection";
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarChangeOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 6ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarChangeOperator::run(const Json::Value& params,
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
    const std::string inputDomainStr = getEnum(params, "inputDomain", s_domains, "linear_power");

    const int bandA = getInt(params, "bandA", 1);
    const int bandB = getInt(params, "bandB", 1);
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );

    // Stage 1: log-difference magnitude (dB) streamed into a work-dir
    // temporary; the mask stage re-reads it.
    context.reportProgress(0.05, "Computing SAR log-ratio magnitude");
    const std::string tempMagPath = context.tempPath("sar_change_mag.tif");

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
    sicnu::sar::RatioParams ratioParams;
    ratioParams.output = sicnu::sar::RatioOutput::LogDifference;
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

    GdalStreamingOutput mag(QString::fromStdString(tempMagPath), srcA.width(), srcA.height(),
                            1, GDT_Float32, srcA.geoTransform(), srcA.projection());
    if (!mag.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create temporary magnitude raster");
    }
    mag.setNoDataValue(std::numeric_limits<float>::quiet_NaN());

    context.throwIfCancelled();
    const bool ok = sicnu::sar::ratioRaster(srcA, bandA, srcB, bandB, ratioParams,
                                            nodataA, nodataB, mag, 256,
                                            polarizations, sensor);
    if (!ok) {
        mag.abandon();
        throw RSOperatorError(ErrorCode::GdalError,
                              "SAR change magnitude failed while streaming");
    }
    QString error;
    if (!mag.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to finalize magnitude raster: " + error.toStdString());
    }

    // Stage 2: threshold the dB magnitude into the change mask (values at or
    // above the threshold are changed — the manual dB threshold passes
    // through unchanged).
    context.reportProgress(0.5, "Thresholding SAR change magnitude");
    ChangeStreamingOptions opts;
    opts.makeMask = true;
    opts.threshold = static_cast<float>(getDouble(params, "threshold", 0.0));
    opts.thresholdMethod = getEnum(params, "thresholdMethod", s_threshold_methods, "otsu");
    opts.percentile = getDouble(params, "percentile", 90.0);
    opts.statisticalK = getDouble(params, "statisticalK", 2.0);
    opts.cleanup = getEnum(params, "cleanup", s_cleanups, "none");
    opts.cleanupIterations = getInt(params, "cleanupIterations", 1);
    opts.minAreaPixels = getInt(params, "minAreaPixels", 0);
    opts.outputPath = outputPath;
    opts.methodLabel = "sar_change";

    const MaskDerivation derived = thresholdRasterToMask(tempMagPath, opts, context);

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["thresholdUsed"] = derived.thresholdUsed;
    result["changedPixels"] = static_cast<Json::UInt64>(derived.changed);
    result["evaluatedPixels"] = static_cast<Json::UInt64>(derived.evaluated);
    result["changedPercent"] = derived.evaluated == 0
        ? 0.0
        : 100.0 * static_cast<double>(derived.changed) / static_cast<double>(derived.evaluated);
    result["magnitudeDomain"] = "db";
    context.reportProgress(1.0, "SAR change detection complete");
    return result;
}

} // namespace sicnu::operators::rs
