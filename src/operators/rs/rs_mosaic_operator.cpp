#include "rs_mosaic_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QString>

#include <gdal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kTileSize = 512;

struct RasterTileMeta {
    std::string path;
    int width = 0;
    int height = 0;
    std::array<double, 6> geotransform{};
    QString projection;
    float nodata = std::numeric_limits<float>::quiet_NaN();
    int64_t offsetX = 0;
    int64_t offsetY = 0;
};

struct OutputFileCleaner {
    GdalDatasetWrapper *ds = nullptr;
    QString path;
    bool committed = false;
    OutputFileCleaner() = default;
    OutputFileCleaner(const OutputFileCleaner&) = delete;
    OutputFileCleaner& operator=(const OutputFileCleaner&) = delete;
    ~OutputFileCleaner() {
        if (!committed && !path.isEmpty()) {
            if (ds) {
                ds->close();
            }
            QFile::remove(path);
        }
    }
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
    // Window/tile streaming: working set is bounded by the tile buffers (512x512 floats)
    // independent of the total union mosaic size.
    // 1 output tile buffer (512x512 Float32 = 1MB) + 1 input scratch tile buffer (1MB)
    // + bounded GDAL working set (~2MB). Total ~4MB.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileSize;
    est["tileHeight"] = kTileSize;
    est["estimatedRamBytes"] = 4194304;
    return est;
}

