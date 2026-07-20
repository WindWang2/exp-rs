/***************************************************************************
 * gdal_reproject_operator.cpp  —  GDAL raster reprojection
 ***************************************************************************/
#include "gdal_reproject_operator.h"
#include "gdal_operator_utils.h"

#include "operators/framework/rs_schema.h"

namespace sicnu::operators::gdal {

Json::Value GdalReprojectOperator::schema() const {
    using namespace schema;
    using namespace util;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster to reproject");
    props["output"] = makeOutputParam("output", "Output reprojected raster", "tif");
    props["dstCrs"] = makeStringParam("dstCrs",
                                      "Target CRS (e.g. EPSG:3857 or WKT)",
                                      "");
    props["srcCrs"] = makeStringParam("srcCrs",
                                      "Override source CRS (optional)",
                                      "");
    props["srcCrs"]["required"] = false;
    props["resampling"] = makeEnumParam("resampling", "Resampling method",
                                        resamplingNames(), "bilinear");
    props["targetResolution"] = makeNumberParam("targetResolution",
                                                "Output pixel size in target CRS units (optional)",
                                                0.0);
    props["targetResolution"]["required"] = false;
    props["nodata"] = makeNumberParam("nodata", "Output no-data value (optional)", 0.0);
    props["nodata"]["required"] = false;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Reprojected GeoTIFF", "tif");
    outputs["width"] = makeIntegerParam("width", "Output width in pixels", 0);
    outputs["height"] = makeIntegerParam("height", "Output height in pixels", 0);

    Json::Value root = makeRootSchema("gdal:reproject", description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "dstCrs"});
    return root;
}

Json::Value GdalReprojectOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "gdal";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("reproject");
    meta["tags"].append("warp");
    meta["tags"].append("crs");
    meta["tags"].append("geometry");
    meta["purpose"] = "Change the coordinate reference system of a raster";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["useCases"].append("Align multi-source imagery to a common CRS before fusion");
    meta["useCases"].append("Prepare rasters for web mercator display");
    return meta;
}

Json::Value GdalReprojectOperator::run(const Json::Value& params,
                                       RSOperatorContext& context) {
    using namespace util;

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string dstCrs = requireString(params, "dstCrs");
    const std::string srcCrs = getString(params, "srcCrs", "");
    const std::string resampling = getEnum(params, "resampling", resamplingNames(), "bilinear");
    const double targetResolution = getDouble(params, "targetResolution", 0.0);
    const bool hasNodata = hasNumber(params, "nodata");
    const double nodata = hasNodata ? getDouble(params, "nodata", 0.0) : 0.0;

    if (dstCrs.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter, "dstCrs must not be empty");
    }

    std::vector<std::string> options;
    appendGeoTiffDefaults(options, resampling);

    options.emplace_back("-t_srs");
    options.emplace_back(dstCrs);

    if (!srcCrs.empty()) {
        options.emplace_back("-s_srs");
        options.emplace_back(srcCrs);
    }

    if (targetResolution > 0.0) {
        options.emplace_back("-tr");
        options.emplace_back(std::to_string(targetResolution));
        options.emplace_back(std::to_string(targetResolution));
    }

    if (hasNodata) {
        options.emplace_back("-dstnodata");
        options.emplace_back(std::to_string(nodata));
    }

    auto [width, height] = runGdalWarp(inputPath, outputPath, options, context,
                                       "GDAL reproject");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["width"] = width;
    result["height"] = height;
    result["dstCrs"] = dstCrs;
    return result;
}

} // namespace sicnu::operators::gdal
