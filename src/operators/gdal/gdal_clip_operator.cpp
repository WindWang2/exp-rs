/***************************************************************************
 * gdal_clip_operator.cpp  —  GDAL raster clip
 ***************************************************************************/
#include "gdal_clip_operator.h"
#include "gdal_operator_utils.h"

#include "operators/framework/rs_schema.h"

#include <sstream>

namespace sicnu::operators::gdal {

Json::Value GdalClipOperator::schema() const {
    using namespace schema;
    using namespace util;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster to clip");
    props["output"] = makeOutputParam("output", "Output clipped raster", "tif");
    props["cutline"] = makeVectorParam("cutline",
                                       "Vector cutline (shapefile/geojson/gpkg). Optional if extent is set.");
    props["cutline"]["required"] = false;
    props["cropToCutline"] = makeBooleanParam("cropToCutline",
                                              "Crop output extent to cutline bounds",
                                              true);
    props["extent"] = makeStringParam("extent",
                                      "Clip extent as JSON array [xmin,ymin,xmax,ymax] "
                                      "or four-number array property (optional)",
                                      "");
    props["extent"]["required"] = false;
    // Also accept extent as array of 4 numbers in params directly.
    props["resampling"] = makeEnumParam("resampling", "Resampling method",
                                        resamplingNames(), "nearest");
    props["nodata"] = makeNumberParam("nodata", "Output no-data value (optional)", 0.0);
    props["nodata"]["required"] = false;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Clipped GeoTIFF", "tif");
    outputs["width"] = makeIntegerParam("width", "Output width in pixels", 0);
    outputs["height"] = makeIntegerParam("height", "Output height in pixels", 0);

    Json::Value root = makeRootSchema("gdal:clip", description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value GdalClipOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "gdal";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("clip");
    meta["tags"].append("cutline");
    meta["tags"].append("extent");
    meta["tags"].append("geometry");
    meta["purpose"] = "Spatially subset a raster to a region of interest";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["useCases"].append("Clip imagery to study-area polygon before classification");
    meta["useCases"].append("Extract rectangular AOI by extent");
    return meta;
}

namespace {

bool parseExtent(const Json::Value& params, double& xmin, double& ymin,
                 double& xmax, double& ymax) {
    if (!params.isMember("extent")) return false;

    const Json::Value& ext = params["extent"];
    if (ext.isArray() && ext.size() == 4) {
        for (Json::ArrayIndex i = 0; i < 4; ++i) {
            if (!ext[i].isNumeric()) {
                throw RSOperatorError(ErrorCode::TypeMismatch,
                                      "extent array elements must be numbers");
            }
        }
        xmin = ext[0].asDouble();
        ymin = ext[1].asDouble();
        xmax = ext[2].asDouble();
        ymax = ext[3].asDouble();
        return true;
    }

    if (ext.isString()) {
        // Accept "xmin,ymin,xmax,ymax"
        std::stringstream ss(ext.asString());
        char comma;
        if (!(ss >> xmin >> comma >> ymin >> comma >> xmax >> comma >> ymax)) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "extent string must be xmin,ymin,xmax,ymax");
        }
        return true;
    }

    throw RSOperatorError(ErrorCode::TypeMismatch,
                          "extent must be a 4-number array or 'xmin,ymin,xmax,ymax' string");
}

} // namespace

Json::Value GdalClipOperator::run(const Json::Value& params,
                                  RSOperatorContext& context) {
    using namespace util;

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string cutline = getString(params, "cutline", "");
    const bool cropToCutline = params.isMember("cropToCutline")
                                   ? (params["cropToCutline"].isBool()
                                          ? params["cropToCutline"].asBool()
                                          : true)
                                   : true;
    const std::string resampling = getEnum(params, "resampling", resamplingNames(), "nearest");
    const bool hasNodata = hasNumber(params, "nodata");
    const double nodata = hasNodata ? getDouble(params, "nodata", 0.0) : 0.0;

    double xmin = 0, ymin = 0, xmax = 0, ymax = 0;
    const bool hasExtent = parseExtent(params, xmin, ymin, xmax, ymax);

    if (cutline.empty() && !hasExtent) {
        throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                              "gdal:clip requires either 'cutline' or 'extent'");
    }

    if (!cutline.empty() && !fileExists(cutline)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Cutline not found: " + cutline);
    }

    if (hasExtent && (xmax <= xmin || ymax <= ymin)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "extent must have xmax > xmin and ymax > ymin");
    }

    std::vector<std::string> options;
    appendGeoTiffDefaults(options, resampling);

    if (!cutline.empty()) {
        options.emplace_back("-cutline");
        options.emplace_back(cutline);
        if (cropToCutline) {
            options.emplace_back("-crop_to_cutline");
        }
    }

    if (hasExtent) {
        options.emplace_back("-te");
        options.emplace_back(std::to_string(xmin));
        options.emplace_back(std::to_string(ymin));
        options.emplace_back(std::to_string(xmax));
        options.emplace_back(std::to_string(ymax));
    }

    if (hasNodata) {
        options.emplace_back("-dstnodata");
        options.emplace_back(std::to_string(nodata));
    }

    auto [width, height] = runGdalWarp(inputPath, outputPath, options, context,
                                       "GDAL clip");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::gdal
