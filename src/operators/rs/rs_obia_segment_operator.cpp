/***************************************************************************
 * rs_obia_segment_operator.cpp  —  thin wrapper over shared segutil
 ***************************************************************************/
#include "rs_obia_segment_operator.h"
#include "rs_segmentation_utils.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using namespace segutil;

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
    const size_t nPix = static_cast<size_t>(width) * static_cast<size_t>(height);

    context.reportProgress(0.1, "Reading bands for intensity mean");
    std::vector<std::vector<float>> bandData(bands.size());
    for (size_t bi = 0; bi < bands.size(); ++bi) {
        bandData[bi].resize(nPix);
        if (!ds.readBandData(bands[bi], bandData[bi].data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bands[bi]));
        }
        context.throwIfCancelled();
    }

    cv::Mat labels = segmentQuantize(bandData, width, height, smoothKernel, quantizeBins,
                                     minRegionSize, context);

    int maxId = 0;
    for (int y = 0; y < height; ++y) {
        const int* row = labels.ptr<int>(y);
        for (int x = 0; x < width; ++x)
            maxId = std::max(maxId, row[x]);
    }

    const auto gt = ds.geoTransform();
    double gtArr[6] = {gt[0], gt[1], gt[2], gt[3], gt[4], gt[5]};
    context.reportProgress(0.9, "Writing label map");
    writeLabelGeoTiff(outputPath, labels, width, height, gtArr,
                      ds.projection().toStdString());

    context.reportProgress(1.0, "Segmentation complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["segments"] = maxId;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::rs
