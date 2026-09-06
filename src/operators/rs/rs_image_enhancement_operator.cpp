/***************************************************************************
 * rs_image_enhancement_operator.cpp — streaming dispatch promoted verbatim
 * from the image enhancement panel's worker lambda.
 ***************************************************************************/
#include "rs_image_enhancement_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsImageEnhancementOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster");
    props["output"] = makeOutputParam("output", "Output raster", "tif");
    props["method"] = makeEnumParam("method",
                                    "Enhancement family",
                                    {"stretch", "filter", "ratio_ihs", "speckle"},
                                    "stretch");
    props["stretchType"] = makeEnumParam("stretchType",
                                         "stretch: linear, percent_clip, stddev, histogram_equalize",
                                         {"linear", "percent_clip", "stddev", "histogram_equalize"},
                                         "linear");
    props["clipPercent"] = makeNumberParam("clipPercent", "stretch percent_clip: trim % per end", 2.0);
    props["stddevK"] = makeNumberParam("stddevK", "stretch stddev: mean +/- K sigma", 2.0);
    props["filterType"] = makeEnumParam("filterType",
                                        "filter: mean, gaussian, median, sobel, laplacian",
                                        {"mean", "gaussian", "median", "sobel", "laplacian"},
                                        "mean");
    props["kernelSize"] = makeIntegerParam("kernelSize", "filter/speckle window size (odd)", 3);
    props["sigma"] = makeNumberParam("sigma", "filter gaussian sigma", 1.0);
    props["transform"] = makeEnumParam("transform", "ratio_ihs: ratio or ihs",
                                       {"ratio", "ihs"}, "ratio");
    props["band1"] = makeIntegerParam("band1", "ratio_ihs first band (1-based)", 1);
    props["band2"] = makeIntegerParam("band2", "ratio_ihs second band (1-based)", 2);
    props["band3"] = makeIntegerParam("band3", "ratio_ihs third band, ihs only (1-based)", 3);
    props["speckleType"] = makeEnumParam("speckleType", "speckle: lee, frost, kuan, gamma_map",
                                         {"lee", "frost", "kuan", "gamma_map"}, "lee");
    props["noiseVariance"] = makeNumberParam("noiseVariance", "speckle lee/kuan noise variance", 1.0);
    props["damping"] = makeNumberParam("damping", "speckle frost damping", 2.0);
    setRange(props["kernelSize"], 3, 15);
    setRange(props["band1"], 1, 10000);
    setRange(props["band2"], 1, 10000);
    setRange(props["band3"], 1, 10000);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Output raster path");
    outputs["bands"] = makeIntegerParam("bands", "Output band count", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    stampDeterminismGrade(root, "bit-exact");
    return root;
}

Json::Value RsImageEnhancementOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("enhancement");
    meta["tags"].append("stretch");
    meta["tags"].append("filter");
    meta["tags"].append("speckle");
    meta["purpose"] = "Tile-streaming image enhancement (O(tile) memory, any raster size).";
    meta["workflowHints"].append("Set method first; only that method's parameters are read.");
    return meta;
}

Json::Value RsImageEnhancementOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 8388608;
    return est;
}

