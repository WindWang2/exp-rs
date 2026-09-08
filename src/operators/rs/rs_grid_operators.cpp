/***************************************************************************
 * rs_grid_operators.cpp — Foundation 6.0, Milestone D: rs:resample / rs:align
 * over the authoritative GDAL warp seam (geospatial/convert/raster_convert).
 ***************************************************************************/
#include "rs_grid_operators.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "geospatial/convert/raster_convert.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <cmath>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_resamplingKernels = {
    "near", "bilinear", "cubic", "cubicspline", "lanczos", "average", "mode", "rms"
};

/// Continuous kernels are interpolated; categorical data must pick one of
/// the exact-pick kernels (never average interpolated classes).
bool isExactPickKernel( const std::string &kernel )
{
    return kernel == "near" || kernel == "mode";
}

sicnu::geo::ConvertProgress makeProgress( RSOperatorContext &context, double from, double to )
{
    sicnu::geo::ConvertProgress progress;
    progress.report = [ & ]( double fraction, const std::string &message ) {
        context.reportProgress( from + ( to - from ) * fraction, message );
    };
    return progress;
}

std::string resolveKernel( const Json::Value &params, RSOperatorContext &context,
                           bool categorical )
{
    const std::string kernel = getEnum( params, "resampling", s_resamplingKernels,
                                        categorical ? "near" : "bilinear" );
    if ( categorical && !isExactPickKernel( kernel ) )
    {
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "categorical rasters must use 'near' or 'mode' resampling; got '"
                                   + kernel + "'. Interpolated kernels fabricate class values." );
    }
    return kernel;
}

std::size_t resolveWarpMemoryLimit( const Json::Value &params )
{
    if ( params.isMember( "warpMemoryLimitBytes" ) && params["warpMemoryLimitBytes"].isNumeric() )
    {
        const double v = params["warpMemoryLimitBytes"].asDouble();
        if ( !( v > 0.0 ) || !std::isfinite( v ) )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "warpMemoryLimitBytes must be a positive number" );
        return static_cast<std::size_t>( v );
    }
    return 0; // GDAL default
}

void requireCrs( GdalDatasetWrapper &dataset, const std::string &path, const char *role )
{
    const QString wkt = dataset.projection();
    if ( wkt.trimmed().isEmpty() )
    {
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               std::string( role ) + " raster has no CRS; refusing to guess ("
                                   + path + "). Assign a CRS first — grid contracts are "
                                             "never inferred." );
    }
}

} // anonymous namespace

Json::Value RsResampleOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster");
    props["output"] = makeOutputParam("output", "Resampled raster path", "tif");
    props["resolution"] = makeNumberParam("resolution", "Target pixel size in CRS units (square cells)", 0.0);
    props["resampling"] = makeEnumParam("resampling", "Resampling kernel; categorical data must stay exact-pick", s_resamplingKernels, "bilinear");
    props["categorical"] = makeBooleanParam("categorical", "Declare the raster categorical (classes/labels); forces exact-pick kernels and nearest by default", false);
    Json::Value warpMemory(Json::objectValue);
    warpMemory["type"] = "number";
    warpMemory["description"] = "Optional warp memory budget in bytes (GDAL -wm)";
    props["warpMemoryLimitBytes"] = warpMemory;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Resampled raster path");
    outputs["width"] = makeIntegerParam("width", "Output width (pixels)");
    outputs["height"] = makeIntegerParam("height", "Output height (pixels)");
    outputs["resolutionX"] = makeNumberParam("resolutionX", "Output pixel width in CRS units");
    outputs["resolutionY"] = makeNumberParam("resolutionY", "Output pixel height in CRS units");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "resolution"});
    return root;
}

Json::Value RsResampleOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("grid");
    meta["tags"].append("resampling");
    meta["purpose"] = "Resolution change on the input's own CRS. The authoritative "
                      "GDAL warp engine does the resampling; this operator owns the "
                      "policy: explicit kernel, categorical safety pin, atomic "
                      "publish, bounded warp memory.";
    meta["prerequisites"] = "Input raster with a declared CRS (never guessed).";
    meta["memoryPolicy"] = "streaming";
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    meta["limitations"].append("Keeps the input CRS and derived extent; use rs:align "
                               "to hit an exact reference grid.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "raster";
    contract["crs"] = "required-declared";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsResampleOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 512;
    est["tileHeight"] = 512;
    est["estimatedRamBytes"] = Json::Value::UInt64( 64ULL * 1024ULL * 1024ULL );
    return est;
}

