/***************************************************************************
 * rs_mosaic_operator.cpp  —  Multi-raster mosaic RSOperator
 ***************************************************************************/
#include "rs_mosaic_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/mosaic.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <gdal.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

struct RasterTile {
    std::string path;
    int width = 0;
    int height = 0;
    std::array<double, 6> geotransform{};
    QString projection;
    std::vector<float> data;
    /// Band-1 NoData value (NaN when the band has none).
    float nodata = std::numeric_limits<float>::quiet_NaN();
};

} // namespace

Json::Value RsMosaicOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    Json::Value inputs = makeStringParam("inputs",
                                         "Array of input raster paths (band 1 used)",
                                         "");
    inputs["type"] = "array";
    inputs["items"] = Json::Value(Json::objectValue);
    inputs["items"]["type"] = "string";
    props["inputs"] = inputs;
    props["output"] = makeOutputParam("output", "Output mosaic GeoTIFF", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Mosaic GeoTIFF", "tif");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);
    outputs["inputCount"] = makeIntegerParam("inputCount", "Number of inputs mosaicked", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"inputs", "output"});
    return root;
}

Json::Value RsMosaicOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("mosaic");
    meta["tags"].append("merge");
    meta["tags"].append("composition");
    meta["purpose"] = "Stitch adjacent or overlapping rasters into a single scene";
    meta["limitations"] = "Uses band 1 only; requires matching CRS across inputs";
    return meta;
}

Json::Value RsMosaicOperator::executionEstimate() const
{
    // FullRaster (default policy): band 1 of every input is loaded fully in
    // memory along with the output mosaic buffer (capped at 200M output pixels).
    // Typical: 2 x 1024x1024 inputs + 1 output, all Float32.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216; // 3 x 1024x1024 Float32 buffers
    return est;
}

