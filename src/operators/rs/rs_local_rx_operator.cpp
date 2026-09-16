/***************************************************************************
 * rs_local_rx_operator.cpp  —  dual-window local RX anomaly detection
 ***************************************************************************/
#include "rs_local_rx_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_local_rx.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <gdal.h>

#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsLocalRxOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Local RX score raster (NaN = unscored)", "tif");
    props["qualityOut"] = makeOutputParam("qualityOut",
        "Optional per-pixel background valid-sample count raster (Float32)", "tif");
    props["outerWindow"] = makeIntegerParam("outerWindow",
        "Background window side length (odd, >= 3)", 5);
    props["innerWindow"] = makeIntegerParam("innerWindow",
        "Guard window side length (odd, >= 1, < outerWindow); excluded from the background", 3);
    props["covariance"] = makeEnumParam("covariance",
        "Local covariance estimator (diagonal scales to high band counts)",
        {"full", "diagonal"}, "full");
    props["loading"] = makeNumberParam("loading",
        "Scaled diagonal loading alpha (covariance += loading * trace/bands * I)", 1e-3);
    props["minSamples"] = makeIntegerParam("minSamples",
        "Minimum valid background samples (0 = auto: 2*bands+2 full / bands+1 diagonal)", 0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Local RX score raster path");
    outputs["mean"] = makeNumberParam("mean", "Mean score over scored pixels", 0.0);
    outputs["max"] = makeNumberParam("max", "Maximum score", 0.0);
    outputs["scoredPixels"] = makeIntegerParam("scoredPixels", "Pixels with a valid score", 0);
    outputs["unscoredPixels"] = makeIntegerParam("unscoredPixels",
        "Invalid or statistically under-sampled pixels (NaN in the output)", 0);
    outputs["covariance"] = makeEnumParam("covariance", "Covariance mode used",
                                          {"full", "diagonal"}, "full");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsLocalRxOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["task"] = "anomaly-detection";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("rx");
    meta["tags"].append("anomaly");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("local");
    meta["purpose"] = "Detect pixels deviating from their LOCAL background "
                      "(dual window with guard), catching anomalies embedded in "
                      "heterogeneous scenes that global RX averages away.";
    meta["workflowHints"].append("Use a guard window (innerWindow) around each pixel so "
                                 "compact anomalies do not contaminate their own background.");
    meta["workflowHints"].append("Use covariance=diagonal for very high band counts; the "
                                 "full local covariance costs O(window * bands^2) per pixel.");
    meta["limitations"].append("Pixels whose window has fewer valid background samples than "
                               "the minimum stay NaN (reported in unscoredPixels), never faked.");
    meta["limitations"].append("Raster edges use clamped (shrunken) windows; no replicated "
                               "border pixels enter the statistics.");
    return meta;
}

Json::Value RsLocalRxOperator::executionEstimate() const {
    // Tile working set: (tile + 2*halo)^2 * bands * sizeof(float) for the
    // padded BIP window plus the per-pixel background gather (window^2*bands
    // floats). Full mode adds bands^2 doubles per pixel transiently (matrix
    // inversion) — declared at the documented nominal so the estimate is
    // honest for multispectral inputs; real cost grows with bands.
    constexpr double kTileW = 256, kTileH = 256, kHalo = 2, kNominalBands = 30;
    const double windowPixels = 5 * 5;
    const double paddedTileBytes =
        ( kTileW + 2 * kHalo ) * ( kTileH + 2 * kHalo ) * kNominalBands * sizeof( float );
    const double gatherBytes = windowPixels * kNominalBands * sizeof( float );
    const double stateBytes = kNominalBands * kNominalBands * sizeof( double );
    Json::Value est( Json::objectValue );
    est["tileWidth"] = static_cast<Json::Int64>( kTileW );
    est["tileHeight"] = static_cast<Json::Int64>( kTileH );
    est["estimatedRamBytes"] = static_cast<Json::UInt64>(
        paddedTileBytes + gatherBytes + stateBytes );
    return est;
}

