/***************************************************************************
 * rs_change_detection_operator.cpp  —  Change detection RSOperator
 ***************************************************************************/
#include "rs_change_detection_operator.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {
    "difference", "normalized_difference", "ratio", "cva", "change_mask"
};

const std::vector<std::string> s_threshold_methods = {
    "manual", "otsu", "percentile"
};

const std::vector<std::string> s_cleanups = {
    "none", "erode", "dilate", "open", "close"
};

ChangeDetection::MorphOp morphOpFromName(const std::string& name)
{
    if (name == "erode") return ChangeDetection::MorphOp::Erode;
    if (name == "dilate") return ChangeDetection::MorphOp::Dilate;
    if (name == "open") return ChangeDetection::MorphOp::Open;
    if (name == "close") return ChangeDetection::MorphOp::Close;
    return ChangeDetection::MorphOp::None;
}

} // anonymous namespace

Json::Value RsChangeDetectionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["before"] = makeRasterParam("before", "Before-date raster");
    props["after"] = makeRasterParam("after", "After-date raster");
    props["output"] = makeOutputParam("output", "Output change raster", "tif");
    props["method"] = makeEnumParam("method", "Change detection method", s_methods, "difference");
    props["threshold"] = makeNumberParam("threshold", "Threshold for change_mask", 0.5);
    props["band"] = makeIntegerParam("band", "1-based band for both images (fallback)", 1);
    props["beforeBand"] = makeIntegerParam("beforeBand", "1-based band on before image (overrides band)", 0);
    props["afterBand"] = makeIntegerParam("afterBand", "1-based band on after image (overrides band)", 0);
    props["makeMask"] = makeBooleanParam("makeMask", "Also write a binary change mask (UInt8 0/1)", false);
    props["thresholdMethod"] = makeEnumParam("thresholdMethod", "Mask threshold strategy", s_threshold_methods, "manual");
    props["percentile"] = makeNumberParam("percentile", "Percentile for thresholdMethod=percentile (0-100)", 90.0);
    props["cleanup"] = makeEnumParam("cleanup", "Morphological mask cleanup", s_cleanups, "none");
    props["cleanupIterations"] = makeIntegerParam("cleanupIterations", "Cleanup iterations", 1);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method", "");
    outputs["mean"] = makeNumberParam("mean", "Mean of change magnitude", 0.0);
    outputs["stddev"] = makeNumberParam("stddev", "Stddev of change magnitude", 0.0);
    outputs["thresholdUsed"] = makeNumberParam("thresholdUsed", "Effective mask threshold", 0.0);
    outputs["changedPixels"] = makeIntegerParam("changedPixels", "Changed pixel count (mask)", 0);
    outputs["totalPixels"] = makeIntegerParam("totalPixels", "Evaluated pixel count (mask)", 0);
    outputs["changedPercent"] = makeNumberParam("changedPercent", "Changed pixel percentage (mask)", 0.0);
    outputs["changedArea"] = makeNumberParam("changedArea", "Changed area in map units squared (mask)", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"before", "after", "output"});
    return root;
}

Json::Value RsChangeDetectionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("change-detection");
    meta["tags"].append("temporal");
    meta["tags"].append("difference");
    meta["purpose"] = "Identify land-cover or surface changes between two dates.";
    meta["prerequisites"].append("Before and after rasters must be co-registered and same size "
                                 "(grid compatibility is preflighted).");
    meta["workflowHints"].append("Apply atmospheric correction to both dates before comparison.");
    meta["limitations"].append("ratio outputs after/before (NaN where before is 0); "
                               "cva uses all bands of both rasters; makeMask writes a UInt8 "
                               "0/1 mask with manual/Otsu/percentile thresholds and optional "
                               "morphological cleanup.");
    return meta;
}

