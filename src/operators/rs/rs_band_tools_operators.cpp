/***************************************************************************
 * rs_band_tools_operators.cpp — thin JSON adapters over BandTools.
 ***************************************************************************/
#include "rs_band_tools_operators.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/band_tools.h"

#include <QVector>
#include <QString>

namespace sicnu::operators::rs {

using namespace params;

// ---------------------------------------------------------------------------
// rs:band_ratio
// ---------------------------------------------------------------------------

Json::Value RsBandRatioOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output raster", "tif");
    props["mode"] = makeEnumParam("mode", "Transform mode",
                                  {"ratio", "ihs"}, "ratio");
    props["numeratorBand"] = makeIntegerParam("numeratorBand",
                                              "Ratio numerator band (1-based)", 1);
    props["denominatorBand"] = makeIntegerParam("denominatorBand",
                                                "Ratio denominator band (1-based)", 2);
    props["redBand"] = makeIntegerParam("redBand", "IHS red band (1-based)", 1);
    props["greenBand"] = makeIntegerParam("greenBand", "IHS green band (1-based)", 2);
    props["blueBand"] = makeIntegerParam("blueBand", "IHS blue band (1-based)", 3);
    setRange(props["numeratorBand"], 1, 10000);
    setRange(props["denominatorBand"], 1, 10000);
    setRange(props["redBand"], 1, 10000);
    setRange(props["greenBand"], 1, 10000);
    setRange(props["blueBand"], 1, 10000);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Output raster path");
    outputs["bands"] = makeIntegerParam("bands", "Output band count", 1);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    stampDeterminismGrade(root, "bit-exact");
    return root;
}

Json::Value RsBandRatioOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("band-ratio");
    meta["tags"].append("ihs");
    meta["tags"].append("enhancement");
    meta["purpose"] = "Compute band ratios or RGB-to-IHS color decomposition.";
    meta["workflowHints"].append("ratio mode needs distinct numerator/denominator bands; ihs mode needs R/G/B band numbers.");
    meta["limitations"].append("IHS masks NoData/sentinel pixels to NaN in all components.");
    return meta;
}

Json::Value RsBandRatioOperator::executionEstimate() const {
    // Streaming pair/triple tiles: O(tile) — a small constant frame budget.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 4194304;
    return est;
}

Json::Value RsBandRatioOperator::run(const Json::Value& params,
                                     RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string mode = getEnum(params, "mode", {"ratio", "ihs"}, "ratio");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    context.throwIfCancelled();
    context.reportProgress(0.1, mode == "ihs" ? "Running IHS transform"
                                              : "Running band ratio");

    QString errorMessage;
    bool ok = false;
    const auto cancelled = [&context] { return context.isCancelled(); };
    if (mode == "ihs") {
        ok = BandTools::processRgbToIhsFile(
            QString::fromStdString(inputPath), QString::fromStdString(outputPath),
            getInt(params, "redBand", 1), getInt(params, "greenBand", 2),
            getInt(params, "blueBand", 3), &errorMessage, cancelled);
    } else {
        const int numerator = getInt(params, "numeratorBand", 1);
        const int denominator = getInt(params, "denominatorBand", 2);
        if (numerator == denominator) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Numerator and denominator bands must differ");
        }
        ok = BandTools::processBandRatioFile(
            QString::fromStdString(inputPath), QString::fromStdString(outputPath),
            numerator, denominator, &errorMessage, cancelled);
    }
    if (!ok) {
        if (cancelled()) {
            throw RSOperatorError(ErrorCode::Cancelled, "Band ratio/IHS cancelled");
        }
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Band ratio/IHS failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["mode"] = mode;
    result["bands"] = mode == "ihs" ? 3 : 1;
    return result;
}

// ---------------------------------------------------------------------------
// rs:extract_bands
// ---------------------------------------------------------------------------

Json::Value RsExtractBandsOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output raster", "tif");
    Json::Value bands(Json::objectValue);
    bands["name"] = "bands";
    bands["type"] = "array";
    bands["description"] = "1-based band numbers to extract, in output order";
    bands["items"] = makeIntegerParam("band", "1-based band number", 1);
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Output raster path");
    outputs["bands"] = makeIntegerParam("bands", "Output band count", 1);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "bands"});
    stampDeterminismGrade(root, "bit-exact");
    return root;
}

Json::Value RsExtractBandsOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("extract");
    meta["tags"].append("bands");
    meta["purpose"] = "Copy selected bands into a new multi-band raster.";
    meta["workflowHints"].append("Bands are 1-based; order in the array is the output order.");
    return meta;
}

Json::Value RsExtractBandsOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 2097152;
    return est;
}