Json::Value RsResampleOperator::estimateExecution(const Json::Value& params) const {
    const double resolution = getDouble(params, "resolution", 0.0);
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 512;
    est["tileHeight"] = 512;
    est["estimatedRamBytes"] =
        Json::Value::UInt64( resolution > 0.0 ? 64ULL * 1024ULL * 1024ULL
                                              : 128ULL * 1024ULL * 1024ULL );
    ( void ) resolution;
    return est;
}

Json::Value RsResampleOperator::run(const Json::Value& params,
                                    RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const bool categorical = getBool(params, "categorical", false);

    double resolutionX = 0.0;
    double resolutionY = 0.0;
    if (params.isMember("resolution") && params["resolution"].isNumeric()) {
        resolutionX = resolutionY = params["resolution"].asDouble();
    }
    if (!(resolutionX > 0.0) || !std::isfinite(resolutionX)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "resolution must be a positive number in CRS units");
    }

    const std::string kernel = resolveKernel(params, context, categorical);

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }
    requireCrs(src, inputPath, "input");
    const std::string crsWkt = src.projection().toStdString();

    sicnu::geo::WarpOptions options;
    options.targetCrs = crsWkt; // same CRS: resolution change, not reprojection
    options.resampling = kernel;
    options.targetResolutionX = resolutionX;
    options.targetResolutionY = resolutionY;
    options.warpMemoryLimitBytes = resolveWarpMemoryLimit(params);

    context.throwIfCancelled();
    context.reportProgress(0.05, "Resampling raster");
    sicnu::geo::ConvertProgress progress = makeProgress(context, 0.05, 0.95);
    const sicnu::geo::TranslateResult result =
        sicnu::geo::warpRaster(inputPath, outputPath, options, &progress);
    context.throwIfCancelled();

    Json::Value out(Json::objectValue);
    out["output"] = result.output;
    out["width"] = result.width;
    out["height"] = result.height;
    out["resolutionX"] = resolutionX;
    out["resolutionY"] = resolutionY;
    out["resampling"] = kernel;
    out["categorical"] = categorical;
    if (!result.warnings.empty())
        out["warnings"] = result.warnings;
    context.reportProgress(1.0, "Resample complete");
    return out;
}

Json::Value RsAlignOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Raster to align");
    props["reference"] = makeRasterParam("reference", "Reference raster defining the exact target grid (CRS, origin, resolution, extent)");
    props["reference"]["x-rs-contract"] = [] {
        Json::Value c(Json::objectValue);
        c["modality"] = "raster";
        c["gridRelation"] = "defines-target-grid";
        return c;
    }();
    props["output"] = makeOutputParam("output", "Aligned raster path", "tif");
    props["resampling"] = makeEnumParam("resampling", "Resampling kernel; categorical data must stay exact-pick", s_resamplingKernels, "near");
    props["categorical"] = makeBooleanParam("categorical", "Declare the raster categorical (classes/labels); forces exact-pick kernels", false);
    Json::Value warpMemory(Json::objectValue);
    warpMemory["type"] = "number";
    warpMemory["description"] = "Optional warp memory budget in bytes (GDAL -wm)";
    props["warpMemoryLimitBytes"] = warpMemory;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Aligned raster path");
    outputs["width"] = makeIntegerParam("width", "Output width (pixels)");
    outputs["height"] = makeIntegerParam("height", "Output height (pixels)");
    outputs["alreadyAligned"] = makeBooleanParam("alreadyAligned", "True when the input already matched the reference grid (lossless copy)");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "reference", "output"});
    return root;
}

Json::Value RsAlignOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("grid");
    meta["tags"].append("alignment");
    meta["purpose"] = "Exact grid harmonization: warp the input onto the reference "
                      "raster's CRS, origin, resolution and extent so grid-compat "
                      "preflights in multi-input operators pass. Identical grids "
                      "skip the warp and publish a lossless copy.";
    meta["prerequisites"] = "Input and reference rasters with declared CRS and "
                            "north-up geotransforms.";
    meta["memoryPolicy"] = "streaming";
    meta["deterministic"] = true;
    meta["supportsCancellation"] = true;
    meta["limitations"].append("Rotated or flipped reference grids are refused; "
                               "no hidden resampling of the reference itself.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "raster";
    contract["crs"] = "required-declared";
    contract["gridRelation"] = "exact-reference";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsAlignOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 512;
    est["tileHeight"] = 512;
    est["estimatedRamBytes"] = Json::Value::UInt64( 64ULL * 1024ULL * 1024ULL );
    return est;
}

