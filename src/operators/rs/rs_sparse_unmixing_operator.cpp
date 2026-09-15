/***************************************************************************
 * rs_sparse_unmixing_operator.cpp  —  sparse (L1, non-negative) unmixing
 ***************************************************************************/
#include "rs_sparse_unmixing_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_sparse_unmixing.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "rs_spectral_reference_input.h"

#include <gdal.h>

#include <QString>

#include <cmath>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsSparseUnmixingOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band raster to unmix");
    props["output"] = makeOutputParam("output", "Abundance raster (one band per atom)", "tif");
    const Json::Value referenceProps = referenceInputSchemaProps(
        "endmembers", "endmembersRef", "Endmember dictionary: array of arrays of band-count floats");
    for (const auto& key : referenceProps.getMemberNames())
        props[key] = referenceProps[key];
    props["bands"] = makeIntegerParam("bands", "1-based band subset (reserved; default all)", 0);
    props["errorOut"] = makeOutputParam("errorOut", "Optional per-pixel reconstruction-error raster", "tif");
    props["lambda"] = makeNumberParam("lambda", "L1 sparsity weight (>= 0)", 0.01);
    props["sumToOnePenalty"] = makeNumberParam("sumToOnePenalty",
        "Sum-to-one penalty rho (>= 0; FCLS uses ~1e6 * mean diag); 0 = free", 0.0);
    props["maxIterations"] = makeIntegerParam("maxIterations", "Per-pixel FISTA iteration cap", 2000);
    props["tolerance"] = makeNumberParam("tolerance", "Relative iterate change stopping rule", 1e-8);
    props["collinearAngleDegrees"] = makeNumberParam("collinearAngleDegrees",
        "Pairwise spectral-angle refusal threshold; 0 disables the guard", 0.5);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Abundance raster path");
    outputs["atoms"] = makeIntegerParam("atoms", "Number of dictionary atoms", 0);
    outputs["meanError"] = makeNumberParam("meanError", "Mean reconstruction error", 0.0);
    outputs["convergedFraction"] = makeNumberParam("convergedFraction",
        "Fraction of valid pixels whose FISTA run reached tolerance", 0.0);
    outputs["meanIterations"] = makeNumberParam("meanIterations",
        "Mean iterations over valid pixels", 0.0);
    outputs["lambda"] = makeNumberParam("lambda", "L1 weight applied", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSparseUnmixingOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["task"] = "unmixing";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("spectral");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("unmixing");
    meta["tags"].append("sparse");
    meta["purpose"] = "Estimate sparse per-pixel abundances against an "
                      "(overcomplete) endmember dictionary with L1 + "
                      "non-negativity constraints.";
    meta["prerequisites"].append("Atoms must use the same band order and units as the input raster.");
    meta["workflowHints"].append("Prefer an overcomplete library dictionary when the scene's "
                                 "materials are not known in advance; lambda controls support size.");
    meta["limitations"].append("Sum-to-one is a penalty (like the FCLS solver), not a hard "
                               "constraint; report mean |sum-1| via sumToOnePenalty QA if needed.");
    meta["limitations"].append("Near-duplicate atoms (below collinearAngleDegrees) refuse: "
                               "an L1 split across near-identical atoms is not interpretable.");
    return meta;
}

Json::Value RsSparseUnmixingOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 512;
    est["tileHeight"] = 512;
    est["estimatedRamBytes"] = 12582912; // 12 MiB tile working set + O(atoms^2) dictionary
    est["temporaryDiskBytes"] = 0;
    return est;
}

