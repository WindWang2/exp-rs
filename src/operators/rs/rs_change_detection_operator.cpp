/***************************************************************************
 * rs_change_detection_operator.cpp  —  Change detection RSOperator
 *
 * Compatibility facade over the shared tile-streaming change kernel
 * (rs_change_streaming.h). Every method — difference, normalized_difference,
 * ratio, cva, mad and the mask workflows — runs through the same
 * memory-bounded path (O(tilePixels * bands + bands^2)) as the atomic
 * primitives (rs:change_difference / rs:change_normalized_difference /
 * rs:change_ratio / rs:change_cva / rs:change_mad).
 ***************************************************************************/
#include "rs_change_detection_operator.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "rs_change_streaming.h"

#include <QString>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {
    "difference", "normalized_difference", "ratio", "cva", "mad", "change_mask"
};

const std::vector<std::string> s_threshold_methods = {
    "manual", "otsu", "percentile", "statistical"
};

const std::vector<std::string> s_cleanups = {
    "none", "erode", "dilate", "open", "close"
};

ChangeMetric metricFromMethod( const std::string &method )
{
    if ( method == "normalized_difference" ) return ChangeMetric::NormalizedDifference;
    if ( method == "ratio" ) return ChangeMetric::Ratio;
    if ( method == "cva" ) return ChangeMetric::Cva;
    if ( method == "mad" ) return ChangeMetric::Mad;
    // "difference" and the legacy "change_mask" both use the absolute
    // |after - before| magnitude (the legacy kernel's semantics) so the
    // facade stays numerically compatible with previous releases.
    return ChangeMetric::AbsoluteDifference;
}

} // anonymous namespace

Json::Value RsChangeDetectionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["before"] = makeRasterParam("before", "Before-date raster");
    props["after"] = makeRasterParam("after", "After-date raster");
    // Machine-readable data contracts consumed by preflight: same-grid
    // relation and accepted radiometric states (comparability, ADR 0114).
    for (const char *port : { "before", "after" })
    {
        props[port]["x-rs-contract"]["dataKind"] = "raster";
        props[port]["x-rs-contract"]["gridRelation"] = "same-grid";
        Json::Value states(Json::arrayValue);
        states.append(SatelliteProducts::kRadiometricStateDigitalNumber);
        states.append(SatelliteProducts::kRadiometricStateRadiance);
        states.append(SatelliteProducts::kRadiometricStateToaReflectance);
        states.append(SatelliteProducts::kRadiometricStateSurfaceReflectance);
        props[port]["x-rs-contract"]["radiometricState"] = states;
    }
    props["output"] = makeOutputParam("output", "Output change raster", "tif");
    props["method"] = makeEnumParam("method", "Change detection method", s_methods, "difference");
    props["threshold"] = makeNumberParam("threshold", "Threshold for change_mask", 0.5);
    props["band"] = makeIntegerParam("band", "1-based band for both images (fallback)", 1);
    props["beforeBand"] = makeIntegerParam("beforeBand", "1-based band on before image (overrides band)", 0);
    props["afterBand"] = makeIntegerParam("afterBand", "1-based band on after image (overrides band)", 0);
    props["makeMask"] = makeBooleanParam("makeMask", "Also write a binary change mask (UInt8 0/1)", false);
    props["thresholdMethod"] = makeEnumParam("thresholdMethod", "Mask threshold strategy", s_threshold_methods, "manual");
    props["percentile"] = makeNumberParam("percentile", "Percentile for thresholdMethod=percentile (0-100)", 90.0);
    props["statisticalK"] = makeNumberParam("statisticalK", "Stddev multiplier for thresholdMethod=statistical (mean + k*stddev)", 2.0);
    props["minAreaPixels"] = makeIntegerParam("minAreaPixels", "Minimum mapping unit: drop changed components smaller than this (pixels); 0 disables", 0);
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
        meta["task"] = "change-detection";
    meta["gpu"] = false;
    meta["purpose"] = "Identify land-cover or surface changes between two dates.";
    meta["prerequisites"].append("Before and after rasters must be co-registered and same size "
                                 "(grid compatibility is preflighted).");
    meta["workflowHints"].append("Apply atmospheric correction to both dates before comparison.");
    meta["limitations"].append("ratio outputs after/before (NaN where before is 0); "
                               "cva and mad stream over 256x256 tiles in O(tile*bands + "
                               "bands^2) memory (mad is multi-pass); makeMask writes a "
                               "UInt8 0/1 mask with manual/Otsu/percentile/statistical "
                               "thresholds and optional morphological cleanup.");
    // Compatibility facade over the atomic change-metric primitives.
    meta["facadeOf"] = "rs:change_difference,rs:change_normalized_difference,"
                       "rs:change_ratio,rs:change_cva,rs:change_mad,rs:threshold_raster";
    return meta;
}