Json::Value RsAlignOperator::estimateExecution(const Json::Value& params) const {
    ( void ) params;
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 512;
    est["tileHeight"] = 512;
    est["estimatedRamBytes"] = Json::Value::UInt64( 64ULL * 1024ULL * 1024ULL );
    return est;
}

Json::Value RsAlignOperator::run(const Json::Value& params,
                                 RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }
    const std::string inputPath = requireString(params, "input");
    const std::string referencePath = requireString(params, "reference");
    const std::string outputPath = requireString(params, "output");
    const bool categorical = getBool(params, "categorical", false);
    const std::string kernel = resolveKernel(params, context, categorical);

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }
    requireCrs(src, inputPath, "input");
    GdalDatasetWrapper ref;
    if (!ref.open(QString::fromStdString(referencePath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open reference raster: " + referencePath);
    }
    requireCrs(ref, referencePath, "reference");

    const std::array<double, 6> gt = ref.geoTransform();
    if (!(std::abs(gt[2]) < 1e-12 && std::abs(gt[4]) < 1e-12) || gt[5] >= 0.0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "reference grid must be north-up (no rotation, negative "
                              "y pixel size); refusing to approximate it");
    }
    const double resX = gt[1];
    const double resY = -gt[5];
    const double minX = gt[0];
    const double maxY = gt[3];
    const double minY = gt[3] + ref.height() * gt[5];
    const double maxX = gt[0] + ref.width() * gt[1];

    const std::string refWkt = ref.projection().toStdString();
    const bool sameCrs = src.projection().toStdString() == refWkt;
    const auto srcGt = src.geoTransform();
    const bool sameGrid = sameCrs && src.width() == ref.width() &&
                          src.height() == ref.height() &&
                          std::abs(srcGt[0] - gt[0]) < 1e-9 &&
                          std::abs(srcGt[3] - gt[3]) < 1e-9 &&
                          std::abs(srcGt[1] - gt[1]) < 1e-12 &&
                          std::abs(srcGt[5] - gt[5]) < 1e-12;

    sicnu::geo::TranslateResult result;
    bool alreadyAligned = false;
    if (sameGrid) {
        // Exact same grid: publish a lossless copy, no resampling at all.
        context.logInfo("rs:align: input already matches the reference grid; "
                        "publishing a lossless copy");
        sicnu::geo::TranslateOptions copy;
        copy.creationOptions = {"COMPRESS=DEFLATE"};
        sicnu::geo::ConvertProgress progress = makeProgress(context, 0.05, 0.95);
        result = sicnu::geo::translateRaster(inputPath, outputPath, copy, &progress);
        alreadyAligned = true;
    } else {
        sicnu::geo::WarpOptions options;
        options.targetCrs = refWkt;
        options.resampling = kernel;
        // te + tr exactly reproduce the reference grid: origin = gt origin,
        // extent = width*resX / height*resY — the warped raster and the
        // reference share every grid line (no -tap snapping).
        options.targetResolutionX = resX;
        options.targetResolutionY = resY;
        options.targetBounds = {minX, minY, maxX, maxY};
        options.warpMemoryLimitBytes = resolveWarpMemoryLimit(params);
        context.throwIfCancelled();
        context.reportProgress(0.05, "Aligning raster to reference grid");
        sicnu::geo::ConvertProgress progress = makeProgress(context, 0.05, 0.95);
        result = sicnu::geo::warpRaster(inputPath, outputPath, options, &progress);
    }
    context.throwIfCancelled();

    Json::Value out(Json::objectValue);
    out["output"] = result.output;
    out["width"] = result.width;
    out["height"] = result.height;
    out["alreadyAligned"] = alreadyAligned;
    out["resampling"] = kernel;
    out["referenceGrid"]["width"] = ref.width();
    out["referenceGrid"]["height"] = ref.height();
    out["referenceGrid"]["resolutionX"] = resX;
    out["referenceGrid"]["resolutionY"] = resY;
    out["referenceGrid"]["minX"] = minX;
    out["referenceGrid"]["minY"] = minY;
    out["referenceGrid"]["maxX"] = maxX;
    out["referenceGrid"]["maxY"] = maxY;
    if (!result.warnings.empty())
        out["warnings"] = result.warnings;
    context.reportProgress(1.0, "Align complete");
    return out;
}

} // namespace sicnu::operators::rs
