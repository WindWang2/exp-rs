/***************************************************************************
 * rs_spectral_band_select_operator.cpp — band selection / bad-band exclusion
 ***************************************************************************/
#include "rs_spectral_band_select_operator.h"

#include "rs_partial_output_guard.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_wavelength.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <QString>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

struct BandSelection
{
    std::vector<int> bands; ///< 1-based, input order
};

/// Read the full wavelength grid of a raster (nm-normalized). Refuses when
/// any band lacks WAVELENGTH or the grid is invalid.
SpectralWavelength::Grid requireGrid(GdalDatasetWrapper &ds, int bandCount)
{
    std::vector<std::pair<double, std::string>> wavelengths;
    for (int b = 1; b <= bandCount; ++b)
    {
        const QString wl = ds.bandMetadataItem(b, "WAVELENGTH");
        bool ok = false;
        const double value = wl.toDouble(&ok);
        if (wl.isEmpty() || !ok)
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Wavelength-based band selection requires WAVELENGTH metadata on "
                "every band; band " + std::to_string(b) + " has none (use the explicit "
                "'bands' mode instead)");
        wavelengths.emplace_back(value,
                                 ds.bandMetadataItem(b, "WAVELENGTH_UNITS").toStdString());
    }
    SpectralWavelength::Grid grid;
    const SpectralWavelength::Status status =
        SpectralWavelength::gridFromBandValues(wavelengths, {}, &grid);
    if (status != SpectralWavelength::Status::Ok)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              std::string("Invalid band wavelength grid: ") +
                                  SpectralWavelength::statusText(status));
    return grid;
}

} // namespace

Json::Value RsSpectralBandSelectOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Band-subset raster", "tif");
    props["bands"] = [] {
        Json::Value arr(Json::objectValue);
        arr["type"] = "array";
        arr["description"] = "1-based band indices to keep (explicit mode)";
        arr["items"]["type"] = "integer";
        return arr;
    }();
    props["wavelengthMin"] = makeNumberParam("wavelengthMin", "Inclusive window start (nm; "
                                            "wavelength mode)", 0.0);
    props["wavelengthMax"] = makeNumberParam("wavelengthMax", "Inclusive window end (nm; "
                                            "wavelength mode)", 0.0);
    props["excludeRanges"] = [] {
        Json::Value arr(Json::objectValue);
        arr["type"] = "array";
        arr["description"] = "Bad-band ranges to drop: array of {minNm, maxNm} objects "
                             "(exclusion mode)";
        arr["items"]["type"] = "object";
        return arr;
    }();

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["bands"] = makeIntegerParam("bands", "Number of bands in the output", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSpectralBandSelectOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("spectral");
    meta["tags"].append("preprocessing");
    meta["tags"].append("bad-bands");
    meta["purpose"] = "Drop atmospheric/noisy bands or select a wavelength window "
                      "before spectral analysis.";
    meta["prerequisites"].append("wavelengthMin/Max and excludeRanges need WAVELENGTH "
                                 "band metadata; the explicit 'bands' mode does not.");
    meta["workflowHints"].append("Chain between analysis-ready masking and spectral "
                                 "transforms: rs:apply_mask -> rs:spectral_band_select "
                                 "-> rs:sam_classify / rs:spectral_unmixing.");
    meta["limitations"].append("Band selection renumbers bands; wavelength metadata of "
                               "kept bands is preserved and normalized to nm.");
    return meta;
}

Json::Value RsSpectralBandSelectOperator::executionEstimate() const {
    // Row-streaming copy: one row buffer per kept band pass.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 1;
    est["estimatedRamBytes"] = 8388608; // 8 MiB nominal row working set
    return est;
}

