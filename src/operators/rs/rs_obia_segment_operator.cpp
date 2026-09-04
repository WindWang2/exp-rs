/***************************************************************************
 * rs_obia_segment_operator.cpp  —  OBIA segmentation over the analysis layer
 *
 * ADR 0060 — the simple engine delegates to RsSimpleSegmenter (analysis, the
 * single teaching segmenter) and writes labels via RsSegmentMap::toGeoTIFF
 * (ADR 0054). Issue #663 — the OTB MeanShift engine (and the prefer-OTB /
 * teaching-fallback `auto` policy, ADR 0058) that the OBIA GUI used to own
 * in src/app now lives here behind the operator seam; frontends select an
 * engine, they do not call segmenters.
 ***************************************************************************/
#include "rs_obia_segment_operator.h"

#include "analysis/segmentation/rs_otb_segmenter.h"
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

namespace {

const std::vector<std::string> s_engines = {"simple", "otb", "auto"};

} // namespace

Json::Value RsObiaSegmentOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster to segment");
    props["output"] = makeOutputParam("output", "Output label raster (UInt32)", "tif");
    props["engine"] = makeEnumParam(
        "engine",
        "Segmentation engine: simple (teaching, default), otb (MeanShift, fail-closed), "
        "auto (prefer OTB, fall back to teaching)",
        s_engines, "simple");
    props["smoothKernel"] = makeIntegerParam("smoothKernel", "Gaussian kernel size (odd, simple engine)", 5);
    props["quantizeBins"] = makeIntegerParam("quantizeBins", "Intensity quantization levels (simple engine)", 32);
    props["minRegionSize"] = makeIntegerParam("minRegionSize", "Merge regions smaller than this (both engines)", 50);
    props["spatialRadius"] = makeIntegerParam("spatialRadius", "MeanShift spatial radius (otb engine)", 5);
    props["rangeRadius"] = makeNumberParam("rangeRadius", "MeanShift range radius (otb engine)", 15.0);
    props["maxIterations"] = makeIntegerParam("maxIterations", "MeanShift iteration cap (otb engine)", 100);
    props["threshold"] = makeNumberParam("threshold", "MeanShift convergence threshold (otb engine)", 0.1);

    Json::Value bands = makeStringParam("bands", "Optional 1-based band indices for mean intensity", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Label map", "tif");
    outputs["segments"] = makeIntegerParam("segments", "Number of segments", 0);
    outputs["engine"] = makeEnumParam("engine", "Engine that produced the output", s_engines, "simple");

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
    meta["purpose"] = "Create object primitives for OBIA workflows (teaching or OTB MeanShift quality)";
    meta["limitations"] =
        "engine=simple is a teaching segmenter (not OTB MeanShift quality); engine=otb/auto "
        "requires the OTB Segmentation CLI (SICNU_OTB_PATH) — otb fails closed, auto falls back";
    Json::Value useCases(Json::arrayValue);
    useCases.append("Object primitives for rs:obia_features / rs:obia_classify chains");
    useCases.append("Interactive OBIA window segmentation (engine=auto)");
    meta["useCases"] = useCases;
    Json::Value hints(Json::arrayValue);
    hints.append("Chain with rs:obia_features (labels = this output) then rs:obia_classify");
    meta["workflowHints"] = hints;
    return meta;
}

Json::Value RsObiaSegmentOperator::executionEstimate() const
{
    // FullRaster (default policy): input bands plus the segmenter working set —
    // per-band smoothed/quantized copies, the composite key grid and the UInt32
    // label map are all resident simultaneously. The OTB engine runs as an
    // external CLI (its RAM is outside this process) and writes a temp label
    // raster before rehydration.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 67108864; // ~16 x 1024x1024 working buffers (64 MiB)
    est["temporaryDiskBytes"] = 8388608; // OTB temp label raster (otb/auto engines)
    return est;
}

namespace {

/// Teaching engine: band reads → RsSimpleSegmenter (ADR 0060 single stack).
RsSegmentMap runSimpleEngine(const std::string& inputPath,
                             const std::vector<int>& bands,
                             int smoothKernel, int quantizeBins, int minRegionSize,
                             RSOperatorContext& context,
                             GdalDatasetWrapper& ds, int width, int height) {
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
    RsSegmentMap segMap = RsSimpleSegmenter::segmentMultiBand(
        bandPtrs.data(), nBands, width, height, nodata, segParams,
        [&context]() { return context.isCancelled(); },
        [&context](float f) { context.reportProgress(0.15 + 0.7 * f, "Segmenting"); });
    context.throwIfCancelled();
    return segMap;
}

/// OTB MeanShift engine: the analysis-layer OTB adapter (ADR 0058 raster
/// dialect, one CLI dialect for all OBIA paths).
RsSegmentMap runOtbEngine(const std::string& inputPath,
                          int spatialRadius, double rangeRadius, int minRegionSize,
                          int maxIterations, double threshold,
                          RSOperatorContext& context) {
    RsLevelSpec spec;
    spec.filter = RsLevelSpec::Filter::MeanShift;
    spec.spatialRadius = spatialRadius;
    spec.rangeRadius = rangeRadius;
    spec.minRegionSize = minRegionSize;
    spec.maxIterations = maxIterations;
    spec.threshold = threshold;

    context.reportProgress(0.1, "OTB MeanShift segmentation");
    RsSegmenterResult otb = RsOtbSegmenter{}.segment(
        QString::fromStdString(inputPath), spec,
        [&context]() { return context.isCancelled(); });
    context.throwIfCancelled();
    if (!otb.ok)
        throw RSOperatorError(ErrorCode::OtbError, otb.errorMessage.toStdString());
    return otb.segMap;
}

} // namespace

