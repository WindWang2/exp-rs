/***************************************************************************
 * rs_spectral_similarity_operator.cpp  —  SID-SAM hybrid similarity
 ***************************************************************************/
#include "rs_spectral_similarity_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_hybrid_similarity.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "rs_spectral_reference_input.h"

#include <gdal.h>

#include <QString>

#include <cmath>
#include <limits>
#include <memory>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsSpectralSimilarityOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output",
        "Label raster (Float32; -9999 where unlabelled/invalid)", "tif");
    const Json::Value referenceProps = referenceInputSchemaProps(
        "refs", "refsRef", "Reference spectra: array of arrays of band-count floats");
    for (const auto& key : referenceProps.getMemberNames())
        props[key] = referenceProps[key];
    props["bands"] = makeIntegerParam("bands", "1-based band subset (reserved; default all)", 0);
    props["scoreOut"] = makeOutputParam("scoreOut", "Optional best-hybrid-score raster", "tif");
    props["form"] = makeEnumParam("form",
        "Hybrid form: bounded product (product_normalized) or classic SID*tan(theta)",
        {"product_normalized", "classic_tan"}, "product_normalized");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Label raster path");
    outputs["refs"] = makeIntegerParam("refs", "Number of reference spectra", 0);
    outputs["meanScore"] = makeNumberParam("meanScore", "Mean best score over labelled pixels", 0.0);
    outputs["form"] = makeStringParam("form", "Hybrid form applied");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSpectralSimilarityOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["task"] = "classification";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("spectral");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("similarity");
    meta["purpose"] = "Classify pixels with a hybrid SAM+SID similarity — "
                      "shape and distribution information in one bounded measure.";
    meta["prerequisites"].append("References must be reflectance-like (non-negative) on the "
                                 "same band grid as the input.");
    meta["workflowHints"].append("product_normalized is bounded in [0,1] and comparable "
                                 "across scenes; classic_tan is the unbounded literature form.");
    meta["limitations"].append("Spectra with negative bands or zero norm are unlabelled "
                               "(NaN score), never forced into a class.");
    return meta;
}

Json::Value RsSpectralSimilarityOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 6291456; // 256^2 tile BIP + label/score buffers
    est["temporaryDiskBytes"] = 0;
    return est;
}