Json::Value RsMosaicOperator::run(const Json::Value& params, RSOperatorContext& context) {
    if (!params.isMember("inputs") || !params["inputs"].isArray() || params["inputs"].empty()) {
        throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                              "Parameter 'inputs' must be a non-empty array of paths");
    }

    const std::string outputPath = requireString(params, "output");
    const Json::Value& inputsJson = params["inputs"];
    const int inputCount = static_cast<int>(inputsJson.size());

    ensureGdalInit();
    context.reportProgress(0.0, "Loading " + std::to_string(inputCount) + " inputs");

    std::vector<RasterTile> tiles;
    tiles.reserve(static_cast<size_t>(inputCount));

    for (int i = 0; i < inputCount; ++i) {
        if (!inputsJson[i].isString()) {
            throw RSOperatorError(ErrorCode::TypeMismatch,
                                  "inputs[" + std::to_string(i) + "] must be a string path");
        }
        const std::string path = inputsJson[i].asString();
        if (!fileExists(path)) {
            throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + path);
        }

        GdalDatasetWrapper ds;
        if (!ds.open(QString::fromStdString(path))) {
            throw RSOperatorError(ErrorCode::GdalError, "Failed to open: " + path);
        }

        RasterTile tile;
        tile.path = path;
        tile.width = ds.width();
        tile.height = ds.height();
        tile.geotransform = ds.geoTransform();
        tile.projection = ds.projection();

        if (tile.width <= 0 || tile.height <= 0) {
            throw RSOperatorError(ErrorCode::InvalidInputData, "Invalid dimensions: " + path);
        }

        // Propagate the input's real NoData value (if any) into the mosaic
        // merge so numeric NoData pixels are NOT blended as valid data. NaN
        // (no declared NoData) keeps the previous behavior.
        bool hasNodata = false;
        const double nd = ds.bandNoDataValue(1, &hasNodata);
        tile.nodata = hasNodata ? static_cast<float>(nd)
                                : std::numeric_limits<float>::quiet_NaN();

        const size_t n = static_cast<size_t>(tile.width) * static_cast<size_t>(tile.height);
        tile.data.resize(n);
        if (!ds.readBandData(1, tile.data.data(), tile.width, tile.height)) {
            throw RSOperatorError(ErrorCode::GdalError, "Failed to read band 1: " + path);
        }

        tiles.push_back(std::move(tile));
        context.reportProgress(0.3 * (i + 1) / inputCount, "Loaded " + path);
        context.throwIfCancelled();
    }

    // CRS consistency
    for (int i = 1; i < inputCount; ++i) {
        if (tiles[i].projection != tiles[0].projection) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "CRS mismatch between " + tiles[0].path + " and " + tiles[i].path);
        }
    }

    // Union extent
    double unionMinX = std::numeric_limits<double>::max();
    double unionMinY = std::numeric_limits<double>::max();
    double unionMaxX = std::numeric_limits<double>::lowest();
    double unionMaxY = std::numeric_limits<double>::lowest();

    const double refPixelW = tiles[0].geotransform[1];
    const double refPixelH = tiles[0].geotransform[5];
    if (std::abs(refPixelW) < 1e-15 || std::abs(refPixelH) < 1e-15) {
        throw RSOperatorError(ErrorCode::InvalidInputData, "Invalid pixel size on first input");
    }

    // Pixel-size consistency (P0): mosaicking rasters with different pixel
    // sizes silently misaligns the union grid. Reject mismatched inputs with
    // an actionable error instead of producing offset/dirty data.
    const double kPixelTol = 1e-6; // relative tolerance
    for (int i = 1; i < inputCount; ++i) {
        const auto& gt = tiles[i].geotransform;
        const double pw = std::abs(gt[1]);
        const double ph = std::abs(gt[5]);
        const bool sizeMismatch =
            (std::abs(pw - std::abs(refPixelW)) > kPixelTol * std::abs(refPixelW))
            || (std::abs(ph - std::abs(refPixelH)) > kPixelTol * std::abs(refPixelH));
        if (sizeMismatch) {
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Pixel size mismatch between " + tiles[0].path + " ("
                    + std::to_string(std::abs(refPixelW)) + " x "
                    + std::to_string(std::abs(refPixelH)) + ") and " + tiles[i].path + " ("
                    + std::to_string(pw) + " x " + std::to_string(ph)
                    + "); resample the inputs to a common resolution before mosaicking");
        }
    }

    for (const auto& t : tiles) {
        const auto& gt = t.geotransform;
        const double tlX = gt[0];
        const double tlY = gt[3];
        const double brX = gt[0] + t.width * gt[1];
        const double brY = gt[3] + t.height * gt[5];
        unionMinX = std::min(unionMinX, std::min(tlX, brX));
        unionMinY = std::min(unionMinY, std::min(tlY, brY));
        unionMaxX = std::max(unionMaxX, std::max(tlX, brX));
        unionMaxY = std::max(unionMaxY, std::max(tlY, brY));
    }

    const int outWidth = static_cast<int>(std::round((unionMaxX - unionMinX) / std::abs(refPixelW)));
    const int outHeight = static_cast<int>(std::round((unionMaxY - unionMinY) / std::abs(refPixelH)));
    if (outWidth <= 0 || outHeight <= 0) {
        throw RSOperatorError(ErrorCode::InvalidInputData, "Computed mosaic dimensions are invalid");
    }

    // Cap memory for accidental huge extents
    const size_t outPixels = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
    if (outPixels > 200'000'000ULL) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Mosaic output too large (" + std::to_string(outWidth) + "x" +
                                  std::to_string(outHeight) + ")");
    }

    std::array<double, 6> outGT{};
    outGT[0] = unionMinX;
    outGT[1] = refPixelW;
    outGT[2] = tiles[0].geotransform[2];
    outGT[3] = unionMaxY;
    outGT[4] = tiles[0].geotransform[4];
    outGT[5] = refPixelH;

    std::vector<Mosaic::MosaicSource> sources(static_cast<size_t>(inputCount));
    for (int i = 0; i < inputCount; ++i) {
        const auto& gt = tiles[i].geotransform;
        sources[i].data = tiles[i].data.data();
        sources[i].width = static_cast<size_t>(tiles[i].width);
        sources[i].height = static_cast<size_t>(tiles[i].height);
        sources[i].offsetX = static_cast<size_t>(
            std::round((gt[0] - unionMinX) / std::abs(refPixelW)));
        sources[i].offsetY = static_cast<size_t>(
            std::round((unionMaxY - gt[3]) / std::abs(refPixelH)));
        sources[i].nodata = tiles[i].nodata;
    }

    context.reportProgress(0.6, "Merging mosaic");
    context.throwIfCancelled();

    std::vector<float> outBuf(outPixels, std::numeric_limits<float>::quiet_NaN());
    if (!Mosaic::merge(sources.data(), sources.size(), outBuf.data(),
                       static_cast<size_t>(outWidth), static_cast<size_t>(outHeight))) {
        throw RSOperatorError(ErrorCode::ComputationError, "Mosaic::merge failed");
    }

    context.reportProgress(0.85, "Writing output");

    QString error;
    std::vector<std::vector<float>> bands(1);
    bands[0] = std::move(outBuf);
    if (!writeGdalOutput(QString::fromStdString(outputPath), outWidth, outHeight,
                         bands, outGT, tiles[0].projection, &error)) {
        throw RSOperatorError(ErrorCode::GdalError,
                              error.isEmpty() ? "Failed to write mosaic output" : error.toStdString());
    }

    context.reportProgress(1.0, "Mosaic complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["width"] = outWidth;
    result["height"] = outHeight;
    result["inputCount"] = inputCount;
    return result;
}

} // namespace sicnu::operators::rs
