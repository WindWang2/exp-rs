/***************************************************************************
 * rs_change_detection_operator.cpp  —  Change detection RSOperator
 ***************************************************************************/
#include "rs_change_detection_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/change_detection.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {
    "difference", "normalized_difference", "change_mask"
};

} // anonymous namespace

Json::Value RsChangeDetectionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["before"] = makeRasterParam("before", "Before-date raster");
    props["after"] = makeRasterParam("after", "After-date raster");
    props["output"] = makeOutputParam("output", "Output change raster", "tif");
    props["method"] = makeEnumParam("method", "Change detection method", s_methods, "difference");
    props["threshold"] = makeNumberParam("threshold", "Threshold for change_mask", 0.5);
    props["band"] = makeIntegerParam("band", "1-based band for both images (fallback)", 1);
    props["beforeBand"] = makeIntegerParam("beforeBand", "1-based band on before image (overrides band)", 0);
    props["afterBand"] = makeIntegerParam("afterBand", "1-based band on after image (overrides band)", 0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method", "");
    outputs["mean"] = makeNumberParam("mean", "Mean of difference", 0.0);
    outputs["stddev"] = makeNumberParam("stddev", "Stddev of difference", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"before", "after", "output"});
    return root;
}

Json::Value RsChangeDetectionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("change-detection");
    meta["tags"].append("temporal");
    meta["tags"].append("difference");
    meta["purpose"] = "Identify land-cover or surface changes between two dates.";
    meta["prerequisites"].append("Before and after rasters must be co-registered and same size.");
    meta["workflowHints"].append("Apply atmospheric correction to both dates before comparison.");
    return meta;
}

Json::Value RsChangeDetectionOperator::run(const Json::Value& params,
                                           RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string beforePath = requireString(params, "before");
    const std::string afterPath = requireString(params, "after");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(beforePath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Before raster not found: " + beforePath);
    }
    if (!fileExists(afterPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "After raster not found: " + afterPath);
    }

    const std::string method = getEnum(params, "method", s_methods, "difference");
    const float threshold = static_cast<float>(getDouble(params, "threshold", 0.5));
    const int defaultBand = getInt(params, "band", 1);
    const int beforeBandParam = getInt(params, "beforeBand", 0);
    const int afterBandParam = getInt(params, "afterBand", 0);
    const int beforeBand = beforeBandParam > 0 ? beforeBandParam : defaultBand;
    const int afterBand = afterBandParam > 0 ? afterBandParam : defaultBand;

    ensureGdalInit();

    GdalDatasetWrapper beforeDs;
    if (!beforeDs.open(QString::fromStdString(beforePath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open before raster: " + beforePath);
    }

    GdalDatasetWrapper afterDs;
    if (!afterDs.open(QString::fromStdString(afterPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open after raster: " + afterPath);
    }

    const int width = beforeDs.width();
    const int height = beforeDs.height();

    if (afterDs.width() != width || afterDs.height() != height) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Before and after rasters must have the same dimensions");
    }

    if (beforeBand < 1 || beforeBand > beforeDs.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Before band " + std::to_string(beforeBand) + " is out of range");
    }
    if (afterBand < 1 || afterBand > afterDs.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "After band " + std::to_string(afterBand) + " is out of range");
    }

    context.logInfo("Computing " + method + " between " + beforePath + " and " + afterPath);
    context.reportProgress(0.2, "Reading input bands");

    std::vector<float> before, after, out;
    before.resize(static_cast<size_t>(width) * height);
    after.resize(before.size());

    if (!beforeDs.readBandData(beforeBand, before.data(), width, height)) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to read band " + std::to_string(beforeBand) + " from before raster");
    }
    if (!afterDs.readBandData(afterBand, after.data(), width, height)) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to read band " + std::to_string(afterBand) + " from after raster");
    }

    out.resize(before.size());
    context.reportProgress(0.5, "Computing change");

    bool ok = false;
    if (method == "difference") {
        ok = ChangeDetection::difference(before.data(), after.data(), out.data(), out.size());
    } else if (method == "normalized_difference") {
        ok = ChangeDetection::normalizedDifference(before.data(), after.data(), out.data(), out.size());
    } else if (method == "change_mask") {
        std::vector<float> diff;
        diff.resize(before.size());
        ok = ChangeDetection::difference(before.data(), after.data(), diff.data(), diff.size());
        if (ok) {
            std::vector<uint8_t> mask;
            mask.resize(before.size());
            ok = ChangeDetection::changeMask(diff.data(), mask.data(), mask.size(), threshold);
            if (ok) {
                std::transform(mask.begin(), mask.end(), out.begin(),
                               [](uint8_t v) { return static_cast<float>(v); });
            }
        }
    }

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Change detection computation failed");
    }

    context.throwIfCancelled();
    context.reportProgress(0.8, "Writing output raster");

    std::vector<std::vector<float>> bands = {std::move(out)};
    QString errorMessage;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, bands,
                         beforeDs.geoTransform(), beforeDs.projection(), &errorMessage)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + errorMessage.toStdString());
    }

    // Compute statistics for reporting
    ChangeDetection::ChangeStats stats;
    if (method != "change_mask") {
        stats = ChangeDetection::statistics(bands[0].data(), bands[0].size());
    }

    beforeDs.close();
    afterDs.close();

    context.reportProgress(1.0, "Change detection complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["mean"] = stats.mean;
    result["stddev"] = stats.stddev;
    return result;
}

} // namespace sicnu::operators::rs