Json::Value RsImageEnhancementOperator::run(const Json::Value& params,
                                            RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string method = getEnum(params, "method",
                                       {"stretch", "filter", "ratio_ihs", "speckle"},
                                       "stretch");
    const int methodIndex = method == "filter" ? 1
                            : method == "ratio_ihs" ? 2
                            : method == "speckle" ? 3
                            : 0;

    // Per-method parameter mapping (schema defaults keep GUI parity: the panel
    // submits the same enums it used to encode as combo indexes).
    int stretchType = 0;
    double clipPercent = getDouble(params, "clipPercent", 2.0);
    double stddevMult = getDouble(params, "stddevK", 2.0);
    int filterType = 0;
    int kernelSize = getInt(params, "kernelSize", 3);
    double sigma = getDouble(params, "sigma", 1.0);
    int ratioType = 0;
    int band1 = getInt(params, "band1", 1);
    int band2 = getInt(params, "band2", 2);
    int band3 = getInt(params, "band3", 3);
    int speckleType = 0;
    int speckleKernel = kernelSize;
    double noiseVar = getDouble(params, "noiseVariance", 1.0);
    double damping = getDouble(params, "damping", 2.0);

    if (methodIndex == 0) {
        const std::string stretch = getEnum(params, "stretchType",
                                            {"linear", "percent_clip", "stddev", "histogram_equalize"},
                                            "linear");
        stretchType = stretch == "percent_clip" ? 1
                      : stretch == "stddev" ? 2
                      : stretch == "histogram_equalize" ? 3
                      : 0;
    } else if (methodIndex == 1) {
        const std::string filter = getEnum(params, "filterType",
                                           {"mean", "gaussian", "median", "sobel", "laplacian"},
                                           "mean");
        filterType = filter == "gaussian" ? 1
                     : filter == "median" ? 2
                     : filter == "sobel" ? 3
                     : filter == "laplacian" ? 4
                     : 0;
    } else if (methodIndex == 2) {
        ratioType = getEnum(params, "transform", {"ratio", "ihs"}, "ratio") == "ihs" ? 1 : 0;
    } else {
        const std::string speckle = getEnum(params, "speckleType",
                                            {"lee", "frost", "kuan", "gamma_map"}, "lee");
        speckleType = speckle == "frost" ? 1
                      : speckle == "kuan" ? 2
                      : speckle == "gamma_map" ? 3
                      : 0;
    }

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input raster not found: " + inputPath);
    }

    context.throwIfCancelled();
    context.reportProgress(0.05, "Enhancing (" + method + ")");

    using namespace ImageEnhancementStreaming;
    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::ComputationError, "Failed to open GDAL dataset");
    }

    const int w = src.width();
    const int h = src.height();
    const int bands = src.bandCount();

    // Band-count guards (the panel's worker-side backstop, now typed).
    if (methodIndex == 2 && ratioType == 0 && bands < 2) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Band ratio requires at least 2 bands");
    }
    if (methodIndex == 2 && ratioType == 1 && bands < 3) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "IHS transform requires at least 3 bands");
    }

    // Resolve each band's declared NoData (float-cast; NaN when undeclared)
    // so stretches mask the real sentinel instead of a fabricated -9999 (#445).
    std::vector<float> bandNodata(bands, std::numeric_limits<float>::quiet_NaN());
    for (int b = 0; b < bands; ++b) {
        bool hasNd = false;
        const double nd = src.bandNoDataValue(b + 1, &hasNd);
        if (hasNd && std::isfinite(nd))
            bandNodata[b] = static_cast<float>(nd);
    }

    // Tile-streamed output (#691 contract): O(tile) memory for any raster size.
    int outBands = bands;
    if (methodIndex == 2)
        outBands = (ratioType == 0) ? 1 : 3;
    GdalStreamingOutput dst(QString::fromStdString(outputPath), w, h, outBands, GDT_Float32,
                            src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::ComputationError, "Failed to create output raster");
    }
    QString closeError;
    bool ok = true;

    if (methodIndex == 0) {
        // Contrast stretch: streaming statistics pass + streaming apply pass
        // per band (exact replica of the stretch kernels).
        StretchParams stretch;
        switch (stretchType) {
        case 1: stretch.kind = StretchKind::PercentClip; break;
        case 2: stretch.kind = StretchKind::StdDev; break;
        case 3: stretch.kind = StretchKind::HistogramEqualize; break;
        default: stretch.kind = StretchKind::Linear; break;
        }
        stretch.clipPercent = static_cast<float>(clipPercent);
        stretch.stddevK = static_cast<float>(stddevMult);
        for (int b = 1; b <= bands && ok; ++b) {
            ok = streamBandStretch(src, b, bandNodata[b - 1], stretch, dst, kTileDim);
            context.reportProgress(static_cast<double>(b) / bands, "Stretching bands");
        }
    } else if (methodIndex == 1) {
        // Spatial filter: halo tiles per band (halo = kernel radius; the
        // Sobel/Laplacian edge filters use a fixed 3×3 window).
        const int half = (filterType == 3 || filterType == 4) ? 1 : kernelSize / 2;
        for (int b = 1; b <= bands && ok; ++b) {
            WindowedTileFn kernel;
            switch (filterType) {
            case 0: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        convolveTileMean(tile, buf, core, kernelSize); }; break;
            case 1: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        convolveTileGaussian(tile, buf, core, kernelSize, static_cast<float>(sigma)); }; break;
            case 2: {
                // Full-frame medianFilter clamps the kernel to 7x7 — keep
                // the streamed path behaviorally identical (review P2).
                const int medianKernel = std::min(kernelSize, 7);
                kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        convolveTileMedian(tile, buf, core, medianKernel); }; break;
            }
            case 3: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        convolveTileSobel(tile, buf, core); }; break;
            case 4: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        convolveTileLaplacian(tile, buf, core); }; break;
            }
            ok = streamBandWindowed(src, b, dst, kTileDim, half, kernel);
            context.reportProgress(static_cast<double>(b) / bands, "Filtering bands");
        }
    } else if (methodIndex == 2) {
        // Band ratio / IHS — stream only the involved bands (band-pair or
        // band-triple BIP tiles), never the whole band stack.
        if (ratioType == 0) {
            const std::vector<int> pair = { std::min(band1, bands), std::min(band2, bands) };
            GdalMultibandBlockStream stream(src, pair, kTileDim, kTileDim);
            std::vector<float> band1Buf(static_cast<size_t>(kTileDim) * kTileDim);
            std::vector<float> band2Buf(static_cast<size_t>(kTileDim) * kTileDim);
            std::vector<float> out(static_cast<size_t>(kTileDim) * kTileDim);
            ok = stream.forEach([&](const GdalBlockStream::Tile &tile, const float *bip) {
                const size_t n = static_cast<size_t>(tile.width) * tile.height;
                for (size_t i = 0; i < n; ++i) {
                    band1Buf[i] = bip[i * 2];
                    band2Buf[i] = bip[i * 2 + 1];
                }
                bandRatioTile(band1Buf.data(), band2Buf.data(), out.data(), n);
                return dst.writeTile(1, tile, out.data());
            });
        } else {
            // IHS decomposition — true I/H/S components (panel-side fix for
            // #380), applied per band-triple tile with NaN masking.
            const std::vector<int> triple = { std::min(band1, bands), std::min(band2, bands),
                                              std::min(band3, bands) };
            const float ndR = bandNodata[triple[0] - 1];
            const float ndG = bandNodata[triple[1] - 1];
            const float ndB = bandNodata[triple[2] - 1];
            GdalMultibandBlockStream stream(src, triple, kTileDim, kTileDim);
            std::vector<float> outI(static_cast<size_t>(kTileDim) * kTileDim);
            std::vector<float> outH(static_cast<size_t>(kTileDim) * kTileDim);
            std::vector<float> outS(static_cast<size_t>(kTileDim) * kTileDim);
            ok = stream.forEach([&](const GdalBlockStream::Tile &tile, const float *bip) {
                const size_t n = static_cast<size_t>(tile.width) * tile.height;
                ihsTransformTile(bip, ndR, ndG, ndB, outI.data(), outH.data(), outS.data(), n);
                return dst.writeTile(1, tile, outI.data())
                    && dst.writeTile(2, tile, outH.data())
                    && dst.writeTile(3, tile, outS.data());
            });
        }
    } else {
        // Speckle filter: the same tile-window kernels as the speckle
        // dialog (halo = kernel radius).
        const int half = speckleKernel / 2;
        for (int b = 1; b <= bands && ok; ++b) {
            WindowedTileFn kernel;
            switch (speckleType) {
            case 0: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        speckleTileLee(tile, buf, core, speckleKernel, static_cast<float>(noiseVar)); }; break;
            case 1: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        speckleTileFrost(tile, buf, core, speckleKernel, static_cast<float>(damping)); }; break;
            case 2: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        speckleTileKuan(tile, buf, core, speckleKernel, static_cast<float>(noiseVar)); }; break;
            case 3: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                        speckleTileGammaMap(tile, buf, core, speckleKernel, static_cast<float>(noiseVar)); }; break;
            }
            ok = streamBandWindowed(src, b, dst, kTileDim, half, kernel);
            context.reportProgress(static_cast<double>(b) / bands, "Despeckling bands");
        }
    }

    if (!ok) {
        // Abandon: close removes the partial output (#647).
        dst.abandon();
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Enhancement tile pass failed; partial output removed");
    }

    context.throwIfCancelled();
    if (!dst.closeWithError(&closeError)) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Failed to finalize output: " + closeError.toStdString());
    }

    context.reportProgress(1.0, "Complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["bands"] = outBands;
    return result;
}

} // namespace sicnu::operators::rs
