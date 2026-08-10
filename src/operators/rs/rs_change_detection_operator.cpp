/***************************************************************************
 * rs_change_detection_operator.cpp  —  Change detection RSOperator
 ***************************************************************************/
#include "rs_change_detection_operator.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"

#include <QFile>
#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {
    "difference", "normalized_difference", "ratio", "cva", "mad", "change_mask"
};

const std::vector<std::string> s_threshold_methods = {
    "manual", "otsu", "percentile", "statistical"
};

const std::vector<std::string> s_cleanups = {
    "none", "erode", "dilate", "open", "close"
};

ChangeDetection::MorphOp morphOpFromName(const std::string& name)
{
    if (name == "erode") return ChangeDetection::MorphOp::Erode;
    if (name == "dilate") return ChangeDetection::MorphOp::Dilate;
    if (name == "open") return ChangeDetection::MorphOp::Open;
    if (name == "close") return ChangeDetection::MorphOp::Close;
    return ChangeDetection::MorphOp::None;
}

// --- Memory-bounded tile-streaming helpers for the cva/mad paths -----------

constexpr int kTileDim = 256;
constexpr int kMaskHistogramBins = 65536;

/// Read one tile of both datasets into band-interleaved-by-pixel buffers
/// (bip[p * bandCount + band]). Called only with in-extent windows — edge
/// tiles are clamped to the remaining width/height by the caller — so
/// GDALRasterIO never sees a window past the raster extent. Returns false on
/// any failed band read.
bool readTileBip(const GdalDatasetWrapper& beforeDs, const GdalDatasetWrapper& afterDs,
                 int bandCount, int xOff, int yOff, int w, int h,
                 std::vector<float>& beforeBip, std::vector<float>& afterBip,
                 std::vector<float>& bandScratch)
{
    const size_t tilePixels = static_cast<size_t>(w) * h;
    const size_t B = static_cast<size_t>(bandCount);
    for (int b = 0; b < bandCount; ++b) {
        if (!beforeDs.readBandWindow(b + 1, xOff, yOff, w, h, bandScratch.data()))
            return false;
        for (size_t p = 0; p < tilePixels; ++p)
            beforeBip[p * B + static_cast<size_t>(b)] = bandScratch[p];
        if (!afterDs.readBandWindow(b + 1, xOff, yOff, w, h, bandScratch.data()))
            return false;
        for (size_t p = 0; p < tilePixels; ++p)
            afterBip[p * B + static_cast<size_t>(b)] = bandScratch[p];
    }
    return true;
}

/// Streaming mean / population stddev over non-NaN magnitude values, plus
/// running min/max. Matches ChangeDetection::statistics() semantics (NaNs are
/// skipped, stddev uses the N denominator) so the mask threshold and the
/// result JSON agree with the legacy full-scene computation.
struct StreamingMagnitudeStats
{
    size_t validCount = 0;
    double mean = 0.0;
    double m2 = 0.0; // Welford M2 accumulator
    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();

    void add(float v)
    {
        if (std::isnan(v))
            return;
        ++validCount;
        const double d = static_cast<double>(v) - mean;
        mean += d / static_cast<double>(validCount);
        m2 += d * (static_cast<double>(v) - mean);
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }

    double stddev() const
    {
        return (validCount > 1) ? std::sqrt(m2 / static_cast<double>(validCount)) : 0.0;
    }
};

/**
 * Streaming cva/mad implementation (memory-bounded, O(tilePixels*bands +
 * bands^2) working set). MAD runs pass 1 (sums) -> madFinalizeMeans -> pass 2
 * (centered products) -> madFinalize -> pass 3 (chi-square transform, written
 * per tile). CVA is a single tile pass reproducing cvaMagnitude()'s math.
 *
 * makeMask=false writes the Float32 magnitude straight to outputPath.
 * makeMask=true writes the magnitude to a temp path, derives the threshold
 * from a streaming histogram + Welford stats, builds the full-resolution Byte
 * mask (changeMask -> morphological cleanup -> connected-component filter) at
 * outputPath, and deletes the temp magnitude.
 */