Json::Value RsChangeDetectionOperator::run(const Json::Value& params,
                                           RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string beforePath = requireString(params, "before");
    const std::string afterPath = requireString(params, "after");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(beforePath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Before raster not found: " + beforePath);
    }
    if (!fileExists(afterPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "After raster not found: " + afterPath);
    }

    const std::string method = getEnum(params, "method", s_methods, "difference");
    const float threshold = static_cast<float>(getDouble(params, "threshold", 0.5));
    const int defaultBand = getInt(params, "band", 1);
    const int beforeBandParam = getInt(params, "beforeBand", 0);
    const int afterBandParam = getInt(params, "afterBand", 0);
    const int beforeBand = beforeBandParam > 0 ? beforeBandParam : defaultBand;
    const int afterBand = afterBandParam > 0 ? afterBandParam : defaultBand;

    const bool makeMask = getBool(params, "makeMask", false);
    const std::string thresholdMethod =
        getEnum(params, "thresholdMethod", s_threshold_methods, "manual");
    const double percentile = getDouble(params, "percentile", 90.0);
    const std::string cleanup = getEnum(params, "cleanup", s_cleanups, "none");
    const int cleanupIterations = getInt(params, "cleanupIterations", 1);

    ensureGdalInit();

    GdalDatasetWrapper beforeDs;
    if (!beforeDs.open(QString::fromStdString(beforePath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open before raster: " + beforePath);
    }

    GdalDatasetWrapper afterDs;
    if (!afterDs.open(QString::fromStdString(afterPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open after raster: " + afterPath);
    }

    const int width = beforeDs.width();
    const int height = beforeDs.height();

    // Radiometric comparability (ADR 0114): differencing rasters in different
    // physical states (e.g. TOA reflectance vs radiance) is meaningless. Both
    // sides must declare the same state; absent declarations are skipped.
    const QString beforeState =
        SatelliteProducts::readRadiometricState( QString::fromStdString( beforePath ) );
    const QString afterState =
        SatelliteProducts::readRadiometricState( QString::fromStdString( afterPath ) );
    if ( !beforeState.isEmpty() && !afterState.isEmpty() && beforeState != afterState )
    {
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "Before and after rasters are in different radiometric states (" +
            beforeState.toStdString() + " vs " + afterState.toStdString() +
            "); calibrate or atmospherically correct both acquisitions to the "
            "same state before comparing them");
    }
    if ( !beforeState.isEmpty() && !afterState.isEmpty() )
        context.logInfo( "Radiometric state: " + beforeState.toStdString() );

    // Shared pixel-grid preflight (CRS, resolution, origin alignment, extent)
    // before any pixel comparison. Two unreferenced rasters are not spatially
    // comparable and pass as compatible; the dimension check below remains the
    // fallback for them.
    const sicnu::data::GridCompatReport gridReport =
        sicnu::data::compareGrids(sicnu::processing::gridFromDataset(beforeDs),
                                  sicnu::processing::gridFromDataset(afterDs));
    for (const sicnu::data::GridCompatIssue& issue : gridReport.issues) {
        if (issue.blocking) {
            throw RSOperatorError(ErrorCode::InvalidInputData, issue.message.toStdString());
        }
        context.logWarning(issue.message.toStdString());
    }

    if (afterDs.width() != width || afterDs.height() != height) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Before and after rasters must have the same dimensions");
    }

    if (method == "cva") {
        if (beforeDs.bandCount() != afterDs.bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "CVA requires the same band count on both rasters");
        }
    } else {
        if (beforeBand < 1 || beforeBand > beforeDs.bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Before band " + std::to_string(beforeBand) + " is out of range");
        }
        if (afterBand < 1 || afterBand > afterDs.bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "After band " + std::to_string(afterBand) + " is out of range");
        }
    }

    context.logInfo("Computing " + method + " between " + beforePath + " and " + afterPath);
    context.reportProgress(0.2, "Reading input bands");

    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> mag(pixelCount);
    std::string computeError;
    bool ok = false;

    if (method == "cva") {
        // Multi-band Change Vector Analysis magnitude.
        const int bandCount = beforeDs.bandCount();
        std::vector<std::vector<float>> beforeBands(bandCount);
        std::vector<std::vector<float>> afterBands(bandCount);
        std::vector<const float*> beforePtrs(bandCount);
        std::vector<const float*> afterPtrs(bandCount);
        for (int b = 0; b < bandCount; ++b) {
            beforeBands[b].resize(pixelCount);
            afterBands[b].resize(pixelCount);
            if (!beforeDs.readBandData(b + 1, beforeBands[b].data(), width, height)
                || !afterDs.readBandData(b + 1, afterBands[b].data(), width, height)) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read band " + std::to_string(b + 1));
            }
            beforePtrs[b] = beforeBands[b].data();
            afterPtrs[b] = afterBands[b].data();
        }
        QString cvaError;
        ok = ChangeDetection::cvaMagnitude(beforePtrs.data(), afterPtrs.data(),
                                           bandCount, pixelCount, mag.data(), &cvaError);
        computeError = cvaError.toStdString();
    } else {
        std::vector<float> before(pixelCount), after(pixelCount);
        if (!beforeDs.readBandData(beforeBand, before.data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(beforeBand) + " from before raster");
        }
        if (!afterDs.readBandData(afterBand, after.data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(afterBand) + " from after raster");
        }
        context.reportProgress(0.5, "Computing change");

        if (method == "difference") {
            ok = ChangeDetection::difference(before.data(), after.data(), mag.data(), pixelCount);
        } else if (method == "normalized_difference") {
            ok = ChangeDetection::normalizedDifference(before.data(), after.data(), mag.data(), pixelCount);
        } else if (method == "ratio") {
            ok = ChangeDetection::ratio(before.data(), after.data(), mag.data(), pixelCount);
        } else { // legacy "change_mask": difference -> threshold -> float mask
            std::vector<float> diff(pixelCount);
            ok = ChangeDetection::difference(before.data(), after.data(), diff.data(), pixelCount);
            if (ok) {
                std::vector<uint8_t> mask(pixelCount);
                ok = ChangeDetection::changeMask(diff.data(), mask.data(), pixelCount, threshold);
                if (ok) {
                    std::transform(mask.begin(), mask.end(), mag.begin(),
                                   [](uint8_t v) { return static_cast<float>(v); });
                }
            }
        }
    }

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Change detection computation failed"
                                  + (computeError.empty() ? std::string() : ": " + computeError));
    }

    context.throwIfCancelled();

    ChangeDetection::ChangeStats stats =
        ChangeDetection::statistics(mag.data(), mag.size());

    // --- Mask path: threshold strategies + morphological cleanup + area stats.
    if (makeMask && method != "change_mask") {
        float thresholdUsed = threshold;
        if (thresholdMethod == "otsu") {
            float otsu = threshold;
            if (ChangeDetection::otsuThreshold(mag.data(), pixelCount, &otsu))
                thresholdUsed = otsu;
        } else if (thresholdMethod == "percentile") {
            float pct = threshold;
            if (ChangeDetection::percentileThreshold(mag.data(), pixelCount, percentile, &pct))
                thresholdUsed = pct;
        }

        std::vector<uint8_t> mask(pixelCount);
        if (!ChangeDetection::changeMask(mag.data(), mask.data(), pixelCount, thresholdUsed)) {
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "Change mask computation failed");
        }
        ChangeDetection::morphologicalCleanup(mask.data(), width, height,
                                              cleanupIterations, morphOpFromName(cleanup));

        context.reportProgress(0.8, "Writing change mask");
        QString errorMessage;
        GDALDatasetH outDs = createOutputTiff(QString::fromStdString(outputPath), width, height,
                                              1, static_cast<int>(GDT_Byte),
                                              beforeDs.geoTransform(), beforeDs.projection(),
                                              &errorMessage);
        if (!outDs) {
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to create change mask: " + errorMessage.toStdString());
        }
        GDALRasterBandH outBand = GDALGetRasterBand(outDs, 1);
        const CPLErr writeErr = GDALRasterIO(outBand, GF_Write, 0, 0, width, height,
                                             mask.data(), width, height, GDT_Byte, 0, 0);
        GDALSetMetadataItem(outDs, "SICNU_CHANGE_METHOD", method.c_str(), nullptr);
        GDALSetMetadataItem(outDs, "SICNU_CHANGE_THRESHOLD",
                            QString::number(thresholdUsed, 'g', 10).toUtf8().constData(),
                            nullptr);
        GDALClose(outDs);
        if (writeErr != CE_None) {
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to write change mask: " + outputPath);
        }

        size_t changed = 0;
        size_t evaluated = 0;
        for (uint8_t v : mask) {
            if (v == 255)
                continue;
            ++evaluated;
            if (v == 1)
                ++changed;
        }

        Json::Value result(Json::objectValue);
        result["output"] = outputPath;
        result["method"] = method;
        result["thresholdUsed"] = thresholdUsed;
        result["changedPixels"] = static_cast<Json::UInt64>(changed);
        result["totalPixels"] = static_cast<Json::UInt64>(evaluated);
        result["changedPercent"] = evaluated == 0
            ? 0.0
            : 100.0 * static_cast<double>(changed) / static_cast<double>(evaluated);
        result["mean"] = stats.mean;
        result["stddev"] = stats.stddev;
        if (beforeDs.hasGeoTransform()) {
            const auto gt = beforeDs.geoTransform();
            const double pixelArea = std::abs(gt[1] * gt[5]);
            if (pixelArea > 0.0)
                result["changedArea"] = static_cast<double>(changed) * pixelArea;
        }
        beforeDs.close();
        afterDs.close();
        context.reportProgress(1.0, "Change detection complete");
        return result;
    }

    // --- Raster path: write the change magnitude as Float32.
    context.reportProgress(0.8, "Writing output raster");
    std::vector<std::vector<float>> bands = {std::move(mag)};
    QString errorMessage;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, bands,
                         beforeDs.geoTransform(), beforeDs.projection(), &errorMessage)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + errorMessage.toStdString());
    }

    beforeDs.close();
    afterDs.close();

    context.reportProgress(1.0, "Change detection complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["mean"] = stats.mean;
    result["stddev"] = stats.stddev;
    return result;
}

} // namespace sicnu::operators::rs
