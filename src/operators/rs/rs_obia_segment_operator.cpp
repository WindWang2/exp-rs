/***************************************************************************
 * rs_obia_segment_operator.cpp  —  teaching segmenter over the analysis layer
 *
 * ADR 0060 — the operator delegates to RsSimpleSegmenter (analysis, the
 * single teaching segmenter) and writes labels via RsSegmentMap::toGeoTIFF
 * (ADR 0054). The segutil cv::Mat stack is retired.
 ***************************************************************************/
#include "rs_obia_segment_operator.h"

#include "analysis/segmentation/rs_simple_segmenter.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsObiaSegmentOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster to segment");
    props["output"] = makeOutputParam("output", "Output label raster (UInt32)", "tif");
    props["smoothKernel"] = makeIntegerParam("smoothKernel", "Gaussian kernel size (odd)", 5);
    props["quantizeBins"] = makeIntegerParam("quantizeBins", "Intensity quantization levels", 32);
    props["minRegionSize"] = makeIntegerParam("minRegionSize", "Merge regions smaller than this", 50);

    Json::Value bands = makeStringParam("bands", "Optional 1-based band indices for mean intensity", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Label map", "tif");
    outputs["segments"] = makeIntegerParam("segments", "Number of segments", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsObiaSegmentOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("obia");
    meta["tags"].append("segmentation");
    meta["purpose"] = "Create object primitives for OBIA teaching workflows";
    meta["limitations"] = "Teaching segmenter; not a substitute for OTB MeanShift";
    return meta;
}

Json::Value RsObiaSegmentOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const int smoothKernel = getInt(params, "smoothKernel", 5);
    const int quantizeBins = getInt(params, "quantizeBins", 32);
    const int minRegionSize = getInt(params, "minRegionSize", 50);

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    }

    ensureGdalInit();
    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input");
    }

    const int width = ds.width();
    const int height = ds.height();
    const std::vector<int> bands = parseBands(params, ds.bandCount());
    const int nBands = static_cast<int>(bands.size());
    const size_t nPix = static_cast<size_t>(width) * static_cast<size_t>(height);

    context.reportProgress(0.1, "Reading bands for intensity mean");
    std::vector<std::vector<float>> bandData(static_cast<size_t>(nBands));
    for (int bi = 0; bi < nBands; ++bi) {
        bandData[static_cast<size_t>(bi)].resize(nPix);
        if (!ds.readBandData(bands[static_cast<size_t>(bi)],
                             bandData[static_cast<size_t>(bi)].data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bands[static_cast<size_t>(bi)]));
        }
        context.throwIfCancelled();
    }

    // Nodata: band 1's declared value when present, else NaN (only actual NaN
    // pixels become nodata). Label 0 = nodata follows the analysis convention.
    bool hasNodata = false;
    const double nodataValue = ds.bandNoDataValue(1, &hasNodata);
    const float nodata = hasNodata ? static_cast<float>(nodataValue)
                                   : std::numeric_limits<float>::quiet_NaN();

    std::vector<const float*> bandPtrs(static_cast<size_t>(nBands));
    for (int bi = 0; bi < nBands; ++bi)
        bandPtrs[static_cast<size_t>(bi)] = bandData[static_cast<size_t>(bi)].data();

    RsSimpleSegmenter::Params segParams;
    segParams.smoothKernel = smoothKernel;
    segParams.quantizeBins = quantizeBins;
    segParams.minRegionSize = minRegionSize;

    // ADR 0060: single teaching segmenter (analysis layer); cancel + progress
    // are plumbed through RSOperatorContext hooks.
    const RsSegmentMap segMap = RsSimpleSegmenter::segmentMultiBand(
        bandPtrs.data(), nBands, width, height, nodata, segParams,
        [&context]() { return context.isCancelled(); },
        [&context](float f) { context.reportProgress(0.15 + 0.7 * f, "Segmenting"); });
    context.throwIfCancelled();
    if (segMap.isEmpty())
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Segmentation produced an empty label map");

    context.reportProgress(0.9, "Writing label map");
    QString err;
    // ADR 0054: RsSegmentMap owns the UInt32 GeoTIFF write (LZW, NoData=0).
    if (!segMap.toGeoTIFF(QString::fromStdString(outputPath),
                          QString::fromStdString(inputPath), &err))
        throw RSOperatorError(ErrorCode::GdalError, err.toStdString());

    context.reportProgress(1.0, "Segmentation complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["segments"] = segMap.segmentCount();
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::rs