Json::Value runCvaMadStreaming(
    const GdalDatasetWrapper& beforeDs, const GdalDatasetWrapper& afterDs,
    int width, int height, int bandCount, bool isMad, bool makeMask,
    float threshold, const std::string& thresholdMethod, double percentile,
    double statisticalK, int minAreaPixels, const std::string& cleanup,
    int cleanupIterations, const std::string& outputPath, const std::string& method,
    RSOperatorContext& context)
{
    constexpr int tile = kTileDim;
    const size_t maxTilePixels = static_cast<size_t>(tile) * tile;
    const size_t B = static_cast<size_t>(bandCount);
    std::vector<float> beforeBip(maxTilePixels * B);
    std::vector<float> afterBip(maxTilePixels * B);
    std::vector<float> bandScratch(maxTilePixels);
    std::vector<float> tileOut(maxTilePixels);
    const float nan = std::numeric_limits<float>::quiet_NaN();

    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (makeMask && pixelCount > static_cast<size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "mask path requires a full-resolution mask; raster too large "
            "(would exceed 2^31 pixels)");
    }

    // Tile iteration shared by the read-only passes: reads the input tile,
    // checks cancellation per tile, then invokes the per-tile worker.
    auto forEachTile = [&](const auto& fn) {
        for (int y = 0; y < height; y += tile) {
            const int h = std::min(tile, height - y);
            for (int x = 0; x < width; x += tile) {
                const int w = std::min(tile, width - x);
                context.throwIfCancelled();
                if (!readTileBip(beforeDs, afterDs, bandCount, x, y, w, h,
                                 beforeBip, afterBip, bandScratch)) {
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to read input tile at (" +
                                              std::to_string(x) + ", " + std::to_string(y) + ")");
                }
                fn(x, y, w, h, static_cast<size_t>(w) * h);
            }
        }
    };

    QString calcError;

    // --- MAD passes 1 & 2: covariance accumulation -------------------------
    ChangeDetection::MadStreamingState madState;
    if (isMad) {
        forEachTile([&](int, int, int, int, size_t n) {
            if (!ChangeDetection::madAccumulateSums(beforeBip.data(), afterBip.data(),
                                                    n, bandCount, &madState)) {
                throw RSOperatorError(ErrorCode::ComputationError,
                                      "MAD sum accumulation failed");
            }
        });
        if (!ChangeDetection::madFinalizeMeans(&madState, &calcError)) {
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "MAD computation failed: " + calcError.toStdString());
        }
        context.reportProgress(0.5, "Computing MAD statistics");

        forEachTile([&](int, int, int, int, size_t n) {
            if (!ChangeDetection::madAccumulateCentered(beforeBip.data(), afterBip.data(),
                                                        n, bandCount, &madState)) {
                throw RSOperatorError(ErrorCode::ComputationError,
                                      "MAD covariance accumulation failed");
            }
        });
        if (!ChangeDetection::madFinalize(&madState, &calcError)) {
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "MAD computation failed: " + calcError.toStdString());
        }
        context.reportProgress(0.6, "MAD coefficients ready");
    }

    // --- Magnitude write pass ----------------------------------------------
    // Open the Float32 magnitude stream: directly at outputPath, or at a temp
    // path when a mask will be derived from it.
    const std::string magPath = makeMask ? context.tempPath(".tif") : outputPath;
    QString outErr;
    GDALDatasetH outDs = createOutputTiff(QString::fromStdString(magPath), width, height,
                                          1, static_cast<int>(GDT_Float32),
                                          beforeDs.geoTransform(), beforeDs.projection(), &outErr);
    if (!outDs) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create change magnitude raster: " +
                                  outErr.toStdString());
    }
    GDALRasterBandH outBand = GDALGetRasterBand(outDs, 1);

    StreamingMagnitudeStats magStats;
    context.reportProgress(isMad ? 0.7 : 0.5, "Computing " + method + " magnitude");

    for (int y = 0; y < height; y += tile) {
        const int h = std::min(tile, height - y);
        for (int x = 0; x < width; x += tile) {
            const int w = std::min(tile, width - x);
            const size_t n = static_cast<size_t>(w) * h;
            context.throwIfCancelled();
            if (!readTileBip(beforeDs, afterDs, bandCount, x, y, w, h,
                             beforeBip, afterBip, bandScratch)) {
                GDALClose(outDs);
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read input tile at (" +
                                          std::to_string(x) + ", " + std::to_string(y) + ")");
            }
            if (isMad) {
                ChangeDetection::madTransformTile(beforeBip.data(), afterBip.data(),
                                                  n, bandCount, madState, tileOut.data());
            } else {
                // CVA magnitude: a NaN delta in any band propagates to a NaN
                // pixel; otherwise sqrt(sum of squared deltas) — exactly
                // cvaMagnitude()'s per-pixel math, inlined for the BIP layout.
                for (size_t p = 0; p < n; ++p) {
                    double sumSq = 0.0;
                    bool hasNan = false;
                    for (int b = 0; b < bandCount; ++b) {
                        const float d = afterBip[p * B + static_cast<size_t>(b)]
                                      - beforeBip[p * B + static_cast<size_t>(b)];
                        if (std::isnan(d)) {
                            hasNan = true;
                            break;
                        }
                        sumSq += static_cast<double>(d) * static_cast<double>(d);
                    }
                    tileOut[p] = hasNan ? nan : static_cast<float>(std::sqrt(sumSq));
                }
            }
            if (GDALRasterIO(outBand, GF_Write, x, y, w, h, tileOut.data(),
                             w, h, GDT_Float32, 0, 0) != CE_None) {
                GDALClose(outDs);
                throw RSOperatorError(ErrorCode::FileNotWritable,
                                      "Failed to write change magnitude tile at (" +
                                          std::to_string(x) + ", " + std::to_string(y) + ")");
            }
            for (size_t p = 0; p < n; ++p)
                magStats.add(tileOut[p]);
        }
    }
    GDALClose(outDs);

    // --- Non-mask path: the magnitude raster is the output. ----------------
    if (!makeMask) {
        Json::Value result(Json::objectValue);
        result["output"] = outputPath;
        result["method"] = method;
        result["mean"] = static_cast<float>(magStats.mean);
        result["stddev"] = static_cast<float>(magStats.stddev());
        context.reportProgress(1.0, "Change detection complete");
        return result;
    }

    // --- Mask path: threshold from the streaming stats, then the mask. -----
    context.reportProgress(0.8, "Computing change threshold");

    // Re-open the temp magnitude raster read-only and build the histogram
    // (min/max are final after the write pass, so binning is exact).
    GdalDatasetWrapper magDs;
    if (!magDs.open(QString::fromStdString(magPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to reopen magnitude raster for masking");
    }
    std::vector<double> hist(static_cast<size_t>(kMaskHistogramBins), 0.0);
    size_t histFinite = 0;
    const double magRange = magStats.maxVal - magStats.minVal;
    if (magStats.validCount > 0 && magRange > 0.0) {
        for (int y = 0; y < height; y += tile) {
            const int h = std::min(tile, height - y);
            for (int x = 0; x < width; x += tile) {
                const int w = std::min(tile, width - x);
                const size_t n = static_cast<size_t>(w) * h;
                context.throwIfCancelled();
                if (!magDs.readBandWindow(1, x, y, w, h, tileOut.data())) {
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to read magnitude tile");
                }
                for (size_t p = 0; p < n; ++p) {
                    const double v = tileOut[p];
                    if (std::isnan(v))
                        continue;
                    ++histFinite;
                    int bin = static_cast<int>((v - magStats.minVal) / magRange
                                               * (kMaskHistogramBins - 1));
                    bin = std::clamp(bin, 0, kMaskHistogramBins - 1);
                    hist[static_cast<size_t>(bin)] += 1.0;
                }
            }
        }
    }

    float thresholdUsed = threshold;
    // When every finite magnitude is identical (range == 0) there is nothing
    // to bin; otsu/percentile reduce to that single value (matching the
    // array-based otsuThreshold behavior on invariant input).
    const bool invariant = magStats.validCount > 0 && magRange <= 0.0;
    if (thresholdMethod == "otsu") {
        if (invariant) {
            thresholdUsed = static_cast<float>(magStats.minVal);
        } else {
            float t = threshold;
            if (ChangeDetection::otsuThresholdFromHistogram(magStats.minVal, magStats.maxVal,
                                                            hist, histFinite, &t))
                thresholdUsed = t;
        }
    } else if (thresholdMethod == "percentile") {
        if (invariant) {
            thresholdUsed = static_cast<float>(magStats.minVal);
        } else {
            float t = threshold;
            if (ChangeDetection::percentileThresholdFromHistogram(magStats.minVal, magStats.maxVal,
                                                                  hist, histFinite, percentile, &t))
                thresholdUsed = t;
        }
    } else if (thresholdMethod == "statistical") {
        // mean + k*stddev over the finite change magnitudes, matching the
        // legacy full-scene statistics pass.
        if (magStats.validCount >= 2 && magStats.stddev() > 0.0) {
            thresholdUsed = static_cast<float>(
                magStats.mean + statisticalK * magStats.stddev());
        } else {
            context.logWarning(
                "statistical threshold: not enough varying finite values; "
                "falling back to the manual threshold");
        }
    }

    // Re-read the magnitude raster tile-by-tile and apply the threshold into
    // a full-resolution mask (the mask path's pre-existing behavior).
    std::vector<uint8_t> mask(pixelCount, 0);
    std::vector<uint8_t> tileMask(maxTilePixels);
    for (int y = 0; y < height; y += tile) {
        const int h = std::min(tile, height - y);
        for (int x = 0; x < width; x += tile) {
            const int w = std::min(tile, width - x);
            const size_t n = static_cast<size_t>(w) * h;
            context.throwIfCancelled();
            if (!magDs.readBandWindow(1, x, y, w, h, tileOut.data())) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read magnitude tile");
            }
            if (!ChangeDetection::changeMask(tileOut.data(), tileMask.data(), n, thresholdUsed)) {
                throw RSOperatorError(ErrorCode::ComputationError,
                                      "Change mask computation failed");
            }
            for (int dy = 0; dy < h; ++dy) {
                std::copy_n(tileMask.data() + static_cast<size_t>(dy) * w, w,
                            mask.data() + static_cast<size_t>(y + dy) * width + x);
            }
        }
    }
    magDs.close();

    ChangeDetection::morphologicalCleanup(mask.data(), width, height,
                                          cleanupIterations, morphOpFromName(cleanup));
    if (minAreaPixels > 0 &&
        !ChangeDetection::connectedComponentFilter(mask.data(), width, height,
                                                   static_cast<size_t>(minAreaPixels))) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Connected-component filter failed");
    }

    context.reportProgress(0.9, "Writing change mask");
    QString maskErr;
    GDALDatasetH maskDs = createOutputTiff(QString::fromStdString(outputPath), width, height,
                                           1, static_cast<int>(GDT_Byte),
                                           beforeDs.geoTransform(), beforeDs.projection(), &maskErr);
    if (!maskDs) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create change mask: " + maskErr.toStdString());
    }
    GDALRasterBandH maskBand = GDALGetRasterBand(maskDs, 1);
    const CPLErr writeErr = GDALRasterIO(maskBand, GF_Write, 0, 0, width, height,
                                         mask.data(), width, height, GDT_Byte, 0, 0);
    GDALSetMetadataItem(maskDs, "SICNU_CHANGE_METHOD", method.c_str(), nullptr);
    GDALSetMetadataItem(maskDs, "SICNU_CHANGE_THRESHOLD",
                        QString::number(thresholdUsed, 'g', 10).toUtf8().constData(), nullptr);
    if (minAreaPixels > 0) {
        GDALSetMetadataItem(maskDs, "SICNU_CHANGE_MIN_AREA",
                            QByteArray::number(minAreaPixels).constData(), nullptr);
    }
    GDALClose(maskDs);
    if (writeErr != CE_None) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write change mask: " + outputPath);
    }

    QFile::remove(QString::fromStdString(magPath));

    size_t changed = 0;
    size_t evaluated = 0;
    for (uint8_t v : mask) {
        if (v == 255)
            continue;
        ++evaluated;
        if (v == 1)
            ++changed;
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["thresholdUsed"] = thresholdUsed;
    result["changedPixels"] = static_cast<Json::UInt64>(changed);
    result["totalPixels"] = static_cast<Json::UInt64>(evaluated);
    result["changedPercent"] = evaluated == 0
        ? 0.0
        : 100.0 * static_cast<double>(changed) / static_cast<double>(evaluated);
    result["mean"] = static_cast<float>(magStats.mean);
    result["stddev"] = static_cast<float>(magStats.stddev());
    if (beforeDs.hasGeoTransform()) {
        const auto gt = beforeDs.geoTransform();
        const double pixelArea = std::abs(gt[1] * gt[5]);
        if (pixelArea > 0.0)
            result["changedArea"] = static_cast<double>(changed) * pixelArea;
    }
    context.reportProgress(1.0, "Change detection complete");
    return result;
}

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
    props["makeMask"] = makeBooleanParam("makeMask", "Also write a binary change mask (UInt8 0/1)", false);
    props["thresholdMethod"] = makeEnumParam("thresholdMethod", "Mask threshold strategy", s_threshold_methods, "manual");
    props["percentile"] = makeNumberParam("percentile", "Percentile for thresholdMethod=percentile (0-100)", 90.0);
    props["statisticalK"] = makeNumberParam("statisticalK", "Stddev multiplier for thresholdMethod=statistical (mean + k*stddev)", 2.0);
    props["minAreaPixels"] = makeIntegerParam("minAreaPixels", "Minimum mapping unit: drop changed components smaller than this (pixels); 0 disables", 0);
    props["cleanup"] = makeEnumParam("cleanup", "Morphological mask cleanup", s_cleanups, "none");
    props["cleanupIterations"] = makeIntegerParam("cleanupIterations", "Cleanup iterations", 1);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method", "");
    outputs["mean"] = makeNumberParam("mean", "Mean of change magnitude", 0.0);
    outputs["stddev"] = makeNumberParam("stddev", "Stddev of change magnitude", 0.0);
    outputs["thresholdUsed"] = makeNumberParam("thresholdUsed", "Effective mask threshold", 0.0);
    outputs["changedPixels"] = makeIntegerParam("changedPixels", "Changed pixel count (mask)", 0);
    outputs["totalPixels"] = makeIntegerParam("totalPixels", "Evaluated pixel count (mask)", 0);
    outputs["changedPercent"] = makeNumberParam("changedPercent", "Changed pixel percentage (mask)", 0.0);
    outputs["changedArea"] = makeNumberParam("changedArea", "Changed area in map units squared (mask)", 0.0);

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
    meta["prerequisites"].append("Before and after rasters must be co-registered and same size "
                                 "(grid compatibility is preflighted).");
    meta["workflowHints"].append("Apply atmospheric correction to both dates before comparison.");
    meta["limitations"].append("ratio outputs after/before (NaN where before is 0); "
                               "cva and mad stream over 256x256 tiles in O(tile*bands + "
                               "bands^2) memory (mad is multi-pass); makeMask writes a "
                               "UInt8 0/1 mask with manual/Otsu/percentile/statistical "
                               "thresholds and optional morphological cleanup.");
    return meta;
}