Json::Value RsSparseUnmixingOperator::run(const Json::Value& params,
                                          RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    const double lambda = getDouble(params, "lambda", 0.01);
    const double sumToOnePenalty = getDouble(params, "sumToOnePenalty", 0.0);
    const int maxIterations = getInt(params, "maxIterations", 2000);
    const double tolerance = getDouble(params, "tolerance", 1e-8);
    const double collinearAngleDegrees = getDouble(params, "collinearAngleDegrees", 0.5);

    SpectralSparseUnmixing::Config config;
    config.lambda = lambda;
    config.sumToOnePenalty = sumToOnePenalty;
    config.maxIterations = maxIterations;
    config.tolerance = tolerance;
    config.collinearAngleDegrees = collinearAngleDegrees;

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (bandCount < 1)
        throw RSOperatorError(ErrorCode::InvalidParameter, "Input raster has no bands");

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
                              "At least one dictionary atom is required");
    // NOTE: no nEndmembers <= bands refusal — overcomplete dictionaries are
    // the point of sparse unmixing; the kernel's guards protect interpreability.

    context.logInfo("Sparse unmixing: " + std::to_string(nEndmembers) + " atoms over "
                    + std::to_string(nBands) + " bands, lambda " + std::to_string(lambda));

    // Build the dictionary ONCE (Gram + Lipschitz). Fail-closed refusals
    // happen here, before any pixel is processed.
    SpectralSparseUnmixing::Dictionary dictionary;
    {
        QString dictionaryError;
        if (!SpectralSparseUnmixing::buildDictionary(endmembers.data(), nBands, nEndmembers,
                                                     config, &dictionary, &dictionaryError))
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  dictionaryError.isEmpty() ? "Dictionary build failed"
                                                            : dictionaryError.toStdString());
    }

    std::vector<std::pair<bool, double>> bandNoData(nBands);
    for (int bi = 0; bi < nBands; ++bi) {
        bool hasNodata = false;
        double nd = ds.bandNoDataValue(bands[bi], &hasNodata);
        bandNoData[bi] = {hasNodata, nd};
    }

    const int tileWidth = 512;
    const int tileHeight = 512;

    GdalDatasetWrapper outDataset;
    if (!outDataset.create(QString::fromStdString(outputPath), width, height, nEndmembers,
                           GDT_Float32, ds.geoTransform(), ds.projection()))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output abundance dataset: " + outputPath);
    for (int e = 0; e < nEndmembers; ++e)
        outDataset.setBandNoDataValue(e + 1, std::numeric_limits<float>::quiet_NaN());

    const std::string errorPath = getString(params, "errorOut", "");
    GdalDatasetWrapper errorDataset;
    if (!errorPath.empty()) {
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
    uint64_t totalValidPixels = 0;
    uint64_t convergedPixels = 0;
    double iterationsSum = 0.0;
    double sumDeviationAcc = 0.0;
    uint64_t sumDeviationPixels = 0;

    const int totalBlocksY = (height + tileHeight - 1) / tileHeight;
    int processedBlocksY = 0;

    try {
        for (int y = 0; y < height; y += tileHeight) {
            context.throwIfCancelled();
            const int bh = std::min(tileHeight, height - y);
            for (int x = 0; x < width; x += tileWidth) {
                const int bw = std::min(tileWidth, width - x);
                const size_t tileSize = static_cast<size_t>(bw) * bh;

                for (int bi = 0; bi < nBands; ++bi) {
                    if (!ds.readBandWindow(bands[bi], x, y, bw, bh, bandData.data()))
                        throw RSOperatorError(ErrorCode::GdalError,
                                              "Failed to read band window "
                                                  + std::to_string(bands[bi]) + " at ("
                                                  + std::to_string(x) + "," + std::to_string(y) + ")");
                    for (size_t p = 0; p < tileSize; ++p) {
                        float val = bandData[p];
                        if (bandNoData[bi].first && val == static_cast<float>(bandNoData[bi].second))
                            val = std::numeric_limits<float>::quiet_NaN();
                        tilePixels[p * static_cast<size_t>(nBands) + bi] = val;
                    }
                }

                SpectralSparseUnmixing::SparseUnmixResult sparseResult;
                QString kernelError;
                if (!SpectralSparseUnmixing::unmixSparse(tilePixels.data(), tileSize, nBands,
                                                         endmembers.data(), nEndmembers,
                                                         config, &sparseResult, &kernelError))
                    throw RSOperatorError(ErrorCode::ComputationError,
                                          kernelError.isEmpty() ? "Sparse unmixing failed"
                                                                : kernelError.toStdString());

                for (int e = 0; e < nEndmembers; ++e) {
                    for (size_t p = 0; p < tileSize; ++p)
                        abundanceBuffer[p] =
                            sparseResult.abundances[p * static_cast<size_t>(nEndmembers) + e];
                    if (!outDataset.writeBandWindow(e + 1, x, y, bw, bh, abundanceBuffer.data()))
                        throw RSOperatorError(ErrorCode::FileNotWritable,
                                              "Failed to write abundance band window");
                }

                if (!errorPath.empty()
                    && !errorDataset.writeBandWindow(1, x, y, bw, bh,
                                                     sparseResult.reconstructionError.data()))
                    throw RSOperatorError(ErrorCode::FileNotWritable,
                                          "Failed to write reconstruction error band window");

                for (size_t p = 0; p < tileSize; ++p) {
                    const float err = sparseResult.reconstructionError[p];
                    if (!std::isnan(err)) {
                        totalErrorSum += err;
                        ++totalValidPixels;
                        iterationsSum += sparseResult.iterations[p];
                        if (sparseResult.converged[p])
                            ++convergedPixels;
                        sumDeviationAcc += std::abs(
                            static_cast<double>(sparseResult.abundanceSums[p]) - 1.0);
                        ++sumDeviationPixels;
                    }
                }
            }
            ++processedBlocksY;
            context.reportProgress(0.05 + 0.9 * (static_cast<double>(processedBlocksY) / totalBlocksY),
                                   "Sparse unmixing tile rows");
        }
    } catch (...) {
        throw;
    }

    const double meanError = totalValidPixels > 0
                                 ? totalErrorSum / static_cast<double>(totalValidPixels)
                                 : 0.0;
    const double convergedFraction = totalValidPixels > 0
                                         ? static_cast<double>(convergedPixels)
                                               / static_cast<double>(totalValidPixels)
                                         : 0.0;
    const double meanIterations = totalValidPixels > 0
                                      ? iterationsSum / static_cast<double>(totalValidPixels)
                                      : 0.0;

    ds.close();
    QString closeError;
    if (!outDataset.closeWithError(&closeError))
        throw RSOperatorError(ErrorCode::GdalError,
                              closeError.isEmpty() ? "Failed to finalize abundance raster"
                                                   : closeError.toStdString());
    if (!errorPath.empty()) {
        QString errorCloseError;
        if (!errorDataset.closeWithError(&errorCloseError))
            throw RSOperatorError(ErrorCode::GdalError,
                                  errorCloseError.isEmpty()
                                      ? "Failed to finalize reconstruction-error raster"
                                      : errorCloseError.toStdString());
    }

    context.reportProgress(1.0, "Sparse unmixing complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["atoms"] = nEndmembers;
    result["meanError"] = meanError;
    result["lambda"] = lambda;
    result["sumToOnePenalty"] = sumToOnePenalty;
    result["convergedFraction"] = convergedFraction;
    result["meanIterations"] = meanIterations;
    result["meanAbundanceSumDeviation"] =
        sumDeviationPixels > 0
            ? sumDeviationAcc / static_cast<double>(sumDeviationPixels)
            : 0.0;
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
