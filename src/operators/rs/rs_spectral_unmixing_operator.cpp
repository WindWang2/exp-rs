/***************************************************************************
 * rs_spectral_unmixing_operator.cpp  —  Linear spectral unmixing RSOperator
 ***************************************************************************/
#include "rs_spectral_unmixing_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_unmixing.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <numeric>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// Parses the `endmembers` parameter (array of band-count arrays) into the
/// endmember-major flat buffer the kernel expects. Mirrors the reference
/// parsing in rs_sam_classify_operator.
std::vector<float> parseEndmembers(const Json::Value& endmembers, int bandCount)
{
    if (!endmembers.isArray() || endmembers.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "'endmembers' must be a non-empty array of spectra");

    std::vector<float> flat;
    flat.reserve(static_cast<size_t>(endmembers.size()) * static_cast<size_t>(bandCount));
    int idx = 0;
    for (const auto& entry : endmembers)
    {
        if (!entry.isArray() || static_cast<int>(entry.size()) != bandCount)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Endmember " + std::to_string(idx) +
                                      " must be an array of " + std::to_string(bandCount) +
                                      " numbers");
        for (Json::ArrayIndex b = 0; b < entry.size(); ++b)
        {
            if (!entry[b].isNumeric())
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "Endmember " + std::to_string(idx) +
                                          " contains a non-numeric value");
            flat.push_back(static_cast<float>(entry[b].asDouble()));
        }
        ++idx;
    }
    return flat;
}

/// Parses the 1-based band subset; empty = all bands.
std::vector<int> parseBands(const Json::Value& params, int bandCount)
{
    std::vector<int> bands;
    if (params.isMember("bands") && params["bands"].isArray() && !params["bands"].empty())
    {
        for (Json::ArrayIndex i = 0; i < params["bands"].size(); ++i)
            bands.push_back(params["bands"][i].asInt());
    }
    else
    {
        for (int b = 1; b <= bandCount; ++b)
            bands.push_back(b);
    }
    return bands;
}

} // anonymous namespace

Json::Value RsSpectralUnmixingOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band raster to unmix");
    props["output"] = makeOutputParam("output", "Abundance raster (one band per endmember)", "tif");
    Json::Value endsParam(Json::objectValue);
    endsParam["type"] = "array";
    endsParam["description"] = "Endmember spectra: array of arrays of band-count floats";
    endsParam["items"]["type"] = "array";
    endsParam["items"]["items"]["type"] = "number";
    props["endmembers"] = endsParam;
    props["bands"] = makeIntegerParam("bands", "1-based band subset (reserved; default all)", 0);
    props["errorOut"] = makeOutputParam("errorOut", "Optional per-pixel reconstruction-error raster", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Abundance raster path");
    outputs["endmembers"] = makeIntegerParam("endmembers", "Number of endmembers", 0);
    outputs["meanError"] = makeNumberParam("meanError", "Mean reconstruction error", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "endmembers"});
    return root;
}

Json::Value RsSpectralUnmixingOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("unmixing");
    meta["purpose"] = "Estimate per-pixel endmember abundances (mixture analysis).";
    meta["prerequisites"].append("Endmembers must use the same band order and units as the input raster.");
    meta["workflowHints"].append("Run after dimensionality reduction (rs:mnf / rs:pca) or "
                                 "with endmembers from a spectral library.");
    meta["limitations"].append("Abundances are least-squares estimates clipped to [0,1] and "
                               "renormalized to unit sum (approximate fully constrained unmixing).");
    return meta;
}

Json::Value RsSpectralUnmixingOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 512;
    est["tileHeight"] = 512;
    est["estimatedRamBytes"] = 12582912; // 12 MiB tile working set
    est["temporaryDiskBytes"] = 0;
    return est;
}

