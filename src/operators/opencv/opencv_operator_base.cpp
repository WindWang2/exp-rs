/***************************************************************************
 * opencv_operator_base.cpp  —  Common OpenCV operator logic
 ***************************************************************************/
#include "opencv_operator_base.h"
#include "opencv_utils.h"

#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sicnu::operators::opencv {

namespace {

/// Streaming tile dimension for the filter path (GdalBlockStream default).
constexpr int kStreamTileDim = 256;

/**
 * Converts a raw tile buffer to the operator's NaN convention, mirroring
 * readRasterBandsToMats: with a declared finite NoData, sentinel and
 * non-finite pixels become NaN; without one, values pass through unchanged
 * so valid 0 pixels are never masked (#444).
 */
void maskBufferToNan(float* pixels, size_t count, bool hasNodata, float nodataF) {
    if (!hasNodata) {
        return;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(pixels[i]) || pixels[i] == nodataF) {
            pixels[i] = nan;
        }
    }
}

} // namespace

int OpenCvOperatorBase::neighborhoodRadius(const Json::Value& params) const {
    (void)params;
    return -1; // full-frame by default; windowed filters override
}

Json::Value OpenCvOperatorBase::run(const Json::Value& params, RSOperatorContext& context) {
    validateCommonParams(params);

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    const int halo = neighborhoodRadius(params);
    if (halo >= 0) {
        return runStreaming(inputPath, outputPath, halo, params, context);
    }

    context.logInfo("Reading raster: " + inputPath);
    std::vector<cv::Mat> bands = readRasterBandsToMats(inputPath);
    if (bands.empty()) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Failed to read raster bands from: " + inputPath);
    }

    const int bandCount = static_cast<int>(bands.size());
    for (int i = 0; i < bandCount; ++i) {
        context.throwIfCancelled();
        context.reportProgress(static_cast<double>(i) / bandCount,
                               "Processing band " + std::to_string(i + 1) + "/" + std::to_string(bandCount));

        applyFilter(bands[i], params);

        context.reportProgress(static_cast<double>(i + 1) / bandCount,
                               "Finished band " + std::to_string(i + 1));
    }

    context.logInfo("Writing output: " + outputPath);
    std::string writeError;
    if (!writeMatsToRaster(outputPath, bands, inputPath, &writeError)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + writeError);
    }

    context.reportProgress(1.0, "Complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["bands"] = bandCount;
    return result;
}

