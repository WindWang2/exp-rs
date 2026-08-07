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
    context.reportProgress(0.15, "Reading bands");

    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> pixels(pixelCount * static_cast<size_t>(nBands), 0.0f);
    for (int bi = 0; bi < nBands; ++bi)
    {
        std::vector<float> bandData(pixelCount);
        if (!ds.readBandData(bands[bi], bandData.data(), width, height))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bands[bi]));
        for (size_t p = 0; p < pixelCount; ++p)
            pixels[p * static_cast<size_t>(nBands) + bi] = bandData[p];
    }

    context.reportProgress(0.45, "Unmixing");
    context.throwIfCancelled();

    SpectralUnmixing::UnmixResult unmixResult;
    QString error;
    if (!SpectralUnmixing::unmix(pixels.data(), pixelCount, nBands,
                                 endmembers.data(), nEndmembers,
                                 &unmixResult, &error))
        throw RSOperatorError(ErrorCode::ComputationError,
                              error.isEmpty() ? "Spectral unmixing failed" : error.toStdString());

    context.reportProgress(0.75, "Writing abundance raster");

    // One abundance band per endmember (pixel-major -> band-major).
    std::vector<std::vector<float>> abundanceBands(
        nEndmembers, std::vector<float>(pixelCount));
    for (int e = 0; e < nEndmembers; ++e)
        for (size_t p = 0; p < pixelCount; ++p)
            abundanceBands[e][p] = unmixResult.abundances[p * static_cast<size_t>(nEndmembers) + e];

    QString writeError;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, abundanceBands,
                         ds.geoTransform(), ds.projection(), &writeError))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write abundance raster: " + writeError.toStdString());

    // Optional reconstruction-error band.
    const std::string errorPath = getString(params, "errorOut", "");
    if (!errorPath.empty())
    {
        if (!writeGdalOutput(QString::fromStdString(errorPath), width, height,
                             {unmixResult.reconstructionError},
                             ds.geoTransform(), ds.projection(), &writeError))
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to write error raster: " + writeError.toStdString());
    }

    const double meanError = unmixResult.reconstructionError.empty()
        ? 0.0
        : std::accumulate(unmixResult.reconstructionError.begin(),
                          unmixResult.reconstructionError.end(), 0.0)
              / static_cast<double>(unmixResult.reconstructionError.size());

    ds.close();
    context.reportProgress(1.0, "Spectral unmixing complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["endmembers"] = nEndmembers;
    result["meanError"] = meanError;
    return result;
}

} // namespace sicnu::operators::rs
