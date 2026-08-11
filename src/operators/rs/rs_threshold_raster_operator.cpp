/***************************************************************************
 * rs_threshold_raster_operator.cpp  —  Reusable threshold / mask operator
 ***************************************************************************/
#include "rs_threshold_raster_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "rs_change_streaming.h"

#include <QString>

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

} // anonymous namespace

Json::Value RsThresholdRasterOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input single-band raster to threshold");
    props["output"] = makeOutputParam("output", "Output binary mask raster (UInt8)", "tif");
    props["thresholdMethod"] = makeEnumParam("thresholdMethod", "Threshold strategy", s_threshold_methods, "manual");
    props["threshold"] = makeNumberParam("threshold", "Manual threshold value", 0.5);
    props["percentile"] = makeNumberParam("percentile", "Percentile for thresholdMethod=percentile (0-100)", 90.0);
    props["statisticalK"] = makeNumberParam("statisticalK", "Stddev multiplier for thresholdMethod=statistical (mean + k*stddev)", 2.0);
    props["cleanup"] = makeEnumParam("cleanup", "Morphological mask cleanup", s_cleanups, "none");
    props["cleanupIterations"] = makeIntegerParam("cleanupIterations", "Cleanup iterations", 1);
    props["minAreaPixels"] = makeIntegerParam("minAreaPixels", "Minimum mapping unit: drop components smaller than this (pixels); 0 disables", 0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output mask raster path");
    outputs["thresholdUsed"] = makeNumberParam("thresholdUsed", "Effective threshold", 0.0);
    outputs["maskedPixels"] = makeIntegerParam("maskedPixels", "Pixels at/above threshold", 0);
    outputs["totalPixels"] = makeIntegerParam("totalPixels", "Evaluated pixels", 0);
    outputs["maskedPercent"] = makeNumberParam("maskedPercent", "Masked pixel percentage", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsThresholdRasterOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("threshold");
    meta["tags"].append("masking");
    meta["tags"].append("segmentation");
    meta["purpose"] = "Convert a score/magnitude raster into a binary mask with a "
                      "manual, Otsu, percentile or statistical threshold.";
    meta["prerequisites"].append("Input must be a single-band raster (e.g. a change "
                                 "magnitude from rs:change_difference / rs:change_cva).");
    meta["workflowHints"].append("Chain after a change metric primitive to build a "
                                 "change mask: change_difference -> threshold_raster.");
    meta["workflowHints"].append("255 marks NoData pixels in the output mask.");
    meta["limitations"].append("Multi-band input uses band 1 only.");
    meta["facadeOf"] = "change_detection";
    return meta;
}

Json::Value RsThresholdRasterOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    // Tile buffer + 65536-bin histogram; the mask path materializes a
    // full-resolution Byte mask (O(width*height)) for cleanup/MMU, so large
    // rasters additionally hold ~1 byte/pixel (see the 2^31-pixel guard in
    // the kernel).
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * 256ULL * 256ULL * 4ULL + 65536ULL * 8ULL );
    return est;
}

Json::Value RsThresholdRasterOperator::run(const Json::Value& params,
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

    ChangeStreamingOptions opts;
    opts.makeMask = true;
    opts.threshold = static_cast<float>(getDouble(params, "threshold", 0.5));
    opts.thresholdMethod = getEnum(params, "thresholdMethod", s_threshold_methods, "manual");
    opts.percentile = getDouble(params, "percentile", 90.0);
    opts.statisticalK = getDouble(params, "statisticalK", 2.0);
    opts.cleanup = getEnum(params, "cleanup", s_cleanups, "none");
    opts.cleanupIterations = getInt(params, "cleanupIterations", 1);
    opts.minAreaPixels = getInt(params, "minAreaPixels", 0);
    opts.outputPath = outputPath;
    opts.methodLabel = "threshold";

    const MaskDerivation derived = thresholdRasterToMask(inputPath, opts, context);

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["thresholdUsed"] = derived.thresholdUsed;
    result["maskedPixels"] = static_cast<Json::UInt64>(derived.changed);
    result["totalPixels"] = static_cast<Json::UInt64>(derived.evaluated);
    result["maskedPercent"] = derived.evaluated == 0
        ? 0.0
        : 100.0 * static_cast<double>(derived.changed) / static_cast<double>(derived.evaluated);
    context.reportProgress(1.0, "Threshold complete");
    return result;
}

} // namespace sicnu::operators::rs
