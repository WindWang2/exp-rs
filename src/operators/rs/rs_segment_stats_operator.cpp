/***************************************************************************
 * rs_segment_stats_operator.cpp
 ***************************************************************************/
#include "rs_segment_stats_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTextStream>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsSegmentStatsOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band image for spectral stats");
    props["labels"] = makeRasterParam("labels", "Segment/class label raster");
    props["output"] = makeOutputParam("output", "Output CSV path", "csv");

    Json::Value bands = makeStringParam("bands", "Optional 1-based band indices", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "CSV statistics", "csv");
    outputs["segments"] = makeIntegerParam("segments", "Number of segments written", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "labels", "output"});
    return root;
}

Json::Value RsSegmentStatsOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("obia");
    meta["tags"].append("statistics");
    meta["tags"].append("csv");
    meta["purpose"] = "Export per-object spectral means and area for lab reports";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["useCases"].append("After rs:obia_segment, export object table for Excel/Python");
    return meta;
}

Json::Value RsSegmentStatsOperator::executionEstimate() const {
    // FullRaster (base policy): the label band and every selected spectral
    // band are read into full float buffers, then per-segment sums are
    // accumulated in a map. Typical 1024x1024 float32 with 4 bands:
    // (1 label + 4 bands) x 4 MiB = 20 MiB, plus accumulation -> ~24 MiB.
    Json::Value estimate(Json::objectValue);
    estimate["tileWidth"] = 0;         // full-raster processing: tiling not applicable
    estimate["tileHeight"] = 0;
    estimate["estimatedRamBytes"] = 24 * 1024 * 1024; // ~24 MiB
    return estimate;
}

Json::Value RsSegmentStatsOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string labelsPath = requireString(params, "labels");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    if (!fileExists(labelsPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Labels not found: " + labelsPath);

    ensureGdalInit();

    GdalDatasetWrapper img;
    if (!img.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input image");

    GdalDatasetWrapper lab;
    if (!lab.open(QString::fromStdString(labelsPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open labels");

    if (img.width() != lab.width() || img.height() != lab.height()) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Input and labels must have the same dimensions");
    }

    const int width = img.width();
    const int height = img.height();
    const size_t nPix = static_cast<size_t>(width) * height;
    const std::vector<int> bands = parseBands(params, img.bandCount());
    const int nFeat = static_cast<int>(bands.size());

    context.reportProgress(0.1, "Reading labels");
    // Labels may be Byte, UInt16, UInt32 — read as float then cast
    std::vector<float> labelF(nPix);
    if (!lab.readBandData(1, labelF.data(), width, height))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to read label band");

    context.reportProgress(0.25, "Reading spectral bands");
    std::vector<std::vector<float>> bandData(static_cast<size_t>(nFeat));
    for (int i = 0; i < nFeat; ++i) {
        bandData[static_cast<size_t>(i)].resize(nPix);
        if (!img.readBandData(bands[static_cast<size_t>(i)],
                              bandData[static_cast<size_t>(i)].data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bands[static_cast<size_t>(i)]));
        }
        context.throwIfCancelled();
    }

    context.reportProgress(0.5, "Accumulating per-segment statistics");
    struct Acc {
        std::vector<double> sum;
        int count = 0;
    };
    std::map<int, Acc> acc;
    for (size_t i = 0; i < nPix; ++i) {
        const int sid = static_cast<int>(std::lround(labelF[i]));
        if (sid <= 0)
            continue;
        auto& a = acc[sid];
        if (a.sum.empty())
            a.sum.assign(static_cast<size_t>(nFeat), 0.0);
        for (int f = 0; f < nFeat; ++f)
            a.sum[static_cast<size_t>(f)] += bandData[static_cast<size_t>(f)][i];
        a.count++;
    }

    if (acc.empty())
        throw RSOperatorError(ErrorCode::InvalidInputData, "No positive label IDs found");

    context.reportProgress(0.85, "Writing CSV");
    QFile out(QString::fromStdString(outputPath));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        throw RSOperatorError(ErrorCode::FileNotWritable, "Cannot write: " + outputPath);
    }
    QTextStream ts(&out);
    ts << "segment_id,area_pixels";
    for (int f = 0; f < nFeat; ++f)
        ts << ",mean_b" << bands[static_cast<size_t>(f)];
    ts << "\n";

    int written = 0;
    for (const auto& [sid, a] : acc) {
        ts << sid << "," << a.count;
        for (int f = 0; f < nFeat; ++f) {
            const double m = a.count > 0 ? a.sum[static_cast<size_t>(f)] / a.count : 0.0;
            ts << "," << m;
        }
        ts << "\n";
        ++written;
    }
    out.close();

    context.reportProgress(1.0, "Segment stats complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["segments"] = written;
    result["features"] = nFeat;
    return result;
}

} // namespace sicnu::operators::rs
