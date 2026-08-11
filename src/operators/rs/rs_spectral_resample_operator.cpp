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

#include <gdal.h>

#include <algorithm>
#include <cstdio>
#include <limits>
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

Json::Value RsSpectralResampleOperator::executionEstimate() const {
    // Tile-streaming: single pass over 256x256 tiles holding all source bands
    // BIP + all target bands per tile. O(tilePixels * (srcBands + dstBands)),
    // independent of the raster dimensions. Nominal 224->224 bands:
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    // 2 x (256x256 x 224 bands x 4 B) tile buffers ≈ 117 MiB for a 224-band
    // imaging spectrometer; scales with tilePixels*bands, not width*height*bands.
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * 256ULL * 256ULL * 224ULL * 4ULL );
    return est;
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
    context.reportProgress(0.1, "Building resampling LUT");

    // --- Precompute the interpolation LUT once (perf: the per-pixel bracket
    // scan becomes a direct weighted dot product). -------------------------
    // For each target band: (srcLo, frac) with srcLo = bracketing lower source
    // index; outOfRange marks targets outside the source range (output NaN).
    // Matches SpectralResampling::resampleSpectrum semantics exactly.
    for (int i = 1; i < bandCount; ++i)
    {
        if (sourceWavelengths[i] <= sourceWavelengths[i - 1])
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "sourceWavelengths must be strictly increasing");
    }
    const int dstBands = static_cast<int>(targetWavelengths.size());
    struct LutEntry { int srcLo = 0; float frac = 0.0f; bool outOfRange = true; };
    std::vector<LutEntry> lut(static_cast<size_t>(dstBands));
    for (int t = 0; t < dstBands; ++t)
    {
        const float target = targetWavelengths[t];
        LutEntry e;
        e.outOfRange = (target < sourceWavelengths[0] || target > sourceWavelengths[bandCount - 1]);
        if (!e.outOfRange)
        {
            int i = 1;
            while (i < bandCount && sourceWavelengths[i] < target)
                ++i;
            if (i >= bandCount)
            {
                e.srcLo = bandCount - 1;
                e.frac = 0.0f;
            }
            else
            {
                const float w0 = sourceWavelengths[i - 1];
                const float w1 = sourceWavelengths[i];
                e.srcLo = i - 1;
                e.frac = (w1 == w0) ? 0.0f : (target - w0) / (w1 - w0);
            }
        }
        lut[static_cast<size_t>(t)] = e;
    }

    // --- Single streaming pass over 256x256 tiles. -------------------------
    constexpr int kTile = 256;
    const size_t maxTilePixels = static_cast<size_t>(kTile) * kTile;
    const size_t B = static_cast<size_t>(bandCount);
    const size_t D = static_cast<size_t>(dstBands);
    std::vector<float> tileBip(maxTilePixels * B, 0.0f);
    std::vector<float> tileOut(maxTilePixels * D, 0.0f);
    std::vector<float> bandScratch(maxTilePixels);
    std::vector<float> bandTile(maxTilePixels);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const size_t pixelCount = static_cast<size_t>(width) * height;

    QString outErr;
    GDALDatasetH outDs = createOutputTiff(QString::fromStdString(outputPath), width, height,
                                          dstBands, static_cast<int>(GDT_Float32),
                                          ds.geoTransform(), ds.projection(), &outErr);
    if (!outDs)
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create resampled raster: " + outErr.toStdString());

    for (int y = 0; y < height; y += kTile)
    {
        const int h = std::min(kTile, height - y);
        for (int x = 0; x < width; x += kTile)
        {
            const int w = std::min(kTile, width - x);
            const size_t n = static_cast<size_t>(w) * h;
            context.throwIfCancelled();
            context.reportProgress(0.1 + 0.8 * (static_cast<double>(y) * width + x) / pixelCount,
                                   "Resampling tiles");

            // Read the tile's source bands into BIP layout.
            for (int b = 0; b < bandCount; ++b)
            {
                if (!ds.readBandWindow(b + 1, x, y, w, h, bandScratch.data()))
                {
                    GDALClose(outDs);
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to read input tile at (" +
                                              std::to_string(x) + ", " + std::to_string(y) + ")");
                }
                for (size_t p = 0; p < n; ++p)
                    tileBip[p * B + static_cast<size_t>(b)] = bandScratch[p];
            }

            // Apply the LUT: out_t = src_lo + frac * (src_hi - src_lo).
            for (size_t p = 0; p < n; ++p)
            {
                const float* spectrum = tileBip.data() + p * B;
                float* outSpectrum = tileOut.data() + p * D;
                for (int t = 0; t < dstBands; ++t)
                {
                    const LutEntry& e = lut[static_cast<size_t>(t)];
                    if (e.outOfRange)
                    {
                        outSpectrum[t] = nan;
                        continue;
                    }
                    const float lo = spectrum[e.srcLo];
                    const float hi = (e.srcLo + 1 < bandCount) ? spectrum[e.srcLo + 1] : lo;
                    outSpectrum[t] = lo + e.frac * (hi - lo);
                }
            }

            // Write each target band's tile (bandTile hoisted outside the loop).
            for (int t = 0; t < dstBands; ++t)
            {
                for (size_t p = 0; p < n; ++p)
                    bandTile[p] = tileOut[p * D + static_cast<size_t>(t)];
                GDALRasterBandH outBand = GDALGetRasterBand(outDs, t + 1);
                if (GDALRasterIO(outBand, GF_Write, x, y, w, h, bandTile.data(),
                                 w, h, GDT_Float32, 0, 0) != CE_None)
                {
                    GDALClose(outDs);
                    throw RSOperatorError(ErrorCode::FileNotWritable,
                                          "Failed to write resampled tile at (" +
                                              std::to_string(x) + ", " + std::to_string(y) + ")");
                }
            }
        }
    }

    // Record target wavelengths on the output bands (spectral-library and
    // cross-sensor workflows consume them downstream).
    for (int t = 0; t < dstBands; ++t)
    {
        char meta[64];
        std::snprintf(meta, sizeof(meta), "%g", static_cast<double>(targetWavelengths[t]));
        GDALSetMetadataItem(GDALGetRasterBand(outDs, t + 1), "WAVELENGTH", meta, nullptr);
        GDALSetMetadataItem(GDALGetRasterBand(outDs, t + 1), "WAVELENGTH_UNITS", "nm", nullptr);
    }
    GDALClose(outDs);
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
