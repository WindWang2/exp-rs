/***************************************************************************
 * rs_post_classification_change_operator.cpp  —  Post-classification change
 ***************************************************************************/
#include "rs_post_classification_change_operator.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/post_classification.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"

#include <QString>

#include <gdal.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kBlockRows = 256;

constexpr uint32_t kMaxUInt16Code = 65535u; // NoData code for the change map

/// Maximum class count representable as a UInt16 transition code
/// (code = from * classCount + to must fit).
constexpr int kMaxClassCount = 255;

} // anonymous namespace

Json::Value RsPostClassificationChangeOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["before"] = makeRasterParam("before", "Before-date thematic raster (classification)");
    props["after"] = makeRasterParam("after", "After-date thematic raster (classification)");
    props["output"] = makeOutputParam("output", "Change-type map (UInt16: before * classCount + after)", "tif");
    props["band"] = makeIntegerParam("band", "1-based class band on both rasters (fallback)", 1);
    props["beforeBand"] = makeIntegerParam("beforeBand", "1-based class band on before (overrides band)", 0);
    props["afterBand"] = makeIntegerParam("afterBand", "1-based class band on after (overrides band)", 0);
    props["class_count"] = makeIntegerParam("class_count", "Number of classes (0 = auto from max observed + 1, max 255)", 0);
    props["class_labels"] = makeStringParam("class_labels", "Optional class names (JSON array of strings; length = classCount)");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Change-type map path");
    outputs["classCount"] = makeIntegerParam("classCount", "Effective class count", 0);
    outputs["transitionMatrix"] = makeStringParam("transitionMatrix", "classCount x classCount counts (row = before, column = after)", "");
    outputs["fromTotals"] = makeStringParam("fromTotals", "Per-before-class totals", "");
    outputs["toTotals"] = makeStringParam("toTotals", "Per-after-class totals", "");
    outputs["netChange"] = makeStringParam("netChange", "toTotals - fromTotals per class", "");
    outputs["changedPixels"] = makeIntegerParam("changedPixels", "Pixels whose class changed", 0);
    outputs["unchangedPixels"] = makeIntegerParam("unchangedPixels", "Pixels whose class stayed", 0);
    outputs["totalPixels"] = makeIntegerParam("totalPixels", "Evaluated (valid) pixel count", 0);
    outputs["changedPercent"] = makeNumberParam("changedPercent", "Percentage of evaluated pixels that changed", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"before", "after", "output"});
    return root;
}

Json::Value RsPostClassificationChangeOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("change-detection");
    meta["tags"].append("temporal");
    meta["tags"].append("classification");
    meta["tags"].append("transition-matrix");
    meta["purpose"] = "Post-classification comparison: per-class transition matrix, "
                      "gains/losses, and a change-type map from two thematic rasters.";
    meta["prerequisites"].append("Both inputs are thematic rasters with a shared class "
                                 "coding; before/after grids must be aligned (grid "
                                 "compatibility is preflighted).");
    meta["workflowHints"].append("Run rs:supervised_classification on both dates, then "
                                 "compare the two classified rasters here.");
    meta["limitations"].append("Change-type codes are UInt16: class_count must be <= 255 "
                               "so before * classCount + after fits; auto class_count "
                               "derives from the maximum observed class + 1.");
    return meta;
}

Json::Value RsPostClassificationChangeOperator::executionEstimate() const {
    // MultiPassStreaming: two passes over full-width x kBlockRows (256) row
    // blocks. Typical 1024-wide input holds 3 float block buffers
    // (before/after/codes) plus the classCount x classCount transition
    // matrix (<= 255^2 x 8 B), ~3.7 MiB -> 4 MiB.
    Json::Value estimate(Json::objectValue);
    estimate["tileWidth"] = 0;         // blocks span the full scanline width (auto)
    estimate["tileHeight"] = 256;      // matches kBlockRows
    estimate["estimatedRamBytes"] = 4 * 1024 * 1024; // ~4 MiB
    return estimate;
}