Json::Value RsChangeDetectionOperator::executionEstimate() const {
    // MultiPassStreaming: every method processes 256x256 tiles out-of-core, so
    // peak RAM is dominated by the tile buffers plus the bands^2 covariance /
    // coefficient matrices and is independent of the raster dimensions. For a
    // nominal 6-band input: 2 BIP input tiles + output tile + band scratch +
    // bands^2 doubles.
    constexpr long long kTilePixels = 256LL * 256;
    constexpr long long kBandCount = 6;
    constexpr long long ramBytes =
        3 * kTilePixels * kBandCount * static_cast<long long>(sizeof(float)) // BIP in x2 + out
        + kTilePixels * static_cast<long long>(sizeof(float))                // band scratch
        + kBandCount * kBandCount * static_cast<long long>(sizeof(double));  // bands^2 state
    Json::Value estimate(Json::objectValue);
    estimate["tileWidth"] = 256;
    estimate["tileHeight"] = 256;
    estimate["estimatedRamBytes"] = static_cast<Json::UInt64>(ramBytes);
    return estimate;
}

Json::Value RsChangeDetectionOperator::estimateExecution( const Json::Value &params ) const
{
    // Dynamic estimate: derive the working set from the actual band count of
    // the input raster when it is available (tileWidth*tileHeight*bands*4).
    // Falls back to the static typical-input estimate otherwise.
    if ( params.isObject() && params.isMember( "before" ) && params["before"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["before"].asString() ) )
             && probe.bandCount() > 0 )
        {
            const std::uint64_t bands = static_cast<std::uint64_t>( probe.bandCount() );
            const std::uint64_t tilePixels = 256ULL * 256;
            std::optional<std::uint64_t> ram =
                sicnu::processing::checkedMulN( { tilePixels, bands, static_cast<std::uint64_t>( sizeof( float ) ), 3ULL } );
            if ( ram )
            {
                std::optional<std::uint64_t> matrix =
                    sicnu::processing::checkedMulN( { bands, bands, static_cast<std::uint64_t>( sizeof( double ) ) } );
                const std::uint64_t scratch = tilePixels * static_cast<std::uint64_t>( sizeof( float ) );
                const std::uint64_t total = *ram + ( matrix ? *matrix : 0ULL ) + scratch;
                Json::Value est( Json::objectValue );
                est["tileWidth"] = Json::Value::UInt64( 256 );
                est["tileHeight"] = Json::Value::UInt64( 256 );
                est["estimatedRamBytes"] = Json::Value::UInt64( total );
                est["basis"] = "dynamic";
                return est;
            }
        }
    }
    return executionEstimate();
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
    const double statisticalK = getDouble(params, "statisticalK", 2.0);
    const int minAreaPixels = getInt(params, "minAreaPixels", 0);
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

    const bool multiBand = (method == "cva" || method == "mad");
    if (multiBand) {
        if (beforeDs.bandCount() != afterDs.bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  method + " requires the same band count on both rasters");
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

    // The legacy "change_mask" method is a manual-threshold binary mask over
    // the difference; the shared kernel's mask path produces the same 0/1 mask
    // (UInt8 instead of the historical Float32 — values read identically).
    const bool legacyChangeMask = (method == "change_mask");

    ChangeStreamingOptions opts;
    opts.beforeBand = beforeBand;
    opts.afterBand = afterBand;
    opts.makeMask = makeMask || legacyChangeMask;
    opts.threshold = threshold;
    opts.thresholdMethod = legacyChangeMask ? std::string( "manual" ) : thresholdMethod;
    opts.percentile = percentile;
    opts.statisticalK = statisticalK;
    opts.minAreaPixels = minAreaPixels;
    opts.cleanup = cleanup;
    opts.cleanupIterations = cleanupIterations;
    opts.outputPath = outputPath;
    opts.methodLabel = method;

    return runChangeStreaming(beforeDs, afterDs, width, height,
                              metricFromMethod(method), opts, context);
}

} // namespace sicnu::operators::rs
