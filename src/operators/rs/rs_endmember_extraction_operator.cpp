/***************************************************************************
 * rs_endmember_extraction_operator.cpp  —  PPI endmember extraction RSOperator
 ***************************************************************************/
#include "rs_endmember_extraction_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/endmember_extraction.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsEndmemberExtractionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["nEndmembers"] = makeIntegerParam("nEndmembers", "Number of endmembers to extract", 0);
    props["projections"] = makeIntegerParam("projections", "Random projections (min 16)", 1000);

    Json::Value outputs(Json::objectValue);
    outputs["endmembers"] = Json::Value(Json::objectValue);
    outputs["endmembers"]["type"] = "array";
    outputs["endmembers"]["description"] = "Extracted endmember spectra";
    outputs["indices"] = Json::Value(Json::objectValue);
    outputs["indices"]["type"] = "array";
    outputs["indices"]["description"] = "Source pixel index per endmember";

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "nEndmembers"});
    return root;
}

Json::Value RsEndmemberExtractionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("endmember");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("ppi");
    meta["purpose"] = "Extract the purest spectra of a scene by Pixel Purity Index "
                     "for spectral unmixing / matching.";
    meta["workflowHints"].append("Feed the returned endmembers into rs:spectral_unmixing "
                                 "or rs:sam_classify.");
    meta["limitations"].append("PPI finds pixels at the data hull; it assumes endmembers "
                               "are present as pure pixels in the scene.");
    return meta;
}

Json::Value RsEndmemberExtractionOperator::executionEstimate() const {
    // FullRaster (default policy): the whole raster is resident. For a typical
    // 1024x1024x4-band float32 input (16 MiB), peak RAM is the input pixel
    // buffer (16 MiB) + per-pixel PPI extreme counts (4 MiB) + the pixel
    // ordering vector for ranking (8 MiB); projection directions are
    // bands-sized and negligible.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;          // full-raster: tiling not applicable
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 29360128; // ~28 MiB
    return est;
}

Json::Value RsEndmemberExtractionOperator::run(const Json::Value& params,
                                               RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    const int nEndmembers = getInt(params, "nEndmembers", 0);
    if (nEndmembers < 1)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "nEndmembers must be at least 1");
    const int projections = getInt(params, "projections", 1000);

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
                              "Endmember extraction requires at least 2 bands, got "
                                  + std::to_string(bandCount));

    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (static_cast<size_t>(nEndmembers) > pixelCount)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "nEndmembers exceeds the pixel count");

    std::vector<float> pixels(pixelCount * static_cast<size_t>(bandCount), 0.0f);
    context.reportProgress(0.15, "Reading bands");
    for (int b = 1; b <= bandCount; ++b)
    {
        std::vector<float> bandData(pixelCount);
        if (!ds.readBandData(b, bandData.data(), width, height))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(b));
        for (size_t p = 0; p < pixelCount; ++p)
            pixels[p * static_cast<size_t>(bandCount) + (b - 1)] = bandData[p];
    }

    context.reportProgress(0.45, "Running Pixel Purity Index");
    context.throwIfCancelled();

    EndmemberExtraction::EndmemberResult result;
    QString error;
    if (!EndmemberExtraction::pixelPurityIndex(pixels.data(), pixelCount, bandCount,
                                               nEndmembers, projections, &result, &error))
        throw RSOperatorError(ErrorCode::ComputationError,
                              error.isEmpty() ? "Endmember extraction failed" : error.toStdString());

    ds.close();
    context.reportProgress(1.0, "Endmember extraction complete");

    Json::Value json(Json::objectValue);
    Json::Value ends(Json::arrayValue);
    for (int e = 0; e < nEndmembers; ++e)
    {
        Json::Value spectrum(Json::arrayValue);
        for (int b = 0; b < bandCount; ++b)
            spectrum.append(result.endmembers[static_cast<size_t>(e) * bandCount + b]);
        ends.append(spectrum);
    }
    json["endmembers"] = ends;
    Json::Value indices(Json::arrayValue);
    for (int index : result.endmemberIndices)
        indices.append(index);
    json["indices"] = indices;
    Json::Value counts(Json::arrayValue);
    for (int c : result.ppiCounts)
        counts.append(c);
    json["ppiCounts"] = counts;
    return json;
}

} // namespace sicnu::operators::rs