Json::Value RsPostClassificationChangeOperator::run(const Json::Value& params,
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

    const int defaultBand = getInt(params, "band", 1);
    const int beforeBandParam = getInt(params, "beforeBand", 0);
    const int afterBandParam = getInt(params, "afterBand", 0);
    const int beforeBand = beforeBandParam > 0 ? beforeBandParam : defaultBand;
    const int afterBand = afterBandParam > 0 ? afterBandParam : defaultBand;
    const int classCountParam = getInt(params, "class_count", 0);
    if (classCountParam < 0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "class_count must be >= 0 (0 = auto)");
    }
    const std::vector<std::string> classLabels = getStringArray(params, "class_labels");

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

    // Shared pixel-grid preflight (CRS, resolution, origin, extent).
    const sicnu::data::GridCompatReport gridReport =
        sicnu::data::compareGrids(sicnu::processing::gridFromDataset(beforeDs),
                                  sicnu::processing::gridFromDataset(afterDs));
    for (const sicnu::data::GridCompatIssue& issue : gridReport.issues) {
        if (issue.blocking) {
            throw RSOperatorError(ErrorCode::InvalidInputData, issue.message.toStdString());
        }
        context.logWarning(issue.message.toStdString());
    }
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
    if (classCountParam > kMaxClassCount) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "class_count must be <= " + std::to_string(kMaxClassCount) +
                                  " so change codes fit a UInt16 raster");
    }

    bool beforeHasNoData = false, afterHasNoData = false;
    const float beforeNoData = static_cast<float>(beforeDs.bandNoDataValue(beforeBand, &beforeHasNoData));
    const float afterNoData = static_cast<float>(afterDs.bandNoDataValue(afterBand, &afterHasNoData));

    context.logInfo("Comparing classifications " + beforePath + " -> " + afterPath);

    // ---- Pass 1: count transitions (block streaming) and probe class range.
    std::unordered_map<uint64_t, uint64_t> transitions;
    int maxSeen = -1;
    std::vector<float> beforeBuf(static_cast<size_t>(width) * kBlockRows);
    std::vector<float> afterBuf(static_cast<size_t>(width) * kBlockRows);
    std::vector<float> codes(static_cast<size_t>(width) * kBlockRows);

    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (int y0 = 0; y0 < height; y0 += kBlockRows) {
        const int blockH = (std::min)(kBlockRows, height - y0);
        const size_t blockPixels = static_cast<size_t>(width) * blockH;
        if (!beforeDs.readBandWindow(beforeBand, 0, y0, width, blockH, beforeBuf.data())) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read before band " + std::to_string(beforeBand));
        }
        if (!afterDs.readBandWindow(afterBand, 0, y0, width, blockH, afterBuf.data())) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read after band " + std::to_string(afterBand));
        }
        for (size_t i = 0; i < blockPixels; ++i) {
            const float bv = beforeBuf[i];
            const float av = afterBuf[i];
            const bool invalid = std::isnan(bv) || std::isnan(av) ||
                                 (beforeHasNoData && bv == beforeNoData) ||
                                 (afterHasNoData && av == afterNoData);
            if (invalid)
                continue;
            const int32_t from = static_cast<int32_t>(std::llround(bv));
            const int32_t to = static_cast<int32_t>(std::llround(av));
            if (from < 0 || to < 0) {
                throw RSOperatorError(
                    ErrorCode::InvalidInputData,
                    "Negative class value " + std::to_string(from) + "/" +
                        std::to_string(to) + " found; classifications must use classes >= 0");
            }
            maxSeen = (std::max)(maxSeen, (std::max)(from, to));
            ++transitions[(static_cast<uint64_t>(from) << 32) | static_cast<uint32_t>(to)];
        }
        // #700: bound the sparse map PER BLOCK. Feeding a continuous raster
        // used to insert millions of map nodes before pass 1 ended and only
        // then failed the class-range check; the number of distinct classes
        // is known as soon as a block is read, so bail out early.
        if (maxSeen >= kMaxClassCount ||
            (classCountParam > 0 && maxSeen >= classCountParam)) {
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Observed class " + std::to_string(maxSeen) +
                    (maxSeen >= kMaxClassCount
                         ? " exceeds the UInt16 change-code limit (" +
                               std::to_string(kMaxClassCount - 1) +
                               "); the input does not look like a thematic classification"
                         : " but class_count is " + std::to_string(classCountParam) +
                               "; increase class_count or leave it 0 (auto)"));
        }
        context.throwIfCancelled();
        context.reportProgress(0.4 * static_cast<double>(y0 + blockH) / height,
                               "Counting class transitions");
    }

    const int classCount = classCountParam > 0 ? classCountParam : maxSeen + 1;
    if (maxSeen >= classCount) {
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "Observed class " + std::to_string(maxSeen) + " but class_count is " +
                std::to_string(classCountParam) + "; increase class_count or leave it 0 (auto)");
    }
    // The change-type code (from * classCount + to) must fit UInt16 and stay
    // below the 65535 NoData sentinel: classCount is capped at 255 even in
    // auto mode (a stray large band value must not OOM the matrix or corrupt
    // the codes).
    if (classCount > kMaxClassCount) {
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "Observed maximum class " + std::to_string(maxSeen) +
                " exceeds the UInt16 change-code limit (" +
                std::to_string(kMaxClassCount - 1) +
                "); the input does not look like a thematic classification");
    }
    if (classCount < 1) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "No valid pixels found; nothing to compare");
    }
    if (!classLabels.empty() && static_cast<int>(classLabels.size()) != classCount) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "class_labels length " + std::to_string(classLabels.size()) +
                                  " does not match class_count " + std::to_string(classCount));
    }

    // Materialize the row-major transition matrix from the sparse map.
    std::vector<uint64_t> matrix(static_cast<size_t>(classCount) * classCount, 0);
    for (const auto& [key, count] : transitions) {
        const int32_t from = static_cast<int32_t>(key >> 32);
        const int32_t to = static_cast<int32_t>(key & 0xffffffffu);
        matrix[static_cast<size_t>(from) * classCount + to] += count;
    }
    std::vector<uint64_t> fromTotals, toTotals;
    TransitionMatrix::marginals(matrix, classCount, fromTotals, toTotals);

    uint64_t unchanged = 0;
    for (int c = 0; c < classCount; ++c)
        unchanged += matrix[static_cast<size_t>(c) * classCount + c];
    uint64_t validPixels = 0;
    for (uint64_t v : fromTotals)
        validPixels += v;
    const uint64_t changed = validPixels - unchanged;

    // ---- Pass 2: write the change-type map (UInt16 code = from*C + to).
    context.throwIfCancelled();
    context.reportProgress(0.5, "Writing change-type map");
    GdalDatasetWrapper out;
    QString errorMessage;
    if (!out.create(QString::fromStdString(outputPath), width, height, 1,
                    static_cast<int>(GDT_UInt16), beforeDs.geoTransform(),
                    beforeDs.projection(), &errorMessage)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output raster: " + errorMessage.toStdString());
    }
    for (int y0 = 0; y0 < height; y0 += kBlockRows) {
        const int blockH = (std::min)(kBlockRows, height - y0);
        const size_t blockPixels = static_cast<size_t>(width) * blockH;
        if (!beforeDs.readBandWindow(beforeBand, 0, y0, width, blockH, beforeBuf.data())) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to re-read before band");
        }
        if (!afterDs.readBandWindow(afterBand, 0, y0, width, blockH, afterBuf.data())) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to re-read after band");
        }
        std::fill(codes.begin(), codes.begin() + static_cast<ptrdiff_t>(blockPixels),
                  static_cast<float>(kMaxUInt16Code));
        for (size_t i = 0; i < blockPixels; ++i) {
            const float bv = beforeBuf[i];
            const float av = afterBuf[i];
            const bool invalid = std::isnan(bv) || std::isnan(av) ||
                                 (beforeHasNoData && bv == beforeNoData) ||
                                 (afterHasNoData && av == afterNoData);
            if (invalid)
                continue;
            const int32_t from = static_cast<int32_t>(std::llround(bv));
            const int32_t to = static_cast<int32_t>(std::llround(av));
            codes[i] = static_cast<float>(from * classCount + to);
        }
        if (!out.writeBandWindow(1, 0, y0, width, blockH, codes.data())) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to write change-type map");
        }
        context.throwIfCancelled();
        context.reportProgress(0.5 + 0.5 * static_cast<double>(y0 + blockH) / height,
                               "Writing change-type map");
    }
    GDALRasterBandH outBand = GDALGetRasterBand(out.dataset(), 1);
    if (outBand)
        GDALSetRasterNoDataValue(outBand, static_cast<double>(kMaxUInt16Code));
    GDALSetMetadataItem(out.dataset(), "SICNU_CHANGE_METHOD", "post_classification", nullptr);
    GDALSetMetadataItem(out.dataset(), "SICNU_CHANGE_CLASS_COUNT",
                        std::to_string(classCount).c_str(), nullptr);
    out.close();
    // Deferred flush/trailer errors only surface at close (ADR 0105).
    if (CPLGetLastErrorType() != CE_None) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to finalize output raster: " + outputPath);
    }

    context.reportProgress(1.0, "Post-classification comparison complete");

    Json::Value matrixJson(Json::arrayValue);
    for (int from = 0; from < classCount; ++from) {
        Json::Value row(Json::arrayValue);
        for (int to = 0; to < classCount; ++to)
            row.append(static_cast<Json::UInt64>(matrix[static_cast<size_t>(from) * classCount + to]));
        matrixJson.append(row);
    }
    auto toJsonArray = [](const std::vector<uint64_t>& values) {
        Json::Value arr(Json::arrayValue);
        for (uint64_t v : values)
            arr.append(static_cast<Json::UInt64>(v));
        return arr;
    };
    Json::Value netChange(Json::arrayValue);
    for (int c = 0; c < classCount; ++c)
        netChange.append(static_cast<Json::Int64>(toTotals[c]) - static_cast<Json::Int64>(fromTotals[c]));

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["classCount"] = classCount;
    result["transitionMatrix"] = matrixJson;
    result["fromTotals"] = toJsonArray(fromTotals);
    result["toTotals"] = toJsonArray(toTotals);
    result["netChange"] = netChange;
    if (!classLabels.empty()) {
        Json::Value labels(Json::arrayValue);
        for (const std::string& label : classLabels)
            labels.append(label);
        result["classLabels"] = labels;
    }
    result["changedPixels"] = static_cast<Json::UInt64>(changed);
    result["unchangedPixels"] = static_cast<Json::UInt64>(unchanged);
    result["totalPixels"] = static_cast<Json::UInt64>(validPixels);
    result["changedPercent"] = validPixels == 0
        ? 0.0
        : 100.0 * static_cast<double>(changed) / static_cast<double>(validPixels);
    return result;
}

} // namespace sicnu::operators::rs