Json::Value RsLocalRxOperator::run(const Json::Value& params,
                                   RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string qualityPath = getString(params, "qualityOut", "");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    const int outerWindow = getInt(params, "outerWindow", 5);
    const int innerWindow = getInt(params, "innerWindow", 3);
    const std::string covariance = getEnum(params, "covariance", {"full", "diagonal"}, "full");
    const double loading = getDouble(params, "loading", 1e-3);
    const int minSamples = getInt(params, "minSamples", 0);

    if (outerWindow < 3 || outerWindow % 2 == 0)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "outerWindow must be an odd value >= 3, got "
                                  + std::to_string(outerWindow));
    if (innerWindow < 1 || innerWindow % 2 == 0 || innerWindow >= outerWindow)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "innerWindow must be odd, >= 1, and < outerWindow, got "
                                  + std::to_string(innerWindow));

    SpectralLocalRx::Config config;
    config.outerWindow = outerWindow;
    config.innerWindow = innerWindow;
    SpectralLocalRx::CovarianceMode mode = SpectralLocalRx::CovarianceMode::Full;
    if (!SpectralLocalRx::covarianceModeFromText(covariance, &mode))
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Unknown covariance mode: " + covariance);
    config.covarianceMode = mode;
    config.loading = loading;
    config.minBackgroundSamples = minSamples;

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (bandCount < 2)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Local RX detection requires at least 2 bands, got "
                                  + std::to_string(bandCount));

    // Per-band declared NoData (never fabricated). Invalid pixels neither
    // enter backgrounds nor receive scores.
    std::vector<float> noDataPerBand(static_cast<size_t>(bandCount), 0.0f);
    std::vector<uint8_t> hasNoDataPerBand(static_cast<size_t>(bandCount), 0);
    for (int b = 0; b < bandCount; ++b) {
        bool hasNoData = false;
        const double nd = ds.bandNoDataValue(b + 1, &hasNoData);
        if (hasNoData) {
            hasNoDataPerBand[static_cast<size_t>(b)] = 1;
            noDataPerBand[static_cast<size_t>(b)] = static_cast<float>(nd);
        }
    }

    // Tile loop with an outer-window halo read around every tile. Interior
    // results are identical to a whole-raster pass: windows are clamped to
    // RASTER bounds by the halo read's padding semantics (out-of-raster cells
    // come back as the band NoData / NaN and are invalid for the kernel), so
    // no border replication and no tile seam artifacts.
    const int halo = outerWindow / 2;
    constexpr int kTile = 256;
    std::vector<int> bandList(static_cast<size_t>(bandCount));
    for (int b = 0; b < bandCount; ++b)
        bandList[static_cast<size_t>(b)] = b + 1;

    GdalStreamingOutput out(QString::fromStdString(outputPath), width, height, 1,
                            GDT_Float32, ds.geoTransform(), ds.projection());
    if (!out.isOpen())
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create local RX output raster: " + outputPath);
    out.setNoDataValue(std::numeric_limits<float>::quiet_NaN());
    out.setMetadataItem("EXP_RS_LOCAL_RX_COVARIANCE", QString::fromStdString(covariance));
    out.setMetadataItem("EXP_RS_LOCAL_RX_OUTER_WINDOW", QString::number(outerWindow));
    out.setMetadataItem("EXP_RS_LOCAL_RX_INNER_WINDOW", QString::number(innerWindow));

    std::unique_ptr<GdalStreamingOutput> qualityOut;
    if (!qualityPath.empty()) {
        qualityOut = std::make_unique<GdalStreamingOutput>(
            QString::fromStdString(qualityPath), width, height, 1, GDT_Float32,
            ds.geoTransform(), ds.projection());
        if (!qualityOut->isOpen())
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to create quality raster: " + qualityPath);
        qualityOut->setNoDataValue(std::numeric_limits<float>::quiet_NaN());
    }

    const int tilesX = (width + kTile - 1) / kTile;
    const int tilesY = (height + kTile - 1) / kTile;
    const int totalTiles = tilesX * tilesY;
    const double perTile = totalTiles > 0 ? 1.0 / totalTiles : 0.0;

    double sumScores = 0.0;
    double maxScore = 0.0;
    uint64_t scoredPixels = 0;
    uint64_t unscoredPixels = 0;
    int tilesSeen = 0;

    std::vector<float> padded;     // padded BIP window
    std::vector<float> tileScores; // interior strip for the output
    std::vector<float> tileQuality;

    try {
        for (int ty = 0; ty < tilesY; ++ty) {
            for (int tx = 0; tx < tilesX; ++tx) {
                context.throwIfCancelled();
                const int x0 = tx * kTile;
                const int y0 = ty * kTile;
                const int bw = std::min(kTile, width - x0);
                const int bh = std::min(kTile, height - y0);

                const int px0 = std::max(0, x0 - halo);
                const int py0 = std::max(0, y0 - halo);
                const int pw = std::min(width, x0 + bw + halo) - px0;
                const int ph = std::min(height, y0 + bh + halo) - py0;
                const size_t paddedPixels = static_cast<size_t>(pw) * ph;

                padded.resize(paddedPixels * static_cast<size_t>(bandCount));
                if (!ds.readWindowBip(bandList, px0, py0, pw, ph, padded.data()))
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to read padded window at ("
                                              + std::to_string(px0) + ","
                                              + std::to_string(py0) + ")");

                SpectralLocalRx::Result result;
                QString kernelError;
                if (!SpectralLocalRx::dualWindowRx(padded.data(), pw, ph, bandCount,
                                                   config,
                                                   noDataPerBand.data(),
                                                   hasNoDataPerBand.data(),
                                                   &result, &kernelError))
                    throw RSOperatorError(ErrorCode::ComputationError,
                                          kernelError.isEmpty()
                                              ? "Local RX kernel failed"
                                              : kernelError.toStdString());

                tileScores.resize(static_cast<size_t>(bw) * bh);
                if (!qualityPath.empty())
                    tileQuality.resize(static_cast<size_t>(bw) * bh);
                for (int y = 0; y < bh; ++y) {
                    const int srcY = (y0 + y) - py0;
                    for (int x = 0; x < bw; ++x) {
                        const int srcX = (x0 + x) - px0;
                        const size_t srcIdx = static_cast<size_t>(srcY) * pw + srcX;
                        const size_t dstIdx = static_cast<size_t>(y) * bw + x;
                        const float score = result.scores[srcIdx];
                        tileScores[dstIdx] = score;
                        if (result.scored[srcIdx]) {
                            ++scoredPixels;
                            sumScores += score;
                            if (score > maxScore)
                                maxScore = score;
                        } else {
                            ++unscoredPixels;
                        }
                        if (!qualityPath.empty()) {
                            // Quality plane: background sample count for valid
                            // centers; NaN marks an invalid (NoData/non-finite)
                            // center so it cannot be read as "0 samples".
                            tileQuality[dstIdx] =
                                result.centerValid[srcIdx]
                                    ? static_cast<float>(result.backgroundSamples[srcIdx])
                                    : std::numeric_limits<float>::quiet_NaN();
                        }
                    }
                }

                GdalBlockStream::Tile outTile;
                outTile.xOffset = x0;
                outTile.yOffset = y0;
                outTile.width = bw;
                outTile.height = bh;
                outTile.halo = 0;
                outTile.bufferWidth = bw;
                outTile.bufferHeight = bh;
                if (!out.writeTile(1, outTile, tileScores.data()))
                    throw RSOperatorError(ErrorCode::FileNotWritable,
                                          "Failed to write local RX tile ("
                                              + std::to_string(x0) + ","
                                              + std::to_string(y0) + ")");
                if (!qualityPath.empty()
                    && !qualityOut->writeTile(1, outTile, tileQuality.data()))
                    throw RSOperatorError(ErrorCode::FileNotWritable,
                                          "Failed to write quality tile ("
                                              + std::to_string(x0) + ","
                                              + std::to_string(y0) + ")");

                context.reportProgress((++tilesSeen) * perTile, "Local RX scoring");
            }
        }
    } catch (...) {
        // Atomicity: a cancelled or failed run must not leave partial files.
        out.abandon();
        if (!qualityPath.empty())
            qualityOut->abandon();
        throw;
    }

    QString closeError;
    if (!out.closeWithError(&closeError))
        throw RSOperatorError(ErrorCode::GdalError,
                              closeError.isEmpty() ? "Failed to finalize score raster"
                                                   : closeError.toStdString());
    if (!qualityPath.empty()) {
        QString qualityError;
        if (!qualityOut->closeWithError(&qualityError))
            throw RSOperatorError(ErrorCode::GdalError,
                                  qualityError.isEmpty() ? "Failed to finalize quality raster"
                                                         : qualityError.toStdString());
    }

    ds.close();
    context.reportProgress(1.0, "Local RX anomaly detection complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["mean"] = scoredPixels > 0 ? sumScores / static_cast<double>(scoredPixels) : 0.0;
    result["max"] = maxScore;
    result["scoredPixels"] = static_cast<Json::UInt64>(scoredPixels);
    result["unscoredPixels"] = static_cast<Json::UInt64>(unscoredPixels);
    result["covariance"] = covariance;
    result["outerWindow"] = outerWindow;
    result["innerWindow"] = innerWindow;
    return result;
}

} // namespace sicnu::operators::rs