Json::Value OpenCvOperatorBase::runStreaming(const std::string& inputPath,
                                             const std::string& outputPath,
                                             int halo,
                                             const Json::Value& params,
                                             RSOperatorContext& context) {
    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError, ds.lastError().toStdString());
    }

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (width <= 0 || height <= 0 || bandCount <= 0) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Failed to read raster bands from: " + inputPath);
    }

    context.logInfo("Streaming raster tiles: " + inputPath);

    GdalStreamingOutput out(QString::fromStdString(outputPath), width, height,
                            bandCount, GDT_Float32, ds.geoTransform(), ds.projection());
    if (!out.isOpen()) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output raster: " + outputPath);
    }

    const float quietNan = std::numeric_limits<float>::quiet_NaN();

    for (int band = 1; band <= bandCount; ++band) {
        context.throwIfCancelled();
        context.reportProgress(static_cast<double>(band - 1) / bandCount,
                               "Processing band " + std::to_string(band) + "/" +
                                   std::to_string(bandCount));

        // Masked read (#444): a declared finite NoData turns sentinel and
        // non-finite pixels into NaN; undeclared NoData passes through so
        // valid 0 pixels survive. Per band, like readRasterBandsToMats.
        bool hasNodata = false;
        const double nodata = ds.bandNoDataValue(band, &hasNodata);
        const bool nodataActive = hasNodata && std::isfinite(nodata);
        const float nodataF = nodataActive ? static_cast<float>(nodata) : 0.0f;

        // Output NoData exactly like writeMatsToRaster (#445): declared
        // sentinels are echoed, undeclared sources declare NaN.
        if (!out.setBandNoDataValue(band, nodataActive ? nodata
                                                       : static_cast<double>(quietNan))) {
            out.abandon();
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to declare NoData on output raster: " + outputPath);
        }

        GdalBlockStream stream(ds, band, kStreamTileDim, kStreamTileDim, halo);
        std::vector<float> buf(static_cast<size_t>(kStreamTileDim + 2 * halo) *
                               static_cast<size_t>(kStreamTileDim + 2 * halo));

        bool complete = false;
        try {
            complete = stream.forEach([&](const GdalBlockStream::Tile& tile,
                                          const float* pixels) {
                context.throwIfCancelled();

                const int bufW = tile.bufferWidth;
                const int bufH = tile.bufferHeight;
                const size_t bufPixels = static_cast<size_t>(bufW) * bufH;
                std::copy(pixels, pixels + bufPixels, buf.begin());
                maskBufferToNan(buf.data(), bufPixels, nodataActive, nodataF);

                // Filter the real-data window inside the halo buffer: the
                // kernel then extrapolates its own border exactly where the
                // full-frame call would at the raster border, and interior
                // tiles are covered by real halo — verified bit-exact for the
                // replicate/echo kernels (median/sobel/laplacian). The
                // masked-normalized kernels (gaussian/mean) cannot be tiled
                // bit-exactly and stay on the full-frame path (see their
                // neighborhoodRadius overrides).
                const int readX = std::max(0, tile.xOffset - halo);
                const int readY = std::max(0, tile.yOffset - halo);
                const int readW = std::min(width, tile.xOffset + tile.width + halo) - readX;
                const int readH = std::min(height, tile.yOffset + tile.height + halo) - readY;
                const int dstX = (tile.xOffset - halo < 0) ? (halo - tile.xOffset) : 0;
                const int dstY = (tile.yOffset - halo < 0) ? (halo - tile.yOffset) : 0;

                cv::Mat bufMat(bufH, bufW, CV_32FC1, buf.data());
                cv::Mat roi = bufMat(cv::Rect(dstX, dstY, readW, readH));
                applyFilter(roi, params);

                // Core pixels always sit at (halo, halo) in the buffer.
                cv::Mat core = bufMat(cv::Rect(halo, halo, tile.width, tile.height)).clone();
                if (nodataActive) {
                    float* corePtr = core.ptr<float>();
                    const size_t corePixels = static_cast<size_t>(tile.width) * tile.height;
                    for (size_t i = 0; i < corePixels; ++i) {
                        if (std::isnan(corePtr[i])) {
                            corePtr[i] = nodataF;
                        }
                    }
                }

                if (!out.writeTile(band, tile, core.ptr<float>())) {
                    out.abandon();
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to write tile of output band " +
                                              std::to_string(band) + ": " + outputPath);
                }
                return true;
            });
        } catch (...) {
            out.abandon(); // destructor closes and removes the partial output
            throw;
        }
        if (!complete) {
            out.abandon();
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read raster band " + std::to_string(band) +
                                      " from: " + inputPath);
        }

        context.reportProgress(static_cast<double>(band) / bandCount,
                               "Finished band " + std::to_string(band));
    }

    context.logInfo("Writing output: " + outputPath);
    QString writeError;
    if (!out.closeWithError(&writeError)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + writeError.toStdString());
    }

    context.reportProgress(1.0, "Complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["bands"] = bandCount;
    return result;
}

void OpenCvOperatorBase::validateCommonParams(const Json::Value& params) const {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }
    // Touch required string fields so errors match requireString wording.
    requireString(params, "input");
    requireString(params, "output");
}

Json::Value OpenCvOperatorBase::operatorSchemaProperties() const {
    return Json::Value(Json::objectValue);
}

std::vector<std::string> OpenCvOperatorBase::operatorRequiredParams() const {
    return {};
}

Json::Value OpenCvOperatorBase::buildSchema(const std::string& title,
                                            const std::string& description) const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster file path");
    props["output"] = makeOutputParam("output", "Output raster file path", "tif");
    props["band"] = makeIntegerParam("band", "Band to process (1-based; ignored, all bands are processed)", 1);

    Json::Value opProps = operatorSchemaProperties();
    for (const auto& member : opProps.getMemberNames()) {
        props[member] = opProps[member];
    }

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster file path");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");

    Json::Value root = makeRootSchema(title, description, props, outputs);

    std::vector<std::string> required = {"input", "output"};
    std::vector<std::string> opRequired = operatorRequiredParams();
    required.insert(required.end(), opRequired.begin(), opRequired.end());
    root["required"] = makeRequired(required);

    return root;
}

} // namespace sicnu::operators::opencv