Json::Value RsSpectralBandSelectOperator::run(const Json::Value& params,
                                              RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input raster not found: " + inputPath);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input: " + inputPath);

    const int bandCount = ds.bandCount();
    const int width = ds.width();
    const int height = ds.height();

    const bool hasBands = params.isMember("bands") && params["bands"].isArray()
                          && !params["bands"].empty();
    const bool hasWindow = params.isMember("wavelengthMin") && params.isMember("wavelengthMax")
                           && params["wavelengthMin"].isNumeric()
                           && params["wavelengthMax"].isNumeric();
    const bool hasExclude = params.isMember("excludeRanges") && params["excludeRanges"].isArray()
                            && !params["excludeRanges"].empty();

    int modes = 0;
    if (hasBands) ++modes;
    if (hasWindow) ++modes;
    if (hasExclude) ++modes;
    if (modes == 0)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "One of 'bands', 'wavelengthMin'+'wavelengthMax' or "
                              "'excludeRanges' is required");
    if (modes > 1)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Ambiguous band selection: supply exactly one of 'bands', "
                              "'wavelengthMin'+'wavelengthMax', 'excludeRanges'");

    BandSelection selection;
    SpectralWavelength::Grid grid;
    bool gridRead = false;

    if (hasBands)
    {
        for (Json::ArrayIndex i = 0; i < params["bands"].size(); ++i)
        {
            if (!params["bands"][i].isIntegral())
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "'bands' entries must be integers");
            const int b = params["bands"][i].asInt();
            if (b < 1 || b > bandCount)
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "Band " + std::to_string(b) + " out of range (1-" +
                                          std::to_string(bandCount) + ")");
            selection.bands.push_back(b);
        }
    }
    else if (hasWindow)
    {
        grid = requireGrid(ds, bandCount);
        gridRead = true;
        const double minNm = params["wavelengthMin"].asDouble();
        const double maxNm = params["wavelengthMax"].asDouble();
        if (!(maxNm > minNm))
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "wavelengthMax must be greater than wavelengthMin");
        for (int b = 0; b < bandCount; ++b)
            if (grid.centersNm[static_cast<size_t>(b)] >= minNm
                && grid.centersNm[static_cast<size_t>(b)] <= maxNm)
                selection.bands.push_back(b + 1);
        if (selection.bands.empty())
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "No band center inside [" +
                                      std::to_string(minNm) + ", " + std::to_string(maxNm) +
                                      "] nm; raster coverage is [" +
                                      std::to_string(grid.minNm()) + ", " +
                                      std::to_string(grid.maxNm()) + "] nm");
    }
    else // hasExclude
    {
        grid = requireGrid(ds, bandCount);
        gridRead = true;
        std::vector<bool> excluded(static_cast<size_t>(bandCount), false);
        for (const auto &range : params["excludeRanges"])
        {
            if (!range.isObject() || !range["minNm"].isNumeric() || !range["maxNm"].isNumeric())
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "excludeRanges entries must be objects with numeric "
                                      "minNm/maxNm");
            const double lo = range["minNm"].asDouble();
            const double hi = range["maxNm"].asDouble();
            if (!(hi > lo))
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "excludeRanges maxNm must exceed minNm");
            for (int b = 0; b < bandCount; ++b)
                if (grid.centersNm[static_cast<size_t>(b)] >= lo
                    && grid.centersNm[static_cast<size_t>(b)] <= hi)
                    excluded[static_cast<size_t>(b)] = true;
        }
        for (int b = 0; b < bandCount; ++b)
            if (!excluded[static_cast<size_t>(b)])
                selection.bands.push_back(b + 1);
        if (selection.bands.empty())
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "excludeRanges excluded every band; nothing to write");
    }

    context.reportProgress(0.1, "Writing selected bands");
    GdalDatasetWrapper out;
    if (!out.create(QString::fromStdString(outputPath), width, height,
                    static_cast<int>(selection.bands.size()), GDT_Float32,
                    ds.geoTransform(), ds.projection()))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output raster: " + outputPath);
    PartialOutputGuard partialGuard(QString::fromStdString(outputPath));
    // Close the GDAL handle before the guard removes the path: a removal
    // while the dataset is open is a sharing violation on Windows.
    partialGuard.setCloseFirst([&out] { out.closeWithError(nullptr); });

    GDALDatasetH outHandle = static_cast<GDALDatasetH>( out.dataset() );
    std::vector<float> row(static_cast<size_t>(width), 0.0f);
    for (size_t outBand = 0; outBand < selection.bands.size(); ++outBand)
    {
        const int srcBand = selection.bands[outBand];
        for (int y = 0; y < height; ++y)
        {
            context.throwIfCancelled();
            if (!ds.readBandWindow(srcBand, 0, y, width, 1, row.data()))
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read band " + std::to_string(srcBand));
            if (!out.writeBandWindow(static_cast<int>(outBand) + 1, 0, y, width, 1,
                                     row.data()))
                throw RSOperatorError(ErrorCode::FileNotWritable,
                                      "Failed to write output band " +
                                          std::to_string(outBand + 1));
        }
        // Normalize + propagate the kept band's wavelength metadata.
        if (outHandle)
        {
            const QString wl = ds.bandMetadataItem(srcBand, "WAVELENGTH");
            if (!wl.isEmpty())
            {
                bool ok = false;
                const double value = wl.toDouble(&ok);
                if (ok)
                {
                    float nm = 0.0f;
                    const QString units = ds.bandMetadataItem(srcBand, "WAVELENGTH_UNITS");
                    if (SpectralWavelength::normalizeToNm(value, units.toStdString(), &nm))
                    {
                        const QByteArray wlText =
                            QString::number(static_cast<double>(nm), 'f', 6).toUtf8();
                        GDALRasterBandH outBandH =
                            GDALGetRasterBand(outHandle, static_cast<int>(outBand) + 1);
                        GDALSetMetadataItem(outBandH, "WAVELENGTH", wlText.constData(), nullptr);
                        GDALSetMetadataItem(outBandH, "WAVELENGTH_UNITS", "nm", nullptr);
                    }
                }
            }
        }
        context.reportProgress(0.1 + 0.85 * static_cast<double>(outBand + 1)
                                       / static_cast<double>(selection.bands.size()),
                               "Writing selected bands");
    }

    ds.close();
    QString closeError;
    if (!out.closeWithError(&closeError))
        throw RSOperatorError(ErrorCode::GdalError,
                              closeError.isEmpty()
                                  ? "Failed to finalize band-subset raster"
                                  : closeError.toStdString());
    partialGuard.disarm();
    context.reportProgress(1.0, "Band selection complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["bands"] = static_cast<int>(selection.bands.size());
    Json::Value sourceBands(Json::arrayValue);
    for (int b : selection.bands)
        sourceBands.append(b);
    result["sourceBands"] = sourceBands;
    if (gridRead && !grid.empty())
    {
        result["wavelengthRangeNm"] = "[" + std::to_string(grid.minNm()) + ", "
                                      + std::to_string(grid.maxNm()) + "]";
    }
    return result;
}

} // namespace sicnu::operators::rs