Json::Value RsMosaicOperator::estimateExecution(const Json::Value& params) const
{
    // Input-dependent estimate: probes inputs to confirm readiness, returning
    // the bounded tile working set.
    if (!params.isObject() || !params.isMember("inputs") || !params["inputs"].isArray()
        || params["inputs"].empty())
        return executionEstimate();

    ensureGdalInit();
    bool anyOpened = false;

    for (const auto& item : params["inputs"])
    {
        if (!item.isString())
            continue;
        GdalDatasetWrapper ds;
        if (ds.open(QString::fromStdString(item.asString())))
        {
            anyOpened = true;
            break;
        }
    }

    if (!anyOpened)
        return executionEstimate();

    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileSize;
    est["tileHeight"] = kTileSize;
    est["estimatedRamBytes"] = 4194304;
    est["basis"] = "dynamic";
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
    context.reportProgress(0.0, "Inspecting " + std::to_string(inputCount) + " inputs");

    std::vector<RasterTileMeta> metaList;
    metaList.reserve(static_cast<size_t>(inputCount));
    std::vector<GdalDatasetWrapper> inputDatasets(static_cast<size_t>(inputCount));

    for (int i = 0; i < inputCount; ++i) {
        if (!inputsJson[i].isString()) {
            throw RSOperatorError(ErrorCode::TypeMismatch,
                                  "inputs[" + std::to_string(i) + "] must be a string path");
        }
        const std::string path = inputsJson[i].asString();
        if (!fileExists(path)) {
            throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + path);
        }

        if (!inputDatasets[static_cast<size_t>(i)].open(QString::fromStdString(path))) {
            throw RSOperatorError(ErrorCode::GdalError, "Failed to open: " + path);
        }
        const auto& ds = inputDatasets[static_cast<size_t>(i)];

        RasterTileMeta meta;
        meta.path = path;
        meta.width = ds.width();
        meta.height = ds.height();
        meta.geotransform = ds.geoTransform();
        meta.projection = ds.projection();

        if (meta.width <= 0 || meta.height <= 0) {
            throw RSOperatorError(ErrorCode::InvalidInputData, "Invalid dimensions: " + path);
        }

        // Propagate the input's real NoData value (if any) into the mosaic
        // merge so numeric NoData pixels are NOT blended as valid data. NaN
        // (no declared NoData) keeps the previous behavior.
        bool hasNodata = false;
        const double nd = ds.bandNoDataValue(1, &hasNodata);
        meta.nodata = hasNodata ? static_cast<float>(nd)
                                : std::numeric_limits<float>::quiet_NaN();

        metaList.push_back(std::move(meta));
        context.reportProgress(0.05 * (i + 1) / inputCount, "Inspected " + path);
        context.throwIfCancelled();
    }

    // CRS consistency
    for (int i = 1; i < inputCount; ++i) {
        if (metaList[static_cast<size_t>(i)].projection != metaList[0].projection) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "CRS mismatch between " + metaList[0].path + " and " + metaList[static_cast<size_t>(i)].path);
        }
    }

    // Union extent
    double unionMinX = (std::numeric_limits<double>::max)();
    double unionMinY = (std::numeric_limits<double>::max)();
    double unionMaxX = (std::numeric_limits<double>::lowest)();
    double unionMaxY = (std::numeric_limits<double>::lowest)();

    const double refPixelW = metaList[0].geotransform[1];
    const double refPixelH = metaList[0].geotransform[5];
    if (std::abs(refPixelW) < 1e-15 || std::abs(refPixelH) < 1e-15) {
        throw RSOperatorError(ErrorCode::InvalidInputData, "Invalid pixel size on first input");
    }

    // Check for rotated/sheared rasters (gt[2] != 0 or gt[4] != 0)
    for (int i = 0; i < inputCount; ++i) {
        const auto& gt = metaList[static_cast<size_t>(i)].geotransform;
        if (std::abs(gt[2]) > 1e-12 || std::abs(gt[4]) > 1e-12) {
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Rotated or sheared raster is not supported in mosaic: "
                    + metaList[static_cast<size_t>(i)].path + "; orthorectify before mosaicking");
        }
    }

    // Pixel-size consistency and Y-axis orientation (P0/P3): mosaicking rasters
    // with different pixel sizes or opposite Y-directions silently misaligns or
    // mirrors data. Reject mismatched inputs with an actionable error.
    const double kPixelTol = 1e-6; // relative tolerance
    for (int i = 1; i < inputCount; ++i) {
        const auto& gt = metaList[static_cast<size_t>(i)].geotransform;
        const double pw = std::abs(gt[1]);
        const double ph = std::abs(gt[5]);
        const bool sizeMismatch =
            (std::abs(pw - std::abs(refPixelW)) > kPixelTol * std::abs(refPixelW))
            || (std::abs(ph - std::abs(refPixelH)) > kPixelTol * std::abs(refPixelH));
        if (sizeMismatch) {
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Pixel size mismatch between " + metaList[0].path + " ("
                    + std::to_string(std::abs(refPixelW)) + " x "
                    + std::to_string(std::abs(refPixelH)) + ") and " + metaList[static_cast<size_t>(i)].path + " ("
                    + std::to_string(pw) + " x " + std::to_string(ph)
                    + "); resample the inputs to a common resolution before mosaicking");
        }

        const bool yDirMismatch = (gt[5] * refPixelH <= 0.0);
        if (yDirMismatch) {
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Raster Y-axis orientation mismatch between " + metaList[0].path + " and "
                    + metaList[static_cast<size_t>(i)].path + "; resample/reproject to a common orientation before mosaicking");
        }
    }

    for (const auto& t : metaList) {
        const auto& gt = t.geotransform;
        const double tlX = gt[0];
        const double tlY = gt[3];
        const double brX = gt[0] + t.width * gt[1];
        const double brY = gt[3] + t.height * gt[5];
        unionMinX = (std::min)(unionMinX, (std::min)(tlX, brX));
        unionMinY = (std::min)(unionMinY, (std::min)(tlY, brY));
        unionMaxX = (std::max)(unionMaxX, (std::max)(tlX, brX));
        unionMaxY = (std::max)(unionMaxY, (std::max)(tlY, brY));
    }

    const int64_t outWidth64 = static_cast<int64_t>(std::round((unionMaxX - unionMinX) / std::abs(refPixelW)));
    const int64_t outHeight64 = static_cast<int64_t>(std::round((unionMaxY - unionMinY) / std::abs(refPixelH)));
    if (outWidth64 <= 0 || outHeight64 <= 0
        || outWidth64 > (std::numeric_limits<int>::max)()
        || outHeight64 > (std::numeric_limits<int>::max)()) {
        throw RSOperatorError(ErrorCode::InvalidInputData, "Computed mosaic dimensions are invalid");
    }

    const int outWidth = static_cast<int>(outWidth64);
    const int outHeight = static_cast<int>(outHeight64);

    // Cap memory / dimensions for accidental huge extents
    std::optional<std::uint64_t> outPixels =
        sicnu::processing::checkedMul(static_cast<std::uint64_t>(outWidth),
                                      static_cast<std::uint64_t>(outHeight));
    if (!outPixels || *outPixels > 200'000'000ULL) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Mosaic output too large (" + std::to_string(outWidth) + "x" +
                                  std::to_string(outHeight) + ")");
    }

    std::array<double, 6> outGT{};
    outGT[0] = unionMinX;
    outGT[1] = refPixelW;
    outGT[2] = metaList[0].geotransform[2];
    outGT[3] = (refPixelH < 0) ? unionMaxY : unionMinY;
    outGT[4] = metaList[0].geotransform[4];
    outGT[5] = refPixelH;

    for (int i = 0; i < inputCount; ++i) {
        const auto& gt = metaList[static_cast<size_t>(i)].geotransform;
        metaList[static_cast<size_t>(i)].offsetX = static_cast<int64_t>(
            std::round((gt[0] - unionMinX) / std::abs(refPixelW)));
        metaList[static_cast<size_t>(i)].offsetY = static_cast<int64_t>(
            (refPixelH < 0)
                ? std::round((unionMaxY - gt[3]) / std::abs(refPixelH))
                : std::round((gt[3] - unionMinY) / std::abs(refPixelH)));
    }

    context.reportProgress(0.08, "Creating mosaic output dataset");
    context.throwIfCancelled();

    GdalDatasetWrapper outDs;
    QString outErr;
    OutputFileCleaner cleaner;
    cleaner.ds = &outDs;
    cleaner.path = QString::fromStdString(outputPath);

    if (!outDs.create(cleaner.path, outWidth, outHeight, 1,
                      static_cast<int>(GDT_Float32), outGT, metaList[0].projection, &outErr)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create mosaic output: " + outErr.toStdString());
    }

    float outNodata = std::numeric_limits<float>::quiet_NaN();
    for (const auto& meta : metaList) {
        if (!std::isnan(meta.nodata)) {
            outNodata = static_cast<float>(meta.nodata);
            break;
        }
    }
    outDs.setBandNoDataValue(1, outNodata);

    // Window streaming mosaic execution:
    // Process the output grid in kTileSize x kTileSize windows.
    // For each window, read intersecting sub-windows from inputs in index order
    // (later inputs overwrite earlier ones for non-nodata pixels), then write
    // the window directly to the output GeoTIFF.
    std::vector<float> tileOut(static_cast<size_t>(kTileSize) * kTileSize);
    std::vector<float> tileIn(static_cast<size_t>(kTileSize) * kTileSize);

    const int nx = (outWidth + kTileSize - 1) / kTileSize;
    const int ny = (outHeight + kTileSize - 1) / kTileSize;
    const uint64_t totalTiles = static_cast<uint64_t>(nx) * ny;
    uint64_t processedTiles = 0;

    for (int y = 0; y < outHeight; y += kTileSize) {
        const int h = (std::min)(kTileSize, outHeight - y);
        for (int x = 0; x < outWidth; x += kTileSize) {
            const int w = (std::min)(kTileSize, outWidth - x);
            const size_t currentTilePixels = static_cast<size_t>(w) * h;

            // Initialize current output window with outNodata (unfilled / nodata)
            std::fill(tileOut.begin(), tileOut.begin() + currentTilePixels, outNodata);

            for (int i = 0; i < inputCount; ++i) {
                const auto& meta = metaList[static_cast<size_t>(i)];

                const int64_t interMinX = (std::max)(static_cast<int64_t>(x), meta.offsetX);
                const int64_t interMaxX = (std::min)(static_cast<int64_t>(x + w), meta.offsetX + meta.width);
                const int64_t interMinY = (std::max)(static_cast<int64_t>(y), meta.offsetY);
                const int64_t interMaxY = (std::min)(static_cast<int64_t>(y + h), meta.offsetY + meta.height);

                if (interMinX >= interMaxX || interMinY >= interMaxY) {
                    continue;
                }

                const int iw = static_cast<int>(interMaxX - interMinX);
                const int ih = static_cast<int>(interMaxY - interMinY);
                const int srcX = static_cast<int>(interMinX - meta.offsetX);
                const int srcY = static_cast<int>(interMinY - meta.offsetY);
                const int dstRelX = static_cast<int>(interMinX - x);
                const int dstRelY = static_cast<int>(interMinY - y);

                if (!inputDatasets[static_cast<size_t>(i)].readBandWindow(
                        1, srcX, srcY, iw, ih, tileIn.data())) {
                    throw RSOperatorError(
                        ErrorCode::GdalError,
                        "Failed to read window from " + meta.path + " at ("
                            + std::to_string(srcX) + ", " + std::to_string(srcY) + ")");
                }

                const float nodata = meta.nodata;
                for (int r = 0; r < ih; ++r) {
                    const size_t outRowOffset = static_cast<size_t>(dstRelY + r) * w + dstRelX;
                    const size_t inRowOffset = static_cast<size_t>(r) * iw;
                    for (int c = 0; c < iw; ++c) {
                        const float val = tileIn[inRowOffset + c];
                        const bool valid = !std::isnan(val) && (std::isnan(nodata) || val != nodata);
                        if (valid) {
                            tileOut[outRowOffset + c] = val;
                        }
                    }
                }
            }

            if (!outDs.writeBandWindow(1, x, y, w, h, tileOut.data())) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to write mosaic window at ("
                                          + std::to_string(x) + ", " + std::to_string(y) + ")");
            }

            processedTiles++;
            context.throwIfCancelled();
            context.reportProgress(0.08 + 0.90 * (static_cast<double>(processedTiles) / totalTiles),
                                   "Mosaicking tiles (" + std::to_string(processedTiles) + "/"
                                       + std::to_string(totalTiles) + ")");
        }
    }

    outDs.close();
    cleaner.committed = true;
    context.reportProgress(1.0, "Mosaic complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["width"] = outWidth;
    result["height"] = outHeight;
    result["inputCount"] = inputCount;
    return result;
}

} // namespace sicnu::operators::rs