Json::Value RsChangeDetectionOperator::executionEstimate() const {
    // MultiPassStreaming: cva/mad process 256x256 tiles out-of-core, so peak
    // RAM is dominated by the tile buffers plus the bands^2 covariance /
    // coefficient matrices and is independent of the raster dimensions. For a
    // nominal 6-band input: 2 BIP input tiles + output tile + band scratch +
    // bands^2 doubles. (The single-band methods still read whole bands and are
    // not covered by this estimate.)
    constexpr long long kTilePixels = 256LL * 256;
    constexpr long long kBandCount = 6;
    constexpr long long ramBytes =
        3 * kTilePixels * kBandCount * static_cast<long long>(sizeof(float)) // BIP in x2 + out
        + kTilePixels * static_cast<long long>(sizeof(float))                // band scratch
        + kBandCount * kBandCount * static_cast<long long>(sizeof(double));  // bands^2 state
    Json::Value estimate(Json::objectValue);
    estimate["tileWidth"] = 256;
    estimate["tileHeight"] = 256;
    estimate["estimatedRamBytes"] = static_cast<Json::UInt64>(ramBytes);
    return estimate;
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

    const bool makeMask = getBool(params, "makeMask", false);
    const std::string thresholdMethod =
        getEnum(params, "thresholdMethod", s_threshold_methods, "manual");
    const double percentile = getDouble(params, "percentile", 90.0);
    const double statisticalK = getDouble(params, "statisticalK", 2.0);
    const int minAreaPixels = getInt(params, "minAreaPixels", 0);
    const std::string cleanup = getEnum(params, "cleanup", s_cleanups, "none");
    const int cleanupIterations = getInt(params, "cleanupIterations", 1);

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

    // Radiometric comparability (ADR 0114): differencing rasters in different
    // physical states (e.g. TOA reflectance vs radiance) is meaningless. Both
    // sides must declare the same state; absent declarations are skipped.
    const QString beforeState =
        SatelliteProducts::readRadiometricState( QString::fromStdString( beforePath ) );
    const QString afterState =
        SatelliteProducts::readRadiometricState( QString::fromStdString( afterPath ) );
    if ( !beforeState.isEmpty() && !afterState.isEmpty() && beforeState != afterState )
    {
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "Before and after rasters are in different radiometric states (" +
            beforeState.toStdString() + " vs " + afterState.toStdString() +
            "); calibrate or atmospherically correct both acquisitions to the "
            "same state before comparing them");
    }
    if ( !beforeState.isEmpty() && !afterState.isEmpty() )
        context.logInfo( "Radiometric state: " + beforeState.toStdString() );

    // Shared pixel-grid preflight (CRS, resolution, origin alignment, extent)
    // before any pixel comparison. Two unreferenced rasters are not spatially
    // comparable and pass as compatible; the dimension check below remains the
    // fallback for them.
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

    if (method == "cva" || method == "mad") {
        if (beforeDs.bandCount() != afterDs.bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  method + " requires the same band count on both rasters");
        }
    } else {
        if (beforeBand < 1 || beforeBand > beforeDs.bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Before band " + std::to_string(beforeBand) + " is out of range");
        }
        if (afterBand < 1 || afterBand > afterDs.bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "After band " + std::to_string(afterBand) + " is out of range");
        }
    }

    context.logInfo("Computing " + method + " between " + beforePath + " and " + afterPath);
    context.reportProgress(0.2, "Reading input bands");

    // cva/mad run memory-bounded over 256x256 tiles (MAD is multi-pass
    // streaming, CVA a single pass) and produce their own output + result.
    if (method == "cva" || method == "mad") {
        return runCvaMadStreaming(beforeDs, afterDs, width, height,
                                  beforeDs.bandCount(), method == "mad", makeMask,
                                  threshold, thresholdMethod, percentile, statisticalK,
                                  minAreaPixels, cleanup, cleanupIterations,
                                  outputPath, method, context);
    }

    // --- Single-band methods (unchanged full-scene path) ---
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> mag(pixelCount);
    std::string computeError;
    bool ok = false;

    {
        std::vector<float> before(pixelCount), after(pixelCount);
        if (!beforeDs.readBandData(beforeBand, before.data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(beforeBand) + " from before raster");
        }
        if (!afterDs.readBandData(afterBand, after.data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(afterBand) + " from after raster");
        }
        context.reportProgress(0.5, "Computing change");

        if (method == "difference") {
            ok = ChangeDetection::difference(before.data(), after.data(), mag.data(), pixelCount);
        } else if (method == "normalized_difference") {
            ok = ChangeDetection::normalizedDifference(before.data(), after.data(), mag.data(), pixelCount);
        } else if (method == "ratio") {
            ok = ChangeDetection::ratio(before.data(), after.data(), mag.data(), pixelCount);
        } else { // legacy "change_mask": difference -> threshold -> float mask
            std::vector<float> diff(pixelCount);
            ok = ChangeDetection::difference(before.data(), after.data(), diff.data(), pixelCount);
            if (ok) {
                std::vector<uint8_t> mask(pixelCount);
                ok = ChangeDetection::changeMask(diff.data(), mask.data(), pixelCount, threshold);
                if (ok) {
                    std::transform(mask.begin(), mask.end(), mag.begin(),
                                   [](uint8_t v) { return static_cast<float>(v); });
                }
            }
        }
    }

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Change detection computation failed"
                                  + (computeError.empty() ? std::string() : ": " + computeError));
    }

    context.throwIfCancelled();

    ChangeDetection::ChangeStats stats =
        ChangeDetection::statistics(mag.data(), mag.size());

    // --- Mask path: threshold strategies + morphological cleanup + area stats.
    if (makeMask && method != "change_mask") {
        float thresholdUsed = threshold;
        if (thresholdMethod == "otsu") {
            float otsu = threshold;
            if (ChangeDetection::otsuThreshold(mag.data(), pixelCount, &otsu))
                thresholdUsed = otsu;
        } else if (thresholdMethod == "percentile") {
            float pct = threshold;
            if (ChangeDetection::percentileThreshold(mag.data(), pixelCount, percentile, &pct))
                thresholdUsed = pct;
        } else if (thresholdMethod == "statistical") {
            // mean + k*stddev over the finite change magnitudes (the outer
            // statistics pass already computed them; no second scan).
            if (stats.validCount >= 2 && stats.stddev > 0.0f) {
                thresholdUsed = static_cast<float>(
                    stats.mean + statisticalK * stats.stddev);
            } else {
                // Degenerate input (all identical / all-NaN change values):
                // a zero threshold would flag the whole raster as changed,
                // so fall back to the manual threshold with a warning.
                context.logWarning(
                    "statistical threshold: not enough varying finite values; "
                    "falling back to the manual threshold");
            }
        }

        std::vector<uint8_t> mask(pixelCount);
        if (!ChangeDetection::changeMask(mag.data(), mask.data(), pixelCount, thresholdUsed)) {
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "Change mask computation failed");
        }
        ChangeDetection::morphologicalCleanup(mask.data(), width, height,
                                              cleanupIterations, morphOpFromName(cleanup));
        if (minAreaPixels > 0 &&
            !ChangeDetection::connectedComponentFilter(mask.data(), width, height,
                                                       static_cast<size_t>(minAreaPixels))) {
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "Connected-component filter failed");
        }

        context.reportProgress(0.8, "Writing change mask");
        QString errorMessage;
        GDALDatasetH outDs = createOutputTiff(QString::fromStdString(outputPath), width, height,
                                              1, static_cast<int>(GDT_Byte),
                                              beforeDs.geoTransform(), beforeDs.projection(),
                                              &errorMessage);
        if (!outDs) {
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to create change mask: " + errorMessage.toStdString());
        }
        GDALRasterBandH outBand = GDALGetRasterBand(outDs, 1);
        const CPLErr writeErr = GDALRasterIO(outBand, GF_Write, 0, 0, width, height,
                                             mask.data(), width, height, GDT_Byte, 0, 0);
        GDALSetMetadataItem(outDs, "SICNU_CHANGE_METHOD", method.c_str(), nullptr);
        GDALSetMetadataItem(outDs, "SICNU_CHANGE_THRESHOLD",
                            QString::number(thresholdUsed, 'g', 10).toUtf8().constData(),
                            nullptr);
        if (minAreaPixels > 0) {
            GDALSetMetadataItem(outDs, "SICNU_CHANGE_MIN_AREA",
                                QByteArray::number(minAreaPixels).constData(), nullptr);
        }
        GDALClose(outDs);
        if (writeErr != CE_None) {
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to write change mask: " + outputPath);
        }

        size_t changed = 0;
        size_t evaluated = 0;
        for (uint8_t v : mask) {
            if (v == 255)
                continue;
            ++evaluated;
            if (v == 1)
                ++changed;
        }

        Json::Value result(Json::objectValue);
        result["output"] = outputPath;
        result["method"] = method;
        result["thresholdUsed"] = thresholdUsed;
        result["changedPixels"] = static_cast<Json::UInt64>(changed);
        result["totalPixels"] = static_cast<Json::UInt64>(evaluated);
        result["changedPercent"] = evaluated == 0
            ? 0.0
            : 100.0 * static_cast<double>(changed) / static_cast<double>(evaluated);
        result["mean"] = stats.mean;
        result["stddev"] = stats.stddev;
        if (beforeDs.hasGeoTransform()) {
            const auto gt = beforeDs.geoTransform();
            const double pixelArea = std::abs(gt[1] * gt[5]);
            if (pixelArea > 0.0)
                result["changedArea"] = static_cast<double>(changed) * pixelArea;
        }
        beforeDs.close();
        afterDs.close();
        context.reportProgress(1.0, "Change detection complete");
        return result;
    }

    // --- Raster path: write the change magnitude as Float32.
    context.reportProgress(0.8, "Writing output raster");
    std::vector<std::vector<float>> bands = {std::move(mag)};
    QString errorMessage;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, bands,
                         beforeDs.geoTransform(), beforeDs.projection(), &errorMessage)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + errorMessage.toStdString());
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