Json::Value RsObiaSegmentOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string engine = getEnum(params, "engine", s_engines, "simple");
    const int smoothKernel = getInt(params, "smoothKernel", 5);
    const int quantizeBins = getInt(params, "quantizeBins", 32);
    const int minRegionSize = getInt(params, "minRegionSize", 50);
    const int spatialRadius = getInt(params, "spatialRadius", 5);
    const double rangeRadius = getDouble(params, "rangeRadius", 15.0);
    const int maxIterations = getInt(params, "maxIterations", 100);
    const double threshold = getDouble(params, "threshold", 0.1);

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    }

    // Validate only the parameters the selected engine(s) can execute —
    // `auto` may run either engine, so both sets must be valid there.
    const bool mayRunSimple = engine == "simple" || engine == "auto";
    const bool mayRunOtb = engine == "otb" || engine == "auto";
    if (mayRunSimple) {
        if (smoothKernel <= 0 || smoothKernel % 2 == 0)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "smoothKernel must be an odd positive integer");
        if (quantizeBins <= 0 || quantizeBins > 256)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "quantizeBins must be between 1 and 256");
    }
    if (minRegionSize < 0)
        throw RSOperatorError(ErrorCode::InvalidParameter, "minRegionSize must be >= 0");
    if (mayRunOtb) {
        if (spatialRadius <= 0)
            throw RSOperatorError(ErrorCode::InvalidParameter, "spatialRadius must be > 0");
        if (rangeRadius <= 0.0)
            throw RSOperatorError(ErrorCode::InvalidParameter, "rangeRadius must be > 0");
        if (maxIterations <= 0)
            throw RSOperatorError(ErrorCode::InvalidParameter, "maxIterations must be > 0");
        if (threshold <= 0.0)
            throw RSOperatorError(ErrorCode::InvalidParameter, "threshold must be > 0");
    }

    ensureGdalInit();

    RsSegmentMap segMap;
    std::string usedEngine;

    if (engine == "otb") {
        if (!RsOtbSegmenter::isAvailable())
            throw RSOperatorError(
                ErrorCode::OtbError,
                "OTB Segmentation CLI not found — set SICNU_OTB_PATH or install OTB "
                "(engine=otb fails closed; use engine=auto for the teaching fallback)");
        segMap = runOtbEngine(inputPath, spatialRadius, rangeRadius, minRegionSize,
                              maxIterations, threshold, context);
        usedEngine = "otb";
    } else if (engine == "auto") {
        // ADR 0058 policy, now owned by the operator: prefer OTB, fall back to
        // the teaching segmenter when OTB is missing or fails — the OBIA task
        // must still produce an acceptable result on machines without OTB.
        if (RsOtbSegmenter::isAvailable()) {
            try {
                segMap = runOtbEngine(inputPath, spatialRadius, rangeRadius, minRegionSize,
                                      maxIterations, threshold, context);
                usedEngine = "otb";
            } catch (const RSOperatorError& e) {
                if (e.code() == ErrorCode::Cancelled)
                    throw;
                context.logWarning("OTB segmentation failed, falling back to the teaching "
                                   "segmenter: " + e.message());
            }
        }
        if (segMap.isEmpty() && usedEngine.empty()) {
            if (!RsOtbSegmenter::isAvailable())
                context.logWarning("OTB Segmentation CLI not found (SICNU_OTB_PATH); "
                                   "using the teaching segmenter");
            GdalDatasetWrapper ds;
            if (!ds.open(QString::fromStdString(inputPath)))
                throw RSOperatorError(ErrorCode::GdalError, "Failed to open input");
            const std::vector<int> bands = parseBands(params, ds.bandCount());
            segMap = runSimpleEngine(inputPath, bands, smoothKernel, quantizeBins,
                                     minRegionSize, context, ds, ds.width(), ds.height());
            usedEngine = "simple";
        }
    } else { // simple
        GdalDatasetWrapper ds;
        if (!ds.open(QString::fromStdString(inputPath)))
            throw RSOperatorError(ErrorCode::GdalError, "Failed to open input");
        const std::vector<int> bands = parseBands(params, ds.bandCount());
        segMap = runSimpleEngine(inputPath, bands, smoothKernel, quantizeBins,
                                 minRegionSize, context, ds, ds.width(), ds.height());
        usedEngine = "simple";
    }

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
    result["engine"] = usedEngine;
    result["width"] = segMap.width();
    result["height"] = segMap.height();
    return result;
}

} // namespace sicnu::operators::rs