Json::Value RsExtractBandsOperator::run(const Json::Value& params,
                                        RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");

    const Json::Value& bandsJson = params["bands"];
    if (!bandsJson.isArray() || bandsJson.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "bands must be a non-empty array of 1-based band numbers");
    }
    QVector<int> bands;
    for (const Json::Value& b : bandsJson) {
        if (!b.isIntegral()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "bands must contain integer band numbers");
        }
        bands.append(b.asInt());
    }

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    context.throwIfCancelled();
    context.reportProgress(0.1, "Extracting bands");

    QString errorMessage;
    if (!BandTools::processExtractBandsFile(QString::fromStdString(inputPath),
                                            QString::fromStdString(outputPath),
                                            bands, &errorMessage,
                                            [&context] { return context.isCancelled(); })) {
        if (context.isCancelled()) {
            throw RSOperatorError(ErrorCode::Cancelled, "Band extraction cancelled");
        }
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Band extraction failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["bands"] = static_cast<int>(bands.size());
    return result;
}

// ---------------------------------------------------------------------------
// rs:contrast_stretch
// ---------------------------------------------------------------------------

Json::Value RsContrastStretchOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster");
    props["output"] = makeOutputParam("output", "Output raster", "tif");
    props["method"] = makeEnumParam("method",
                                    "Stretch method: linear min-max, percent clip, stddev, histogram equalize, piecewise LUT",
                                    {"linear", "percent_clip", "stddev", "histogram_equalize", "piecewise"},
                                    "linear");
    props["clipPercent"] = makeNumberParam("clipPercent",
                                           "PercentClip: percentage trimmed at each end", 2.0);
    props["stddevK"] = makeNumberParam("stddevK", "StdDev: stretch to mean +/- K * sigma", 2.0);
    Json::Value points(Json::objectValue);
    points["name"] = "piecewisePoints";
    points["type"] = "array";
    points["description"] = "Piecewise control points [[in, out], ...] in [-oo, +oo]";
    props["piecewisePoints"] = points;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Output raster path");
    outputs["bands"] = makeIntegerParam("bands", "Output band count", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    stampDeterminismGrade(root, "bit-exact");
    return root;
}

Json::Value RsContrastStretchOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("contrast");
    meta["tags"].append("stretch");
    meta["tags"].append("enhancement");
    meta["purpose"] = "Enhance per-band contrast with declared-NoData masking.";
    meta["workflowHints"].append("percent_clip uses clipPercent; stddev uses stddevK; piecewise uses piecewisePoints.");
    return meta;
}

Json::Value RsContrastStretchOperator::executionEstimate() const {
    // Two-pass streaming per band: O(tile) plus histogram/percentile buffers.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 8388608;
    return est;
}

Json::Value RsContrastStretchOperator::run(const Json::Value& params,
                                           RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string method = getEnum(
        params, "method",
        {"linear", "percent_clip", "stddev", "histogram_equalize", "piecewise"},
        "linear");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    BandTools::StretchSpec spec;
    if (method == "percent_clip") {
        spec.kind = ImageEnhancementStreaming::StretchKind::PercentClip;
        spec.clipPercent = static_cast<float>(getDouble(params, "clipPercent", 2.0));
    } else if (method == "stddev") {
        spec.kind = ImageEnhancementStreaming::StretchKind::StdDev;
        spec.stddevK = static_cast<float>(getDouble(params, "stddevK", 2.0));
    } else if (method == "histogram_equalize") {
        spec.kind = ImageEnhancementStreaming::StretchKind::HistogramEqualize;
    } else if (method == "piecewise") {
        spec.kind = ImageEnhancementStreaming::StretchKind::Piecewise;
        const Json::Value& pts = params["piecewisePoints"];
        if (!pts.isArray() || pts.size() < 2) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "piecewise method requires at least two [in, out] control points");
        }
        for (const Json::Value& pt : pts) {
            if (!pt.isArray() || pt.size() != 2 || !pt[0].isNumeric() || !pt[1].isNumeric()) {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "piecewisePoints entries must be [in, out] number pairs");
            }
            spec.piecewisePoints.emplace_back(
                static_cast<float>(pt[0].asDouble()), static_cast<float>(pt[1].asDouble()));
        }
    } else {
        spec.kind = ImageEnhancementStreaming::StretchKind::Linear;
    }

    context.throwIfCancelled();
    context.reportProgress(0.1, "Running contrast stretch (" + method + ")");

    QString errorMessage;
    if (!BandTools::processContrastStretchFile(QString::fromStdString(inputPath),
                                               QString::fromStdString(outputPath),
                                               spec, &errorMessage,
                                               [&context] { return context.isCancelled(); })) {
        if (context.isCancelled()) {
            throw RSOperatorError(ErrorCode::Cancelled, "Contrast stretch cancelled");
        }
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Contrast stretch failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    return result;
}

} // namespace sicnu::operators::rs
