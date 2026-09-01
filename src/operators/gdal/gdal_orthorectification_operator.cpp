/***************************************************************************
 * gdal_orthorectification_operator.cpp  —  GDAL orthorectification
 ***************************************************************************/
#include "gdal_orthorectification_operator.h"
#include "gdal_operator_utils.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <gdal.h>
#include <gdal_utils.h>
#include <gdalwarper.h>
#include <cpl_string.h>
#include <ogr_srs_api.h>

#include <vector>

namespace sicnu::operators::gdal {

using namespace util;

namespace {

const std::vector<std::string> s_resampling = {
    "nearest", "bilinear", "cubic", "cubicspline", "lanczos"
};

GDALResampleAlg parseResampling(const std::string& name) {
    if (name == "nearest") return GRA_NearestNeighbour;
    if (name == "bilinear") return GRA_Bilinear;
    if (name == "cubic") return GRA_Cubic;
    if (name == "cubicspline") return GRA_CubicSpline;
    if (name == "lanczos") return GRA_Lanczos;
    return GRA_Bilinear;
}

bool hasRpcMetadata(GDALDatasetH ds) {
    CSLConstList md = GDALGetMetadata(ds, "RPC");
    return md != nullptr && CSLCount(md) > 0;
}

bool hasGcpMetadata(GDALDatasetH ds) {
    return GDALGetGCPCount(ds) > 0;
}

} // anonymous namespace

Json::Value GdalOrthorectificationOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input",
                                     "Input raster with RPC or GCP metadata");
    props["output"] = makeOutputParam("output", "Output orthorectified raster", "tif");
    props["dem"] = makeRasterParam("dem", "Digital Elevation Model raster (optional)");
    props["dem"]["required"] = false;
    props["dstCrs"] = makeStringParam("dstCrs",
                                      "Target CRS (e.g. EPSG:4326); default uses RPC/GCP CRS",
                                      "");
    props["dstCrs"]["required"] = false;
    props["resampling"] = makeEnumParam("resampling", "Resampling algorithm", s_resampling, "bilinear");
    props["targetResolution"] = makeNumberParam("targetResolution",
                                                "Output pixel size in target CRS units (optional)",
                                                0.0);
    props["targetResolution"]["required"] = false;
    props["nodata"] = makeNumberParam("nodata", "Output no-data value (optional)", 0.0);
    props["nodata"]["required"] = false;
    props["height"] = makeNumberParam("height",
                                      "Constant elevation fallback in meters (when no DEM)",
                                      0.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output orthorectified raster path");
    outputs["width"] = makeIntegerParam("width", "Output raster width");
    outputs["height"] = makeIntegerParam("height", "Output raster height");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value GdalOrthorectificationOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("orthorectification");
    meta["tags"].append("gdal");
    meta["tags"].append("rpc");
    meta["tags"].append("dem");
    meta["purpose"] = "Remove sensor and terrain distortions from satellite/aerial imagery.";
    meta["prerequisites"].append("Input raster must contain RPC metadata or GCPs.");
    meta["prerequisites"].append("DEM should overlap the input image for terrain-corrected results.");
    meta["workflowHints"].append("Use otb:compute_images_statistics or opencv filters before/after as needed.");
    meta["limitations"].append("Accuracy depends on the quality of RPC/GCP and DEM.");
    return meta;
}

Json::Value GdalOrthorectificationOperator::run(const Json::Value& params,
                                                RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");

    // Validate algorithm choice before I/O so tests and Agents get a typed error.
    const std::string resampling = getEnum(params, "resampling", s_resampling, "bilinear");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    const std::string demPath = getString(params, "dem", {});
    if (!demPath.empty() && !fileExists(demPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "DEM raster not found: " + demPath);
    }

    const std::string dstCrs = getString(params, "dstCrs", {});
    const double targetResolution = getDouble(params, "targetResolution", 0.0);
    const double nodata = getDouble(params, "nodata", 0.0);
    const bool hasNodata = params.isMember("nodata");
    const double height = getDouble(params, "height", 0.0);

    ensureGdalInit();

    GDALDatasetH hSrcDS = GDALOpen(inputPath.c_str(), GA_ReadOnly);
    if (!hSrcDS) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);
    }

    if (!hasRpcMetadata(hSrcDS) && !hasGcpMetadata(hSrcDS)) {
        GDALClose(hSrcDS);
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Input raster lacks RPC metadata and GCPs: " + inputPath);
    }

    context.logInfo("Input has " + std::string(hasRpcMetadata(hSrcDS) ? "RPC" : "GCP") +
                    " georeferencing metadata");

    // Build GDALWarp options.
    std::vector<std::string> optionStrings;

    optionStrings.emplace_back("-of");
    optionStrings.emplace_back("GTiff");

    optionStrings.emplace_back("-co");
    optionStrings.emplace_back("COMPRESS=LZW");

    optionStrings.emplace_back("-r");
    optionStrings.emplace_back(resampling);

    if (hasRpcMetadata(hSrcDS)) {
        optionStrings.emplace_back("-rpc");
    }

    if (!demPath.empty()) {
        optionStrings.emplace_back("-to");
        optionStrings.emplace_back("RPC_DEM=" + demPath);
        optionStrings.emplace_back("-to");
        optionStrings.emplace_back("RPC_DEMINTERPOLATION=bilinear");
    }

    if (height != 0.0) {
        optionStrings.emplace_back("-to");
        optionStrings.emplace_back("RPC_HEIGHT=" + QString::number(height, 'f', 2).toStdString());
    }

    if (!dstCrs.empty()) {
        optionStrings.emplace_back("-t_srs");
        optionStrings.emplace_back(dstCrs);
    }

    if (targetResolution > 0.0) {
        const std::string res = QString::number(targetResolution, 'f', 10).toStdString();
        optionStrings.emplace_back("-tr");
        optionStrings.emplace_back(res);
        optionStrings.emplace_back(res);
    }

    if (hasNodata) {
        optionStrings.emplace_back("-dstnodata");
        optionStrings.emplace_back(QString::number(nodata, 'f', 10).toStdString());
    }

    context.logInfo("Starting GDAL orthorectification: " + inputPath + " -> " + outputPath);

    // #694: delegate to the shared warp runner instead of an inlined copy —
    // it polls context.isCancelled() during the warp and removes the partial
    // GTiff on cancel/failure, so a cancelled or failed orthorectification
    // never leaves a partial output at the user's output path.
    std::pair<int, int> outputDims{0, 0};
    try {
        outputDims = runGdalWarpOnDataset(hSrcDS, outputPath, optionStrings, context,
                                          "Orthorectification");
    } catch (...) {
        GDALClose(hSrcDS);
        throw;
    }
    GDALClose(hSrcDS);

    context.reportProgress(1.0, "Orthorectification complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["width"] = outputDims.first;
    result["height"] = outputDims.second;
    return result;
}

} // namespace sicnu::operators::gdal