Json::Value RsSpectralSimilarityOperator::run(const Json::Value& params,
                                              RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    const std::string formText = getEnum(params, "form",
                                         {"product_normalized", "classic_tan"},
                                         "product_normalized");
    SpectralHybridSimilarity::Form form = SpectralHybridSimilarity::Form::ProductNormalized;
    if (!SpectralHybridSimilarity::formFromText(formText, &form))
        throw RSOperatorError(ErrorCode::InvalidParameter, "Unknown hybrid form: " + formText);

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

    QString gridError;
    const RasterWavelengthGrid inputGrid = RasterWavelengthGrid::read(ds, bands, &gridError);
    if (!gridError.isEmpty())
        throw RSOperatorError(ErrorCode::InvalidInputData, gridError.toStdString());
    const ResolvedSpectralReference refsResolved =
        resolveSpectralReference(params, "refs", "refsRef", ds, bands, inputGrid);
    const std::vector<float> refs = refsResolved.flat;
    const int refCount = refsResolved.count;
    if (refCount < 1)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "At least one reference spectrum is required");

    context.logInfo("Hybrid similarity: " + std::to_string(refCount)
                    + " references, form " + formText);

    GdalStreamingOutput out(QString::fromStdString(outputPath), width, height, 1,
                            GDT_Float32, ds.geoTransform(), ds.projection());
    if (!out.isOpen())
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create label raster: " + outputPath);
    out.setNoDataValue(-9999.0);

    const std::string scorePath = getString(params, "scoreOut", "");
    std::unique_ptr<GdalStreamingOutput> scoreOut;
    if (!scorePath.empty()) {
        scoreOut = std::make_unique<GdalStreamingOutput>(
            QString::fromStdString(scorePath), width, height, 1, GDT_Float32,
            ds.geoTransform(), ds.projection());
        if (!scoreOut->isOpen())
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to create score raster: " + scorePath);
        scoreOut->setNoDataValue(std::numeric_limits<float>::quiet_NaN());
    }

    constexpr int kTile = 256;
    GdalMultibandBlockStream stream(ds, bands, kTile, kTile);
    const int totalTiles = stream.tileCount();
    const double perTile = totalTiles > 0 ? 1.0 / totalTiles : 0.0;

    double scoreSum = 0.0;
    uint64_t labelledPixels = 0;
    int tilesSeen = 0;

    try {
        std::vector<float> tileLabels;
        std::vector<float> tileScores;
        if (!stream.forEach([&](const GdalMultibandBlockStream::Tile& tile, const float* bip) {
                context.throwIfCancelled();
                const size_t tilePixels = static_cast<size_t>(tile.width) * tile.height;
                tileLabels.assign(tilePixels, -9999.0f);
                if (!scorePath.empty())
                    tileScores.assign(tilePixels, std::numeric_limits<float>::quiet_NaN());

                std::vector<int> labels(tilePixels, -1);
                // Scores are always computed so the meanScore QA is real
                // even when no score raster is requested.
                std::vector<float> scores(tilePixels, 0.0f);
                QString kernelError;
                if (!SpectralHybridSimilarity::classify(
                        bip, tilePixels, nBands, refs.data(), refCount,
                        labels.data(), scores.data(),
                        form, -9999.0f, &kernelError))
                    throw RSOperatorError(ErrorCode::ComputationError,
                                          kernelError.isEmpty()
                                              ? "Hybrid similarity classification failed"
                                              : kernelError.toStdString());

                for (size_t p = 0; p < tilePixels; ++p) {
                    if (labels[p] >= 0) {
                        tileLabels[p] = static_cast<float>(labels[p]);
                        ++labelledPixels;
                        scoreSum += scores[p];
                    }
                    if (!scorePath.empty())
                        tileScores[p] = scores[p];
                }

                if (!out.writeTile(1, tile, tileLabels.data()))
                    return false;
                if (!scorePath.empty() && !scoreOut->writeTile(1, tile, tileScores.data()))
                    return false;
                context.reportProgress((++tilesSeen) * perTile, "Hybrid similarity");
                return true;
            }))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to stream tiles for hybrid similarity");
    } catch (...) {
        out.abandon();
        if (!scorePath.empty())
            scoreOut->abandon();
        throw;
    }

    QString closeError;
    if (!out.closeWithError(&closeError))
        throw RSOperatorError(ErrorCode::GdalError,
                              closeError.isEmpty() ? "Failed to finalize label raster"
                                                   : closeError.toStdString());
    if (!scorePath.empty()) {
        QString scoreError;
        if (!scoreOut->closeWithError(&scoreError))
            throw RSOperatorError(ErrorCode::GdalError,
                                  scoreError.isEmpty() ? "Failed to finalize score raster"
                                                       : scoreError.toStdString());
    }

    ds.close();
    context.reportProgress(1.0, "Hybrid similarity complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["refs"] = refCount;
    result["form"] = formText;
    result["meanScore"] = labelledPixels > 0
                              ? scoreSum / static_cast<double>(labelledPixels)
                              : 0.0;
    result["labelledPixels"] = static_cast<Json::UInt64>(labelledPixels);
    result["referenceSource"] = refsResolved.sourceDescription.toStdString();
    if (refsResolved.resampled)
        result["referencesResampled"] = true;
    if (!refsResolved.license.isEmpty())
        result["referenceLicense"] = refsResolved.license.toStdString();
    if (refsResolved.measured)
        result["referencesMeasured"] = true;
    return result;
}

} // namespace sicnu::operators::rs
