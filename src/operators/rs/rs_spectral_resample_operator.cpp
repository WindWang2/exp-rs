/***************************************************************************
 * rs_spectral_resample_operator.cpp  —  Spectral resampling RSOperator
 ***************************************************************************/
#include "rs_spectral_resample_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_resampling.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// Parses a JSON array of numbers into a float vector.
std::vector<float> parseFloatArray(const Json::Value& array, const char* label)
{
    if (!array.isArray() || array.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              std::string(label) + " must be a non-empty array of numbers");
    std::vector<float> values;
    for (Json::ArrayIndex i = 0; i < array.size(); ++i)
    {
        if (!array[i].isNumeric())
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  std::string(label) + " contains a non-numeric value");
        values.push_back(static_cast<float>(array[i].asDouble()));
    }
    return values;
}

} // anonymous namespace

Json::Value RsSpectralResampleOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output resampled raster", "tif");
    Json::Value wlParam(Json::objectValue);
    wlParam["type"] = "array";
    wlParam["description"] = "Target band center wavelengths (nm)";
    wlParam["items"]["type"] = "number";
    props["wavelengths"] = wlParam;
    Json::Value srcParam(Json::objectValue);
    srcParam["type"] = "array";
    srcParam["description"] = "Source wavelengths (nm); default reads band WAVELENGTH metadata";
    srcParam["items"]["type"] = "number";
    props["sourceWavelengths"] = srcParam;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["bands"] = makeIntegerParam("bands", "Output band count", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "wavelengths"});
    return root;
}

Json::Value RsSpectralResampleOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("spectral");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("resampling");
    meta["purpose"] = "Resample spectra onto a target wavelength grid for "
                      "cross-sensor comparison or library matching.";
    meta["prerequisites"].append("Input bands carry WAVELENGTH metadata (product-stacked) "
                                 "or sourceWavelengths is provided.");
    meta["workflowHints"].append("Resample an imaging spectrometer onto Landsat / Sentinel-2 "
                                 "band positions before index or library workflows.");
    meta["limitations"].append("Linear interpolation between band centers; target wavelengths "
                               "outside the source range yield NaN.");
    return meta;
}

Json::Value RsSpectralResampleOperator::run(const Json::Value& params,
                                            RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    const std::vector<float> targetWavelengths =
        parseFloatArray(params["wavelengths"], "'wavelengths'");
    std::vector<float> sourceWavelengths =
        params.isMember("sourceWavelengths")
            ? parseFloatArray(params["sourceWavelengths"], "'sourceWavelengths'")
            : std::vector<float>();

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
                              "Spectral resampling requires at least 2 bands, got "
                                  + std::to_string(bandCount));

    // Resolve source wavelengths: explicit param, else per-band WAVELENGTH
    // metadata (written by product stacking).
    if (sourceWavelengths.empty())
    {
        sourceWavelengths.resize(bandCount);
        bool anyMissing = false;
        for (int b = 1; b <= bandCount; ++b)
        {
            const QString wl = ds.bandMetadataItem(b, "WAVELENGTH");
            bool ok = false;
            const double v = wl.toDouble(&ok);
            if (!ok || v <= 0.0)
            {
                anyMissing = true;
                break;
            }
            sourceWavelengths[b - 1] = static_cast<float>(v);
        }
        if (anyMissing)
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "Input bands carry no WAVELENGTH metadata; pass sourceWavelengths explicitly");
    }
    if (static_cast<int>(sourceWavelengths.size()) != bandCount)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "sourceWavelengths size (" + std::to_string(sourceWavelengths.size()) +
                                  ") must equal the input band count (" + std::to_string(bandCount) + ")");

    context.logInfo("Spectral resampling: " + std::to_string(bandCount) +
                    " source bands -> " + std::to_string(targetWavelengths.size()) +
                    " target bands");
    context.reportProgress(0.15, "Reading bands");

    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> pixels(pixelCount * static_cast<size_t>(bandCount), 0.0f);
    for (int b = 1; b <= bandCount; ++b)
    {
        std::vector<float> bandData(pixelCount);
        if (!ds.readBandData(b, bandData.data(), width, height))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(b));
        for (size_t p = 0; p < pixelCount; ++p)
            pixels[p * static_cast<size_t>(bandCount) + (b - 1)] = bandData[p];
    }

    context.reportProgress(0.45, "Resampling");
    context.throwIfCancelled();

    const int dstBands = static_cast<int>(targetWavelengths.size());
    std::vector<std::vector<float>> outBands(dstBands, std::vector<float>(pixelCount));
    std::vector<float> outSpectrum(dstBands);
    for (size_t p = 0; p < pixelCount; ++p)
    {
        const float* spectrum = pixels.data() + p * static_cast<size_t>(bandCount);
        if (!SpectralResampling::resampleSpectrum(
                spectrum, sourceWavelengths.data(), bandCount,
                targetWavelengths.data(), dstBands, outSpectrum.data()))
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "Spectral resampling kernel failed");
        for (int t = 0; t < dstBands; ++t)
            outBands[t][p] = outSpectrum[t];
    }

    context.reportProgress(0.75, "Writing output");

    QString writeError;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, outBands,
                         ds.geoTransform(), ds.projection(), &writeError))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write resampled raster: " + writeError.toStdString());

    ds.close();
    context.reportProgress(1.0, "Spectral resampling complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["bands"] = dstBands;
    Json::Value srcArr(Json::arrayValue);
    for (float w : sourceWavelengths)
        srcArr.append(w);
    result["sourceWavelengths"] = srcArr;
    return result;
}

} // namespace sicnu::operators::rs