Json::Value RsSpectralUnmixingOperator::run(const Json::Value& params,
                                            RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (bandCount < 1)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Input raster has no bands");

    const std::vector<int> bands = parseBands(params, bandCount);
    const int nBands = static_cast<int>(bands.size());

    const std::vector<float> endmembers = parseEndmembers(params["endmembers"], nBands);
    const int nEndmembers = static_cast<int>(endmembers.size() / static_cast<size_t>(nBands));
    if (nEndmembers > nBands)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Endmember count (" + std::to_string(nEndmembers) +
                                  ") exceeds the band count (" + std::to_string(nBands) + ")");

    context.logInfo("Spectral unmixing: " + std::to_string(nEndmembers) +
                    " endmembers over " + std::to_string(nBands) + " bands");
    context.reportProgress(0.1, "Initializing datasets");

    const int tileWidth = 512;
    const int tileHeight = 512;

    GdalDatasetWrapper outDataset;
    if (!outDataset.create(QString::fromStdString(outputPath), width, height, nEndmembers, GDT_Float32,
                           ds.geoTransform(), ds.projection()))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output abundance dataset: " + outputPath);

    const std::string errorPath = getString(params, "errorOut", "");
    GdalDatasetWrapper errorDataset;
    if (!errorPath.empty())
    {
        if (!errorDataset.create(QString::fromStdString(errorPath), width, height, 1, GDT_Float32,
                                 ds.geoTransform(), ds.projection()))
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to create error output dataset: " + errorPath);
    }

    std::vector<float> tilePixels(tileWidth * tileHeight * static_cast<size_t>(nBands));
    std::vector<float> bandData(tileWidth * tileHeight);
    std::vector<float> abundanceBuffer(tileWidth * tileHeight);

    double totalErrorSum = 0.0;
    uint64_t totalUnmixedPixels = 0;

    const int totalBlocksY = (height + tileHeight - 1) / tileHeight;
    int processedBlocksY = 0;

    for (int y = 0; y < height; y += tileHeight) {
        context.throwIfCancelled();
        const int bh = std::min(tileHeight, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            const int bw = std::min(tileWidth, width - x);
            const size_t tileSize = static_cast<size_t>(bw) * bh;

            // Read band windows into tilePixels (pixel-major for unmix kernel)
            for (int bi = 0; bi < nBands; ++bi) {
                if (!ds.readBandWindow(bands[bi], x, y, bw, bh, bandData.data()))
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to read band window " + std::to_string(bands[bi]) +
                                          " at (" + std::to_string(x) + "," + std::to_string(y) + ")");
                for (size_t p = 0; p < tileSize; ++p)
                    tilePixels[p * static_cast<size_t>(nBands) + bi] = bandData[p];
            }

            SpectralUnmixing::UnmixResult unmixResult;
            QString errorMsg;
            if (!SpectralUnmixing::unmix(tilePixels.data(), tileSize, nBands,
                                         endmembers.data(), nEndmembers,
                                         &unmixResult, &errorMsg))
                throw RSOperatorError(ErrorCode::ComputationError,
                                      errorMsg.isEmpty() ? "Spectral unmixing failed" : errorMsg.toStdString());

            // Write abundance bands
            for (int e = 0; e < nEndmembers; ++e) {
                for (size_t p = 0; p < tileSize; ++p)
                    abundanceBuffer[p] = unmixResult.abundances[p * static_cast<size_t>(nEndmembers) + e];

                if (!outDataset.writeBandWindow(e + 1, x, y, bw, bh, abundanceBuffer.data()))
                    throw RSOperatorError(ErrorCode::FileNotWritable,
                                          "Failed to write abundance band window " + std::to_string(e + 1));
            }

            // Write error band if requested
            if (!errorPath.empty()) {
                if (!errorDataset.writeBandWindow(1, x, y, bw, bh, unmixResult.reconstructionError.data()))
                    throw RSOperatorError(ErrorCode::FileNotWritable,
                                          "Failed to write reconstruction error band window");
            }

            for (double err : unmixResult.reconstructionError) {
                totalErrorSum += err;
                totalUnmixedPixels++;
            }
        }
        processedBlocksY++;
        context.reportProgress(0.1 + 0.85 * (static_cast<double>(processedBlocksY) / totalBlocksY), "Unmixing tile rows");
    }

    const double meanError = (totalUnmixedPixels > 0) ? (totalErrorSum / static_cast<double>(totalUnmixedPixels)) : 0.0;

    ds.close();
    outDataset.close();
    if (!errorPath.empty()) errorDataset.close();

    context.reportProgress(1.0, "Spectral unmixing complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["endmembers"] = nEndmembers;
    result["meanError"] = meanError;
    return result;
}

} // namespace sicnu::operators::rs
