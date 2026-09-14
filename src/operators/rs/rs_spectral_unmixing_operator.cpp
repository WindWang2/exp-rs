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
#include "rs_spectral_reference_input.h"

#include <gdal.h>

#include <QString>

#include <cmath>
#include <numeric>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsSpectralUnmixingOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band raster to unmix");
    props["output"] = makeOutputParam("output", "Abundance raster (one band per endmember)", "tif");
    // Endmember inputs: exactly one of endmembers (inline), endmembersRef
    // (table/library artifact path) or libraryPath (+libraryMaterials).
    const Json::Value referenceProps = referenceInputSchemaProps(
        "endmembers", "endmembersRef", "Endmember spectra: array of arrays of band-count floats");
    for (const auto& key : referenceProps.getMemberNames())
        props[key] = referenceProps[key];
    props["bands"] = makeIntegerParam("bands", "1-based band subset (reserved; default all)", 0);
    props["method"] = makeEnumParam("method", "Abundance estimation method",
                                    {"ols", "fcls"}, "ols");
    props["errorOut"] = makeOutputParam("errorOut", "Optional per-pixel reconstruction-error raster", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Abundance raster path");
    outputs["endmembers"] = makeIntegerParam("endmembers", "Number of endmembers", 0);
    outputs["meanError"] = makeNumberParam("meanError", "Mean reconstruction error", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
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

    // Shared reference seam: inline endmembers / endmembersRef / libraryPath.
    QString gridError;
    const RasterWavelengthGrid inputGrid = RasterWavelengthGrid::read(ds, bands, &gridError);
    if (!gridError.isEmpty())
        throw RSOperatorError(ErrorCode::InvalidInputData, gridError.toStdString());
    const ResolvedSpectralReference endsResolved =
        resolveSpectralReference(params, "endmembers", "endmembersRef", ds, bands, inputGrid);
    const std::vector<float> endmembers = endsResolved.flat;
    const int nEndmembers = endsResolved.count;
    if (nEndmembers < 1)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "At least one endmember spectrum is required");
    if (nEndmembers > nBands)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Endmember count (" + std::to_string(nEndmembers) +
                                  ") exceeds the band count (" + std::to_string(nBands) + ")");

    const std::string method = getEnum(params, "method", {"ols", "fcls"}, "ols");

    context.logInfo("Spectral unmixing: " + std::to_string(nEndmembers) +
                    " endmembers over " + std::to_string(nBands) + " bands");
    context.reportProgress(0.1, "Initializing datasets");

    std::vector<std::pair<bool, double>> bandNoData(nBands);
    for (int bi = 0; bi < nBands; ++bi) {
        bool hasNodata = false;
        double nd = ds.bandNoDataValue(bands[bi], &hasNodata);
        bandNoData[bi] = {hasNodata, nd};
    }

    const int tileWidth = 512;
    const int tileHeight = 512;

    GdalDatasetWrapper outDataset;
    if (!outDataset.create(QString::fromStdString(outputPath), width, height, nEndmembers, GDT_Float32,
                           ds.geoTransform(), ds.projection()))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output abundance dataset: " + outputPath);

    for (int e = 0; e < nEndmembers; ++e)
        outDataset.setBandNoDataValue(e + 1, std::numeric_limits<float>::quiet_NaN());

    const std::string errorPath = getString(params, "errorOut", "");
    GdalDatasetWrapper errorDataset;
    if (!errorPath.empty())
    {
        if (!errorDataset.create(QString::fromStdString(errorPath), width, height, 1, GDT_Float32,
                                 ds.geoTransform(), ds.projection()))
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to create error output dataset: " + errorPath);
        errorDataset.setBandNoDataValue(1, std::numeric_limits<float>::quiet_NaN());
    }

    std::vector<float> tilePixels(tileWidth * tileHeight * static_cast<size_t>(nBands));
    std::vector<float> bandData(tileWidth * tileHeight);
    std::vector<float> abundanceBuffer(tileWidth * tileHeight);

    double totalErrorSum = 0.0;
    uint64_t totalUnmixedPixels = 0;
    double sumDeviationAcc = 0.0;
    uint64_t sumDeviationPixels = 0;

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
                for (size_t p = 0; p < tileSize; ++p) {
                    float val = bandData[p];
                    if (bandNoData[bi].first && val == static_cast<float>(bandNoData[bi].second))
                        val = std::numeric_limits<float>::quiet_NaN();
                    tilePixels[p * static_cast<size_t>(nBands) + bi] = val;
                }
            }

            SpectralUnmixing::UnmixResult unmixResult;
            QString errorMsg;
            const bool unmixOk = (method == "fcls")
                ? SpectralUnmixing::unmixFcls(tilePixels.data(), tileSize, nBands,
                                              endmembers.data(), nEndmembers,
                                              &unmixResult, &errorMsg)
                : SpectralUnmixing::unmix(tilePixels.data(), tileSize, nBands,
                                          endmembers.data(), nEndmembers,
                                          &unmixResult, &errorMsg);
            if (!unmixOk)
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
                if (!std::isnan(err)) {
                    totalErrorSum += err;
                    totalUnmixedPixels++;
                }
            }

            // Abundance-sum QA (documented for FCLS): mean |sum(a) - 1| over
            // valid pixels of the tile.
            {
                double devSum = 0.0;
                uint64_t devCount = 0;
                for (size_t p = 0; p < tileSize; ++p) {
                    const float *ab =
                        &unmixResult.abundances[p * static_cast<size_t>(nEndmembers)];
                    double sum = 0.0;
                    bool valid = true;
                    for (int e = 0; e < nEndmembers; ++e) {
                        if (std::isnan(ab[e])) { valid = false; break; }
                        sum += ab[e];
                    }
                    if (!valid) continue;
                    devSum += std::abs(sum - 1.0);
                    ++devCount;
                }
                sumDeviationAcc += devSum;
                sumDeviationPixels += devCount;
            }
        }
        processedBlocksY++;
        context.reportProgress(0.1 + 0.85 * (static_cast<double>(processedBlocksY) / totalBlocksY), "Unmixing tile rows");
    }

    const double meanError = (totalUnmixedPixels > 0) ? (totalErrorSum / static_cast<double>(totalUnmixedPixels)) : 0.0;

    ds.close();
    QString closeError;
    if (!outDataset.closeWithError(&closeError))
        throw RSOperatorError(ErrorCode::GdalError,
                              closeError.isEmpty()
                                  ? "Failed to finalize abundance raster"
                                  : closeError.toStdString());
    if (!errorPath.empty())
    {
        QString errorCloseError;
        if (!errorDataset.closeWithError(&errorCloseError))
            throw RSOperatorError(ErrorCode::GdalError,
                                  errorCloseError.isEmpty()
                                      ? "Failed to finalize reconstruction-error raster"
                                      : errorCloseError.toStdString());
    }

    context.reportProgress(1.0, "Spectral unmixing complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["endmembers"] = nEndmembers;
    result["meanError"] = meanError;
    result["method"] = method;
    result["abundanceSumDeviation"] =
        (sumDeviationPixels > 0)
            ? (sumDeviationAcc / static_cast<double>(sumDeviationPixels))
            : 0.0;
    // Endmember provenance echo (library/table identity, resampling, license).
    result["endmemberSource"] = endsResolved.sourceDescription.toStdString();
    if (endsResolved.resampled)
        result["endmembersResampled"] = true;
    if (!endsResolved.license.isEmpty())
        result["endmemberLicense"] = endsResolved.license.toStdString();
    if (endsResolved.measured)
        result["endmembersMeasured"] = true;
    return result;
}

} // namespace sicnu::operators::rs
